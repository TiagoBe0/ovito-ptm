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

#pragma once


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include <ovito/crystalanalysis/objects/DislocationVis.h>
#include <ovito/gui/desktop/mainwin/data_inspector/DataInspectionApplet.h>
#include <ovito/gui/base/viewport/ViewportInputMode.h>
#include <ovito/gui/base/viewport/ViewportInputManager.h>
#include <ovito/core/viewport/ViewportGizmo.h>

namespace Ovito {

/**
 * \brief Data inspector page for dislocation lines.
 */
class DislocationInspectionApplet : public DataInspectionApplet
{
    OVITO_CLASS(DislocationInspectionApplet)
    Q_OBJECT

public:

    /// Constructor.
    void initializeObject() { DataInspectionApplet::initializeObject(DislocationNetwork::OOClass()); }

    /// Returns the key value for this applet that is used for ordering the applet tabs.
    virtual int orderingKey() const override { return 50; }

    /// Determines whether the given pipeline data contains data that can be displayed by this applet.
    virtual bool appliesTo(const DataCollection& data) override;

    /// Lets the applet create the UI widget that is to be placed into the data inspector panel.
    virtual QWidget* createWidget() override;

    /// Updates the contents displayed in the inspector.
    virtual void updateDisplay() override;

    /// This is called when the applet is no longer visible.
    virtual void deactivate() override;

    /// Returns the help topic ID for the documentation page of this applet.
    virtual QString helpTopicId() const override { return QStringLiteral("manual:data_inspector.dislocations"); }

private:

    /// A table model for displaying the dislocation list.
    class DislocationTableModel : public QAbstractTableModel
    {
    public:

        /// Constructor.
        DislocationTableModel(QObject* parent) : QAbstractTableModel(parent) {}

        /// Returns the number of rows.
        virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override {
            if(parent.isValid()) return 0;
            if(_dislocationObj) return _dislocationObj->segments().size();
            return 0;
        }

        /// Returns the number of columns.
        virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override {
            return parent.isValid() ? 0 : 10;
        }

        /// Returns the data stored under the given 'role' for the item referred to by the 'index'.
        virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

        /// Returns the data for the given role and section in the header with the specified orientation.
        virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
            if(orientation == Qt::Horizontal && role == Qt::DisplayRole) {
                switch(section) {
                case 0: return tr("Id");
                case 1: return tr("Burgers vector");
                case 2: return tr("Spatial Burgers vector");
                case 3: return tr("Length");
                case 4: return tr("Cluster");
                case 5: return tr("Crystal structure");
                case 6: return tr("Head vertex coordinates");
                case 7: return tr("Tail vertex coordinates");
                case 8: return tr("Track");
                case 9: return tr("Velocity");
                }
            }
            return QAbstractTableModel::headerData(section, orientation, role);
        }

        /// Replaces the contents of this data model.
        void setContents(const PipelineFlowState& state) {
            beginResetModel();
            if(state) {
                _dislocationObj = state.getObject<DislocationNetwork>();
            }
            else {
                _dislocationObj.reset();
            }
            endResetModel();
        }

    private:

        DataOORef<const DislocationNetwork> _dislocationObj;
    };


    /// Viewport input mode that lets the user pick dislocations.
    class PickingMode : public ViewportInputMode, ViewportGizmo
    {
    public:

        /// Constructor.
        void initializeObject(DislocationInspectionApplet* applet) {
            ViewportInputMode::initializeObject();
            _applet = applet;
        }

        /// This is called by the system after the input handler has become the active handler.
        virtual void activated(bool temporaryActivation) override {
            ViewportInputMode::activated(temporaryActivation);
            inputManager()->addViewportGizmo(this);
        }

        /// This is called by the system after the input handler is no longer the active handler.
        virtual void deactivated(bool temporary) override {
            inputManager()->removeViewportGizmo(this);
            ViewportInputMode::deactivated(temporary);
        }

        /// Handles the mouse up events for a viewport.
        virtual void mouseReleaseEvent(ViewportWindow* vpwin, QMouseEvent* event) override;

        /// Handles the mouse move event for the given viewport.
        virtual void mouseMoveEvent(ViewportWindow* vpwin, QMouseEvent* event) override;

        /// Lets the input mode render its overlay content in a viewport.
        virtual void renderOverlay(Viewport* vp, ViewportWindow* vpWin, FrameGraph& frameGraph, DataSet* dataset) override;

    private:

        /// The owner object.
        DislocationInspectionApplet* _applet;

        /// Determines the dislocation under the mouse cursor.
        int pickDislocation(ViewportWindow* vpwin, const QPoint& pos) const;
    };

private:

    /// The data display widget.
    QTableView* _tableView = nullptr;

    /// The table model.
    DislocationTableModel* _tableModel = nullptr;

    /// The viewport input mode for picking dislocations.
    OORef<PickingMode> _pickingMode;
};

}   // End of namespace
