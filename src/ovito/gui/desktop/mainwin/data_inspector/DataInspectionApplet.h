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


#include <ovito/gui/desktop/GUI.h>
#include <ovito/core/oo/OvitoObject.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/core/dataset/scene/Pipeline.h>

namespace Ovito {

/**
 * \brief Abstract base class for applets shown in the data inspector.
 */
class OVITO_GUI_EXPORT DataInspectionApplet : public QObject, public OvitoObject, public UserInterfaceComponent<MainWindowUI>
{
    OVITO_CLASS(DataInspectionApplet)
    Q_OBJECT

public:

    /// Returns the key value for this applet that is used for ordering the applet tabs.
    virtual int orderingKey() const { return std::numeric_limits<int>::max(); }

    /// Determines whether the given pipeline data contains data that can be displayed by this applet.
    virtual bool appliesTo(const DataCollection& data);

    /// Returns whether this applet's tab should be shown even when the currently selected pipeline
    /// produces no data the applet applies to. Applets that are not tied to a particular data object
    /// (e.g. the AI agent console) return true here.
    virtual bool alwaysAvailable() const { return false; }

    /// Determines the list of data objects that are displayed by the applet.
    virtual std::vector<ConstDataObjectPath> getDataObjectPaths() {
        return currentState().getObjectsRecursive(*_dataObjectClass);
    }

    /// Lets the applet create the UI widget that is to be placed into the data inspector panel.
    virtual QWidget* createWidget() = 0;

    /// Creates and returns the list widget displaying the list of data object objects.
    QListWidget* objectSelectionWidget();

    /// Makes the applet update its data display.
    virtual void updateDisplay();

    /// This is called when the applet is no longer visible.
    virtual void deactivate() {}

    /// Selects a specific data object in this applet.
    virtual bool selectDataObject(const PipelineNode* createdByNode, const QString& objectIdentifierHint, const QVariant& modeHint);

    /// Returns the help topic ID for the documentation page of this applet.
    /// An empty string means that no help page is available for this applet.
    /// Then, the main help page of the data inspector is opened instead.
    virtual QString helpTopicId() const { return {}; }

    /// Returns the currently selected data pipeline.
    Pipeline* currentPipeline() const;

    /// Returns the currently selected pipeline scene node.
    SceneNode* currentSceneNode() const;

    /// Returns the current output of the data pipeline displayed in the applet.
    const PipelineFlowState& currentState() const;

    /// Returns the data object that is currently selected.
    const DataObject* selectedDataObject() const { return _selectedDataObject; }

    /// Returns the data collection path of the currently selected data object.
    const ConstDataObjectPath& selectedDataObjectPath() const { return _selectedDataObjectPath; }

    /// Returns the panel hosting this applet.
    DataInspectorPanel* inspectorPanel() const { OVITO_ASSERT(_inspectorPanel); return _inspectorPanel; }

    /// Sets the panel hosting this applet.
    void setInspectorPanel(DataInspectorPanel* inspectorPanel);

protected:

    /// Constructor.
    void initializeObject(const DataObject::OOMetaClass& dataObjectClass) {
        OvitoObject::initializeObject();
        _dataObjectClass = &dataObjectClass;
    }

    /// Updates the list of data objects displayed in the inspector.
    void updateDataObjectList();

Q_SIGNALS:

    /// This signal is emitted when the user selects a different data object in the list.
    void currentObjectChanged(const DataObject* dataObject);

    /// This signal is emitted when the user selects a different data object in the list.
    void currentObjectPathChanged(const QString& dataObjectPath);

private:

    /// The type of data objects displayed by this applet.
    const DataObject::OOMetaClass* _dataObjectClass = nullptr;

    /// The panel hosting this applet.
    DataInspectorPanel* _inspectorPanel = nullptr;

    /// The widget for selecting the current data object.
    QListWidget* _objectSelectionWidget = nullptr;

    /// The path of the currently selected data object.
    ConstDataObjectPath _selectedDataObjectPath;

    /// The identifier path of the currently selected data object.
    QString _selectedDataObjectPathString;

    /// Pointer to the currently selected data object.
    DataOORef<const DataObject> _selectedDataObject;
};

}   // End of namespace
