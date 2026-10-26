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
#include <ovito/particles/modifier/analysis/structure_factor/StructureFactorModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/VariantComboBoxParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include "StructureFactorModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(StructureFactorModifierEditor);
SET_OVITO_OBJECT_EDITOR(StructureFactorModifier, StructureFactorModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void StructureFactorModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Structure factor"), rolloutParams, "manual:particles.modifiers.structure_factor");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(6);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    VariantComboBoxParameterUI* modePUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(StructureFactorModifier::mode));
    modePUI->comboBox()->addItem(tr("Debye (radial distribution function)"), QVariant::fromValue((int)StructureFactorModifier::Debye));
    modePUI->comboBox()->addItem(tr("Direct (reciprocal lattice)"), QVariant::fromValue((int)StructureFactorModifier::Direct));
    layout2->addWidget(new QLabel(tr("Method:")), 0, 0);
    layout2->addWidget(modePUI->comboBox(), 0, 1);

    FloatParameterUI* kMinPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(StructureFactorModifier::kMin));
    layout2->addWidget(kMinPUI->label(), 1, 0);
    layout2->addLayout(kMinPUI->createFieldLayout(), 1, 1);

    FloatParameterUI* kMaxPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(StructureFactorModifier::kMax));
    layout2->addWidget(kMaxPUI->label(), 2, 0);
    layout2->addLayout(kMaxPUI->createFieldLayout(), 2, 1);

    IntegerParameterUI* binsPUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(StructureFactorModifier::numberOfBins));
    layout2->addWidget(binsPUI->label(), 3, 0);
    layout2->addLayout(binsPUI->createFieldLayout(), 3, 1);

    BooleanParameterUI* partialsPUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(StructureFactorModifier::computePartials));
    layout2->addWidget(partialsPUI->checkBox(), 4, 0, 1, 2);

    BooleanParameterUI* formFactorsPUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(StructureFactorModifier::useFormFactors));
    layout2->addWidget(formFactorsPUI->checkBox(), 5, 0, 1, 2);

    BooleanParameterUI* onlySelectedPUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(StructureFactorModifier::onlySelected));
    layout2->addWidget(onlySelectedPUI->checkBox(), 6, 0, 1, 2);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"), QString::fromLatin1(StructureFactorModifier::tableIdentifier));
    layout1->addWidget(openDataInspectorBtn);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
