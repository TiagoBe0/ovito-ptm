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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/core/viewport/ViewportWindow.h>
#include <ovito/gui/base/actions/ViewportModeAction.h>
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/desktop/widgets/general/CopyableTableView.h>
#include "DislocationInspectionApplet.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationInspectionApplet);
OVITO_CLASSINFO(DislocationInspectionApplet, "DisplayName", "Dislocations");

/******************************************************************************
* Determines whether the given pipeline dataset contains data that can be
* displayed by this applet.
******************************************************************************/
bool DislocationInspectionApplet::appliesTo(const DataCollection& data)
{
    return data.containsObject<DislocationNetwork>();
}

/******************************************************************************
* Lets the applet create the UI widget that is to be placed into the data
* inspector panel.
******************************************************************************/
QWidget* DislocationInspectionApplet::createWidget()
{
    QWidget* panel = new QWidget();
    QGridLayout* layout = new QGridLayout(panel);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);

    _pickingMode = OORef<PickingMode>::create(this);
    connect(this, &QObject::destroyed, _pickingMode, &ViewportInputMode::removeMode);
    ViewportModeAction* pickModeAction = new ViewportModeAction(ui(), tr("Select in viewports"), this, _pickingMode);
    pickModeAction->setIcon(QIcon::fromTheme("particles_select_mode"));

    QToolBar* toolbar = new QToolBar();
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(18,18));
    toolbar->addAction(pickModeAction);
    layout->addWidget(toolbar, 0, 0);

    QWidget* pickModeButton = toolbar->widgetForAction(pickModeAction);
    connect(_pickingMode, &ViewportInputMode::statusChanged, pickModeButton, [pickModeButton](bool active) {
        if(active) {
            QToolTip::showText(pickModeButton->mapToGlobal(pickModeButton->rect().bottomRight()),
#ifndef Q_OS_MACOS
                DislocationInspectionApplet::tr("Pick a dislocation in the viewports. Hold down the CONTROL key to select multiple dislocations."),
#else
                DislocationInspectionApplet::tr("Pick a dislocation in the viewports. Hold down the COMMAND key to select multiple dislocations."),
#endif
                pickModeButton, QRect(), 2000);
        }
    });

    _tableView = new CopyableTableView();
    _tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tableModel = new DislocationTableModel(_tableView);
    _tableView->setModel(_tableModel);
    _tableView->horizontalHeader()->resizeSection(0, 60);
    _tableView->horizontalHeader()->resizeSection(1, 140);
    _tableView->horizontalHeader()->resizeSection(2, 200);
    _tableView->horizontalHeader()->resizeSection(4, 60);
    _tableView->horizontalHeader()->resizeSection(6, 200);
    _tableView->horizontalHeader()->resizeSection(7, 200);
#ifdef Q_OS_WIN
    _tableView->horizontalHeader()->setStretchLastSection(true); // To avoid empty space to the right of the last columns on Windows, where the table has a different background color.
#endif
    _tableView->verticalHeader()->hide();
    layout->addWidget(_tableView, 1, 0);
    layout->setRowStretch(1, 1);

    connect(_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if(_pickingMode->isActive())
            _pickingMode->requestViewportUpdate();
    });

    return panel;
}

/******************************************************************************
* Updates the contents displayed in the inspector.
******************************************************************************/
void DislocationInspectionApplet::updateDisplay()
{
    DataInspectionApplet::updateDisplay();
    _tableModel->setContents(currentState());
}

/******************************************************************************
* This is called when the applet is no longer visible.
******************************************************************************/
void DislocationInspectionApplet::deactivate()
{
    _pickingMode->removeMode();
}

/******************************************************************************
* Returns the data stored under the given 'role' for the item referred to by the 'index'.
******************************************************************************/
QVariant DislocationInspectionApplet::DislocationTableModel::data(const QModelIndex& index, int role) const
{
    if(role == Qt::DisplayRole) {
        if(_dislocationObj) {
            DislocationSegment* segment = _dislocationObj->segments()[index.row()];
            switch(index.column()) {
            case 0: return _dislocationObj->segments()[index.row()]->id;
            case 1: return DislocationVis::formatBurgersVector(segment->burgersVector.localVec(), _dislocationObj->structureById(segment->burgersVector.cluster()->structure));
            case 2: { Vector3 b = segment->burgersVector.toSpatialVector();
                    return QStringLiteral("%1 %2 %3")
                                .arg(QLocale::c().toString(b.x(), 'f', 4), 7)
                                .arg(QLocale::c().toString(b.y(), 'f', 4), 7)
                                .arg(QLocale::c().toString(b.z(), 'f', 4), 7); }
            case 3: return QLocale::c().toString(segment->calculateLength());
            case 4: return segment->burgersVector.cluster()->id;
            case 5: return _dislocationObj->structureById(segment->burgersVector.cluster()->structure)->name();
            case 6: { Point3 headLocation = segment->backwardNode().position();
                        if(_dislocationObj->domain()) headLocation = _dislocationObj->domain()->wrapPoint(headLocation);
                        return QStringLiteral("%1 %2 %3")
                            .arg(QLocale::c().toString(headLocation.x(), 'f', 4), 7)
                            .arg(QLocale::c().toString(headLocation.y(), 'f', 4), 7)
                            .arg(QLocale::c().toString(headLocation.z(), 'f', 4), 7); }
            case 7: { Point3 tailLocation = segment->forwardNode().position();
                        if(_dislocationObj->domain()) tailLocation = _dislocationObj->domain()->wrapPoint(tailLocation);
                        return QStringLiteral("%1 %2 %3")
                            .arg(QLocale::c().toString(tailLocation.x(), 'f', 4), 7)
                            .arg(QLocale::c().toString(tailLocation.y(), 'f', 4), 7)
                            .arg(QLocale::c().toString(tailLocation.z(), 'f', 4), 7); }
            // Track identity and velocity, filled in by the Dislocation tracking modifier. Both are
            // empty when that modifier is not part of the pipeline; the velocity is also empty for a
            // track that is too short-lived for a drift fit.
            case 8: return (segment->trackId >= 0) ? QVariant(segment->trackId) : QVariant();
            case 9: return (segment->trackVelocity >= 0) ? QVariant(QLocale::c().toString(segment->trackVelocity, 'f', 4)) : QVariant();
            }
        }
    }
    else if(role == Qt::DecorationRole && index.column() == 1) {
        if(_dislocationObj) {
            const DislocationSegment* segment = _dislocationObj->segments()[index.row()];
            const MicrostructurePhase* crystalStructure = _dislocationObj->structureById(segment->burgersVector.cluster()->structure);
            const BurgersVectorFamily* family = crystalStructure->defaultBurgersVectorFamily();
            for(const BurgersVectorFamily* f : crystalStructure->burgersVectorFamilies()) {
                if(f->isMember(segment->burgersVector.localVec(), crystalStructure)) {
                    family = f;
                    break;
                }
            }
            if(family) return (QColor)family->color();
        }
    }
    return {};
}

/******************************************************************************
* Handles the mouse up events for a Viewport.
******************************************************************************/
void DislocationInspectionApplet::PickingMode::mouseReleaseEvent(ViewportWindow* vpwin, QMouseEvent* event)
{
    if(event->button() == Qt::LeftButton) {
        int pickedDislocationIndex = pickDislocation(vpwin, event->pos());
        if(pickedDislocationIndex != -1) {
            if(!event->modifiers().testFlag(Qt::ControlModifier)) {
                _applet->_tableView->selectRow(pickedDislocationIndex);
                _applet->_tableView->scrollTo(_applet->_tableView->model()->index(pickedDislocationIndex, 0));
            }
            else {
                _applet->_tableView->selectionModel()->select(_applet->_tableView->model()->index(pickedDislocationIndex, 0),
                    QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
            }
        }
    }
    ViewportInputMode::mouseReleaseEvent(vpwin, event);
}

/******************************************************************************
* Determines the dislocation under the mouse cursor.
******************************************************************************/
int DislocationInspectionApplet::PickingMode::pickDislocation(ViewportWindow* vpwin, const QPoint& pos) const
{
    // Check if user has clicked on something.
    if(std::optional<ViewportWindow::PickResult> vpPickResult = vpwin->pick(pos)) {
        // Check if that was a dislocation.
        DislocationPickInfo* pickInfo = dynamic_object_cast<DislocationPickInfo>(vpPickResult->pickInfo().get());
        if(pickInfo && vpPickResult->sceneNode() == _applet->currentSceneNode()) {
            return pickInfo->segmentIndexFromSubObjectID(vpPickResult->subobjectId());
        }
    }

    return -1;
}

/******************************************************************************
* Handles the mouse move event for the given viewport.
******************************************************************************/
void DislocationInspectionApplet::PickingMode::mouseMoveEvent(ViewportWindow* vpwin, QMouseEvent* event)
{
    // Change mouse cursor while hovering over a dislocation.
    if(pickDislocation(vpwin, event->pos()) != -1)
        setCursor(SelectionMode::selectionCursor());
    else
        setCursor(QCursor());

    ViewportInputMode::mouseMoveEvent(vpwin, event);
}

/******************************************************************************
* Lets the input mode render its overlay content in a viewport.
******************************************************************************/
void DislocationInspectionApplet::PickingMode::renderOverlay(Viewport* vp, ViewportWindow* vpWin, FrameGraph& frameGraph, DataSet* dataset)
{
    if(!_applet->currentPipeline())
        return;

    PipelineEvaluationRequest request(frameGraph.time(), frameGraph.stopOnPipelineError(), frameGraph.isInteractive());
    const PipelineFlowState flowState = _applet->currentPipeline()->evaluatePipeline(request).blockForResult();
    const DislocationNetwork* dislocations = flowState.getObject<DislocationNetwork>();
    if(!dislocations)
        return;
    DislocationVis* vis = dynamic_object_cast<DislocationVis>(dislocations->visElement());
    if(!vis)
        return;

    for(const QModelIndex& index : _applet->_tableView->selectionModel()->selectedRows()) {
        int segmentIndex = index.row();
        if(segmentIndex >= 0 && segmentIndex < dislocations->segments().size())
            vis->renderOverlayMarker(dislocations, flowState, segmentIndex, frameGraph, _applet->currentSceneNode());
    }
}

}   // End of namespace
