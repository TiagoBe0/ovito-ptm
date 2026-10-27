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
#include <ovito/particles/modifier/analysis/planar_faults/IdentifyFCCPlanarFaultsModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/ColorParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "IdentifyFCCPlanarFaultsModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(IdentifyFCCPlanarFaultsModifierEditor);
SET_OVITO_OBJECT_EDITOR(IdentifyFCCPlanarFaultsModifier, IdentifyFCCPlanarFaultsModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void IdentifyFCCPlanarFaultsModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Identify fcc planar faults"), rolloutParams, "manual:modifiers.identify_fcc_planar_faults");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QLabel* infoLabel = new QLabel(tr("Requires the Structure Type property of a preceding Polyhedral template matching or Common neighbor analysis modifier."));
    infoLabel->setWordWrap(true);
    layout1->addWidget(infoLabel);

    BooleanParameterUI* colorParticlesUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorParticles));
    layout1->addSpacing(6);
    layout1->addWidget(colorParticlesUI->checkBox());

    QGridLayout* colorsLayout = new QGridLayout();
    colorsLayout->setContentsMargins(0,0,0,0);
    colorsLayout->setColumnStretch(1, 1);
    int row = 0;
    for(const PropertyFieldDescriptor* field : {PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorNonHCP), PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorOther),
            PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorISF), PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorTwin), PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier::colorMulti)}) {
        ColorParameterUI* colorUI = createParamUI<ColorParameterUI>(field);
        colorsLayout->addWidget(colorUI->label(), row, 0);
        colorsLayout->addWidget(colorUI->colorPicker(), row, 1);
        row++;
    }
    layout1->addLayout(colorsLayout);

    _plotWidget = new DataTablePlotWidget();
    _plotWidget->setMinimumHeight(180);
    _plotWidget->setMaximumHeight(180);
    layout1->addSpacing(10);
    layout1->addWidget(_plotWidget);

    _areaLabel = new QLabel();
    _areaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout1->addWidget(_areaLabel);

    // Update the results whenever the modifier has calculated new results.
    connect(this, &PropertiesEditor::pipelineOutputChanged, this, &IdentifyFCCPlanarFaultsModifierEditor::updateResults);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

/******************************************************************************
* Updates the bar chart and the area summary computed by the modifier.
******************************************************************************/
void IdentifyFCCPlanarFaultsModifierEditor::updateResults()
{
    handleExceptions([&]() {
        const PipelineFlowState& state = getPipelineOutput();
        _plotWidget->setTable(state ? state.getObjectBy<DataTable>(modificationNode(), QStringLiteral("planar_faults")) : nullptr);
        if(state && state.getAttributeValue(modificationNode(), QStringLiteral("PlanarFaults.isf_area")).isValid()) {
            _areaLabel->setText(tr("ISF area: %1\nTB area: %2")
                .arg(state.getAttributeValue(modificationNode(), QStringLiteral("PlanarFaults.isf_area")).toDouble(), 0, 'g', 6)
                .arg(state.getAttributeValue(modificationNode(), QStringLiteral("PlanarFaults.tb_area")).toDouble(), 0, 'g', 6));
        }
        else {
            _areaLabel->clear();
        }
    });
}

}   // End of namespace
