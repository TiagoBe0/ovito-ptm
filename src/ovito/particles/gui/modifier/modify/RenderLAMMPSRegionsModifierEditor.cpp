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
#include <ovito/particles/modifier/modify/RenderLAMMPSRegionsModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/StringParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/widgets/general/AutocompleteTextEdit.h>
#include "RenderLAMMPSRegionsModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(RenderLAMMPSRegionsModifierEditor);
SET_OVITO_OBJECT_EDITOR(RenderLAMMPSRegionsModifier, RenderLAMMPSRegionsModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void RenderLAMMPSRegionsModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Render LAMMPS regions"), rolloutParams, "manual:modifiers.render_lammps_regions");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    QLabel* infoLabel = new QLabel(tr("Supported styles: <i>block, cone, cylinder, ellipsoid, plane, prism, sphere</i>.<br>"
        "Supported keywords: <i>rotate, move, side, open</i>."));
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    layout->addWidget(new QLabel(tr("Region commands:")));
    StringParameterUI* commandsUI = createParamUI<StringParameterUI>(PROPERTY_FIELD(RenderLAMMPSRegionsModifier::commands));
    AutocompleteTextEdit* commandsEdit = new AutocompleteTextEdit();
    commandsEdit->setCommitOnReturn(false);
    commandsEdit->setWordList({QStringLiteral("region"), QStringLiteral("block"), QStringLiteral("cone"), QStringLiteral("cylinder"),
        QStringLiteral("ellipsoid"), QStringLiteral("plane"), QStringLiteral("prism"), QStringLiteral("sphere"), QStringLiteral("side"),
        QStringLiteral("open"), QStringLiteral("move"), QStringLiteral("rotate"), QStringLiteral("INF"), QStringLiteral("EDGE")});
    commandsEdit->setPlaceholderText(QStringLiteral("region 1 sphere 0 0 0 10"));
    commandsEdit->setToolTip(tr("One LAMMPS region command per line. Lines ending with '&' continue on the next line; '#' starts a comment. "
        "The commands are applied when the text field loses the input focus."));
    commandsUI->setTextBox(commandsEdit);
    layout->addWidget(commandsUI->textBox());

    BooleanParameterUI* colorUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(RenderLAMMPSRegionsModifier::colorByRegion));
    layout->addWidget(colorUI->checkBox());

    BooleanParameterUI* selectUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(RenderLAMMPSRegionsModifier::selectParticles));
    layout->addWidget(selectUI->checkBox());

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show regions in data inspector"),
        QString::fromLatin1(RenderLAMMPSRegionsModifier::meshIdentifier), 2);   // 2 = region list view
    layout->addWidget(openDataInspectorBtn);

    // Status label.
    layout->addSpacing(10);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the mesh vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(RenderLAMMPSRegionsModifier::regionsVis), rolloutParams.after(rollout));
}

}   // End of namespace
