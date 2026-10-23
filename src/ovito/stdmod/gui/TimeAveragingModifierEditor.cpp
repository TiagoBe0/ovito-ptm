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
#include <ovito/stdobj/properties/PropertyContainer.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/stdmod/modifiers/TimeAveragingModifier.h>
#include "TimeAveragingModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(TimeAveragingModifierEditor);
SET_OVITO_OBJECT_EDITOR(TimeAveragingModifier, TimeAveragingModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void TimeAveragingModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Time averaging"), rolloutParams, "manual:particles.modifiers.time_averaging");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("Input quantities:")));
    _inputList = new QListWidget();
    _inputList->setMinimumHeight(160);
    layout->addWidget(_inputList);
    connect(_inputList, &QListWidget::itemChanged, this, &TimeAveragingModifierEditor::onInputSelectionChanged);

    BooleanRadioButtonParameterUI* overwriteUI = createParamUI<BooleanRadioButtonParameterUI>(PROPERTY_FIELD(TimeAveragingModifier::overwrite));
    overwriteUI->buttonFalse()->setText(tr("Create new values"));
    overwriteUI->buttonTrue()->setText(tr("Replace original values"));
    layout->addWidget(overwriteUI->buttonFalse());
    layout->addWidget(overwriteUI->buttonTrue());

    QGridLayout* gridlayout = new QGridLayout();
    gridlayout->setContentsMargins(0,0,0,0);
    gridlayout->setColumnStretch(1, 1);

    IntegerParameterUI* samplingFrequencyUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeAveragingModifier::samplingFrequency));
    gridlayout->addWidget(samplingFrequencyUI->label(), 0, 0);
    gridlayout->addLayout(samplingFrequencyUI->createFieldLayout(), 0, 1);

    BooleanParameterUI* useCustomIntervalUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(TimeAveragingModifier::useCustomInterval));
    gridlayout->addWidget(useCustomIntervalUI->checkBox(), 1, 0, 1, 2);

    IntegerParameterUI* intervalStartUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeAveragingModifier::customIntervalStart));
    gridlayout->addWidget(intervalStartUI->label(), 2, 0);
    gridlayout->addLayout(intervalStartUI->createFieldLayout(), 2, 1);
    IntegerParameterUI* intervalEndUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeAveragingModifier::customIntervalEnd));
    gridlayout->addWidget(intervalEndUI->label(), 3, 0);
    gridlayout->addLayout(intervalEndUI->createFieldLayout(), 3, 1);
    intervalStartUI->setEnabled(false);
    intervalEndUI->setEnabled(false);
    connect(useCustomIntervalUI->checkBox(), &QCheckBox::toggled, intervalStartUI, &IntegerParameterUI::setEnabled);
    connect(useCustomIntervalUI->checkBox(), &QCheckBox::toggled, intervalEndUI, &IntegerParameterUI::setEnabled);

    layout->addLayout(gridlayout);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"));
    layout->addWidget(openDataInspectorBtn);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    connect(this, &PropertiesEditor::contentsReplaced, this, &TimeAveragingModifierEditor::updateInputList);
    connect(this, &PropertiesEditor::pipelineInputChanged, this, &TimeAveragingModifierEditor::updateInputList);
}

/******************************************************************************
* Rebuilds the list of quantities that can be averaged.
******************************************************************************/
void TimeAveragingModifierEditor::updateInputList()
{
    QSignalBlocker blockList(_inputList);
    _inputList->clear();

    TimeAveragingModifier* mod = static_object_cast<TimeAveragingModifier>(editObject());
    if(!mod)
        return;

    // Collect the available quantities as (key, display text) pairs.
    std::vector<std::pair<QString, QString>> available;
    const PipelineFlowState& input = getPipelineInput();
    if(input.data()) {
        for(const QString& name : input.data()->buildAttributesMap().keys())
            available.emplace_back(QStringLiteral("attribute:") + name, tr("Attribute: %1").arg(name));
        for(const ConstDataObjectPath& path : input.data()->getObjectsRecursive(DataTable::OOClass())) {
            const DataObject* table = path.back();
            available.emplace_back(QStringLiteral("table:") + path.toString(), tr("Table: %1").arg(table->objectTitle()));
        }
        for(const ConstDataObjectPath& path : input.data()->getObjectsRecursive(PropertyContainer::OOClass())) {
            if(dynamic_object_cast<DataTable>(path.back()))
                continue;
            const PropertyContainer* container = static_object_cast<PropertyContainer>(path.back());
            for(const Property* property : container->properties())
                available.emplace_back(QStringLiteral("property:%1/%2").arg(path.toString()).arg(property->name()),
                    QStringLiteral("%1: %2").arg(container->objectTitle()).arg(property->name()));
        }
        if(input.getObject<SimulationCell>())
            available.emplace_back(QStringLiteral("cell"), tr("Simulation cell"));
    }

    auto addItem = [&](const QString& key, const QString& text) {
        QListWidgetItem* item = new QListWidgetItem(text, _inputList);
        item->setData(Qt::UserRole, key);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren);
        item->setCheckState(mod->operateOn().contains(key) ? Qt::Checked : Qt::Unchecked);
    };
    // Keep selected quantities listed even if they are not present in the current input.
    for(const QString& key : mod->operateOn()) {
        if(std::none_of(available.begin(), available.end(), [&](const auto& a) { return a.first == key; }))
            addItem(key, key);
    }
    for(const auto& [key, text] : available)
        addItem(key, text);
}

/******************************************************************************
* Is called when the user checks/unchecks an item in the list.
******************************************************************************/
void TimeAveragingModifierEditor::onInputSelectionChanged()
{
    TimeAveragingModifier* mod = static_object_cast<TimeAveragingModifier>(editObject());
    if(!mod)
        return;

    QStringList selected;
    for(int index = 0; index < _inputList->count(); index++) {
        if(_inputList->item(index)->checkState() == Qt::Checked)
            selected.push_back(_inputList->item(index)->data(Qt::UserRole).toString());
    }
    performTransaction(tr("Select quantities to average"), [&]() {
        mod->setOperateOn(selected);
    });
}

}   // End of namespace
