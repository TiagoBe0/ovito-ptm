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
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/core/dataset/animation/TimeInterval.h>
#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/TaskProgress.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include <ovito/core/app/undo/UndoableTransaction.h>
#include <ovito/gui/desktop/properties/FilenameParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/StringParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
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
    std::vector<PipelineFlowState> states,
    float      cutoff,
    int        maxNeighbors,
    QString    labelPropName,
    int        h1,
    int        h2,
    int        numEpochsArg,
    float      lrArg,
    int        batchSizeArg,
    QString    outPath)
{
    TaskProgress progress(this_task::ui());

    // -----------------------------------------------------------------------
    // Phase 1: extract per-atom descriptors and labels from all frames
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: extracting features from %1 frames...")
        .arg(states.size()));
    progress.setMaximum(static_cast<qlonglong>(states.size()));

    std::vector<float>   features;   // flattened [totalAtoms, maxNeighbors]
    std::vector<int64_t> labels;     // class index per atom

    const int  numFeatures  = maxNeighbors;
    const float invCutoff   = 1.0f / cutoff;

    for(size_t fi = 0; fi < states.size(); ++fi) {
        this_task::throwIfCanceled();

        const auto& state = states[fi];

        const Particles*   particles = state.getObject<Particles>();
        const SimulationCell* simCell = state.getObject<SimulationCell>();
        if(!particles || !simCell) {
            progress.incrementValue();
            continue;
        }

        const Property* posProp =
            particles->getProperty(Particles::PositionProperty);
        const Property* labelPropObj =
            particles->getProperty(labelPropName);

        if(!posProp || !labelPropObj) {
            progress.incrementValue();
            continue;
        }

        const size_t N = posProp->size();
        if(N == 0) {
            progress.incrementValue();
            continue;
        }

        // Read integer labels (convert any integer type to int64).
        {
            BufferAccessConvertedTo<int32_t> labelAccess(labelPropObj);
            for(size_t i = 0; i < N; ++i)
                labels.push_back(static_cast<int64_t>(labelAccess[i]));
        }

        // Build sorted normalised neighbour-distance descriptors.
        // The CutoffNeighborFinder is constructed from the captured property data;
        // the PipelineFlowState in `states` keeps that data alive.
        CutoffNeighborFinder nf(static_cast<FloatType>(cutoff), posProp, simCell, nullptr);

        const size_t base = features.size();
        features.resize(base + N * static_cast<size_t>(numFeatures), 1.0f); // default = 1.0 (max dist)

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

        progress.incrementValue();
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
        progress.setValue(static_cast<qlonglong>(epoch + 1));
    }

    // -----------------------------------------------------------------------
    // Phase 4: trace to TorchScript and save
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("MLTraining: saving TorchScript model to %1...").arg(outPath));

    model->eval();

    // Trace the trained module with a dummy input.
    // The resulting TorchScript module is compatible with MLStructureModifier.
    std::vector<torch::jit::IValue> exampleInputs;
    exampleInputs.push_back(torch::zeros({1, static_cast<int64_t>(numFeatures)}, torch::kFloat32));

    torch::jit::Module traced = torch::jit::trace(model.ptr(), exampleInputs);
    try {
        traced.save(outPath.toStdString());
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
    // Input descriptor section
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Input descriptor"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        // Cutoff radius
        FloatParameterUI* cutoffUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::cutoffRadius));
        grid->addWidget(cutoffUI->label(), row, 0);
        grid->addLayout(cutoffUI->createFieldLayout(), row, 1);
        ++row;

        // Max neighbours
        IntegerParameterUI* numNeighUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::numNeighbors));
        grid->addWidget(numNeighUI->label(), row, 0);
        grid->addLayout(numNeighUI->createFieldLayout(), row, 1);
        ++row;

        // Label property name
        StringParameterUI* labelUI = createParamUI<StringParameterUI>(
            PROPERTY_FIELD(MLTrainingModifier::labelProperty));
        grid->addWidget(new QLabel(tr("Label property:"), box), row, 0);
        grid->addWidget(labelUI->textBox(), row, 1);

        mainLayout->addWidget(box);
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
            tr("<small>Evaluates the upstream pipeline at every animation frame,\n"
               "collects per-atom descriptors and labels, trains a 3-layer MLP,\n"
               "and saves the model as a TorchScript .pt file.</small>"),
            box));

        _trainButton = new QPushButton(tr("Collect from All Frames && Train"), box);
        _trainButton->setToolTip(tr(
            "Iterate over all animation frames, extract neighbour-distance\n"
            "descriptors and integer class labels, train the MLP, and save\n"
            "the TorchScript model to the specified output path."));
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
}

// ---------------------------------------------------------------------------
// updateTrainButtonState
// ---------------------------------------------------------------------------

void MLTrainingModifierEditor::updateTrainButtonState()
{
    if(!_trainButton) return;
#ifdef OVITO_ML_HAS_LIBTORCH
    _trainButton->setEnabled(true);
    _trainButton->setToolTip(_trainButton->toolTip()); // keep existing tooltip
#else
    _trainButton->setEnabled(false);
    _trainButton->setToolTip(tr("LibTorch is not available in this build.\n"
        "Rebuild OVITO with -DOVITO_USE_LIBTORCH=ON to enable training."));
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

    ModificationNode* node = modificationNode();
    if(!node) return;

    // -----------------------------------------------------------------------
    // Collect animation frame times
    // -----------------------------------------------------------------------
    AnimationSettings* anim = ui()->dataset()->animationSettings();
    if(!anim) return;

    const int firstFrame = anim->firstFrame();
    const int lastFrame  = anim->lastFrame();

    if(firstFrame > lastFrame) {
        QMessageBox::warning(parentWindow(),
            tr("No frames"),
            tr("The animation interval is empty. Load at least one .dump file first."));
        return;
    }

    std::vector<AnimationTime> times;
    times.reserve(static_cast<size_t>(lastFrame - firstFrame + 1));
    for(int f = firstFrame; f <= lastFrame; ++f)
        times.push_back(AnimationTime::fromFrame(f));

    // Capture parameters by value — the modifier might change or be deleted
    // while the async operations are in flight.
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
    if(cutoff <= 0.f) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Cutoff radius must be positive."));
        return;
    }
    if(outPath.isEmpty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Please specify an output model path (.pt)."));
        return;
    }

    if(_statusLabel)
        _statusLabel->setText(tr("Collecting data from %1 frames...").arg(times.size()));

    // -----------------------------------------------------------------------
    // Step 1: evaluate the upstream pipeline at every frame.
    //
    // evaluateInputMultiple() returns Future<vector<PipelineFlowState>>.
    // scheduleOperationAfter() shows a progress dialog and calls our
    // continuation in the GUI thread once all frames have been evaluated.
    // -----------------------------------------------------------------------
    auto evalFuture = node->evaluateInputMultiple(
        PipelineEvaluationRequest(AnimationTime::fromFrame(firstFrame),
                                  /*throwOnError=*/false,
                                  /*interactiveMode=*/false),
        std::move(times));

    scheduleOperationAfter(std::move(evalFuture),
        [this,
         cutoff, maxNeigh, labelProp,
         h1, h2, epochs, lr, batchSz, outPath]
        (std::vector<PipelineFlowState> states) mutable
    {
        if(states.empty()) {
            if(_statusLabel)
                _statusLabel->setText(tr("Error: pipeline returned no frames."));
            return;
        }

        if(_statusLabel)
            _statusLabel->setText(tr("Training MLP on %1 frames...").arg(states.size()));

        // -----------------------------------------------------------------------
        // Step 2: feature extraction + training in a background thread.
        //
        // asyncLaunch() runs on a thread-pool worker and returns
        // Future<TrainingResult>.
        // -----------------------------------------------------------------------
        auto trainFuture = asyncLaunch(
            [states = std::move(states),
             cutoff, maxNeigh, labelProp,
             h1, h2, epochs, lr, batchSz, outPath]() mutable -> TrainingResult
            {
                return trainMLP(std::move(states),
                                cutoff, maxNeigh, labelProp,
                                h1, h2, epochs, lr, batchSz, outPath);
            });

        // Wrap self in a QPointer so the continuation is safe even if the
        // editor gets destroyed before training finishes.
        QPointer<MLTrainingModifierEditor> self(this);

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
    });
#endif // OVITO_ML_HAS_LIBTORCH
}

}  // namespace Ovito
