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
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/core/app/PluginManager.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include "DataInspectorPanel.h"
#include "DataInspectionApplet.h"

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
DataInspectorPanel::DataInspectorPanel(MainWindowUI& ui) :
    UserInterfaceComponent<MainWindowUI>(ui),
    _scenePreparation(OORef<ScenePreparation>::create(ui))
{
    // Create data inspection applets.
    for(OvitoClassPtr clazz : PluginManager::instance().listClasses(DataInspectionApplet::OOClass())) {
        OORef<DataInspectionApplet> applet = static_object_cast<DataInspectionApplet>(clazz->createInstance());
        applet->setInspectorPanel(this);
        _applets.push_back(std::move(applet));
    }
    // Give applets a fixed ordering.
    std::sort(_applets.begin(), _applets.end(), [](DataInspectionApplet* a, DataInspectionApplet* b) { return a->orderingKey() < b->orderingKey(); });
    _appletsToTabs.resize(_applets.size(), -1);

    QGridLayout* layout = new QGridLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(3, 1);

    _tabBar = new QTabBar();
    _tabBar->setShape(QTabBar::RoundedNorth);
    _tabBar->setDrawBase(false);
    _tabBar->setExpanding(false);
    _tabBar->setDocumentMode(false);
    layout->addWidget(_tabBar, 0, 1);

    _waitingForSceneIndicator = new QLabel();
    _waitingForSceneAnim.setCacheMode(QMovie::CacheAll);
    _waitingForSceneIndicator->setMovie(&_waitingForSceneAnim);
    _waitingForSceneIndicator->hide();
    layout->addWidget(_waitingForSceneIndicator, 0, 2);
    _waitingForSceneAnim.jumpToNextFrame();
    QSize indicatorSize = _waitingForSceneAnim.currentImage().size();
    layout->setRowMinimumHeight(0, indicatorSize.height());
    layout->setColumnMinimumWidth(2, indicatorSize.width());

    QToolBar* horizontalToolbar = new QToolBar();
    horizontalToolbar->setOrientation(Qt::Horizontal);
    horizontalToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    horizontalToolbar->setIconSize(QSize(16, 16));
    horizontalToolbar->setStyleSheet("QToolButton { padding: 0px; }");

    QAction* helpAction = new QAction(QStringLiteral("?"), this);
    helpAction->setToolTip(tr("Open help page for the current data inspector tab"));
    connect(helpAction, &QAction::triggered, this, &DataInspectorPanel::onHelp);
    horizontalToolbar->addAction(helpAction);

    _expandCollapseAction = new QAction(this);
    _expandCollapseAction->setIcon(_expandIcon);
    _expandCollapseAction->setToolTip(tr("Expand"));
    horizontalToolbar->addAction(_expandCollapseAction);

    layout->addWidget(horizontalToolbar, 0, 4);

    _appletContainer = new QStackedWidget();
    _appletContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    _appletContainer->setMinimumWidth(10);
    _appletContainer->resize(0,0);
    QLabel* label = new QLabel(tr("This panel will list the output of the currently selected pipeline."));
    label->setAlignment(Qt::AlignCenter);
    _appletContainer->addWidget(label);
    for(DataInspectionApplet* applet : _applets)
        _appletContainer->insertWidget(_appletContainer->count() - 1, applet->createWidget());
    layout->addWidget(_appletContainer, 1, 0, 1, -1);

    connect(_expandCollapseAction, &QAction::triggered, this, &DataInspectorPanel::toggle);
    connect(_tabBar, &QTabBar::tabBarClicked, this, &DataInspectorPanel::onTabBarClicked);
    connect(_tabBar, &QTabBar::currentChanged, this, &DataInspectorPanel::onCurrentTabChanged);
    connect(_appletContainer, &QStackedWidget::currentChanged, this, &DataInspectorPanel::onCurrentPageChanged);
    connect(&datasetContainer(), &DataSetContainer::selectionChangeComplete, this, &DataInspectorPanel::onSceneSelectionChanged);
    connect(&datasetContainer(), &DataSetContainer::sceneReplaced, _scenePreparation.get(), [this](Scene* scene) { _scenePreparation->setScene(scene); });
    connect(_scenePreparation.get(), &ScenePreparation::scenePreparationStarted, this, &DataInspectorPanel::onScenePreparationStarted);
    connect(_scenePreparation.get(), &ScenePreparation::scenePreparationFinished, this, &DataInspectorPanel::onScenePreparationFinished);

    // Keep requesting output from the scene pipeline(s) indefinitely.
    _scenePreparation->setAutoRestart(true);

    updateTabsList();
}

/******************************************************************************
* Is called when the user clicked on the tab bar.
******************************************************************************/
void DataInspectorPanel::changeEvent(QEvent* event)
{
    if(event->type() == QEvent::EnabledChange) {
        // Temporarily disable updates of the inspector widget while the widget is disabled.
        // This happens, for example, during image rendering when the entire main window gets disabled.
        _scenePreparation->setScene(isEnabled() ? datasetContainer().activeScene() : nullptr);
    }
    QWidget::changeEvent(event);
}

/******************************************************************************
* Is called when the user clicked on the tab bar.
******************************************************************************/
void DataInspectorPanel::onTabBarClicked(int index)
{
    if(index == -1 || _appletContainer->height() == 0) {
        if(index != -1)
            _tabBar->setCurrentIndex(index);
        if(_appletContainer->height() == 0)
            parentWidget()->setMaximumHeight(16777215);
        if(_appletContainer->height() != 0) {
            collapse();
        }
        else {
            open();
        }
    }
}

/******************************************************************************
* Hides the inspector panel.
******************************************************************************/
void DataInspectorPanel::collapse()
{
    if(_appletContainer->height() != 0) {
        if(QSplitter* parentSplitter = qobject_cast<QSplitter*>(parentWidget())) {
            parentSplitter->setSizes({ parentSplitter->height(), 0 });
        }
    }
}

/******************************************************************************
* Shows the inspector panel.
******************************************************************************/
void DataInspectorPanel::open()
{
    if(_appletContainer->height() == 0) {
        if(QSplitter* parentSplitter = qobject_cast<QSplitter*>(parentWidget())) {
            int viewportSize = parentSplitter->height() * 2 / 3;
            int dataInspectorSize = parentSplitter->height() - viewportSize;
            parentSplitter->setSizes({ viewportSize, dataInspectorSize });
        }
    }
}

/******************************************************************************
* This is called whenever the scene node selection has changed.
******************************************************************************/
void DataInspectorPanel::onSceneSelectionChanged(SelectionSet* selection)
{
    // Find the first selected Pipeline:
    SceneNode* selectedSceneNode = nullptr;
    Pipeline* selectedPipeline = nullptr;
    if(selection) {
        for(SceneNode* node : selection->nodes()) {
            if(node->pipeline()) {
                selectedSceneNode = node;
                selectedPipeline = node->pipeline();
                break;
            }
        }
    }
    _selectedSceneNode = selectedSceneNode;
    if(selectedPipeline != _selectedPipeline) {
        _selectedPipeline = selectedPipeline;
        Q_EMIT selectedPipelineChanged(selectedPipeline);
        updateInspector();
    }
}

/******************************************************************************
* Is emitted whenever the scene of the current dataset has been changed and
* is being made ready for rendering.
******************************************************************************/
void DataInspectorPanel::onScenePreparationStarted()
{
    _activityDelayTimer.start(400, Qt::CoarseTimer, this);
}

/******************************************************************************
*  Is called whenever the scene became ready for rendering.
******************************************************************************/
void DataInspectorPanel::onScenePreparationFinished()
{
    _activityDelayTimer.stop();
    _waitingForSceneIndicator->hide();
    _waitingForSceneAnim.stop();
    updateInspector();
}

/******************************************************************************
* Handles timer events for this object.
******************************************************************************/
void DataInspectorPanel::timerEvent(QTimerEvent* event)
{
    if(event->timerId() == _activityDelayTimer.timerId()) {
        OVITO_ASSERT(_activityDelayTimer.isActive());
        _activityDelayTimer.stop();
        _waitingForSceneAnim.start();
        _waitingForSceneIndicator->show();
    }
    QWidget::timerEvent(event);
}

/******************************************************************************
* Is called whenever the inspector panel was resized.
******************************************************************************/
void DataInspectorPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    bool isPanelOpen = (_appletContainer->height() > 0);
    if(!_inspectorActive && isPanelOpen) {
        _inspectorActive = true;
        _expandCollapseAction->setIcon(_collapseIcon);
        _expandCollapseAction->setToolTip(tr("Collapse"));
        if(_activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
            _applets[_activeAppletIndex]->updateDisplay();
        }
        _appletContainer->setEnabled(true);
    }
    else if(_inspectorActive && !isPanelOpen) {
        _inspectorActive = false;
        _expandCollapseAction->setIcon(_expandIcon);
        _expandCollapseAction->setToolTip(tr("Expand"));
        if(_activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
            _applets[_activeAppletIndex]->deactivate();
        }
        _appletContainer->setEnabled(false);
    }
}

/******************************************************************************
* Updates the contents displayed in the data inspector.
******************************************************************************/
void DataInspectorPanel::updateInspector()
{
    // Obtain the pipeline output of the currently selected scene node.
    updatePipelineOutput();

    // Update the list of visible tabs.
    updateTabsList();

    // Update content displayed by the current inspector tab page.
    if(_inspectorActive) {
        if(_activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
            _applets[_activeAppletIndex]->updateDisplay();
        }
    }
}

/******************************************************************************
* Updates the list of visible tabs.
******************************************************************************/
void DataInspectorPanel::updateTabsList()
{
    OVITO_ASSERT(_appletsToTabs.size() == _applets.size());
    size_t numActiveApplets = 0;
    const DataCollection* dataCollection = pipelineOutput().data();

    // Remove tabs that have become inactive.
    for(int appletIndex = _applets.size()-1; appletIndex >= 0; appletIndex--) {
        if(_appletsToTabs[appletIndex] != -1) {
            DataInspectionApplet* applet = _applets[appletIndex];
            if(!applet->alwaysAvailable() && (!dataCollection || !applet->appliesTo(*dataCollection))) {
                int tabIndex = _appletsToTabs[appletIndex];
                _appletsToTabs[appletIndex] = -1;
                // Shift tab indices.
                for(int i = appletIndex + 1; i < _applets.size(); i++)
                    if(_appletsToTabs[i] != -1) _appletsToTabs[i]--;
                // Remove the tab for this applet.
                _tabBar->removeTab(tabIndex);
            }
            else numActiveApplets++;
        }
    }

    // Create tabs for applets that became active.
    int tabIndex = 0;
    for(int appletIndex = 0; appletIndex < _applets.size(); appletIndex++) {
        if(_appletsToTabs[appletIndex] == -1) {
            DataInspectionApplet* applet = _applets[appletIndex];
            if(applet->alwaysAvailable() || (dataCollection && applet->appliesTo(*dataCollection))) {
                _appletsToTabs[appletIndex] = tabIndex;
                // Shift tab indices.
                for(int i = appletIndex + 1; i < _applets.size(); i++)
                    if(_appletsToTabs[i] != -1) _appletsToTabs[i]++;
                // Create a new tab for the applet.
                _tabBar->insertTab(tabIndex, applet->getOOClass().displayName());
                tabIndex++;
                numActiveApplets++;
            }
        }
        else tabIndex = _appletsToTabs[appletIndex]+1;
    }

    // Show the "Data Inspector" default tab if there are no active applets.
    if(numActiveApplets == 0 && _tabBar->count() == 0) {
        _tabBar->addTab(tr("Data Inspector"));
    }
    else if(numActiveApplets != 0 && _tabBar->count() != numActiveApplets) {
        if(_tabBar->currentIndex() == _tabBar->count() - 1)
            _tabBar->setCurrentIndex(0);
        _tabBar->removeTab(_tabBar->count() - 1);
    }
}

/******************************************************************************
* Evaluates the selected pipeline to obtains its output state.
******************************************************************************/
bool DataInspectorPanel::updatePipelineOutput()
{
    _pipelineOutput.reset();
    if(selectedPipeline()) {
        if(AnimationSettings* anim = activeAnimationSettings()) {
            _pipelineOutput = selectedPipeline()->getCachedPipelineOutput(anim->currentTime(), false);
        }
    }
    return (bool)_pipelineOutput;
}

/******************************************************************************
* Is called when the user selects a new tab.
******************************************************************************/
void DataInspectorPanel::onCurrentTabChanged(int tabIndex)
{
    int appletIndex = _applets.size();
    if(tabIndex >= 0)
        appletIndex = std::find(_appletsToTabs.begin(), _appletsToTabs.end(), tabIndex) - _appletsToTabs.begin();
    OVITO_ASSERT(appletIndex >= 0 && appletIndex < _appletContainer->count());
    _appletContainer->setCurrentIndex(appletIndex);
}

/******************************************************************************
* Is called whenever the user has switched to a different page of the inspector.
******************************************************************************/
void DataInspectorPanel::onCurrentPageChanged(int index)
{
    if(_activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
        _applets[_activeAppletIndex]->deactivate();
    }

    _activeAppletIndex = index;

    if(_inspectorActive && _activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
        // Obtain the output of the currently selected pipeline and update the new tab page.
        updatePipelineOutput();
        _applets[_activeAppletIndex]->updateDisplay();
    }
}

/******************************************************************************
* Selects a specific data object in the data inspector.
******************************************************************************/
bool DataInspectorPanel::selectDataObject(const PipelineNode* createdByNode, const QString& objectIdentifierHint, const QVariant& modeHint)
{
    for(int appletIndex = 0; appletIndex < _applets.size(); appletIndex++) {
        if(_appletsToTabs[appletIndex] == -1) continue;
        DataInspectionApplet* applet = _applets[appletIndex];

        // Update content of the tab.
        applet->updateDisplay();

        // Check if this applet contains the requested data object.
        if(applet->selectDataObject(createdByNode, objectIdentifierHint, modeHint)) {
            // If yes, switch to the tab and we are done.
            _tabBar->setCurrentIndex(_appletsToTabs[appletIndex]);
            return true;
        }
    }
    return false;
}

/******************************************************************************
* This is called when the user has pressed the help button of the data inspector panel.
******************************************************************************/
void DataInspectorPanel::onHelp()
{
    if(_inspectorActive && _activeAppletIndex >= 0 && _activeAppletIndex < _applets.size()) {
        QString helpTopicId = _applets[_activeAppletIndex]->helpTopicId();
        if(!helpTopicId.isEmpty()) {
            actionManager()->openHelpTopic(helpTopicId);
            return;
        }
    }

    actionManager()->openHelpTopic(QStringLiteral("manual:data_inspector"));
}

}   // End of namespace
