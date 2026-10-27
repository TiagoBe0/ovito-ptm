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
#include <ovito/particles/modifier/modify/ShrinkWrapBoxModifier.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "ShrinkWrapBoxModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ShrinkWrapBoxModifierEditor);
SET_OVITO_OBJECT_EDITOR(ShrinkWrapBoxModifier, ShrinkWrapBoxModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void ShrinkWrapBoxModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Shrink-wrap simulation box"), rolloutParams, "manual:modifiers.shrink_wrap_box");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(6);

    QLabel* description = new QLabel(tr("Replaces the simulation cell with the axis-aligned bounding box of the particles. "
        "Periodic boundary conditions are turned off. This modifier has no adjustable parameters."));
    description->setWordWrap(true);
    layout->addWidget(description);

    // Status label (shows the size of the new cell).
    layout->addSpacing(10);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
