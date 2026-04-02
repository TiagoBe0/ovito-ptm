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
#include <ovito/particles/modifier/analysis/wignerseitz/WignerSeitzAnalysisModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/core/dataset/io/FileSource.h>
#include "WignerSeitzAnalysisModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(WignerSeitzAnalysisModifierEditor);
SET_OVITO_OBJECT_EDITOR(WignerSeitzAnalysisModifier, WignerSeitzAnalysisModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void WignerSeitzAnalysisModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Wigner-Seitz defect analysis"), rolloutParams, "manual:particles.modifiers.wigner_seitz_analysis");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    QGroupBox* optionsGroupBox = new QGroupBox(tr("Output options"));
    layout->addWidget(optionsGroupBox);

    QGridLayout* sublayout = new QGridLayout(optionsGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);

    BooleanParameterUI* perTypeOccupancyUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(WignerSeitzAnalysisModifier::perTypeOccupancy));

    sublayout->setColumnMinimumWidth(0, 16);
    sublayout->addWidget(new QLabel(tr("Output mode:")), 0, 0, 1, 2);
    BooleanRadioButtonParameterUI* outputCurrentConfigUI = createParamUI<BooleanRadioButtonParameterUI>(PROPERTY_FIELD(WignerSeitzAnalysisModifier::outputCurrentConfig));
    outputCurrentConfigUI->buttonFalse()->setText(tr("Sites (reference config)"));
    sublayout->addWidget(outputCurrentConfigUI->buttonFalse(), 1, 1);
    outputCurrentConfigUI->buttonTrue()->setText(tr("Atoms (displaced config)"));
    sublayout->addWidget(outputCurrentConfigUI->buttonTrue(), 2, 1);

    sublayout->setRowMinimumHeight(3, 6);
    sublayout->addWidget(perTypeOccupancyUI->checkBox(), 4, 0, 1, 2);

    QGroupBox* adaptiveCellGroupBox = new QGroupBox(tr("Adaptive Wigner-Seitz cell"));
    layout->addWidget(adaptiveCellGroupBox);

    QGridLayout* adaptiveLayout = new QGridLayout(adaptiveCellGroupBox);
    adaptiveLayout->setContentsMargins(4,4,4,4);
    adaptiveLayout->setSpacing(4);

    BooleanParameterUI* adaptiveCellUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(WignerSeitzAnalysisModifier::adaptiveCell));
    adaptiveCellUI->checkBox()->setText(tr("Enable adaptive cell (strain-weighted Voronoi)"));
    adaptiveLayout->addWidget(adaptiveCellUI->checkBox(), 0, 0, 1, 3);

    FloatParameterUI* strainSensitivityUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(WignerSeitzAnalysisModifier::strainSensitivity));
    strainSensitivityUI->label()->setText(tr("Strain sensitivity:"));
    adaptiveLayout->addWidget(strainSensitivityUI->label(), 1, 1);
    adaptiveLayout->addLayout(strainSensitivityUI->createFieldLayout(), 1, 2);
    strainSensitivityUI->setEnabled(false);
    connect(adaptiveCellUI->checkBox(), &QCheckBox::toggled, strainSensitivityUI, &FloatParameterUI::setEnabled);

    QGroupBox* mappingGroupBox = new QGroupBox(tr("Affine mapping of simulation cell"));
    layout->addWidget(mappingGroupBox);

    sublayout = new QGridLayout(mappingGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);

    IntegerRadioButtonParameterUI* affineMappingUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(ReferenceConfigurationModifier::affineMapping));
    sublayout->addWidget(affineMappingUI->addRadioButton(ReferenceConfigurationModifier::NO_MAPPING, tr("Off")), 0, 0);
    sublayout->addWidget(affineMappingUI->addRadioButton(ReferenceConfigurationModifier::TO_REFERENCE_CELL, tr("To reference")), 0, 1);

    QGroupBox* referenceSourceGroupBox = new QGroupBox(tr("Reference configuration source"));
    layout->addWidget(referenceSourceGroupBox);

    sublayout = new QGridLayout(referenceSourceGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);

    _sourceButtonGroup = new QButtonGroup(this);
    connect(_sourceButtonGroup, &QButtonGroup::idClicked, this, &WignerSeitzAnalysisModifierEditor::onSourceButtonClicked);
    QRadioButton* upstreamPipelineBtn = new QRadioButton(tr("Upstream pipeline"));
    QRadioButton* externalFileBtn = new QRadioButton(tr("External file"));
    _sourceButtonGroup->addButton(upstreamPipelineBtn, 0);
    _sourceButtonGroup->addButton(externalFileBtn, 1);
    sublayout->addWidget(upstreamPipelineBtn, 0, 0, 1, 2);
    sublayout->addWidget(externalFileBtn, 1, 0, 1, 2);

    QGroupBox* referenceFrameGroupBox = new QGroupBox(tr("Reference animation frame"));
    layout->addWidget(referenceFrameGroupBox);

    sublayout = new QGridLayout(referenceFrameGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(0, 5);
    sublayout->setColumnStretch(2, 95);

    // Add box for selection between absolute and relative reference frames.
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
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(WignerSeitzAnalysisModifier::referenceConfiguration), RolloutInsertionParameters().setTitle(tr("Reference: %1")));

    connect(this, &PropertiesEditor::contentsChanged, this, &WignerSeitzAnalysisModifierEditor::onContentsChanged);

    // Whenever the pipeline input of the modifier changes, update the state of the UI.
    connect(this, &PropertiesEditor::pipelineInputChanged, this, [this, mappingGroupBox]() {
        mappingGroupBox->setEnabled(getPipelineInput().getObject<SimulationCell>() != nullptr);
    });
}

/******************************************************************************
* Is called when the user clicks one of the source mode buttons.
******************************************************************************/
void WignerSeitzAnalysisModifierEditor::onSourceButtonClicked(int id)
{
    ReferenceConfigurationModifier* mod = static_object_cast<ReferenceConfigurationModifier>(editObject());
    if(!mod) return;

    performTransaction(tr("Set reference source mode"), [mod,id]() {
        if(id == 1) {
            // Create a file source object, which can be used for loading
            // the reference configuration from a separate file.
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
void WignerSeitzAnalysisModifierEditor::onContentsChanged(RefTarget* editObject)
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
