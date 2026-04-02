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
#include <ovito/ml/MLStructureModifier.h>
#include <ovito/gui/desktop/properties/FilenameParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "MLStructureModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(MLStructureModifierEditor);
SET_OVITO_OBJECT_EDITOR(MLStructureModifier, MLStructureModifierEditor);

/******************************************************************************
* Builds the rollout widget shown in the Properties panel.
******************************************************************************/
void MLStructureModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // -----------------------------------------------------------------------
    // Main rollout
    // -----------------------------------------------------------------------
    QWidget* rollout = createRollout(tr("ML Structure Modifier"), rolloutParams);

    QVBoxLayout* mainLayout = new QVBoxLayout(rollout);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // -----------------------------------------------------------------------
    // Section: Model file
    // -----------------------------------------------------------------------
    QGroupBox* modelBox = new QGroupBox(tr("Model"), rollout);
    QVBoxLayout* modelLayout = new QVBoxLayout(modelBox);
    modelLayout->setContentsMargins(4, 4, 4, 4);
    modelLayout->setSpacing(4);

    QLabel* modelLabel = new QLabel(
        tr("TorchScript model file (<tt>.pt</tt>):"), modelBox);
    modelLayout->addWidget(modelLabel);

    QStringList ptFilter = { tr("TorchScript models (*.pt)"),
                             tr("All files (*)") };
    FilenameParameterUI* modelPathUI = createParamUI<FilenameParameterUI>(
        PROPERTY_FIELD(MLStructureModifier::modelPath), ptFilter, /*existingFile=*/true);
    modelLayout->addWidget(modelPathUI->selectorWidget());

    QLabel* trainHint = new QLabel(
        tr("<small>Train and export a model with:<br>"
           "<tt>python scripts/train_structure_classifier.py</tt></small>"),
        modelBox);
    trainHint->setWordWrap(true);
    trainHint->setOpenExternalLinks(false);
    modelLayout->addWidget(trainHint);

    mainLayout->addWidget(modelBox);

    // -----------------------------------------------------------------------
    // Section: Descriptor parameters
    // -----------------------------------------------------------------------
    QGroupBox* descBox = new QGroupBox(tr("Descriptor parameters"), rollout);
    QGridLayout* descGrid = new QGridLayout(descBox);
    descGrid->setContentsMargins(4, 4, 4, 4);
    descGrid->setColumnStretch(1, 1);
    int row = 0;

    // Cutoff radius
    FloatParameterUI* cutoffUI = createParamUI<FloatParameterUI>(
        PROPERTY_FIELD(MLStructureModifier::cutoffRadius));
    descGrid->addWidget(cutoffUI->label(), row, 0);
    descGrid->addLayout(cutoffUI->createFieldLayout(), row, 1);
    ++row;

    // Max neighbours
    IntegerParameterUI* numNeighUI = createParamUI<IntegerParameterUI>(
        PROPERTY_FIELD(MLStructureModifier::numNeighbors));
    descGrid->addWidget(numNeighUI->label(), row, 0);
    descGrid->addLayout(numNeighUI->createFieldLayout(), row, 1);
    ++row;

    QLabel* descNote = new QLabel(
        tr("<small>Must match the values used when training the model<br>"
           "(defaults: cutoff = 5.0 Å, neighbours = 16).</small>"),
        descBox);
    descNote->setWordWrap(true);
    descGrid->addWidget(descNote, row, 0, 1, 2);

    mainLayout->addWidget(descBox);

    // -----------------------------------------------------------------------
    // Section: Output key
    // -----------------------------------------------------------------------
    QGroupBox* outputBox = new QGroupBox(tr("Output property: ML_Structure"), rollout);
    QVBoxLayout* outputLayout = new QVBoxLayout(outputBox);
    outputLayout->setContentsMargins(4, 4, 4, 4);

    QLabel* classKey = new QLabel(
        tr("Integer class index written per particle:<br>"
           "&nbsp;&nbsp;0 = FCC<br>"
           "&nbsp;&nbsp;1 = HCP<br>"
           "&nbsp;&nbsp;2 = BCC<br>"
           "&nbsp;&nbsp;3 = Diamond<br>"
           "&nbsp;&nbsp;4 = SC"),
        outputBox);
    classKey->setWordWrap(true);
    outputLayout->addWidget(classKey);

    mainLayout->addWidget(outputBox);

    // -----------------------------------------------------------------------
    // Status display (errors / warnings from the modifier)
    // -----------------------------------------------------------------------
    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}  // namespace Ovito
