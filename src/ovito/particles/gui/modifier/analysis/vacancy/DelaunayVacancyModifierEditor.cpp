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
#include <ovito/particles/modifier/analysis/vacancy/DelaunayVacancyModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "DelaunayVacancyModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DelaunayVacancyModifierEditor);
SET_OVITO_OBJECT_EDITOR(DelaunayVacancyModifier, DelaunayVacancyModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void DelaunayVacancyModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("Vacancy analysis (Delaunay)"), rolloutParams);

    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    // --- Lattice. ---
    QGroupBox* latticeGroupBox = new QGroupBox(tr("Crystal lattice"));
    layout->addWidget(latticeGroupBox);
    QGridLayout* sublayout = new QGridLayout(latticeGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    IntegerRadioButtonParameterUI* latticeUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(DelaunayVacancyModifier::lattice));
    sublayout->addWidget(latticeUI->addRadioButton(DelaunayVacancyModifier::BCC, tr("BCC")), 0, 0);
    sublayout->addWidget(latticeUI->addRadioButton(DelaunayVacancyModifier::FCC, tr("FCC")), 0, 1);

    FloatParameterUI* a0UI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(DelaunayVacancyModifier::latticeConstant));
    a0UI->label()->setText(tr("Lattice constant (0 = auto):"));
    a0UI->label()->setToolTip(tr("0 estimates a0 from the median nearest-neighbor distance. Give it explicitly in strongly deformed or hot systems."));
    sublayout->addWidget(a0UI->label(), 1, 0);
    sublayout->addLayout(a0UI->createFieldLayout(), 1, 1);

    // --- Detection. ---
    QGroupBox* detectionGroupBox = new QGroupBox(tr("Detection"));
    layout->addWidget(detectionGroupBox);
    sublayout = new QGridLayout(detectionGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);

    BooleanParameterUI* refineUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DelaunayVacancyModifier::refineWithLocalLattice));
    refineUI->checkBox()->setToolTip(tr("Fits a lattice around each group of voids and reports every empty lattice site, so divacancies and vacancy clusters are counted site by site."));
    sublayout->addWidget(refineUI->checkBox(), 0, 0);

    BooleanParameterUI* outputUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DelaunayVacancyModifier::outputVacancySites));
    outputUI->checkBox()->setToolTip(tr("Replaces the atoms by the detected vacancy sites. The counts are available as global attributes either way."));
    sublayout->addWidget(outputUI->checkBox(), 1, 0);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
