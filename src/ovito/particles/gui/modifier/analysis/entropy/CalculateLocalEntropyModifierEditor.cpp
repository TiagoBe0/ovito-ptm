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
#include <ovito/particles/modifier/analysis/entropy/CalculateLocalEntropyModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "CalculateLocalEntropyModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CalculateLocalEntropyModifierEditor);
SET_OVITO_OBJECT_EDITOR(CalculateLocalEntropyModifier, CalculateLocalEntropyModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void CalculateLocalEntropyModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Calculate local entropy"), rolloutParams, "manual:modifiers.calculate_local_entropy");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(6);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    FloatParameterUI* cutoffPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CalculateLocalEntropyModifier::cutoff));
    layout2->addWidget(cutoffPUI->label(), 0, 0);
    layout2->addLayout(cutoffPUI->createFieldLayout(), 0, 1);

    FloatParameterUI* sigmaPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CalculateLocalEntropyModifier::sigma));
    layout2->addWidget(sigmaPUI->label(), 1, 0);
    layout2->addLayout(sigmaPUI->createFieldLayout(), 1, 1);

    BooleanParameterUI* useLocalDensityUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CalculateLocalEntropyModifier::useLocalDensity));
    layout2->addWidget(useLocalDensityUI->checkBox(), 2, 0, 1, 2);

    BooleanParameterUI* computeAverageUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CalculateLocalEntropyModifier::computeAverage));
    layout2->addWidget(computeAverageUI->checkBox(), 3, 0, 1, 2);

    FloatParameterUI* averageCutoffPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CalculateLocalEntropyModifier::averageCutoff));
    layout2->addWidget(averageCutoffPUI->label(), 4, 0);
    layout2->addLayout(averageCutoffPUI->createFieldLayout(), 4, 1);
    connect(computeAverageUI->checkBox(), &QCheckBox::toggled, averageCutoffPUI, &FloatParameterUI::setEnabled);
    averageCutoffPUI->setEnabled(false);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
