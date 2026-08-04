////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifier.h>
#include <ovito/gui/desktop/properties/BooleanRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/core/dataset/io/FileSource.h>
#include "AmorphousCarbonTrackingModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonTrackingModifierEditor);
SET_OVITO_OBJECT_EDITOR(AmorphousCarbonTrackingModifier, AmorphousCarbonTrackingModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void AmorphousCarbonTrackingModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("Amorphous carbon cluster tracking"), rolloutParams);
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(6);

    QLabel* introLabel = new QLabel(tr(
        "Requires an upstream 'Amorphous carbon cluster analysis' modifier, applied both here "
        "and in the reference configuration below."));
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    QGroupBox* colorBox = new QGroupBox(tr("Visualization"));
    layout->addWidget(colorBox);
    QVBoxLayout* colorLayout = new QVBoxLayout(colorBox);
    IntegerRadioButtonParameterUI* colorUI =
        createParamUI<IntegerRadioButtonParameterUI>(
            PROPERTY_FIELD(AmorphousCarbonTrackingModifier::colorMode));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonTrackingModifier::NoColoring, tr("Preserve existing colors")));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonTrackingModifier::ColorByFate, tr("Color by cluster fate")));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonTrackingModifier::ColorByGenealogy, tr("Color by cluster genealogy")));

    QLabel* legend = new QLabel(tr(
        "Fate colors: main fragment = sky blue, minor fragment = indigo, still in phase but "
        "below the upstream core-filter threshold = teal, dispersed = amber, left the tracked "
        "hybridization state = deep red, not tracked = graphite. Cool hues mean the atom is "
        "still in the tracked phase."));
    legend->setWordWrap(true);
    layout->addWidget(legend);

    QLabel* genealogyLegend = new QLabel(tr(
        "Genealogy colors (current cluster): stable = sky blue, created (no significant "
        "ancestor in the reference) = deep red, merged (absorbed 2+ reference clusters) = "
        "amber, not a relevant cluster = graphite."));
    genealogyLegend->setWordWrap(true);
    layout->addWidget(genealogyLegend);

    QGroupBox* genealogyBox = new QGroupBox(tr("Bidirectional cluster genealogy"));
    layout->addWidget(genealogyBox);
    QGridLayout* genealogyGrid = new QGridLayout(genealogyBox);
    genealogyGrid->setColumnStretch(1, 1);

    FloatParameterUI* genealogyThresholdUI = createParamUI<FloatParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonTrackingModifier::genealogyOverlapFraction));
    genealogyThresholdUI->label()->setToolTip(
        tr("Minimum fraction of either cluster's own size that a shared-atom overlap must reach\n"
           "before it counts as a genealogical edge between a reference cluster and a current\n"
           "cluster. Each relevant cluster (upstream 'Minimum relevant cluster size') is then\n"
           "labeled purely from its own edge count: a reference cluster with 0/1/2+ edges is\n"
           "Disappeared/Stable/Fragmented; a current cluster with 0/1/2+ edges is\n"
           "Created/Stable/Merged. Validated robust across 0.05-0.30 on camorfo_sp3_60_mCRA."));
    genealogyGrid->addWidget(genealogyThresholdUI->label(), 0, 0);
    genealogyGrid->addLayout(genealogyThresholdUI->createFieldLayout(), 0, 1);

    QLabel* genealogyInfo = new QLabel(tr(
        "Adds \"Reference Cluster Genealogy\" (Disappeared/Stable/Fragmented, a property of the "
        "atom's reference cluster) and \"Cluster Genealogy\" (Created/Stable/Merged, a property "
        "of the atom's current cluster) particle properties, plus per-frame counts under "
        "AmorphousCarbonTracking.reference_clusters_* / current_clusters_*. Unlike the rank-1 "
        "fate/fraction attributes above, this covers every relevant cluster on both sides."));
    genealogyInfo->setWordWrap(true);
    genealogyGrid->addWidget(genealogyInfo, 1, 0, 1, 2);

    // --- Reference configuration source. ---
    QGroupBox* referenceSourceGroupBox = new QGroupBox(tr("Reference configuration source"));
    layout->addWidget(referenceSourceGroupBox);
    QGridLayout* sublayout = new QGridLayout(referenceSourceGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);

    _sourceButtonGroup = new QButtonGroup(this);
    connect(_sourceButtonGroup, &QButtonGroup::idClicked, this, &AmorphousCarbonTrackingModifierEditor::onSourceButtonClicked);
    QRadioButton* upstreamPipelineBtn = new QRadioButton(tr("Upstream pipeline"));
    QRadioButton* externalFileBtn = new QRadioButton(tr("External file"));
    _sourceButtonGroup->addButton(upstreamPipelineBtn, 0);
    _sourceButtonGroup->addButton(externalFileBtn, 1);
    sublayout->addWidget(upstreamPipelineBtn, 0, 0, 1, 2);
    sublayout->addWidget(externalFileBtn, 1, 0, 1, 2);

    // --- Reference animation frame. ---
    QGroupBox* referenceFrameGroupBox = new QGroupBox(tr("Reference animation frame"));
    layout->addWidget(referenceFrameGroupBox);
    sublayout = new QGridLayout(referenceFrameGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(0, 5);
    sublayout->setColumnStretch(2, 95);

    BooleanRadioButtonParameterUI* useFrameOffsetUI = createParamUI<BooleanRadioButtonParameterUI>(PROPERTY_FIELD(ReferenceConfigurationModifier::useReferenceFrameOffset));
    useFrameOffsetUI->buttonFalse()->setText(tr("Constant reference configuration"));
    sublayout->addWidget(useFrameOffsetUI->buttonFalse(), 0, 0, 1, 3);

    IntegerParameterUI* frameNumberUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(ReferenceConfigurationModifier::referenceFrameNumber));
    frameNumberUI->label()->setText(tr("Frame number:"));
    sublayout->addWidget(frameNumberUI->label(), 1, 1, 1, 1);
    sublayout->addLayout(frameNumberUI->createFieldLayout(), 1, 2, 1, 1);
    frameNumberUI->setEnabled(false);
    connect(useFrameOffsetUI->buttonFalse(), &QRadioButton::toggled, frameNumberUI, &IntegerParameterUI::setEnabled);

    useFrameOffsetUI->buttonTrue()->setText(tr("Relative to current frame"));
    sublayout->addWidget(useFrameOffsetUI->buttonTrue(), 2, 0, 1, 3);
    IntegerParameterUI* frameOffsetUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(ReferenceConfigurationModifier::referenceFrameOffset));
    frameOffsetUI->label()->setText(tr("Frame offset:"));
    sublayout->addWidget(frameOffsetUI->label(), 3, 1, 1, 1);
    sublayout->addLayout(frameOffsetUI->createFieldLayout(), 3, 2, 1, 1);
    frameOffsetUI->setEnabled(false);
    connect(useFrameOffsetUI->buttonTrue(), &QRadioButton::toggled, frameOffsetUI, &IntegerParameterUI::setEnabled);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the reference object.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(AmorphousCarbonTrackingModifier::referenceConfiguration), RolloutInsertionParameters().setTitle(tr("Reference: %1")));

    connect(this, &PropertiesEditor::contentsChanged, this, &AmorphousCarbonTrackingModifierEditor::onContentsChanged);
}

/******************************************************************************
* Is called when the user clicks one of the source mode buttons.
******************************************************************************/
void AmorphousCarbonTrackingModifierEditor::onSourceButtonClicked(int id)
{
    ReferenceConfigurationModifier* mod = static_object_cast<ReferenceConfigurationModifier>(editObject());
    if(!mod) return;

    performTransaction(tr("Set reference source mode"), [mod,id]() {
        if(id == 1) {
            // Create a file source object for loading the reference configuration from a separate file.
            mod->setReferenceConfiguration(OORef<FileSource>::create());
        }
        else {
            mod->setReferenceConfiguration(nullptr);
        }
    });
}

/******************************************************************************
* Is called when the object being edited changes.
******************************************************************************/
void AmorphousCarbonTrackingModifierEditor::onContentsChanged(RefTarget* editObject)
{
    ReferenceConfigurationModifier* mod = static_object_cast<ReferenceConfigurationModifier>(editObject);
    if(mod) {
        _sourceButtonGroup->button(0)->setEnabled(true);
        _sourceButtonGroup->button(1)->setEnabled(true);
        _sourceButtonGroup->button(mod->referenceConfiguration() ? 1 : 0)->setChecked(true);
    }
    else {
        _sourceButtonGroup->button(0)->setEnabled(false);
        _sourceButtonGroup->button(1)->setEnabled(false);
    }
}

}   // End of namespace
