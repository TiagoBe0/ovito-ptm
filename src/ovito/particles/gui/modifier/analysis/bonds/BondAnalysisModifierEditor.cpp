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
#include <ovito/particles/modifier/analysis/bonds/BondAnalysisModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/VariantComboBoxParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include "BondAnalysisModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondAnalysisModifierEditor);
SET_OVITO_OBJECT_EDITOR(BondAnalysisModifier, BondAnalysisModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void BondAnalysisModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Bond analysis"), rolloutParams, "manual:particles.modifiers.bond_analysis");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(6);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    IntegerParameterUI* binsPUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(BondAnalysisModifier::bins));
    layout2->addWidget(binsPUI->label(), 0, 0);
    layout2->addLayout(binsPUI->createFieldLayout(), 0, 1);

    FloatParameterUI* cutoffPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(BondAnalysisModifier::lengthCutoff));
    layout2->addWidget(cutoffPUI->label(), 1, 0);
    layout2->addLayout(cutoffPUI->createFieldLayout(), 1, 1);

    BooleanParameterUI* useCosinePUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(BondAnalysisModifier::useCosine));
    layout2->addWidget(useCosinePUI->checkBox(), 2, 0, 1, 2);

    VariantComboBoxParameterUI* partitionPUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(BondAnalysisModifier::partition));
    partitionPUI->comboBox()->addItem(tr("Off"), QVariant::fromValue((int)BondAnalysisModifier::Off));
    partitionPUI->comboBox()->addItem(tr("Bond type"), QVariant::fromValue((int)BondAnalysisModifier::ByBondType));
    partitionPUI->comboBox()->addItem(tr("Bond selection"), QVariant::fromValue((int)BondAnalysisModifier::ByBondSelection));
    partitionPUI->comboBox()->addItem(tr("Particle type"), QVariant::fromValue((int)BondAnalysisModifier::ByParticleType));
    partitionPUI->comboBox()->addItem(tr("Particle selection"), QVariant::fromValue((int)BondAnalysisModifier::ByParticleSelection));
    layout2->addWidget(new QLabel(tr("Partitioning:")), 3, 0);
    layout2->addWidget(partitionPUI->comboBox(), 3, 1);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"), QString::fromLatin1(BondAnalysisModifier::angleTableIdentifier));
    layout1->addWidget(openDataInspectorBtn);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
