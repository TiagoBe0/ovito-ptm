////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/mltraining/MLTrainingModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/TaskProgress.h>
#include <ovito/core/app/undo/UndoableTransaction.h>
#include <ovito/gui/desktop/properties/FilenameParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <QButtonGroup>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QRadioButton>
#include "MLTrainingModifierEditor.h"

// Include LibTorch headers only when the library is available.
#ifdef OVITO_ML_HAS_LIBTORCH
#  include <torch/torch.h>
#  include <torch/script.h>
#endif

namespace Ovito {

// ---------------------------------------------------------------------------
// LibTorch MLP definition (compiled only when LibTorch is available)
// ---------------------------------------------------------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

/// Simple 3-layer MLP used for per-atom classification.
/// Input  : [N, numFeatures]
/// Output : [N, numClasses]  (raw logits — no softmax)
struct MLPNetImpl : torch::nn::Module
{
    torch::nn::Linear fc1{nullptr}, fc2{nullptr}, fc3{nullptr};

    MLPNetImpl(int64_t inFeatures, int64_t h1, int64_t h2, int64_t numClasses)
    {
        fc1 = register_module("fc1", torch::nn::Linear(inFeatures, h1));
        fc2 = register_module("fc2", torch::nn::Linear(h1, h2));
        fc3 = register_module("fc3", torch::nn::Linear(h2, numClasses));
    }

    torch::Tensor forward(torch::Tensor x)
    {
        x = torch::relu(fc1(x));
        x = torch::relu(fc2(x));
        return fc3(x);
    }
};
TORCH_MODULE(MLPNet);

// ---------------------------------------------------------------------------
// Training result returned by the background task
// ---------------------------------------------------------------------------

struct TrainingResult
{
    int     numSamples  = 0;
    int     numClasses  = 0;
    int     numFeatures = 0;
    float   finalLoss   = 0.f;
    QString savedPath;
};

// ---------------------------------------------------------------------------
// Core training function — runs entirely in a background thread.
//
// Takes the collected pipeline states plus all hyper-parameters, builds the
// feature/label tensors, trains the MLP, traces it to TorchScript and saves
// it to disk.
// ---------------------------------------------------------------------------

static TrainingResult trainMLP(
    std::vector<PipelineFlowState>    states,
    MLTrainingModifier::InputMode     inputMode,
    QStringList                       inputProperties,
    float                             cutoff,
    int                               maxNeighbors,
    QString                           labelPropName,
    int        h1,
    int        h2,
    int        numEpochsArg,
    float      lrArg,
    int        batchSizeArg,
    QString    outPath)
{
    // Use the task progress indicator if a UserInterface is available;
    // fall back to the static no-op TaskProgress::Ignore otherwise.
    // The UserInterface may be absent when asyncLaunch is called from
    // the GUI thread (no parent task to inherit the UI from).
    const bool hasTaskUi = this_task::get()
                        && this_task::get()->userInterface();
    TaskProgress& progress = hasTaskUi
        ? *new TaskProgress(this_task::get()->userInterface().get())
        : TaskProgress::Ignore;
    // Clean up the heap-allocated TaskProgress when we leave the function.
    struct ProgressGuard {
        TaskProgress& ref; bool owned;
        ~ProgressGuard() { if(owned) delete &ref; }
    } progressGuard{progress, hasTaskUi};

    // -----------------------------------------------------------------------
    // Phase 1: extract per-atom descriptors and labels from the frame
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: extracting features from %1 frame(s)...")
        .arg(states.size()));
    progress.setMaximum(static_cast<qlonglong>(states.size()));

    std::vector<float>   features;   // flattened [totalAtoms, numFeatures]
    std::vector<int64_t> labels;     // class index per atom

    const bool useNeighDist = (inputMode == MLTrainingModifier::InputMode::NeighborDistances);
    const int  numFeatures  = useNeighDist
                                ? maxNeighbors
                                : static_cast<int>(inputProperties.size());
    const float invCutoff   = 1.0f / cutoff;

    if(!useNeighDist && numFeatures == 0)
        throw Exception(QStringLiteral(
            "MLTraining: no input property columns selected.\n"
            "Please select at least one property column in the modifier panel."));

    // Column descriptor used when resolving property references per frame.
    struct Column { const Property* prop; int comp; size_t nComp; };

    for(size_t fi = 0; fi < states.size(); ++fi) {
        if(this_task::get())
            this_task::throwIfCanceled();

        const auto& state = states[fi];

        const Particles*      particles = state.getObject<Particles>();
        const SimulationCell* simCell   = state.getObject<SimulationCell>();
        if(!particles) {
            progress.incrementValueNoCancel();
            continue;
        }

        const Property* posProp =
            particles->getProperty(Particles::PositionProperty);
        const Property* labelPropObj =
            particles->getProperty(labelPropName);

        if(!posProp || !labelPropObj) {
            progress.incrementValueNoCancel();
            continue;
        }

        const size_t N = posProp->size();
        if(N == 0) {
            progress.incrementValueNoCancel();
            continue;
        }

        // Read integer labels (convert any integer type to int64).
        {
            BufferAccessConvertedTo<int32_t> labelAccess(labelPropObj);
            for(size_t i = 0; i < N; ++i)
                labels.push_back(static_cast<int64_t>(labelAccess[i]));
        }

        const size_t base = features.size();

        if(useNeighDist) {
            // ------------------------------------------------------------------
            // NeighborDistances mode: sorted normalised neighbour distances.
            // Requires a SimulationCell for PBC.
            // ------------------------------------------------------------------
            if(!simCell) {
                // Remove the labels we just added for this frame.
                labels.resize(labels.size() - N);
                progress.incrementValueNoCancel();
                continue;
            }

            features.resize(base + N * static_cast<size_t>(numFeatures), 1.0f);

            CutoffNeighborFinder nf(static_cast<FloatType>(cutoff), posProp, simCell, nullptr);

            std::vector<FloatType> dists;
            dists.reserve(64);
            for(size_t i = 0; i < N; ++i) {
                dists.clear();
                for(CutoffNeighborFinder::Query q(nf, i); !q.atEnd(); q.next())
                    dists.push_back(q.distance());
                std::sort(dists.begin(), dists.end());

                float* row = features.data() + base + i * static_cast<size_t>(numFeatures);
                const int k = static_cast<int>(
                    std::min(dists.size(), static_cast<size_t>(numFeatures)));
                for(int j = 0; j < k; ++j)
                    row[j] = static_cast<float>(dists[j]) * invCutoff;
                // Remaining slots keep the 1.0f default (already set by resize).
            }
        } else {
            // ------------------------------------------------------------------
            // ParticleProperties mode: user-selected property columns.
            // Resolve column references for this frame.
            // ------------------------------------------------------------------
            std::vector<Column> cols;
            cols.reserve(static_cast<size_t>(numFeatures));
            bool allFound = true;
            for(const QString& refStr : inputProperties) {
                PropertyReference ref(refStr);
                QString errMsg;
                auto [prop, comp] = ref.findInContainerWithComponent(
                    particles, errMsg, /*requireComponent=*/false);
                if(!prop) {
                    allFound = false;
                    break;
                }
                cols.push_back({prop, comp < 0 ? 0 : comp, prop->componentCount()});
            }

            if(!allFound) {
                // Column missing in this frame; skip it.
                labels.resize(labels.size() - N);
                progress.incrementValueNoCancel();
                continue;
            }

            features.resize(base + N * static_cast<size_t>(numFeatures));

            // Build read accessors (FloatType conversion).
            std::vector<BufferAccessConvertedTo<float>> accs;
            accs.reserve(cols.size());
            for(const auto& col : cols)
                accs.emplace_back(col.prop);

            for(size_t i = 0; i < N; ++i) {
                for(int j = 0; j < numFeatures; ++j)
                    features[base + i * numFeatures + j] = static_cast<float>(
                        accs[j][i * cols[j].nComp + cols[j].comp]);
            }
        }

        progress.incrementValueNoCancel();
    }

    const int64_t totalSamples = static_cast<int64_t>(labels.size());
    if(totalSamples == 0)
        throw Exception(QStringLiteral(
            "MLTraining: no training samples found.\n"
            "Make sure the label property '%1' exists in the upstream pipeline "
            "and that frames contain particles.").arg(labelPropName));

    // Determine number of classes from the maximum label value.
    const int64_t numClasses =
        *std::max_element(labels.begin(), labels.end()) + 1;

    if(numClasses < 2)
        throw Exception(QStringLiteral(
            "MLTraining: only %1 class(es) found in the label property '%2'.\n"
            "Need at least 2 classes for classification.")
            .arg(numClasses).arg(labelPropName));

    // -----------------------------------------------------------------------
    // Phase 2: build tensors
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: building tensors (%1 samples, %2 classes)...")
        .arg(totalSamples).arg(numClasses));

    // Feature tensor: [N, numFeatures], float32.
    // label tensor:   [N],              int64.
    auto featureTensor = torch::tensor(features, torch::kFloat32)
                             .reshape({totalSamples, static_cast<int64_t>(numFeatures)});
    auto labelTensor   = torch::tensor(labels, torch::kInt64);

    // -----------------------------------------------------------------------
    // Phase 3: build and train the MLP
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: training MLP (%1 epochs)...").arg(numEpochsArg));
    progress.setMaximum(static_cast<qlonglong>(numEpochsArg));

    MLPNet model(static_cast<int64_t>(numFeatures),
                 static_cast<int64_t>(h1),
                 static_cast<int64_t>(h2),
                 numClasses);
    model->train();

    torch::optim::Adam optimizer(
        model->parameters(),
        torch::optim::AdamOptions(static_cast<double>(lrArg)));

    // Mini-batch training. If batchSize <= 0 use the whole dataset per step.
    const int64_t effectiveBatch =
        (batchSizeArg > 0 && static_cast<int64_t>(batchSizeArg) < totalSamples)
            ? static_cast<int64_t>(batchSizeArg)
            : totalSamples;

    float finalLoss = 0.f;

    for(int epoch = 0; epoch < numEpochsArg; ++epoch) {
        if(this_task::get())
            this_task::throwIfCanceled();

        // Shuffle indices for mini-batch sampling.
        auto indices = torch::randperm(totalSamples, torch::kInt64);

        float epochLoss = 0.f;
        int   numSteps  = 0;

        for(int64_t start = 0; start < totalSamples; start += effectiveBatch) {
            const int64_t end = std::min(start + effectiveBatch, totalSamples);
            auto batchIdx  = indices.slice(0, start, end);
            auto batchX    = featureTensor.index_select(0, batchIdx);
            auto batchY    = labelTensor.index_select(0, batchIdx);

            optimizer.zero_grad();
            auto logits = model->forward(batchX);
            auto loss   = torch::nn::functional::cross_entropy(logits, batchY);
            loss.backward();
            optimizer.step();

            epochLoss += loss.item<float>();
            ++numSteps;
        }

        finalLoss = epochLoss / static_cast<float>(numSteps);
        progress.setValueNoCancel(static_cast<qlonglong>(epoch + 1));
    }

    // -----------------------------------------------------------------------
    // Phase 4: trace to TorchScript and save
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: saving TorchScript model to %1...").arg(outPath));

    model->eval();

    // Save as a checkpoint (raw weight tensors via OutputArchive).
    // MLStructureModifier detects this format at load time: torch::jit::load
    // can open the file but the resulting module has no 'forward' method, so
    // the loader falls back to the checkpoint path which reads these tensors
    // directly and performs inference with manual matrix-multiply.
    try {
        torch::serialize::OutputArchive archive;
        archive.write("fc1.weight", model->fc1->weight);
        archive.write("fc1.bias",   model->fc1->bias);
        archive.write("fc2.weight", model->fc2->weight);
        archive.write("fc2.bias",   model->fc2->bias);
        archive.write("fc3.weight", model->fc3->weight);
        archive.write("fc3.bias",   model->fc3->bias);
        archive.save_to(outPath.toStdString());
    }
    catch(const c10::Error& e) {
        throw Exception(QStringLiteral("MLTraining: failed to save model to '%1': %2")
            .arg(outPath).arg(QString::fromStdString(e.what())));
    }

    TrainingResult result;
    result.numSamples  = static_cast<int>(totalSamples);
    result.numClasses  = static_cast<int>(numClasses);
    result.numFeatures = numFeatures;
    result.finalLoss   = finalLoss;
    result.savedPath   = outPath;
    return result;
}

#endif // OVITO_ML_HAS_LIBTORCH

// ---------------------------------------------------------------------------
// Editor class registration
// ---------------------------------------------------------------------------

IMPLEMENT_CREATABLE_OVITO_CLASS(MLTrainingModifierEditor);
SET_OVITO_OBJECT_EDITOR(MLTrainingModifier, MLTrainingModifierEditor);

// ---------------------------------------------------------------------------
// createUI
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("NN Training Modifier"), rolloutParams);
    QVBoxLayout* mainLayout = new QVBoxLayout(rollout);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // -----------------------------------------------------------------------
    // Input features — mode selector
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Input features"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(2);

        QButtonGroup* btnGroup = new QButtonGroup(box);
        QRadioButton* rbNeigh  = new QRadioButton(tr("Neighbour distances (sorted, normalised)"), box);
        QRadioButton* rbProp   = new QRadioButton(tr("Particle property columns"), box);
        btnGroup->addButton(rbNeigh, static_cast<int>(MLTrainingModifier::InputMode::NeighborDistances));
        btnGroup->addButton(rbProp,  static_cast<int>(MLTrainingModifier::InputMode::ParticleProperties));
        lay->addWidget(rbNeigh);
        lay->addWidget(rbProp);
        mainLayout->addWidget(box);

        connect(btnGroup, &QButtonGroup::idClicked, this, [this](int id) {
            if(auto* mod = static_cast<MLTrainingModifier*>(editObject())) {
                UndoableTransaction t;
                t.begin(ui(), tr("Change input mode"));
                mod->setInputMode(static_cast<MLTrainingModifier::InputMode>(id));
                t.commit();
            }
            onInputModeChanged();
        });

        connect(this, &PropertiesEditor::contentsChanged, this, [btnGroup, this]() {
            if(auto* mod = static_cast<MLTrainingModifier*>(editObject())) {
                if(auto* btn = btnGroup->button(static_cast<int>(mod->inputMode())))
                    btn->setChecked(true);
            }
            onInputModeChanged();
        });
    }

    // -----------------------------------------------------------------------
    // NeighborDistances parameters (shown only in that mode)
    // -----------------------------------------------------------------------
    {
        _descParamsBox = new QGroupBox(tr("Descriptor parameters"), rollout);
        QGridLayout* grid = new QGridLayout(_descParamsBox);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        FloatParameterUI* cutoffUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::cutoffRadius));
        grid->addWidget(cutoffUI->label(), row, 0);
        grid->addLayout(cutoffUI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* numNeighUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::numNeighbors));
        grid->addWidget(numNeighUI->label(), row, 0);
        grid->addLayout(numNeighUI->createFieldLayout(), row, 1);

        mainLayout->addWidget(_descParamsBox);
    }

    // -----------------------------------------------------------------------
    // ParticleProperties column selection (shown only in that mode)
    // -----------------------------------------------------------------------
    {
        _propSelectBox = new QGroupBox(tr("Property columns (features)"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(_propSelectBox);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        lay->addWidget(new QLabel(
            tr("Select columns to use as input features.\n"
               "Order matters — must match when loading the model."), _propSelectBox));

        _propListWidget = new QListWidget(_propSelectBox);
        _propListWidget->setSelectionMode(QAbstractItemView::NoSelection);
        _propListWidget->setMinimumHeight(120);
        lay->addWidget(_propListWidget);

        lay->addWidget(new QLabel(
            tr("<small>Apply upstream modifiers (e.g. Voronoi Analysis) first "
               "to see their output columns here.</small>"), _propSelectBox));

        mainLayout->addWidget(_propSelectBox);

        connect(this, &PropertiesEditor::pipelineInputChanged,
                this, &MLTrainingModifierEditor::updatePropertyList);
        connect(_propListWidget, &QListWidget::itemChanged,
                this, &MLTrainingModifierEditor::onPropertyItemChanged);
    }

    // -----------------------------------------------------------------------
    // Target (label) property section
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Target (label) property"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);

        grid->addWidget(new QLabel(tr("Label property:"), box), 0, 0);

        _labelCombo = new QComboBox(box);
        _labelCombo->setEditable(true);
        _labelCombo->setInsertPolicy(QComboBox::NoInsert);
        _labelCombo->setToolTip(tr(
            "Name of the integer particle property used as class labels.\n"
            "This list is populated from the upstream pipeline (integer/enum properties)."));
        grid->addWidget(_labelCombo, 0, 1);

        mainLayout->addWidget(box);

        // Repopulate when the pipeline changes.
        connect(this, &PropertiesEditor::pipelineInputChanged,
                this, &MLTrainingModifierEditor::updateLabelCombo);

        // Sync combo → modifier when the user changes the selection.
        connect(_labelCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            if(_updatingLabelCombo) return;
            auto* mod = static_cast<MLTrainingModifier*>(editObject());
            if(!mod || text == mod->labelProperty()) return;
            UndoableTransaction t;
            t.begin(ui(), tr("Change label property"));
            mod->setLabelProperty(text);
            t.commit();
        });

        // Sync modifier → combo when contentsChanged fires.
        connect(this, &PropertiesEditor::contentsChanged, this, [this]() {
            auto* mod = static_cast<MLTrainingModifier*>(editObject());
            if(!mod || !_labelCombo) return;
            _updatingLabelCombo = true;
            const int idx = _labelCombo->findText(mod->labelProperty());
            if(idx >= 0)
                _labelCombo->setCurrentIndex(idx);
            else
                _labelCombo->setCurrentText(mod->labelProperty());
            _updatingLabelCombo = false;
        });
    }

    // -----------------------------------------------------------------------
    // MLP architecture section
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("MLP architecture"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        IntegerParameterUI* h1UI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::hiddenSize1));
        grid->addWidget(h1UI->label(), row, 0);
        grid->addLayout(h1UI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* h2UI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::hiddenSize2));
        grid->addWidget(h2UI->label(), row, 0);
        grid->addLayout(h2UI->createFieldLayout(), row, 1);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Training hyper-parameters section
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Training"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        IntegerParameterUI* epochsUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::numEpochs));
        grid->addWidget(epochsUI->label(), row, 0);
        grid->addLayout(epochsUI->createFieldLayout(), row, 1);
        ++row;

        FloatParameterUI* lrUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::learningRate));
        grid->addWidget(lrUI->label(), row, 0);
        grid->addLayout(lrUI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* bsUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::batchSize));
        grid->addWidget(bsUI->label(), row, 0);
        grid->addLayout(bsUI->createFieldLayout(), row, 1);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Output model path section
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Output"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        lay->addWidget(new QLabel(tr("TorchScript model output path (<tt>.pt</tt>):"), box));

        QStringList ptFilter = { tr("TorchScript models (*.pt)"), tr("All files (*)") };
        FilenameParameterUI* outPathUI = createParamUI<FilenameParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::outputModelPath), ptFilter, /*existingFile=*/false);
        lay->addWidget(outPathUI->selectorWidget());

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Train button and status display
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Train"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        lay->addWidget(new QLabel(
            tr("<small>Evaluates the upstream pipeline at the current frame,\n"
               "collects per-atom descriptors and labels, trains a 3-layer MLP,\n"
               "and saves the model as a .pt file.</small>"),
            box));

        _trainButton = new QPushButton(tr("Collect from Current Frame && Train"), box);
        _trainButton->setToolTip(tr(
            "Evaluate the current animation frame, extract neighbour-distance\n"
            "descriptors and integer class labels, train the MLP, and save\n"
            "the model to the specified output path."));
        lay->addWidget(_trainButton);
        connect(_trainButton, &QPushButton::clicked, this, &MLTrainingModifierEditor::onTrainClicked);

        _statusLabel = new QLabel(tr("Not trained yet."), box);
        _statusLabel->setWordWrap(true);
        lay->addWidget(_statusLabel);

        mainLayout->addWidget(box);
    }

    // Status display (shows any pipeline errors)
    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    updateTrainButtonState();
    onInputModeChanged();
    updatePropertyList();
    updateLabelCombo();
}

// ---------------------------------------------------------------------------
// updateTrainButtonState
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::updateTrainButtonState()
{
    if(!_trainButton) return;
#ifdef OVITO_ML_HAS_LIBTORCH
    _trainButton->setText(tr("Collect from Current Frame && Train"));
    _trainButton->setEnabled(true);
    _trainButton->setToolTip(_trainButton->toolTip()); // keep existing tooltip
#else
    // Keep the button clickable so users get an explicit explanation dialog.
    _trainButton->setText(tr("Collect from Current Frame && Train (Unavailable)"));
    _trainButton->setEnabled(true);
    _trainButton->setToolTip(tr("LibTorch is not available in this build.\n"
        "Click for details on how to enable training support."));
    if(_statusLabel) {
        _statusLabel->setText(tr("Training unavailable in this build: LibTorch support is disabled. "
                                 "Rebuild OVITO with -DOVITO_USE_LIBTORCH=ON."));
    }
#endif
}

// ---------------------------------------------------------------------------
// onTrainClicked  —  the main training orchestrator
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::onTrainClicked()
{
#ifndef OVITO_ML_HAS_LIBTORCH
    QMessageBox::critical(parentWindow(),
        tr("LibTorch not available"),
        tr("This build of OVITO was compiled without LibTorch support.\n"
           "Rebuild with -DOVITO_USE_LIBTORCH=ON to enable neural-network training."));
    return;
#else
    auto* mod = static_cast<MLTrainingModifier*>(editObject());
    if(!mod) return;

    // Capture parameters by value — the modifier might change or be deleted
    // while the async operations are in flight.
    const MLTrainingModifier::InputMode iMode = mod->inputMode();
    const QStringList inputProps = mod->inputProperties();
    const float   cutoff     = static_cast<float>(mod->cutoffRadius());
    const int     maxNeigh   = mod->numNeighbors();
    const QString labelProp  = mod->labelProperty();
    const int     h1         = mod->hiddenSize1();
    const int     h2         = mod->hiddenSize2();
    const int     epochs     = mod->numEpochs();
    const float   lr         = static_cast<float>(mod->learningRate());
    const int     batchSz    = mod->batchSize();
    const QString outPath    = mod->outputModelPath();

    // Validate early
    if(iMode == MLTrainingModifier::InputMode::NeighborDistances && cutoff <= 0.f) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Cutoff radius must be positive."));
        return;
    }
    if(iMode == MLTrainingModifier::InputMode::ParticleProperties && inputProps.isEmpty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Please select at least one property column as input features."));
        return;
    }
    if(labelProp.trimmed().isEmpty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Please specify a label property."));
        return;
    }
    if(outPath.isEmpty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Please specify an output model path (.pt)."));
        return;
    }

    if(_statusLabel)
        _statusLabel->setText(tr("Collecting data from the current frame..."));

    // -----------------------------------------------------------------------
    // Get the cached upstream pipeline data for the current frame.
    //
    // getPipelineInput() returns the already-evaluated PipelineFlowState
    // synchronously, avoiding the problematic async evaluation patterns.
    // -----------------------------------------------------------------------
    PipelineFlowState state = getPipelineInput();

    if(state.status().type() == PipelineStatus::Error || !state) {
        if(_statusLabel)
            _statusLabel->setText(tr("Error: no valid pipeline data for the current frame.\n"
                                     "Make sure at least one file is loaded and a modifier producing '%1' is upstream.")
                                     .arg(labelProp));
        return;
    }

    if(_statusLabel)
        _statusLabel->setText(tr("Training MLP on current frame..."));

    // Build single-element states vector and launch training in a
    // background thread via asyncLaunch so that this_task::ui() and
    // TaskProgress work correctly.
    std::vector<PipelineFlowState> states;
    states.push_back(std::move(state));

    QPointer<MLTrainingModifierEditor> self(this);

    auto trainFuture = asyncLaunch(
        [states = std::move(states),
         iMode, inputProps, cutoff, maxNeigh, labelProp,
         h1, h2, epochs, lr, batchSz, outPath]() mutable -> TrainingResult
        {
            return trainMLP(
                std::move(states),
                iMode, inputProps,
                cutoff, maxNeigh, labelProp,
                h1, h2, epochs, lr, batchSz, outPath);
        });

    scheduleOperationAfter(std::move(trainFuture),
        [self](TrainingResult result) {
            if(!self) return;
            const QString msg = QStringLiteral(
                "Training complete!\n"
                "  Samples : %1\n"
                "  Classes : %2\n"
                "  Features: %3\n"
                "  Final loss: %4\n"
                "  Saved to: %5")
                .arg(result.numSamples)
                .arg(result.numClasses)
                .arg(result.numFeatures)
                .arg(static_cast<double>(result.finalLoss), 0, 'f', 6)
                .arg(result.savedPath);

            if(self->_statusLabel)
                self->_statusLabel->setText(msg);
        });
#endif // OVITO_ML_HAS_LIBTORCH
}

// ---------------------------------------------------------------------------
// onInputModeChanged — show/hide descriptor params vs. column list
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::onInputModeChanged()
{
    auto* mod = static_cast<MLTrainingModifier*>(editObject());
    const bool useProps = mod &&
        mod->inputMode() == MLTrainingModifier::InputMode::ParticleProperties;

    if(_descParamsBox) _descParamsBox->setVisible(!useProps);
    if(_propSelectBox) _propSelectBox->setVisible(useProps);
}

// ---------------------------------------------------------------------------
// updatePropertyList — repopulate feature-column list from first pipeline frame
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::updatePropertyList()
{
    if(!_propListWidget) return;

    _updatingPropertyList = true;

    QStringList selected;
    if(auto* mod = static_cast<MLTrainingModifier*>(editObject()))
        selected = mod->inputProperties();

    _propListWidget->clear();

    for(const PipelineFlowState& state : getPipelineInputs()) {
        const Particles* particles = state.getObject<Particles>();
        if(!particles) continue;

        for(const Property* prop : particles->properties()) {
            int dt = prop->dataType();
            if(dt != QMetaType::Float   && dt != QMetaType::Double &&
               dt != QMetaType::Int     && dt != QMetaType::LongLong)
                continue;

            const size_t nComp = prop->componentCount();
            const QStringList& compNames = prop->componentNames();

            if(nComp == 1) {
                PropertyReference ref(prop, -1);
                QString key = ref.nameWithComponent();
                auto* item = new QListWidgetItem(prop->name(), _propListWidget);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, key);
            } else {
                for(size_t c = 0; c < nComp; ++c) {
                    PropertyReference ref(prop, static_cast<int>(c));
                    QString key = ref.nameWithComponent();
                    QString label = (c < (size_t)compNames.size())
                        ? QStringLiteral("%1.%2").arg(prop->name(), compNames[c])
                        : QStringLiteral("%1.%2").arg(prop->name()).arg(c);
                    auto* item = new QListWidgetItem(label, _propListWidget);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                    item->setData(Qt::UserRole, key);
                }
            }
        }
        break; // Only use the first frame to infer available columns.
    }

    _updatingPropertyList = false;
}

// ---------------------------------------------------------------------------
// onPropertyItemChanged — commit checked columns to inputProperties
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::onPropertyItemChanged(QListWidgetItem*)
{
    if(_updatingPropertyList) return;
    auto* mod = static_cast<MLTrainingModifier*>(editObject());
    if(!mod) return;

    QStringList newList;
    for(int i = 0; i < _propListWidget->count(); ++i) {
        QListWidgetItem* it = _propListWidget->item(i);
        if(it->checkState() == Qt::Checked)
            newList << it->data(Qt::UserRole).toString();
    }

    UndoableTransaction t;
    t.begin(ui(), tr("Change input properties"));
    mod->setInputProperties(newList);
    t.commit();
}

// ---------------------------------------------------------------------------
// updateLabelCombo — repopulate integer/enum properties for the target
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::updateLabelCombo()
{
    if(!_labelCombo) return;

    _updatingLabelCombo = true;

    const QString current = _labelCombo->currentText();
    _labelCombo->clear();

    for(const PipelineFlowState& state : getPipelineInputs()) {
        const Particles* particles = state.getObject<Particles>();
        if(!particles) continue;

        for(const Property* prop : particles->properties()) {
            int dt = prop->dataType();
            // Show only integer/enum properties — typical for class labels.
            if(dt == QMetaType::Int || dt == QMetaType::LongLong ||
               dt == QMetaType::UInt || dt == QMetaType::ULongLong)
            {
                if(prop->componentCount() == 1)
                    _labelCombo->addItem(prop->name());
            }
        }
        break; // Only use first frame.
    }

    // Restore the previously stored label or the modifier's current value.
    const int idx = _labelCombo->findText(current);
    if(idx >= 0)
        _labelCombo->setCurrentIndex(idx);
    else
        _labelCombo->setCurrentText(current);

    _updatingLabelCombo = false;
}

}  // namespace Ovito
