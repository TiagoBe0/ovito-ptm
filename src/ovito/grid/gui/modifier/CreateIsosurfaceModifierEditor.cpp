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

#include <ovito/gui/desktop/GUI.h>
#include <ovito/stdobj/gui/widgets/PropertyReferenceParameterUI.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/gui/desktop/properties/DataObjectReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanGroupBoxParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/grid/modifier/CreateIsosurfaceModifier.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "CreateIsosurfaceModifierEditor.h"

#include <qwt/qwt_plot_marker.h>
#include <qwt/qwt_plot_picker.h>
#include <qwt/qwt_picker_machine.h>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CreateIsosurfaceModifierEditor);
SET_OVITO_OBJECT_EDITOR(CreateIsosurfaceModifier, CreateIsosurfaceModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void CreateIsosurfaceModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    int row = 0;
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Create isosurface"), rolloutParams, "manual:particles.modifiers.create_isosurface");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(4);

    QGridLayout* layout2 = new QGridLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(4);
    layout2->setColumnStretch(1, 1);
    layout1->addLayout(layout2);

    DataObjectReferenceParameterUI* pclassUI = createParamUI<DataObjectReferenceParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::subject), VoxelGrid::OOClass());
    layout2->addWidget(new QLabel(tr("Operate on:")), row, 0);
    layout2->addWidget(pclassUI->comboBox(), row++, 1);

    PropertyReferenceParameterUI* fieldQuantityUI = createParamUI<PropertyReferenceParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::sourceProperty));
    layout2->addWidget(new QLabel(tr("Field quantity:")), row, 0);
    layout2->addWidget(fieldQuantityUI->comboBox(), row++, 1);
    connect(this, &PropertiesEditor::contentsChanged, this, [fieldQuantityUI](RefTarget* editObject) {
        if(CreateIsosurfaceModifier* modifier = static_object_cast<CreateIsosurfaceModifier>(editObject)) {
            fieldQuantityUI->setContainerRef(modifier->subject());
        }
        else
            fieldQuantityUI->setContainerRef(nullptr);
    });

    // Isolevel parameter.
    FloatParameterUI* isolevelPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::isolevelController));
    layout2->addWidget(isolevelPUI->label(), row, 0);
    layout2->addLayout(isolevelPUI->createFieldLayout(), row++, 1);

    // Smoothing level parameter.
    IntegerParameterUI* smoothingLevelPUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::smoothingLevel));
    layout2->addWidget(smoothingLevelPUI->label(), row, 0);
    layout2->addLayout(smoothingLevelPUI->createFieldLayout(), row++, 1);

    // Transfer field values.
    BooleanParameterUI* transferFieldValuesUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::transferFieldValues));
    layout2->addWidget(transferFieldValuesUI->checkBox(), row++, 1, 1, 1);

    BooleanGroupBoxParameterUI* regionsSettingsUI =
        createParamUI<BooleanGroupBoxParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::identifyRegions));
#ifndef OVITO_BUILD_BASIC
    regionsSettingsUI->groupBox()->setTitle(tr("Identify volumetric regions"));
#else
    regionsSettingsUI->groupBox()->setTitle(tr("Identify volumetric regions (requires OVITO Pro)"));
    regionsSettingsUI->setEnabled(false);
#endif
    layout2->addWidget(regionsSettingsUI->groupBox(), row++, 0, 1, 4);

    QGridLayout* sublayout = new QGridLayout(regionsSettingsUI->childContainer());
    sublayout->setContentsMargins(4, 4, 4, 4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);

    OpenDataInspectorButton* ShowRegionsListBtn =
        new OpenDataInspectorButton(this, tr("List of identified regions"), QStringLiteral("isosurface"),
                                    2);  // Note: Mode hint "2" is used to switch to the surface mesh regions view.
    ShowRegionsListBtn->setEnabled(false);
    sublayout->addWidget(ShowRegionsListBtn, 0, 1, 1, 2);
#ifndef OVITO_BUILD_BASIC
    connect(this, &PropertiesEditor::contentsChanged, this, [this, ShowRegionsListBtn]() {
        CreateIsosurfaceModifier* modifier = static_object_cast<CreateIsosurfaceModifier>(editObject());
        ShowRegionsListBtn->setEnabled(modifier && modifier->identifyRegions());
    });
#endif

    _plotWidget = new DataTablePlotWidget();
    _plotWidget->setMinimumHeight(200);
    _plotWidget->setMaximumHeight(200);
    _isoLevelIndicator = new QwtPlotMarker();
    _isoLevelIndicator->setLineStyle(QwtPlotMarker::VLine);
    _isoLevelIndicator->setLinePen(Qt::blue, 1, Qt::DashLine);
    _isoLevelIndicator->setZ(1);
    _isoLevelIndicator->attach(_plotWidget);
    _isoLevelIndicator->hide();
    _plotWidget->setMouseNavigationEnabled(false);
    QwtPlotPicker* picker = new QwtPlotPicker(_plotWidget->canvas());
    OVITO_ASSERT(picker->isEnabled());
    picker->setTrackerMode(QwtPlotPicker::AlwaysOff);
    picker->setStateMachine(new QwtPickerDragPointMachine());
    connect(picker, qOverload<const QPointF&>(&QwtPlotPicker::appended), this, &CreateIsosurfaceModifierEditor::onPickerPoint);
    connect(picker, qOverload<const QPointF&>(&QwtPlotPicker::moved), this, &CreateIsosurfaceModifierEditor::onPickerPoint);
    connect(picker, &QwtPicker::activated, this, &CreateIsosurfaceModifierEditor::onPickerActivated);
    connect(this, &PropertiesEditor::contentsReplaced, this, [this]() { onPickerActivated(false); });

    layout2->setRowMinimumHeight(row++, 8);
    layout2->addWidget(new QLabel(tr("Histogram of field values:")), row++, 0, 1, 2);
    layout2->addWidget(_plotWidget, row++, 0, 1, 2);

    // Status label.
    layout1->addSpacing(8);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the mesh vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(CreateIsosurfaceModifier::surfaceMeshVis), rolloutParams.after(rollout));

    // Update data plot whenever the modifier has calculated new results.
    connect(this, &PropertiesEditor::pipelineOutputChanged, this, &CreateIsosurfaceModifierEditor::plotHistogram);
}

/******************************************************************************
* Replots the histogram computed by the modifier.
******************************************************************************/
void CreateIsosurfaceModifierEditor::plotHistogram()
{
    CreateIsosurfaceModifier* modifier = static_object_cast<CreateIsosurfaceModifier>(editObject());

    if(modifier && modificationNode()) {
        handleExceptions([&]() {
            _isoLevelIndicator->setXValue(modifier->isolevel());
            _isoLevelIndicator->show();

            // Request the modifier's pipeline output.
            const PipelineFlowState& state = getPipelineOutput();

            // Look up the generated data table in the modifier's pipeline output.
            const DataTable* table = state.getObjectBy<DataTable>(modificationNode(), QStringLiteral("isosurface-histogram"));
            _plotWidget->setTable(table);
        });
    }
    else {
        _isoLevelIndicator->hide();
        _plotWidget->setTable({});
    }
}

/******************************************************************************
* Is called when the user starts or stops picking a location in the plot widget.
******************************************************************************/
void CreateIsosurfaceModifierEditor::onPickerActivated(bool on)
{
    if(on) {
        if(CreateIsosurfaceModifier* modifier = static_object_cast<CreateIsosurfaceModifier>(editObject())) {
            _undoTransaction.begin(ui(), tr("Change iso-value"));
        }
    }
    else {
        if(_undoTransaction.operation()) {
            if(editObject())
                _undoTransaction.commit();
            else
                _undoTransaction.cancel();
        }
    }
}

/******************************************************************************
* Is called when the user picks a location in the plot widget.
******************************************************************************/
void CreateIsosurfaceModifierEditor::onPickerPoint(const QPointF& pt)
{
    if(CreateIsosurfaceModifier* modifier = static_object_cast<CreateIsosurfaceModifier>(editObject())) {
        _undoTransaction.revert();
        performActions(_undoTransaction, [&] {
            modifier->setIsolevel(pt.x());
        });
    }
}

}   // End of namespace
