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
#include <ovito/core/app/undo/UndoableOperation.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/scene/SelectionSet.h>
#include <ovito/core/dataset/scene/Scene.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/core/app/Application.h>
#include <ovito/gui/base/viewport/ViewportInputMode.h>
#include <ovito/gui/base/viewport/ViewportInputManager.h>
#include <ovito/gui/desktop/viewport/input/XFormModes.h>
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/desktop/dialogs/ClonePipelineDialog.h>
#include <ovito/gui/desktop/dialogs/AnimationSettingsDialog.h>
#include "WidgetActionManager.h"

namespace Ovito {

/******************************************************************************
* Initializes the WidgetActionManager.
******************************************************************************/
WidgetActionManager::WidgetActionManager(QObject* parent, MainWindowUI& ui) : ActionManager(parent, ui)
{
    createViewportModeAction(ACTION_XFORM_MOVE_MODE, OORef<MoveMode>::create(), tr("Move"), "edit_mode_move", tr("Move objects."));
    createViewportModeAction(ACTION_XFORM_ROTATE_MODE, OORef<RotateMode>::create(), tr("Rotate"), "edit_mode_rotate", tr("Rotate objects."));

    connect(getAction(ACTION_QUIT), &QAction::triggered, this, &WidgetActionManager::on_Quit_triggered);
    connect(getAction(ACTION_HELP_ABOUT), &QAction::triggered, this, &WidgetActionManager::on_HelpAbout_triggered);
    connect(getAction(ACTION_HELP_GRAPHICS_SYSINFO), &QAction::triggered, this, &WidgetActionManager::on_HelpSystemInfo_triggered);
    connect(getAction(ACTION_HELP_SHOW_ONLINE_HELP), &QAction::triggered, this, &WidgetActionManager::on_HelpShowOnlineHelp_triggered);
    connect(getAction(ACTION_HELP_SHOW_SCRIPTING_HELP), &QAction::triggered, this, &WidgetActionManager::on_HelpShowScriptingReference_triggered);
    connect(getAction(ACTION_HELP_REQUEST_FEATURE), &QAction::triggered, this, &WidgetActionManager::on_HelpRequestFeature_triggered);
    connect(getAction(ACTION_FILE_OPEN), &QAction::triggered, this, &WidgetActionManager::on_FileOpen_triggered);
    connect(getAction(ACTION_FILE_SAVE), &QAction::triggered, this, &WidgetActionManager::on_FileSave_triggered);
    connect(getAction(ACTION_FILE_SAVEAS), &QAction::triggered, this, &WidgetActionManager::on_FileSaveAs_triggered);
    connect(getAction(ACTION_FILE_IMPORT), &QAction::triggered, this, &WidgetActionManager::on_FileImport_triggered);
    connect(getAction(ACTION_FILE_REMOTE_IMPORT), &QAction::triggered, this, &WidgetActionManager::on_FileRemoteImport_triggered);
    connect(getAction(ACTION_FILE_EXPORT), &QAction::triggered, this, &WidgetActionManager::on_FileExport_triggered);
    connect(getAction(ACTION_FILE_NEW_WINDOW), &QAction::triggered, this, &WidgetActionManager::on_FileNewWindow_triggered);
    connect(getAction(ACTION_SETTINGS_DIALOG), &QAction::triggered, this, &WidgetActionManager::on_Settings_triggered);
    connect(getAction(ACTION_ANIMATION_SETTINGS), &QAction::triggered, this, &WidgetActionManager::on_AnimationSettings_triggered);
    connect(getAction(ACTION_RENDER_ACTIVE_VIEWPORT), &QAction::triggered, this, &WidgetActionManager::on_RenderActiveViewport_triggered);
    connect(getAction(ACTION_EDIT_CLONE_PIPELINE), &QAction::triggered, this, &WidgetActionManager::on_ClonePipeline_triggered);
    connect(getAction(ACTION_EDIT_RENAME_PIPELINE), &QAction::triggered, this, &WidgetActionManager::on_RenamePipeline_triggered);
    connect(getAction(ACTION_NEW_PIPELINE_FILESOURCE), &QAction::triggered, this, &WidgetActionManager::on_NewPipelineFileSource_triggered);
    connect(getAction(ACTION_CONFIGURE_VIEWPORT_GRAPHICS), &QAction::triggered, this, &WidgetActionManager::on_ConfigureViewportGraphics_triggered);

    setupCommandSearch();
}

/******************************************************************************
* Handles ACTION_EDIT_CLONE_PIPELINE command
******************************************************************************/
void WidgetActionManager::on_ClonePipeline_triggered()
{
    if(SelectionSet* selection = datasetContainer().activeSelectionSet()) {
        if(SceneNode* sceneNode = selection->firstNode()) {
            if(Pipeline* pipeline = sceneNode->pipeline()) {
                ClonePipelineDialog dialog(ui(), sceneNode, mainWindow());
                dialog.exec();
            }
        }
    }
}

/******************************************************************************
* Handles ACTION_EDIT_RENAME_PIPELINE command
******************************************************************************/
void WidgetActionManager::on_RenamePipeline_triggered()
{
    if(SelectionSet* selection = datasetContainer().activeSelectionSet()) {
        if(SceneNode* sceneNode = selection->firstNode()) {
            QString oldNodeName = sceneNode->objectTitle();
            bool ok;
            QString newName = QInputDialog::getText(mainWindow(), tr("Rename pipeline"), tr("New pipeline name:                                         "), QLineEdit::Normal, oldNodeName, &ok).trimmed();
            if(ok && newName != oldNodeName) {
                performTransaction(tr("Rename pipeline"), [&]() {
                    sceneNode->setSceneNodeName(newName);
                });
            }
        }
    }
}

/******************************************************************************
* Handles ACTION_NEW_PIPELINE_FILESOURCE command
******************************************************************************/
void WidgetActionManager::on_NewPipelineFileSource_triggered()
{
    performTransaction(tr("Create pipeline"), [&]() {

        if(Scene* scene = datasetContainer().activeScene()) {

#ifdef OVITO_BUILD_BASIC
            if(!scene->children().empty())
                throw Exception(tr("OVITO Pro is required to insert more than one pipeline into the scene. Please visit <a href=\"https://www.ovito.org/#proFeatures\">www.ovito.org</a> for more information on the extended version of our software."));
#endif

            // Do not create any animation keys.
            AnimationSuspender animSuspender(ui());

            // Create the FileSource.
            OORef<FileSource> fileSource = OORef<FileSource>::create();

            // Create pipeline.
            OORef<Pipeline> pipeline = OORef<Pipeline>::create();
            pipeline->setHead(fileSource);

            // Create scene node.
            OORef<SceneNode> sceneNode = OORef<SceneNode>::create();
            sceneNode->setPipeline(pipeline);

            // Insert node into scene.
            scene->addChildNode(sceneNode);

            // Select new object in the scene.
            scene->selection()->setNode(sceneNode);
        }
    });
}

/******************************************************************************
* Handles the ACTION_ANIMATION_SETTINGS command.
******************************************************************************/
void WidgetActionManager::on_AnimationSettings_triggered()
{
    if(activeAnimationSettings())
        AnimationSettingsDialog(ui(), mainWindow()).exec();
}

}   // End of namespace
