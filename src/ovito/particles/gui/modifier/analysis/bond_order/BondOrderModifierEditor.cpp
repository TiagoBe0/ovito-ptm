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
#include <ovito/particles/modifier/analysis/bond_order/BondOrderModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "BondOrderModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondOrderModifierEditor);
SET_OVITO_OBJECT_EDITOR(BondOrderModifier, BondOrderModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void BondOrderModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Bond order"), rolloutParams, "manual:particles.modifiers.bond_order");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(6);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    IntegerParameterUI* fragmentSizeUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(BondOrderModifier::fragmentSizeLimit));
    layout2->addWidget(fragmentSizeUI->label(), 0, 0);
    layout2->addLayout(fragmentSizeUI->createFieldLayout(), 0, 1);

    BooleanParameterUI* redistributeUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(BondOrderModifier::redistributeBondOrders));
    layout2->addWidget(redistributeUI->checkBox(), 1, 0, 1, 2);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
