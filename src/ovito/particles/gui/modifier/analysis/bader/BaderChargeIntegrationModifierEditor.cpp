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
#include <ovito/particles/modifier/analysis/bader/BaderChargeIntegrationModifier.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/gui/widgets/PropertyReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/DataObjectReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include "BaderChargeIntegrationModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BaderChargeIntegrationModifierEditor);
SET_OVITO_OBJECT_EDITOR(BaderChargeIntegrationModifier, BaderChargeIntegrationModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void BaderChargeIntegrationModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Bader charge integration"), rolloutParams, "manual:particles.modifiers.bader_charge_integration");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(4);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    DataObjectReferenceParameterUI* gridUI = createParamUI<DataObjectReferenceParameterUI>(PROPERTY_FIELD(BaderChargeIntegrationModifier::subject), VoxelGrid::OOClass());
    layout2->addWidget(new QLabel(tr("Voxel grid:")), 0, 0);
    layout2->addWidget(gridUI->comboBox(), 0, 1);

    PropertyReferenceParameterUI* densityUI = createParamUI<PropertyReferenceParameterUI>(PROPERTY_FIELD(BaderChargeIntegrationModifier::densityProperty));
    layout2->addWidget(new QLabel(tr("Density:")), 1, 0);
    layout2->addWidget(densityUI->comboBox(), 1, 1);

    PropertyReferenceParameterUI* integrandUI = createParamUI<PropertyReferenceParameterUI>(PROPERTY_FIELD(BaderChargeIntegrationModifier::integrandProperty));
    layout2->addWidget(new QLabel(tr("Integrand:")), 2, 0);
    layout2->addWidget(integrandUI->comboBox(), 2, 1);

    // The property lists follow the selected voxel grid.
    connect(this, &PropertiesEditor::contentsChanged, this, [densityUI, integrandUI](RefTarget* editObject) {
        if(BaderChargeIntegrationModifier* modifier = static_object_cast<BaderChargeIntegrationModifier>(editObject)) {
            densityUI->setContainerRef(modifier->subject());
            integrandUI->setContainerRef(modifier->subject());
        }
        else {
            densityUI->setContainerRef(nullptr);
            integrandUI->setContainerRef(nullptr);
        }
    });

    QLabel* note = new QLabel(tr("<p>If no integrand is selected, the density itself is integrated. "
        "Method: M. Yu and D. R. Trinkle, J. Chem. Phys. 134, 064111 (2011).</p>"));
    note->setWordWrap(true);
    layout1->addWidget(note);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show results in data inspector"),
        QString::fromLatin1(BaderChargeIntegrationModifier::tableIdentifier));
    layout1->addWidget(openDataInspectorBtn);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
