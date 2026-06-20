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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "DislocationTrackingModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationTrackingModifierEditor);
SET_OVITO_OBJECT_EDITOR(DislocationTrackingModifier, DislocationTrackingModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void DislocationTrackingModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create the rollout.
    QWidget* rollout = createRollout(tr("Dislocation tracking"), rolloutParams);

    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(6);

    QGroupBox* matchingBox = new QGroupBox(tr("Matching parameters"));
    layout->addWidget(matchingBox);
    QGridLayout* sublayout = new QGridLayout(matchingBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    int row = 0;

    FloatParameterUI* maxDistUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::maxMatchingDistance));
    sublayout->addWidget(maxDistUI->label(), row, 0);
    sublayout->addLayout(maxDistUI->createFieldLayout(), row++, 1);

    FloatParameterUI* burgersTolUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::burgersTolerance));
    sublayout->addWidget(burgersTolUI->label(), row, 0);
    sublayout->addLayout(burgersTolUI->createFieldLayout(), row++, 1);

    IntegerParameterUI* maxBridgeGapUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::maxBridgeGap));
    sublayout->addWidget(maxBridgeGapUI->label(), row, 0);
    sublayout->addLayout(maxBridgeGapUI->createFieldLayout(), row++, 1);

    IntegerParameterUI* minTrackLengthUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::minTrackLength));
    sublayout->addWidget(minTrackLengthUI->label(), row, 0);
    sublayout->addLayout(minTrackLengthUI->createFieldLayout(), row++, 1);

    FloatParameterUI* timePerFrameUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::timePerFrame));
    sublayout->addWidget(timePerFrameUI->label(), row, 0);
    sublayout->addLayout(timePerFrameUI->createFieldLayout(), row++, 1);

    QGroupBox* displayBox = new QGroupBox(tr("Display"));
    layout->addWidget(displayBox);
    QVBoxLayout* sublayout2 = new QVBoxLayout(displayBox);
    sublayout2->setContentsMargins(4,4,4,4);
    sublayout2->setSpacing(4);

    BooleanParameterUI* colorByTrackUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationTrackingModifier::colorByTrack));
    sublayout2->addWidget(colorByTrackUI->checkBox());

    // Status display.
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
