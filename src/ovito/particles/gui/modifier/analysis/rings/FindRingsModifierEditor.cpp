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
#include <ovito/particles/modifier/analysis/rings/FindRingsModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include "FindRingsModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(FindRingsModifierEditor);
SET_OVITO_OBJECT_EDITOR(FindRingsModifier, FindRingsModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void FindRingsModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Find rings (SIMAF)"), rolloutParams, "manual:particles.modifiers.find_rings");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(6);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    IntegerParameterUI* minSizeUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(FindRingsModifier::minRingSize));
    layout2->addWidget(minSizeUI->label(), 0, 0);
    layout2->addLayout(minSizeUI->createFieldLayout(), 0, 1);

    IntegerParameterUI* maxSizeUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(FindRingsModifier::maxRingSize));
    layout2->addWidget(maxSizeUI->label(), 1, 0);
    layout2->addLayout(maxSizeUI->createFieldLayout(), 1, 1);

    BooleanParameterUI* wrappingUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(FindRingsModifier::includeWrappingRings));
    layout2->addWidget(wrappingUI->checkBox(), 2, 0, 1, 2);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show histogram in data inspector"),
        QString::fromLatin1(FindRingsModifier::histogramTableIdentifier));
    layout1->addWidget(openDataInspectorBtn);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the ring mesh vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(FindRingsModifier::ringsVis), rolloutParams.after(rollout));
}

}   // End of namespace
