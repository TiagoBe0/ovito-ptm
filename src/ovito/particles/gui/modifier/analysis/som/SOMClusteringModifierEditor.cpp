////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/modifier/analysis/som/SOMClusteringModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/TaskProgress.h>
#include <ovito/core/app/undo/UndoableTransaction.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/StringParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "SOMClusteringModifierEditor.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

#include <map>
#include <optional>
#include <random>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SOMClusteringModifierEditor);
SET_OVITO_OBJECT_EDITOR(SOMClusteringModifier, SOMClusteringModifierEditor);

// ---------------------------------------------------------------------------
// createUI
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("SOM clustering"), rolloutParams);
    QVBoxLayout* mainLayout = new QVBoxLayout(rollout);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // -----------------------------------------------------------------------
    // Layer list + editing panel
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("SOM layers (hierarchy, top = layer 1)"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        _layerListWidget = new QListWidget(box);
        _layerListWidget->setMinimumHeight(90);
        lay->addWidget(_layerListWidget);
        connect(_layerListWidget, &QListWidget::currentRowChanged, this, &SOMClusteringModifierEditor::onLayerSelectionChanged);

        QHBoxLayout* btnRow = new QHBoxLayout();
        QPushButton* addBtn = new QPushButton(tr("Add Layer"), box);
        QPushButton* removeBtn = new QPushButton(tr("Remove Layer"), box);
        QPushButton* upBtn = new QPushButton(tr("Move Up"), box);
        QPushButton* downBtn = new QPushButton(tr("Move Down"), box);
        btnRow->addWidget(addBtn);
        btnRow->addWidget(removeBtn);
        btnRow->addWidget(upBtn);
        btnRow->addWidget(downBtn);
        lay->addLayout(btnRow);
        connect(addBtn, &QPushButton::clicked, this, &SOMClusteringModifierEditor::onAddLayer);
        connect(removeBtn, &QPushButton::clicked, this, &SOMClusteringModifierEditor::onRemoveLayer);
        connect(upBtn, &QPushButton::clicked, this, &SOMClusteringModifierEditor::onMoveLayerUp);
        connect(downBtn, &QPushButton::clicked, this, &SOMClusteringModifierEditor::onMoveLayerDown);

        _layerParamsBox = new QGroupBox(tr("Selected layer"), box);
        QGridLayout* grid = new QGridLayout(_layerParamsBox);
        grid->setColumnStretch(1, 1);
        int row = 0;

        grid->addWidget(new QLabel(tr("Input features:")), row, 0, 1, 2);
        ++row;
        _featureListWidget = new QListWidget(_layerParamsBox);
        _featureListWidget->setSelectionMode(QAbstractItemView::NoSelection);
        _featureListWidget->setMinimumHeight(100);
        grid->addWidget(_featureListWidget, row, 0, 1, 2);
        ++row;
        connect(_featureListWidget, &QListWidget::itemChanged, this, &SOMClusteringModifierEditor::onFeatureItemChanged);

        grid->addWidget(new QLabel(tr("Scaling:")), row, 0);
        _scalingCombo = new QComboBox(_layerParamsBox);
        _scalingCombo->addItem(tr("Min-max (normal)"), QStringLiteral("normal"));
        _scalingCombo->addItem(tr("Z-score (standard)"), QStringLiteral("standard"));
        _scalingCombo->addItem(tr("Median/IQR (robust)"), QStringLiteral("robust"));
        grid->addWidget(_scalingCombo, row, 1);
        connect(_scalingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SOMClusteringModifierEditor::onLayerParamsChanged);
        ++row;

        grid->addWidget(new QLabel(tr("Neurons (N):")), row, 0);
        _neuronsSpin = new QSpinBox(_layerParamsBox);
        _neuronsSpin->setRange(1, 10000);
        grid->addWidget(_neuronsSpin, row, 1);
        connect(_neuronsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SOMClusteringModifierEditor::onLayerParamsChanged);
        ++row;

        grid->addWidget(new QLabel(tr("Max sigma:")), row, 0);
        _sigmaSpin = new QDoubleSpinBox(_layerParamsBox);
        _sigmaSpin->setRange(1e-6, 1000.0);
        _sigmaSpin->setDecimals(4);
        _sigmaSpin->setSingleStep(0.1);
        grid->addWidget(_sigmaSpin, row, 1);
        connect(_sigmaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SOMClusteringModifierEditor::onLayerParamsChanged);
        ++row;

        grid->addWidget(new QLabel(tr("Max eta (learning rate):")), row, 0);
        _etaSpin = new QDoubleSpinBox(_layerParamsBox);
        _etaSpin->setRange(1e-6, 1000.0);
        _etaSpin->setDecimals(4);
        _etaSpin->setSingleStep(0.1);
        grid->addWidget(_etaSpin, row, 1);
        connect(_etaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SOMClusteringModifierEditor::onLayerParamsChanged);
        ++row;

        grid->addWidget(new QLabel(tr("Training fraction (f):")), row, 0);
        _fractionSpin = new QDoubleSpinBox(_layerParamsBox);
        _fractionSpin->setRange(0.001, 1.0);
        _fractionSpin->setDecimals(3);
        _fractionSpin->setSingleStep(0.05);
        grid->addWidget(_fractionSpin, row, 1);
        connect(_fractionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SOMClusteringModifierEditor::onLayerParamsChanged);

        lay->addWidget(_layerParamsBox);
        mainLayout->addWidget(box);

        connect(this, &PropertiesEditor::contentsChanged, this, &SOMClusteringModifierEditor::loadLayersFromModifier);
        connect(this, &PropertiesEditor::pipelineInputChanged, this, &SOMClusteringModifierEditor::updateFeatureList);
    }

    // -----------------------------------------------------------------------
    // Layer id mapping
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Layer id mapping"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        IntegerRadioButtonParameterUI* mappingUI = createParamUI<IntegerRadioButtonParameterUI>(
            PROPERTY_FIELD(SOMClusteringModifier::mappingMode));
        QRadioButton* defBtn = mappingUI->addRadioButton(SOMClusteringModifier::Default, tr("Default: one property per layer"));
        defBtn->setToolTip(tr("Every layer's local cluster index is kept as its own separate\nparticle property (\"<name> Layer 1\", \"<name> Layer 2\", ...)."));
        lay->addWidget(defBtn);
        QRadioButton* godelBtn = mappingUI->addRadioButton(SOMClusteringModifier::Godel, tr("G\303\266del encoding: single integer id"));
        godelBtn->setToolTip(tr("Combines all layers into one Int64 id via prime-power encoding.\nUnique per branch, but can overflow on very deep/wide hierarchies."));
        lay->addWidget(godelBtn);
        QRadioButton* linearBtn = mappingUI->addRadioButton(SOMClusteringModifier::Linear, tr("Linear interpolation: single float id"));
        linearBtn->setToolTip(tr("Combines all layers into one floating-point id in (0,1) via\nrecursive interval narrowing."));
        lay->addWidget(linearBtn);
        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Output
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Output"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setColumnStretch(1, 1);

        grid->addWidget(new QLabel(tr("Output property name:")), 0, 0);
        StringParameterUI* nameUI = createParamUI<StringParameterUI>(PROPERTY_FIELD(SOMClusteringModifier::outputPropertyBaseName));
        grid->addWidget(nameUI->textBox(), 0, 1);

        IntegerParameterUI* seedUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(SOMClusteringModifier::randomSeed));
        grid->addWidget(seedUI->label(), 1, 0);
        grid->addLayout(seedUI->createFieldLayout(), 1, 1);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Train button + status
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Train"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);

        BooleanParameterUI* onlySelectedUI = createParamUI<BooleanParameterUI>(
            PROPERTY_FIELD(SOMClusteringModifier::onlySelectedParticles));
        onlySelectedUI->checkBox()->setToolTip(tr(
            "Fit the model on the selected particles only (e.g. just the Wigner-Seitz vacancy\n"
            "candidates), which is also what determines the per-feature scaling statistics.\n"
            "The model then classifies only selected particles on every frame; unselected ones\n"
            "are left unassigned. Takes effect on the next Train click."));
        lay->addWidget(onlySelectedUI->checkBox());

        QLabel* info = new QLabel(tr("<small>Trains the whole layer hierarchy on the atoms of the "
            "currently displayed animation frame. The trained model is then applied consistently "
            "to every frame until you train again.</small>"), box);
        info->setWordWrap(true);
        lay->addWidget(info);

        _trainButton = new QPushButton(tr("Train on Current Frame"), box);
        lay->addWidget(_trainButton);
        connect(_trainButton, &QPushButton::clicked, this, &SOMClusteringModifierEditor::onTrainClicked);

        _statusLabel = new QLabel(tr("Not trained yet."), box);
        _statusLabel->setWordWrap(true);
        lay->addWidget(_statusLabel);

        mainLayout->addWidget(box);
    }

    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    loadLayersFromModifier();
}

// ---------------------------------------------------------------------------
// loadLayersFromModifier
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::loadLayersFromModifier()
{
    auto* mod = static_cast<SOMClusteringModifier*>(editObject());
    if(!mod) return;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(mod->layersConfigJson().toUtf8(), &err);
    QString ignoredError;
    _layers = (err.error == QJsonParseError::NoError && doc.isArray())
        ? SomAlgorithm::layersFromJson(doc.array(), ignoredError)
        : std::vector<SomAlgorithm::LayerConfig>{};

    if(_currentLayerIndex >= static_cast<int>(_layers.size()))
        _currentLayerIndex = static_cast<int>(_layers.size()) - 1;

    refreshLayerListWidget();
    refreshLayerParamWidgets();
    if(!mod->trainingProvenance().isEmpty() && _statusLabel)
        _statusLabel->setText(mod->trainingProvenance());
}

// ---------------------------------------------------------------------------
// commitLayers
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::commitLayers(const QString& undoText)
{
    auto* mod = static_cast<SOMClusteringModifier*>(editObject());
    if(!mod) return;
    const QJsonDocument doc(SomAlgorithm::layersToJson(_layers));
    UndoableTransaction t;
    t.begin(ui(), undoText);
    mod->setLayersConfigJson(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    t.commit();
}

// ---------------------------------------------------------------------------
// refreshLayerListWidget
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::refreshLayerListWidget()
{
    if(!_layerListWidget) return;
    _updatingLayerList = true;
    _layerListWidget->clear();
    for(size_t i = 0; i < _layers.size(); ++i) {
        const SomAlgorithm::LayerConfig& cfg = _layers[i];
        _layerListWidget->addItem(tr("Layer %1 \342\200\224 N=%2, %3, %4 feature(s)")
            .arg(i + 1).arg(cfg.numNeurons).arg(SomAlgorithm::scalingMethodToString(cfg.scaling)).arg(cfg.featureRefs.size()));
    }
    if(_currentLayerIndex >= 0 && _currentLayerIndex < _layerListWidget->count())
        _layerListWidget->setCurrentRow(_currentLayerIndex);
    _updatingLayerList = false;
}

// ---------------------------------------------------------------------------
// onLayerSelectionChanged
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::onLayerSelectionChanged()
{
    if(_updatingLayerList || !_layerListWidget) return;
    _currentLayerIndex = _layerListWidget->currentRow();
    refreshLayerParamWidgets();
}

// ---------------------------------------------------------------------------
// onAddLayer / onRemoveLayer / onMoveLayerUp / onMoveLayerDown
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::onAddLayer()
{
    SomAlgorithm::LayerConfig cfg;
    _layers.push_back(cfg);
    _currentLayerIndex = static_cast<int>(_layers.size()) - 1;
    commitLayers(tr("Add SOM layer"));
    refreshLayerListWidget();
    refreshLayerParamWidgets();
}

void SOMClusteringModifierEditor::onRemoveLayer()
{
    if(_currentLayerIndex < 0 || _currentLayerIndex >= static_cast<int>(_layers.size())) return;
    _layers.erase(_layers.begin() + _currentLayerIndex);
    if(_currentLayerIndex >= static_cast<int>(_layers.size()))
        _currentLayerIndex = static_cast<int>(_layers.size()) - 1;
    commitLayers(tr("Remove SOM layer"));
    refreshLayerListWidget();
    refreshLayerParamWidgets();
}

void SOMClusteringModifierEditor::onMoveLayerUp()
{
    if(_currentLayerIndex <= 0 || _currentLayerIndex >= static_cast<int>(_layers.size())) return;
    std::swap(_layers[_currentLayerIndex - 1], _layers[_currentLayerIndex]);
    --_currentLayerIndex;
    commitLayers(tr("Reorder SOM layers"));
    refreshLayerListWidget();
    refreshLayerParamWidgets();
}

void SOMClusteringModifierEditor::onMoveLayerDown()
{
    if(_currentLayerIndex < 0 || _currentLayerIndex + 1 >= static_cast<int>(_layers.size())) return;
    std::swap(_layers[_currentLayerIndex + 1], _layers[_currentLayerIndex]);
    ++_currentLayerIndex;
    commitLayers(tr("Reorder SOM layers"));
    refreshLayerListWidget();
    refreshLayerParamWidgets();
}

// ---------------------------------------------------------------------------
// refreshLayerParamWidgets
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::refreshLayerParamWidgets()
{
    if(!_layerParamsBox) return;
    const bool hasSelection = _currentLayerIndex >= 0 && _currentLayerIndex < static_cast<int>(_layers.size());
    _layerParamsBox->setEnabled(hasSelection);
    if(!hasSelection) {
        if(_featureListWidget) _featureListWidget->clear();
        return;
    }

    const SomAlgorithm::LayerConfig& cfg = _layers[_currentLayerIndex];
    _updatingLayerParams = true;
    if(_scalingCombo) {
        const int idx = _scalingCombo->findData(SomAlgorithm::scalingMethodToString(cfg.scaling));
        _scalingCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if(_neuronsSpin) _neuronsSpin->setValue(cfg.numNeurons);
    if(_sigmaSpin) _sigmaSpin->setValue(cfg.maxSigma);
    if(_etaSpin) _etaSpin->setValue(cfg.maxEta);
    if(_fractionSpin) _fractionSpin->setValue(cfg.sampleFraction);
    _updatingLayerParams = false;

    updateFeatureList();
}

// ---------------------------------------------------------------------------
// onLayerParamsChanged
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::onLayerParamsChanged()
{
    if(_updatingLayerParams) return;
    if(_currentLayerIndex < 0 || _currentLayerIndex >= static_cast<int>(_layers.size())) return;

    SomAlgorithm::LayerConfig& cfg = _layers[_currentLayerIndex];
    cfg.scaling = SomAlgorithm::scalingMethodFromString(_scalingCombo->currentData().toString());
    cfg.numNeurons = _neuronsSpin->value();
    cfg.maxSigma = _sigmaSpin->value();
    cfg.maxEta = _etaSpin->value();
    cfg.sampleFraction = _fractionSpin->value();

    commitLayers(tr("Change SOM layer parameters"));
    refreshLayerListWidget();
}

// ---------------------------------------------------------------------------
// updateFeatureList
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::updateFeatureList()
{
    if(!_featureListWidget) return;
    if(_currentLayerIndex < 0 || _currentLayerIndex >= static_cast<int>(_layers.size())) {
        _featureListWidget->clear();
        return;
    }

    _updatingFeatureList = true;
    const QStringList selected = _layers[_currentLayerIndex].featureRefs;
    _featureListWidget->clear();

    for(const PipelineFlowState& state : getPipelineInputs()) {
        const Particles* particles = state.getObject<Particles>();
        if(!particles) continue;

        for(const Property* prop : particles->properties()) {
            const int dt = prop->dataType();
            if(dt != QMetaType::Float && dt != QMetaType::Double &&
               dt != QMetaType::Int && dt != QMetaType::LongLong)
                continue;

            const size_t nComp = prop->componentCount();
            const QStringList& compNames = prop->componentNames();

            if(nComp == 1) {
                PropertyReference ref(prop, -1);
                const QString key = ref.nameWithComponent();
                auto* item = new QListWidgetItem(prop->name(), _featureListWidget);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, key);
            }
            else {
                for(size_t c = 0; c < nComp; ++c) {
                    PropertyReference ref(prop, static_cast<int>(c));
                    const QString key = ref.nameWithComponent();
                    const QString label = (c < static_cast<size_t>(compNames.size()))
                        ? QStringLiteral("%1.%2").arg(prop->name(), compNames[static_cast<int>(c)])
                        : QStringLiteral("%1.%2").arg(prop->name()).arg(c);
                    auto* item = new QListWidgetItem(label, _featureListWidget);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                    item->setData(Qt::UserRole, key);
                }
            }
        }
        break; // Only the first frame is used to infer available columns.
    }
    _updatingFeatureList = false;
}

// ---------------------------------------------------------------------------
// onFeatureItemChanged
// ---------------------------------------------------------------------------

void SOMClusteringModifierEditor::onFeatureItemChanged(QListWidgetItem*)
{
    if(_updatingFeatureList) return;
    if(_currentLayerIndex < 0 || _currentLayerIndex >= static_cast<int>(_layers.size())) return;

    QStringList newList;
    for(int i = 0; i < _featureListWidget->count(); ++i) {
        QListWidgetItem* it = _featureListWidget->item(i);
        if(it->checkState() == Qt::Checked)
            newList << it->data(Qt::UserRole).toString();
    }
    _layers[_currentLayerIndex].featureRefs = newList;
    commitLayers(tr("Change SOM layer features"));
    refreshLayerListWidget();
}

// ---------------------------------------------------------------------------
// onTrainClicked
// ---------------------------------------------------------------------------

namespace {

struct SomTrainResult {
    SomAlgorithm::SomTreeNode tree;
    std::vector<SomAlgorithm::LayerConfig> layers;
    SOMClusteringModifier::MappingMode mode;
    /// Number of atoms handed to trainTree() (after the selection filter, before the
    /// non-finite filter that trainTree() applies internally).
    size_t atomCount = 0;
    size_t skippedNonFinite = 0;
    bool trainedOnSelection = false;
    /// Total atoms in the training frame, for context when a selection was used.
    size_t frameParticleCount = 0;
};

/// Walks the trained tree breadth-first to build a short human-readable provenance summary
/// for the status label: for each layer except the last, how many distinct groups it
/// actually discovered (i.e. how many non-empty branches its SOM(s) produced).
///
/// The deepest configured layer's own discovered-group count is NOT stored in the tree --
/// SomAlgorithm::trainTree() only records children where there is a further layer to recurse
/// into, so a leaf SOM's fan-out is never persisted. Reporting "0" for it would be actively
/// wrong, so it is omitted; for a single-layer model this means there is nothing to report
/// here at all (the discovered groups are visible per-atom in the output propert(ies)
/// instead). An earlier version of this function shifted every count onto the NEXT layer's
/// label (e.g. printing layer 1's fan-out under "layer 2"), which was misleading for
/// multi-layer models and always trivially "1" for the single-layer case.
QString describeTrainedTree(const SomAlgorithm::SomTreeNode& root, int numLayers, size_t atomCount,
    size_t skippedNonFinite, bool trainedOnSelection, size_t frameParticleCount)
{
    QStringList parts;
    std::vector<const SomAlgorithm::SomTreeNode*> level{&root};
    for(int l = 0; l < numLayers && !level.empty(); ++l) {
        std::vector<const SomAlgorithm::SomTreeNode*> next;
        for(const SomAlgorithm::SomTreeNode* n : level)
            for(const auto& [id, child] : n->children)
                next.push_back(&child);
        if(l + 1 < numLayers)
            parts << QStringLiteral("layer %1 found %2 group(s)").arg(l + 1).arg(next.size());
        level.swap(next);
    }
    const QString detail = parts.isEmpty()
        ? QStringLiteral("per-atom group counts are visible in the output propert(ies)")
        : parts.join(QStringLiteral(", "));
    QString summary = QStringLiteral("Trained %1 layer(s) on %2 atoms \342\200\224 %3.")
        .arg(numLayers).arg(atomCount - skippedNonFinite).arg(detail);
    if(trainedOnSelection) {
        summary += QStringLiteral(" Fitted on the %1 selected atoms of the %2 in the training frame; "
            "the model classifies selected particles only.").arg(atomCount).arg(frameParticleCount);
    }
    if(skippedNonFinite > 0) {
        summary += QStringLiteral(" %1 of those %2 atoms were excluded because a required feature "
            "was non-finite (NaN/inf); they cannot be classified either.")
            .arg(skippedNonFinite).arg(atomCount);
    }
    return summary;
}

} // anonymous namespace

void SOMClusteringModifierEditor::onTrainClicked()
{
    auto* mod = static_cast<SOMClusteringModifier*>(editObject());
    if(!mod) return;

    if(_layers.empty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"), tr("Please add at least one SOM layer."));
        return;
    }
    for(size_t i = 0; i < _layers.size(); ++i) {
        const SomAlgorithm::LayerConfig& cfg = _layers[i];
        if(cfg.featureRefs.isEmpty()) {
            QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
                tr("Layer %1 has no input features selected.").arg(i + 1));
            return;
        }
        if(cfg.numNeurons < 1) {
            QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
                tr("Layer %1 must have at least 1 neuron.").arg(i + 1));
            return;
        }
        if(cfg.sampleFraction <= 0.0 || cfg.sampleFraction > 1.0) {
            QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
                tr("Layer %1's training fraction must be in (0, 1].").arg(i + 1));
            return;
        }
    }

    if(_statusLabel) _statusLabel->setText(tr("Collecting data from the current frame..."));

    PipelineFlowState state = getPipelineInput();
    if(state.status().type() == PipelineStatus::Error || !state) {
        if(_statusLabel)
            _statusLabel->setText(tr("Error: no valid pipeline data for the current frame."));
        return;
    }
    const Particles* particles = state.getObject<Particles>();
    if(!particles || !particles->getProperty(Particles::PositionProperty)) {
        if(_statusLabel)
            _statusLabel->setText(tr("Error: the current frame has no particle positions."));
        return;
    }

    // Resolve every distinct feature referenced by any layer.
    QStringList allRefs;
    std::vector<ConstPropertyPtr> refProps;
    std::vector<int> refComps;
    QString missingRef;
    if(!SOMClusteringModifier::resolveFeatureRefs(_layers, particles, allRefs, refProps, refComps, missingRef)) {
        if(_statusLabel)
            _statusLabel->setText(tr("Error: input feature '%1' not found in the current frame.").arg(missingRef));
        return;
    }

    const size_t particleCount = particles->getProperty(Particles::PositionProperty)->size();

    const bool onlySelected = mod->onlySelectedParticles();
    ConstPropertyPtr selectionProperty;
    if(onlySelected) {
        const Property* sel = particles->getProperty(Particles::SelectionProperty);
        if(!sel) {
            if(_statusLabel)
                _statusLabel->setText(tr("Error: \"Use only selected particles\" is enabled but the current "
                    "frame has no Selection property. Insert a selection modifier upstream, or turn the option off."));
            return;
        }
        selectionProperty = ConstPropertyPtr(sel);
    }

    std::map<QString, int> featureIndex;
    for(int r = 0; r < allRefs.size(); ++r)
        featureIndex.emplace(allRefs[r], r);

    if(_statusLabel) _statusLabel->setText(tr("Training SOM hierarchy..."));

    const std::vector<SomAlgorithm::LayerConfig> layersCopy = _layers;
    const SOMClusteringModifier::MappingMode modeEnum = mod->mappingMode();
    const int seed = mod->randomSeed();

    QPointer<SOMClusteringModifierEditor> self(this);

    // The worker task launched below from a button click has no parent task, so
    // this_task::get()->userInterface() is null inside it. Hand the UI over explicitly:
    // otherwise the progress text never reaches the ProgressDialog opened by
    // scheduleOperationAfter(), which then sits there blank for the whole training run.
    OORef<UserInterface> taskUi = &ui();

    // Per-atom feature-vector extraction (O(particleCount x numFeatures)) is deliberately
    // done INSIDE the background task, not here on the UI thread -- only ConstPropertyPtr
    // handles (cheap, ref-counted) are captured across the asyncLaunch boundary. Extracting
    // synchronously here would block the whole UI for however long a large trajectory frame
    // takes to walk, which is exactly what asyncLaunch exists to avoid; see MLTrainingModifierEditor's
    // trainMLP() for the same pattern (feature extraction happens inside its async lambda too).
    auto trainFuture = asyncLaunch(
        [refProps = std::move(refProps), refComps = std::move(refComps), allRefs, layersCopy,
         featureIndex, modeEnum, seed, particleCount, selectionProperty = std::move(selectionProperty),
         taskUi = std::move(taskUi)]() mutable -> SomTrainResult
        {
            // Report progress through the UI captured above. Never call this_task::ui() here:
            // it asserts (and, in a release build, dereferences a null pointer) because no
            // UI is associated with this worker task.
            const bool hasTaskUi = (bool)taskUi;
            TaskProgress& progress = hasTaskUi
                ? *new TaskProgress(taskUi.get())
                : TaskProgress::Ignore;
            struct ProgressGuard {
                TaskProgress& ref; bool owned;
                ~ProgressGuard() { if(owned) delete &ref; }
            } progressGuard{progress, hasTaskUi};

            progress.setText(QStringLiteral("Extracting SOM training features"));

            std::vector<SomAlgorithm::TrainingSample> samples;
            {
                std::vector<BufferAccessConvertedTo<float>> accessors;
                accessors.reserve(refProps.size());
                for(const ConstPropertyPtr& prop : refProps)
                    accessors.emplace_back(prop.get());

                std::optional<BufferReadAccess<SelectionIntType>> selectionAcc;
                if(selectionProperty)
                    selectionAcc.emplace(selectionProperty);

                samples.reserve(particleCount);
                for(size_t i = 0; i < particleCount; ++i) {
                    if((i & 8191) == 0)
                        this_task::throwIfCanceled();
                    // Unselected atoms are not merely skipped in the training walk: they must
                    // not reach the per-feature scaling statistics either, since those are what
                    // define the model's domain of validity.
                    if(selectionAcc && (*selectionAcc)[i] == 0)
                        continue;
                    SomAlgorithm::TrainingSample sample;
                    sample.allFeatureValues.resize(static_cast<size_t>(allRefs.size()));
                    for(int r = 0; r < allRefs.size(); ++r) {
                        const int nComp = refProps[static_cast<size_t>(r)]->componentCount();
                        sample.allFeatureValues[static_cast<size_t>(r)] =
                            accessors[static_cast<size_t>(r)][i * static_cast<size_t>(nComp) + static_cast<size_t>(refComps[static_cast<size_t>(r)])];
                    }
                    samples.push_back(std::move(sample));
                }
            }

            if(samples.empty())
                throw Exception(QStringLiteral("SOM training: no particles to train on "
                    "(the current frame has no selected particles)."));

            progress.setText(QStringLiteral("Training SOM hierarchy"));
            std::mt19937 rng(seed < 0 ? std::random_device{}() : static_cast<unsigned int>(seed));
            SomTrainResult result;
            result.layers = layersCopy;
            result.mode = modeEnum;
            result.atomCount = samples.size();
            result.trainedOnSelection = (selectionProperty != nullptr);
            result.frameParticleCount = particleCount;
            SomAlgorithm::TrainingReport report;
            result.tree = SomAlgorithm::trainTree(layersCopy, samples, featureIndex, rng,
                [&progress](const QString& text) { progress.setText(text); }, &report);
            result.skippedNonFinite = report.skippedNonFinite;
            return result;
        });

    scheduleOperationAfter(std::move(trainFuture),
        [self](SomTrainResult result) {
            if(!self) return;
            auto* mod = static_cast<SOMClusteringModifier*>(self->editObject());
            if(!mod) return;

            SomAlgorithm::TrainedModel model;
            model.layers = result.layers;
            model.mappingMode = SOMClusteringModifier::toAlgorithmMappingMode(result.mode);
            model.trainedOnSelection = result.trainedOnSelection;
            model.tree = std::move(result.tree);

            const QJsonDocument doc(model.toJson());
            const QString provenance = describeTrainedTree(model.tree, static_cast<int>(model.layers.size()), result.atomCount,
                result.skippedNonFinite, result.trainedOnSelection, result.frameParticleCount);

            UndoableTransaction t;
            t.begin(self->ui(), self->tr("Train SOM model"));
            mod->setTrainedModelJson(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
            mod->setTrainingProvenance(provenance);
            t.commit();

            if(self->_statusLabel)
                self->_statusLabel->setText(provenance);
        });
}

} // End of namespace
