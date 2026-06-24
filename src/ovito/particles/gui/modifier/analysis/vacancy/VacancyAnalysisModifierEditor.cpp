////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
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
#include <ovito/particles/modifier/analysis/vacancy/VacancyAnalysisModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/core/dataset/io/FileSource.h>
#include "VacancyAnalysisModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(VacancyAnalysisModifierEditor);
SET_OVITO_OBJECT_EDITOR(VacancyAnalysisModifier, VacancyAnalysisModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void VacancyAnalysisModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Vacancy analysis (consensus)"), rolloutParams);

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    // --- Frenkel-pair recombination. ---
    QGroupBox* frenkelGroupBox = new QGroupBox(tr("Frenkel-pair recombination"));
    layout->addWidget(frenkelGroupBox);
    QGridLayout* sublayout = new QGridLayout(frenkelGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    BooleanParameterUI* frenkelUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::frenkelRecombination));
    frenkelUI->checkBox()->setText(tr("Recombine adjacent vacancy-interstitial pairs"));
    sublayout->addWidget(frenkelUI->checkBox(), 0, 0, 1, 2);

    FloatParameterUI* recombCutoffUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::recombinationCutoff));
    recombCutoffUI->label()->setText(tr("Cutoff (0 = auto):"));
    sublayout->addWidget(recombCutoffUI->label(), 1, 0);
    sublayout->addLayout(recombCutoffUI->createFieldLayout(), 1, 1);
    connect(frenkelUI->checkBox(), &QCheckBox::toggled, recombCutoffUI, &FloatParameterUI::setEnabled);
    recombCutoffUI->setEnabled(frenkelUI->checkBox()->isChecked());

    // --- Free-volume confirmation. ---
    QGroupBox* freeVolGroupBox = new QGroupBox(tr("Free-volume confirmation"));
    layout->addWidget(freeVolGroupBox);
    sublayout = new QGridLayout(freeVolGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    BooleanParameterUI* freeVolUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::requireFreeVolume));
    freeVolUI->checkBox()->setText(tr("Require a real cavity at the vacant site"));
    sublayout->addWidget(freeVolUI->checkBox(), 0, 0, 1, 2);

    FloatParameterUI* cavityRatioUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::cavityRadiusRatio));
    cavityRatioUI->label()->setText(tr("Min. cavity radius (× nn dist.):"));
    sublayout->addWidget(cavityRatioUI->label(), 1, 0);
    sublayout->addLayout(cavityRatioUI->createFieldLayout(), 1, 1);
    connect(freeVolUI->checkBox(), &QCheckBox::toggled, cavityRatioUI, &FloatParameterUI::setEnabled);
    cavityRatioUI->setEnabled(freeVolUI->checkBox()->isChecked());

    // --- Structural masking. ---
    QGroupBox* maskGroupBox = new QGroupBox(tr("Disordered-region masking"));
    layout->addWidget(maskGroupBox);
    sublayout = new QGridLayout(maskGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    BooleanParameterUI* maskUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::maskDisordered));
    maskUI->checkBox()->setText(tr("Exclude vacancies in grain boundaries / surfaces"));
    sublayout->addWidget(maskUI->checkBox(), 0, 0, 1, 2);

    sublayout->addWidget(new QLabel(tr("Disorder signal (from upstream modifier):")), 1, 0, 1, 2);
    IntegerRadioButtonParameterUI* maskSignalUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::maskSignal));
    QRadioButton* structureBtn = maskSignalUI->addRadioButton(VacancyAnalysisModifier::StructureSignal, tr("Structure Type (PTM/CNA)"));
    QRadioButton* cspBtn = maskSignalUI->addRadioButton(VacancyAnalysisModifier::CentrosymmetrySignal, tr("Centrosymmetry"));
    QRadioButton* shearBtn = maskSignalUI->addRadioButton(VacancyAnalysisModifier::ShearStrainSignal, tr("Shear Strain"));
    sublayout->addWidget(structureBtn, 2, 0, 1, 2);
    sublayout->addWidget(cspBtn, 3, 0, 1, 2);
    sublayout->addWidget(shearBtn, 4, 0, 1, 2);

    FloatParameterUI* maskThresholdUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::maskThreshold));
    maskThresholdUI->label()->setText(tr("Disorder fraction threshold:"));
    sublayout->addWidget(maskThresholdUI->label(), 5, 0);
    sublayout->addLayout(maskThresholdUI->createFieldLayout(), 5, 1);

    connect(maskUI->checkBox(), &QCheckBox::toggled, maskSignalUI, &IntegerRadioButtonParameterUI::setEnabled);
    connect(maskUI->checkBox(), &QCheckBox::toggled, maskThresholdUI, &FloatParameterUI::setEnabled);
    maskSignalUI->setEnabled(maskUI->checkBox()->isChecked());
    maskThresholdUI->setEnabled(maskUI->checkBox()->isChecked());

    // --- Affine mapping of the simulation cell. ---
    QGroupBox* mappingGroupBox = new QGroupBox(tr("Affine mapping of simulation cell"));
    layout->addWidget(mappingGroupBox);
    sublayout = new QGridLayout(mappingGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);

    IntegerRadioButtonParameterUI* affineMappingUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(ReferenceConfigurationModifier::affineMapping));
    sublayout->addWidget(affineMappingUI->addRadioButton(ReferenceConfigurationModifier::NO_MAPPING, tr("Off")), 0, 0);
    sublayout->addWidget(affineMappingUI->addRadioButton(ReferenceConfigurationModifier::TO_REFERENCE_CELL, tr("To reference")), 0, 1);

    // --- Reference configuration source. ---
    QGroupBox* referenceSourceGroupBox = new QGroupBox(tr("Reference configuration source"));
    layout->addWidget(referenceSourceGroupBox);
    sublayout = new QGridLayout(referenceSourceGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);

    _sourceButtonGroup = new QButtonGroup(this);
    connect(_sourceButtonGroup, &QButtonGroup::idClicked, this, &VacancyAnalysisModifierEditor::onSourceButtonClicked);
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
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(VacancyAnalysisModifier::referenceConfiguration), RolloutInsertionParameters().setTitle(tr("Reference: %1")));

    connect(this, &PropertiesEditor::contentsChanged, this, &VacancyAnalysisModifierEditor::onContentsChanged);

    // Whenever the pipeline input of the modifier changes, update the state of the UI.
    connect(this, &PropertiesEditor::pipelineInputChanged, this, [this, mappingGroupBox]() {
        mappingGroupBox->setEnabled(getPipelineInput().getObject<SimulationCell>() != nullptr);
    });
}

/******************************************************************************
* Is called when the user clicks one of the source mode buttons.
******************************************************************************/
void VacancyAnalysisModifierEditor::onSourceButtonClicked(int id)
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
void VacancyAnalysisModifierEditor::onContentsChanged(RefTarget* editObject)
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
