////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <ovito/particles/modifier/analysis/som/SomAlgorithm.h>

#include <vector>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QGroupBox;

namespace Ovito {

/**
 * \brief Properties editor panel for SOMClusteringModifier.
 *
 * Layout:
 *   [Layers]        List of configured SOM layers (add/remove/move) + per-selected-layer
 *                    editing panel (feature checklist, scaling method, N/sigma/eta/fraction).
 *   [Mapping]        Godel / Linear / Default radio buttons.
 *   [Output]         Random seed, output property base name.
 *   [Train button]   Trains the whole hierarchical tree on the current animation frame.
 *   [Status label]   Shows layer/branch provenance of the last training run.
 *
 * The layer list is not a real property field (no precedent in this codebase for a
 * QVector<CustomStruct> property field) -- it is edited here as an in-memory
 * QVector<SomAlgorithm::LayerConfig>, serialized to/from the modifier's layersConfigJson()
 * QString field on every change.
 */
class SOMClusteringModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(SOMClusteringModifierEditor)
    Q_OBJECT

protected:
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:
    /// Re-parses layersConfigJson() from the modifier into _layers and refreshes the UI.
    void loadLayersFromModifier();

    /// Repopulates the feature checklist for the currently selected layer from the
    /// upstream pipeline's available particle properties.
    void updateFeatureList();

    /// Commits a checked/unchecked feature item to _layers[_currentLayerIndex].
    void onFeatureItemChanged(QListWidgetItem* item);

    void onLayerSelectionChanged();
    void onAddLayer();
    void onRemoveLayer();
    void onMoveLayerUp();
    void onMoveLayerDown();

    /// Commits the scaling/N/sigma/eta/fraction spin boxes to the selected layer.
    void onLayerParamsChanged();

    void onTrainClicked();

private:
    /// Serializes _layers and writes it to the modifier's layersConfigJson field.
    void commitLayers(const QString& undoText);

    /// Refreshes _layerListWidget's row labels from _layers, preserving selection.
    void refreshLayerListWidget();

    /// Refreshes the per-layer editing panel (feature list, scaling combo, spin boxes)
    /// for _currentLayerIndex; disables the panel if no layer is selected.
    void refreshLayerParamWidgets();

    /// In-memory editable copy of the modifier's layer configuration.
    std::vector<SomAlgorithm::LayerConfig> _layers;
    int _currentLayerIndex = -1;

    /// Guard flags to prevent re-entrant widget <-> data synchronization.
    bool _updatingLayerList = false;
    bool _updatingFeatureList = false;
    bool _updatingLayerParams = false;

    QListWidget* _layerListWidget = nullptr;
    QGroupBox* _layerParamsBox = nullptr;
    QListWidget* _featureListWidget = nullptr;
    QComboBox* _scalingCombo = nullptr;
    QSpinBox* _neuronsSpin = nullptr;
    QDoubleSpinBox* _sigmaSpin = nullptr;
    QDoubleSpinBox* _etaSpin = nullptr;
    QDoubleSpinBox* _fractionSpin = nullptr;

    QPushButton* _trainButton = nullptr;
    QLabel* _statusLabel = nullptr;
};

} // End of namespace
