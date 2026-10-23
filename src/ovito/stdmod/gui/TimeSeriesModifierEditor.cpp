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
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/stdmod/modifiers/TimeSeriesModifier.h>
#include "TimeSeriesModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(TimeSeriesModifierEditor);
SET_OVITO_OBJECT_EDITOR(TimeSeriesModifier, TimeSeriesModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void TimeSeriesModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Time series"), rolloutParams, "manual:particles.modifiers.time_series");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("Input attributes:")));
    _attributesList = new QListWidget();
    _attributesList->setMinimumHeight(120);
    layout->addWidget(_attributesList);
    connect(_attributesList, &QListWidget::itemChanged, this, &TimeSeriesModifierEditor::onSourceAttributesChanged);

    QGridLayout* gridlayout = new QGridLayout();
    gridlayout->setContentsMargins(0,0,0,0);
    gridlayout->setColumnStretch(1, 1);

    _timeAttributeBox = new QComboBox();
    gridlayout->addWidget(new QLabel(tr("Time axis:")), 0, 0);
    gridlayout->addWidget(_timeAttributeBox, 0, 1);
    connect(_timeAttributeBox, &QComboBox::activated, this, &TimeSeriesModifierEditor::onTimeAttributeChanged);

    IntegerParameterUI* samplingFrequencyUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeSeriesModifier::samplingFrequency));
    gridlayout->addWidget(samplingFrequencyUI->label(), 1, 0);
    gridlayout->addLayout(samplingFrequencyUI->createFieldLayout(), 1, 1);

    BooleanParameterUI* useCustomIntervalUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(TimeSeriesModifier::useCustomInterval));
    gridlayout->addWidget(useCustomIntervalUI->checkBox(), 2, 0, 1, 2);

    IntegerParameterUI* intervalStartUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeSeriesModifier::customIntervalStart));
    gridlayout->addWidget(intervalStartUI->label(), 3, 0);
    gridlayout->addLayout(intervalStartUI->createFieldLayout(), 3, 1);
    IntegerParameterUI* intervalEndUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(TimeSeriesModifier::customIntervalEnd));
    gridlayout->addWidget(intervalEndUI->label(), 4, 0);
    gridlayout->addLayout(intervalEndUI->createFieldLayout(), 4, 1);
    intervalStartUI->setEnabled(false);
    intervalEndUI->setEnabled(false);
    connect(useCustomIntervalUI->checkBox(), &QCheckBox::toggled, intervalStartUI, &IntegerParameterUI::setEnabled);
    connect(useCustomIntervalUI->checkBox(), &QCheckBox::toggled, intervalEndUI, &IntegerParameterUI::setEnabled);

    layout->addLayout(gridlayout);

    _plotWidget = new DataTablePlotWidget();
    _plotWidget->setMinimumHeight(240);
    _plotWidget->setMaximumHeight(240);
    layout->addSpacing(6);
    layout->addWidget(_plotWidget);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"));
    layout->addWidget(openDataInspectorBtn);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    connect(this, &PropertiesEditor::contentsReplaced, this, &TimeSeriesModifierEditor::updateAttributeLists);
    connect(this, &PropertiesEditor::pipelineInputChanged, this, &TimeSeriesModifierEditor::updateAttributeLists);
    connect(this, &PropertiesEditor::pipelineOutputChanged, this, &TimeSeriesModifierEditor::plotTimeSeries);
}

/******************************************************************************
* Rebuilds the list of available input attributes and the time axis selector.
******************************************************************************/
void TimeSeriesModifierEditor::updateAttributeLists()
{
    QSignalBlocker blockList(_attributesList);
    QSignalBlocker blockBox(_timeAttributeBox);
    _attributesList->clear();
    _timeAttributeBox->clear();

    TimeSeriesModifier* mod = static_object_cast<TimeSeriesModifier>(editObject());
    if(!mod)
        return;

    QStringList available;
    if(const PipelineFlowState& input = getPipelineInput(); input.data())
        available = input.data()->buildAttributesMap().keys();

    // Selected attributes come first, in the user's order; keep them even if not present in the current input.
    QStringList listed = mod->sourceAttributes();
    for(const QString& name : available)
        if(!listed.contains(name))
            listed.push_back(name);
    for(const QString& name : listed) {
        QListWidgetItem* item = new QListWidgetItem(name, _attributesList);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren);
        item->setCheckState(mod->sourceAttributes().contains(name) ? Qt::Checked : Qt::Unchecked);
    }

    _timeAttributeBox->addItem(tr("<Animation frame>"), QString());
    for(const QString& name : available)
        _timeAttributeBox->addItem(name, name);
    if(!mod->timeAttribute().isEmpty() && !available.contains(mod->timeAttribute()))
        _timeAttributeBox->addItem(mod->timeAttribute(), mod->timeAttribute());
    _timeAttributeBox->setCurrentIndex(std::max(_timeAttributeBox->findData(mod->timeAttribute()), 0));
}

/******************************************************************************
* Is called when the user checks/unchecks an item in the attributes list.
******************************************************************************/
void TimeSeriesModifierEditor::onSourceAttributesChanged()
{
    TimeSeriesModifier* mod = static_object_cast<TimeSeriesModifier>(editObject());
    if(!mod)
        return;

    QStringList selected;
    for(int index = 0; index < _attributesList->count(); index++) {
        if(_attributesList->item(index)->checkState() == Qt::Checked)
            selected.push_back(_attributesList->item(index)->text());
    }
    performTransaction(tr("Select input attributes"), [&]() {
        mod->setSourceAttributes(selected);
    });
}

/******************************************************************************
* Is called when the user picks a different time axis attribute.
******************************************************************************/
void TimeSeriesModifierEditor::onTimeAttributeChanged()
{
    TimeSeriesModifier* mod = static_object_cast<TimeSeriesModifier>(editObject());
    if(!mod)
        return;

    QString name = _timeAttributeBox->currentData().toString();
    performTransaction(tr("Select time axis"), [&]() {
        mod->setTimeAttribute(name);
    });
}

/******************************************************************************
* Replots the time series computed by the modifier.
******************************************************************************/
void TimeSeriesModifierEditor::plotTimeSeries()
{
    handleExceptions([&]() {
        DataOORef<const DataTable> table = getPipelineOutput().getObjectBy<DataTable>(modificationNode(), QString::fromLatin1(TimeSeriesModifier::tableIdentifier));
        _plotWidget->setTable(table, true);
    });
}

}   // End of namespace
