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

#include <ovito/gui/base/GUIBase.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/scene/SelectionSet.h>
#include <ovito/core/dataset/scene/Scene.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/gui/base/viewport/NavigationModes.h>
#include <ovito/gui/base/viewport/ViewportInputMode.h>
#include <ovito/gui/base/viewport/ViewportInputManager.h>
#include <ovito/core/app/UserInterface.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/app/undo/UndoStack.h>
#include <ovito/gui/base/actions/ViewportModeAction.h>
#include "ActionManager.h"

namespace Ovito {

/******************************************************************************
* Initializes the ActionManager.
******************************************************************************/
ActionManager::ActionManager(QObject* parent, UserInterface& ui) : QAbstractListModel(parent), UserInterfaceComponent<UserInterface>(ui)
{
    // Actions need to be updated whenever a new dataset is loaded or the current selection changes.
    connect(&datasetContainer(), &DataSetContainer::dataSetChanged, this, &ActionManager::onDataSetChanged);
    connect(&datasetContainer(), &DataSetContainer::selectionChangeComplete, this, &ActionManager::onSelectionChangeComplete);
    connect(&datasetContainer(), &DataSetContainer::animationIntervalChanged, this, &ActionManager::onAnimationIntervalChanged);
    connect(&datasetContainer(), &DataSetContainer::maximizedViewportChanged, this, &ActionManager::onMaximizedViewportChanged);
    connect(&datasetContainer(), &DataSetContainer::viewportLayoutChanged, this, &ActionManager::onViewportLayoutChanged);

    createCommandAction(ACTION_QUIT, tr("Quit"), "file_quit", tr("Quit the application."));
    createCommandAction(ACTION_FILE_OPEN, tr("Load Session State..."), "file_open", tr("Load a previously saved session from a file."), QKeySequence::Open);
    createCommandAction(ACTION_FILE_SAVE, tr("Save Session State"), "file_save", tr("Save the current program session to a file."), QKeySequence::Save);
    createCommandAction(ACTION_FILE_SAVEAS, tr("Save Session State As..."), "file_save_as", tr("Save the current program session to a new file."), QKeySequence::SaveAs);
    createCommandAction(ACTION_FILE_IMPORT, tr("Load File..."), "file_import", tr("Import data from a file on this computer."), QKeyCombination(Qt::CTRL, Qt::Key_I));
    createCommandAction(ACTION_FILE_REMOTE_IMPORT, tr("Load Remote File"), "file_import_remote", tr("Import a file from a remote location."), QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_I));
    createCommandAction(ACTION_FILE_EXPORT, tr("Export File..."), "file_export", tr("Export data to a file."), QKeyCombination(Qt::CTRL, Qt::Key_E));
    createCommandAction(ACTION_FILE_NEW_WINDOW, tr("New Program Window"), "file_new_window", tr("Open another OVITO program window."), QKeySequence::New);
    createCommandAction(ACTION_HELP_ABOUT, tr("About OVITO"), "application_about", tr("Show information about this software."));
    createCommandAction(ACTION_HELP_SHOW_ONLINE_HELP, tr("User Manual"), "help_user_manual", tr("Open the OVITO user manual."), QKeySequence::HelpContents);
    createCommandAction(ACTION_HELP_SHOW_SCRIPTING_HELP, tr("Scripting Reference"), "help_scripting_manual", tr("Open the OVITO Python API documentation."));
    createCommandAction(ACTION_HELP_GRAPHICS_SYSINFO, tr("System Information..."), "help_system_info", tr("Display system and graphics hardware information."));
    createCommandAction(ACTION_HELP_REQUEST_FEATURE, tr("Request a Feature"), nullptr, tr("Request a new feature for OVITO."));

    QAction* undoAction = createCommandAction(ACTION_EDIT_UNDO, tr("Undo"), "edit_undo", tr("Reverse the last action."), QKeySequence::Undo);
    QAction* redoAction = createCommandAction(ACTION_EDIT_REDO, tr("Redo"), "edit_redo", tr("Restore the previously reversed action."), QKeySequence::Redo);
    QAction* clearUndoStackAction = createCommandAction(ACTION_EDIT_CLEAR_UNDO_STACK, tr("Clear Undo Stack"), nullptr, tr("Discards all existing undo records."));
    clearUndoStackAction->setVisible(false);
    if(UndoStack* undoStack = this->undoStack()) {
        undoAction->setEnabled(undoStack->canUndo());
        redoAction->setEnabled(undoStack->canRedo());
        undoAction->setText(tr("Undo %1").arg(undoStack->undoText()));
        redoAction->setText(tr("Redo %1").arg(undoStack->redoText()));
        connect(undoStack, &UndoStack::canUndoChanged, undoAction, &QAction::setEnabled);
        connect(undoStack, &UndoStack::canRedoChanged, redoAction, &QAction::setEnabled);
        connect(undoStack, &UndoStack::undoTextChanged, undoAction, [undoAction](const QString& undoText) {
            undoAction->setText(tr("Undo %1").arg(undoText));
        });
        connect(undoStack, &UndoStack::redoTextChanged, redoAction, [redoAction](const QString& redoText) {
            redoAction->setText(tr("Redo %1").arg(redoText));
        });
        connect(undoAction, &QAction::triggered, undoStack, &UndoStack::undo);
        connect(redoAction, &QAction::triggered, undoStack, &UndoStack::redo);
        connect(clearUndoStackAction, &QAction::triggered, undoStack, &UndoStack::clear);
    }
    else {
        undoAction->setEnabled(false);
        redoAction->setEnabled(false);
        clearUndoStackAction->setEnabled(false);
    }

    QAction* createNewPipelineAcion = createCommandAction(ACTION_NEW_PIPELINE_FILESOURCE, tr("External data file"), "edit_create_pipeline", tr("Creates a new pipeline with an external file as data source."));
    QAction* clonePipelineAction = createCommandAction(ACTION_EDIT_CLONE_PIPELINE, tr("Clone Pipeline..."), "edit_clone_pipeline", tr("Duplicate the current pipeline to show multiple datasets side by side."));
#ifdef OVITO_BUILD_BASIC
    createNewPipelineAcion->setText(createNewPipelineAcion->text() + QStringLiteral(" (Pro)"));
    clonePipelineAction->setText(clonePipelineAction->text() + QStringLiteral(" (Pro)"));
#else
    (void)createNewPipelineAcion;
    (void)clonePipelineAction;
#endif
    createCommandAction(ACTION_EDIT_RENAME_PIPELINE, tr("Rename Pipeline..."), "edit_rename_pipeline", tr("Assign a new name to the selected pipeline."));
    createCommandAction(ACTION_EDIT_DELETE, tr("Delete Pipeline"), "edit_delete_pipeline", tr("Delete the selected object from the scene."));

    createCommandAction(ACTION_SETTINGS_DIALOG, tr("Application Settings..."), "application_preferences", tr("Open the application settings dialog"), QKeySequence::Preferences);

    createCommandAction(ACTION_RENDER_ACTIVE_VIEWPORT, tr("Render"), "render_active_viewport", tr("Render an image or animation of the current viewport."));

    createCommandAction(ACTION_VIEWPORT_MAXIMIZE, tr("Maximize Active Viewport"), "viewport_maximize", tr("Enlarge/reduce the active viewport."))->setCheckable(true);
    createCommandAction(ACTION_VIEWPORT_ZOOM_SCENE_EXTENTS, tr("Zoom Scene Extents"), "viewport_zoom_scene_extents",
#ifndef Q_OS_MACOS
        tr("Zoom active viewport to show everything. Use CONTROL key to zoom all viewports at once."));
#else
        tr("Zoom active viewport to show everything. Use COMMAND key to zoom all viewports at once."));
#endif
    createCommandAction(ACTION_VIEWPORT_ZOOM_SCENE_EXTENTS_ALL, tr("Zoom Scene Extents All"), nullptr, tr("Zoom all viewports to show everything."));
    createCommandAction(ACTION_VIEWPORT_ZOOM_SELECTION_EXTENTS, tr("Zoom Selection Extents"), nullptr, tr("Zoom active viewport to show the selected objects."));
    createCommandAction(ACTION_VIEWPORT_ZOOM_SELECTION_EXTENTS_ALL, tr("Zoom Selection Extents All"), nullptr, tr("Zoom all viewports to show the selected objects."));
    createCommandAction(ACTION_CONFIGURE_VIEWPORT_GRAPHICS, tr("Configure Graphics..."), nullptr, tr("Change graphics settings for real-time interactive viewports."));

    if(ViewportInputManager* vpInputManager = viewportInputManager()) {
        createViewportModeAction(ACTION_VIEWPORT_ZOOM, vpInputManager->zoomMode(), tr("Zoom"), "viewport_mode_zoom", tr("Activate zoom mode."));
        createViewportModeAction(ACTION_VIEWPORT_PAN, vpInputManager->panMode(), tr("Pan"), "viewport_mode_pan", tr("Activate pan mode to shift the region visible in the viewports."));
        createViewportModeAction(ACTION_VIEWPORT_ORBIT, vpInputManager->orbitMode(), tr("Orbit Camera"), "viewport_mode_orbit", tr("Activate orbit mode to rotate the camera around the scene."));
        createViewportModeAction(ACTION_VIEWPORT_FOV, vpInputManager->fovMode(), tr("Change Field Of View"), "viewport_mode_fov", tr("Activate field of view mode to change the perspective projection."));
        createViewportModeAction(ACTION_VIEWPORT_PICK_ORBIT_CENTER, vpInputManager->pickOrbitCenterMode(), tr("Set Orbit Center"), nullptr, tr("Set the center of rotation of the viewport camera."))->setVisible(false);
        createViewportModeAction(ACTION_SELECTION_MODE, vpInputManager->selectionMode(), tr("Select"), "edit_mode_select", tr("Select objects in the viewports."));
    }

    createCommandAction(ACTION_GOTO_START_OF_ANIMATION, tr("Go to Start of Animation"), "animation_goto_start", tr("Jump to first frame of the animation."), Qt::Key_Home);
    createCommandAction(ACTION_GOTO_END_OF_ANIMATION, tr("Go to End of Animation"), "animation_goto_end", tr("Jump to the last frame of the animation."), Qt::Key_End);
    createCommandAction(ACTION_GOTO_PREVIOUS_FRAME, tr("Go to Previous Frame"), "animation_goto_previous_frame", tr("Move time slider one animation frame backward."), QKeyCombination(Qt::ALT, Qt::Key_Left));
    createCommandAction(ACTION_GOTO_NEXT_FRAME, tr("Go to Next Frame"), "animation_goto_next_frame", tr("Move time slider one animation frame forward."), QKeyCombination(Qt::ALT, Qt::Key_Right));
    createCommandAction(ACTION_START_ANIMATION_PLAYBACK, tr("Start Animation Playback"), "animation_play", tr("Start playing the animation in the viewports."));
    createCommandAction(ACTION_STOP_ANIMATION_PLAYBACK, tr("Stop Animation Playback"), "animation_stop", tr("Stop playing the animation in the viewports."));
    createCommandAction(ACTION_ANIMATION_SETTINGS, tr("Animation Settings"), "animation_settings", tr("Open the animation settings dialog."));
    createCommandAction(ACTION_AUTO_KEY_MODE_TOGGLE, tr("Auto Key Mode"), "animation_auto_key_mode", tr("Toggle auto-key mode for creating animation keys."))->setCheckable(true);

    QAction* toggleAnimationPlaybackAction = createCommandAction(ACTION_TOGGLE_ANIMATION_PLAYBACK, tr("Play Animation"), "animation_play", tr("Start/stop animation playback. Hold down Shift key to play backwards."), Qt::Key_Space);
    toggleAnimationPlaybackAction->setCheckable(true);
    toggleAnimationPlaybackAction->setChecked(datasetContainer().isPlaybackActive());
    connect(&datasetContainer(), &DataSetContainer::playbackChanged, toggleAnimationPlaybackAction, &QAction::setChecked);
    connect(toggleAnimationPlaybackAction, &QAction::toggled, &datasetContainer(), &DataSetContainer::setAnimationPlayback);

    connect(getAction(ACTION_VIEWPORT_MAXIMIZE), &QAction::triggered, this, &ActionManager::on_ViewportMaximize_triggered);
    connect(getAction(ACTION_VIEWPORT_ZOOM_SCENE_EXTENTS), &QAction::triggered, this, &ActionManager::on_ViewportZoomSceneExtents_triggered);
    connect(getAction(ACTION_VIEWPORT_ZOOM_SELECTION_EXTENTS), &QAction::triggered, this, &ActionManager::on_ViewportZoomSelectionExtents_triggered);
    connect(getAction(ACTION_VIEWPORT_ZOOM_SCENE_EXTENTS_ALL), &QAction::triggered, this, &ActionManager::on_ViewportZoomSceneExtentsAll_triggered);
    connect(getAction(ACTION_VIEWPORT_ZOOM_SELECTION_EXTENTS_ALL), &QAction::triggered, this, &ActionManager::on_ViewportZoomSelectionExtentsAll_triggered);
    connect(getAction(ACTION_GOTO_START_OF_ANIMATION), &QAction::triggered, this, &ActionManager::on_AnimationGotoStart_triggered);
    connect(getAction(ACTION_GOTO_END_OF_ANIMATION), &QAction::triggered, this, &ActionManager::on_AnimationGotoEnd_triggered);
    connect(getAction(ACTION_GOTO_PREVIOUS_FRAME), &QAction::triggered, this, &ActionManager::on_AnimationGotoPreviousFrame_triggered);
    connect(getAction(ACTION_GOTO_NEXT_FRAME), &QAction::triggered, this, &ActionManager::on_AnimationGotoNextFrame_triggered);
    connect(getAction(ACTION_START_ANIMATION_PLAYBACK), &QAction::triggered, this, &ActionManager::on_AnimationStartPlayback_triggered);
    connect(getAction(ACTION_STOP_ANIMATION_PLAYBACK), &QAction::triggered, this, &ActionManager::on_AnimationStopPlayback_triggered);
    connect(getAction(ACTION_EDIT_DELETE), &QAction::triggered, this, &ActionManager::on_EditDelete_triggered);
}

/******************************************************************************
* Returns dataset currently being edited in the main window.
******************************************************************************/
DataSet* ActionManager::dataset() const
{
    return datasetContainer().currentSet();
}

void ActionManager::onDataSetChanged(DataSet* newDataSet)
{
    // Turn off auto-key animation mode.
    getAction(ACTION_AUTO_KEY_MODE_TOGGLE)->setChecked(false);
}

/******************************************************************************
* This is called when the active animation interval has changed.
******************************************************************************/
void ActionManager::onAnimationIntervalChanged(int firstFrame, int lastFrame)
{
    bool isAnimation = (lastFrame > firstFrame);
    getAction(ACTION_GOTO_START_OF_ANIMATION)->setEnabled(isAnimation);
    getAction(ACTION_GOTO_PREVIOUS_FRAME)->setEnabled(isAnimation);
    getAction(ACTION_TOGGLE_ANIMATION_PLAYBACK)->setEnabled(isAnimation);
    getAction(ACTION_GOTO_NEXT_FRAME)->setEnabled(isAnimation);
    getAction(ACTION_GOTO_END_OF_ANIMATION)->setEnabled(isAnimation);
    getAction(ACTION_AUTO_KEY_MODE_TOGGLE)->setEnabled(isAnimation);
    if(!isAnimation && getAction(ACTION_AUTO_KEY_MODE_TOGGLE)->isChecked())
        getAction(ACTION_AUTO_KEY_MODE_TOGGLE)->setChecked(false);
}

/******************************************************************************
* This is called when a different viewport become the maximized one.
******************************************************************************/
void ActionManager::onMaximizedViewportChanged(Viewport* maximizedViewport)
{
    getAction(ACTION_VIEWPORT_MAXIMIZE)->setChecked(maximizedViewport != nullptr);
}

/******************************************************************************
* This is called when the viewport layout changes.
******************************************************************************/
void ActionManager::onViewportLayoutChanged(ViewportConfiguration* viewportConfig)
{
    getAction(ACTION_VIEWPORT_MAXIMIZE)->setEnabled(viewportConfig && viewportConfig->layoutRootCell() && !viewportConfig->layoutRootCell()->children().empty());
}

/******************************************************************************
* This is called whenever the scene node selection changed.
******************************************************************************/
void ActionManager::onSelectionChangeComplete(SelectionSet* selection)
{
    getAction(ACTION_EDIT_DELETE)->setEnabled(selection && !selection->nodes().empty());
    getAction(ACTION_EDIT_CLONE_PIPELINE)->setEnabled(selection && !selection->nodes().empty());
    getAction(ACTION_EDIT_RENAME_PIPELINE)->setEnabled(selection && !selection->nodes().empty());
}

/******************************************************************************
* Registers an action with the ActionManager.
******************************************************************************/
void ActionManager::addAction(QAction* action)
{
    OVITO_CHECK_POINTER(action);
    OVITO_ASSERT_MSG(action->parent() == this || findAction(action->objectName()) == nullptr, "ActionManager::addAction()", qPrintable(QStringLiteral("There is already an action with the same ID: %1").arg(action->objectName())));
    OVITO_ASSERT(!_actions.contains(action));

    // Make the action a child of this object.
    action->setParent(this);
    beginInsertRows(QModelIndex(), _actions.size(), _actions.size());
    _actions.push_back(action);
    endInsertRows();
}

/******************************************************************************
* Removes the given action from the ActionManager and deletes it.
******************************************************************************/
void ActionManager::deleteAction(QAction* action)
{
    OVITO_CHECK_POINTER(action);
    OVITO_ASSERT_MSG(action->parent() == this, "ActionManager::deleteAction()", "The action is not owned by the ActionManager.");
    OVITO_ASSERT_MSG(_actions.contains(action), "ActionManager::deleteAction()", "The action has not been registered with the ActionManager.");

    // Make the action a child of this object.
    int index = _actions.indexOf(action);
    beginRemoveRows(QModelIndex(), index, index);
    _actions.remove(index);
    delete action;
    endRemoveRows();
}

/******************************************************************************
* Creates and registers a new command action with the ActionManager.
******************************************************************************/
QAction* ActionManager::createCommandAction(const QString& id, const QString& title, const char* iconPath, const QString& statusTip, const QKeySequence& shortcut)
{
    QAction* action = new QAction(title, this);
    action->setObjectName(id);
    if(!shortcut.isEmpty())
        action->setShortcut(shortcut);
    if(!statusTip.isEmpty())
        action->setStatusTip(statusTip);
    if(!shortcut.isEmpty())
        action->setToolTip(QStringLiteral("%1 [%2]").arg(title).arg(shortcut.toString(QKeySequence::NativeText)));
    if(iconPath)
        action->setIcon((iconPath[0] == ':') ? QIcon(iconPath) : QIcon::fromTheme(iconPath));
    addAction(action);
    return action;
}

/******************************************************************************
* Creates and registers a new viewport mode action with the ActionManager.
******************************************************************************/
QAction* ActionManager::createViewportModeAction(const QString& id, OORef<ViewportInputMode> inputHandler, const QString& title, const char* iconPath, const QString& statusTip, const QKeySequence& shortcut)
{
    QAction* action = new ViewportModeAction(ui(), title, this, std::move(inputHandler));
    action->setObjectName(id);
    if(!shortcut.isEmpty())
        action->setShortcut(shortcut);
    action->setStatusTip(statusTip);
    if(!shortcut.isEmpty())
        action->setToolTip(QStringLiteral("%1 [%2]").arg(title).arg(shortcut.toString(QKeySequence::NativeText)));
    if(iconPath)
        action->setIcon((iconPath[0] == ':') ? QIcon(iconPath) : QIcon::fromTheme(iconPath));
    addAction(action);
    return action;
}

/******************************************************************************
* Returns the data stored in this list model under the given role.
******************************************************************************/
QVariant ActionManager::data(const QModelIndex& index, int role) const
{
    if(index.row() < 0) return {};
    QAction* action = _actions[index.row()];
    if(role == Qt::DisplayRole) {
        QString text = action->text();
        if(text.endsWith(QStringLiteral("...")))
            text.chop(3);
        return text;
    }
    else if(role == SearchTextRole)
        return QStringLiteral("%1 %2").arg(action->text(), action->statusTip());
    else if(role == ActionRole)
        return QVariant::fromValue(action);
    else if(role == Qt::StatusTipRole)
        return action->statusTip();
    else if(role == Qt::DecorationRole)
        return action->icon();
    else if(role == ShortcutRole)
        return action->shortcut();
    else if(role == Qt::FontRole) {
        static QFont font = QGuiApplication::font();
        font.setBold(true);
        return font;
    }
    return {};
}

/******************************************************************************
* Returns the flags for an item in this list model.
******************************************************************************/
Qt::ItemFlags ActionManager::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = QAbstractListModel::flags(index);
    if(index.row() >= 0 && index.row() < _actions.size()) {
        QAction* action = _actions[index.row()];
        if(!action->isEnabled())
            flags.setFlag(Qt::ItemIsEnabled, false);
    }
    return flags;
}

/******************************************************************************
* Updates the enabled/disabled state of all actions.
******************************************************************************/
void ActionManager::updateActionStates()
{
    Q_EMIT actionUpdateRequested();
}

/******************************************************************************
* Handles ACTION_EDIT_DELETE command.
******************************************************************************/
void ActionManager::on_EditDelete_triggered()
{
    performTransaction(tr("Delete pipeline"), [&]() {
        // Get active scene.
        if(Scene* scene = datasetContainer().activeScene()) {
            // Delete all nodes in selection set.
            while(!scene->selection()->nodes().empty())
                scene->selection()->nodes().front()->requestObjectDeletion();

            // Automatically select one of the remaining nodes.
            if(scene->children().isEmpty() == false)
                scene->selection()->setNode(scene->children().front());
        }
    });
}

/******************************************************************************
* Handles user clicks on an action link.
******************************************************************************/
void ActionManager::handleActionLink(const QString& link)
{
    if(link.startsWith("action:")) {
        QString actionId = link.mid(7);
        if(QAction* action = findAction(actionId)) {
            action->trigger();
        }
        else {
            ui().reportError(tr("Action not found: %1").arg(actionId));
        }
    }
    else if(link.startsWith("manual:")) {
        openHelpTopic(link);
    }
    else {
        ui().reportError(tr("Invalid action link: %1").arg(link));
    }
}

/******************************************************************************
* Shows the online manual and opens the given help page.
******************************************************************************/
void ActionManager::openHelpTopic(const QString& helpTopicId)
{
    // Determine the filesystem path where OVITO's documentation files are installed.
#ifndef Q_OS_WASM
    QDir prefixDir(Application::instance()->applicationDirPath());
    QDir helpDir = QDir(prefixDir.absolutePath() + QChar('/') + QStringLiteral(OVITO_DOCUMENTATION_PATH));
    QUrl url;
#else
    QDir helpDir(QStringLiteral(":/doc/manual/"));
    QUrl baseUrl(QStringLiteral("https://docs.ovito.org/"));
    QUrl url = baseUrl;
#endif

    // Resolve the help topic ID.
    if(helpTopicId.endsWith(".html") || helpTopicId.contains(".html#")) {
        // If a HTML file name has been specified, open it directly.
        url = QUrl::fromLocalFile(helpDir.absoluteFilePath(helpTopicId));
    }
    else if(helpTopicId.startsWith("manual:")) {
        // If a Sphinx link target has been specified, resolve it to a HTML file path using the
        // Intersphinx inventory. The file 'objects.txt' is generated by the script 'ovito/doc/manual/CMakeLists.txt'
        // and gets distributed together with the application.
        QFile inventoryFile(helpDir.absoluteFilePath("objects.txt"));
        if(!inventoryFile.open(QIODevice::ReadOnly | QIODevice::Text))
            qWarning() << "WARNING: Could not open Intersphinx inventory file to resolve help topic reference:" << inventoryFile.fileName() << inventoryFile.errorString();
        else {
            QTextStream stream(&inventoryFile);
            // Skip file until to the line "std:label":
            while(!stream.atEnd()) {
                QString line = stream.readLine();
                if(line.startsWith("std:label"))
                    break;
            }
            // Now parse the link target list.
            QString searchString = helpTopicId.mid(7) + QChar(' ');
            while(!stream.atEnd()) {
                QString line = stream.readLine().trimmed();
                if(line.startsWith(searchString)) {
                    int startIndex = line.lastIndexOf(QChar(' '));
                    QString filePath = line.mid(startIndex + 1).trimmed();
                    QString anchor;
                    int anchorIndex = filePath.indexOf(QChar('#'));
                    if(anchorIndex >= 0) {
                        anchor = filePath.mid(anchorIndex + 1);
                        filePath.truncate(anchorIndex);
                    }
#ifndef Q_OS_WASM
                    url = QUrl::fromLocalFile(helpDir.absoluteFilePath(filePath));
#else
                    url.setPath(QChar('/') + filePath);
#endif
                    url.setFragment(anchor);
                    break;
                }
            }
            OVITO_ASSERT(!url.isEmpty());
        }
    }

#ifndef Q_OS_WASM
    if(url.isEmpty()) {
        // If no help topic has been specified, open the main index page of the user manual.
        url = QUrl::fromLocalFile(helpDir.absoluteFilePath(QStringLiteral("index.html")));
    }
#endif

    // Workaround for a limitation of the Microsoft Edge and Apple Safari browsers:
    // These browsers drop any # fragment in local URLs to be opened, thus making it difficult to reference sub-topics within a HTML help page.
    // Solution is to generate a temporary HTML file which redirects to the actual help page including the # fragment.
    // See also https://forums.madcapsoftware.com/viewtopic.php?f=9&t=28376#p130613
    // and https://stackoverflow.com/questions/26305322/shellexecute-fails-for-local-html-or-file-urls
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    if(url.isLocalFile() && url.hasFragment()) {
        static QTemporaryFile* temporaryHtmlFile = nullptr;
        if(temporaryHtmlFile)
            delete temporaryHtmlFile;
        temporaryHtmlFile = new QTemporaryFile(QDir::temp().absoluteFilePath(QStringLiteral("ovito-help-XXXXXX.html")), qApp);
        if(temporaryHtmlFile->open()) {
            // Write a small HTML file that just contains a redirect directive to the actual help page including the # fragment.
            QTextStream(temporaryHtmlFile) << QStringLiteral("<html><meta http-equiv=Refresh content=\"0; url=%1\"><body><a href=\"%1\">Continue to help topic...</a></body></html>").arg(url.toString(QUrl::FullyEncoded));
            temporaryHtmlFile->close();
            // Let the web browser open the redirect page instead of the original help page.
            url = QUrl::fromLocalFile(temporaryHtmlFile->fileName());
        }
    }
#endif

    // Use the local web browser to display the help page.
    if(!QDesktopServices::openUrl(url)) {
        ui().reportError(QStringLiteral("Failed to launch browser to display OVITO user manual. The requested URL was:\n%1").arg(url.toDisplayString()));
    }
}

}   // End of namespace
