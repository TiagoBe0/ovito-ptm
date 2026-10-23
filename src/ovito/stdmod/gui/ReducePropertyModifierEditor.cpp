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

#include <ovito/stdmod/gui/StdModGui.h>
#include <ovito/stdobj/gui/widgets/PropertyReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/DataObjectReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/VariantComboBoxParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/stdmod/modifiers/ReducePropertyModifier.h>
#include "ReducePropertyModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ReducePropertyModifierEditor);
SET_OVITO_OBJECT_EDITOR(ReducePropertyModifier, ReducePropertyModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void ReducePropertyModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Reduce property"), rolloutParams, "manual:particles.modifiers.reduce_property");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    DataObjectReferenceParameterUI* pclassUI = createParamUI<DataObjectReferenceParameterUI>(PROPERTY_FIELD(GenericPropertyModifier::subject), PropertyContainer::OOClass());
    layout->addWidget(new QLabel(tr("Operate on:")));
    layout->addWidget(pclassUI->comboBox());

    // Vector properties can be selected as a whole, in which case every component is reduced.
    PropertyReferenceParameterUI* sourcePropertyUI = createParamUI<PropertyReferenceParameterUI>(PROPERTY_FIELD(ReducePropertyModifier::sourceProperty),
        nullptr, PropertyReferenceParameterUI::ShowComponentsAndVectorProperties);
    layout->addWidget(new QLabel(tr("Input property:")));
    layout->addWidget(sourcePropertyUI->comboBox());
    connect(this, &PropertiesEditor::contentsChanged, this, [sourcePropertyUI](RefTarget* editObject) {
        GenericPropertyModifier* modifier = static_object_cast<GenericPropertyModifier>(editObject);
        sourcePropertyUI->setContainerRef(modifier ? modifier->subject() : nullptr);
    });

    VariantComboBoxParameterUI* operationUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(ReducePropertyModifier::operation));
    operationUI->comboBox()->addItem(tr("Minimum"), QVariant::fromValue((int)ReducePropertyModifier::Min));
    operationUI->comboBox()->addItem(tr("Maximum"), QVariant::fromValue((int)ReducePropertyModifier::Max));
    operationUI->comboBox()->addItem(tr("Sum"), QVariant::fromValue((int)ReducePropertyModifier::Sum));
    operationUI->comboBox()->addItem(tr("Non-zero count"), QVariant::fromValue((int)ReducePropertyModifier::NonzeroCount));
    operationUI->comboBox()->addItem(tr("Mean"), QVariant::fromValue((int)ReducePropertyModifier::Mean));
    operationUI->comboBox()->addItem(tr("Median"), QVariant::fromValue((int)ReducePropertyModifier::Median));
    operationUI->comboBox()->addItem(tr("Variance"), QVariant::fromValue((int)ReducePropertyModifier::Variance));
    operationUI->comboBox()->addItem(tr("Standard deviation"), QVariant::fromValue((int)ReducePropertyModifier::StdDev));
    operationUI->comboBox()->addItem(tr("Argmin"), QVariant::fromValue((int)ReducePropertyModifier::Argmin));
    operationUI->comboBox()->addItem(tr("Argmax"), QVariant::fromValue((int)ReducePropertyModifier::Argmax));
    layout->addWidget(new QLabel(tr("Reduction operation:")));
    layout->addWidget(operationUI->comboBox());

    BooleanParameterUI* onlySelectedUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ReducePropertyModifier::onlySelectedElements));
    layout->addWidget(onlySelectedUI->checkBox());

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"));
    layout->addWidget(openDataInspectorBtn);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

}   // End of namespace
