#######################################################################################
#
#  Copyright 2025 OVITO GmbH, Germany
#
#  This file is part of OVITO (Open Visualization Tool).
#
#  OVITO is free software; you can redistribute it and/or modify it either under the
#  terms of the GNU General Public License version 3 as published by the Free Software
#  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
#  If you do not alter this notice, a recipient may use your version of this
#  file under either the GPL or the MIT License.
#
#  You should have received a copy of the GPL along with this program in a
#  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
#  with this program in a file LICENSE.MIT.txt
#
#  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
#  either express or implied. See the GPL or the MIT License for the specific language
#  governing rights and limitations.
#
#######################################################################################

# Find the required Qt modules.
SET(OVITO_GUI_REQUIRED_QT_COMPONENTS Widgets Network)
IF(LINUX)
    # On Linux, the DBus component is needed to access dark/light desktop theme settings.
    LIST(APPEND OVITO_GUI_REQUIRED_QT_COMPONENTS DBus)
ENDIF()
FIND_PACKAGE(Qt6 ${OVITO_MINIMUM_REQUIRED_QT_VERSION} COMPONENTS ${OVITO_GUI_REQUIRED_QT_COMPONENTS} REQUIRED)

# Define plugin module.
OVITO_STANDARD_PLUGIN(Gui
    SOURCES
        mainwin/MainWindow.cpp
        mainwin/MainWindowUI.cpp
        mainwin/ViewportsPanel.cpp
        mainwin/TaskDisplayWidget.cpp
        mainwin/cmdpanel/CommandPanel.cpp
        mainwin/cmdpanel/RenderCommandPage.cpp
        mainwin/cmdpanel/ModifyCommandPage.cpp
        mainwin/cmdpanel/OverlayCommandPage.cpp
        mainwin/cmdpanel/UtilityCommandPage.cpp
        mainwin/cmdpanel/UtilityListModel.cpp
        mainwin/data_inspector/DataInspectorPanel.cpp
        mainwin/data_inspector/DataInspectionApplet.cpp
        mainwin/data_inspector/GlobalAttributesInspectionApplet.cpp
        dialogs/AdjustViewDialog.cpp
        dialogs/CameraPathDialog.cpp
        dialogs/ConfigureViewportGraphicsDialog.cpp
        dialogs/AnimationKeyEditorDialog.cpp
        dialogs/AnimationSettingsDialog.cpp
        dialogs/ApplicationSettingsDialog.cpp
        dialogs/GeneralSettingsPage.cpp
        dialogs/ViewportSettingsPage.cpp
        dialogs/ModifierTemplatesPage.cpp
        dialogs/OverlayTemplatesPage.cpp
        dialogs/TemplatesPageBase.cpp
        dialogs/HistoryFileDialog.cpp
        dialogs/ImportFileDialog.cpp
        dialogs/ImportRemoteFileDialog.cpp
        dialogs/SaveImageFileDialog.cpp
        dialogs/LoadImageFileDialog.cpp
        dialogs/RemoteAuthenticationDialog.cpp
        dialogs/FileExporterSettingsDialog.cpp
        dialogs/ClonePipelineDialog.cpp
        dialogs/CopyPipelineItemDialog.cpp
        dialogs/FontSelectionDialog.cpp
        dialogs/ModalPropertiesEditorDialog.cpp
        dialogs/UpdateNotificationDialog.cpp
        dialogs/SystemInformationDialog.cpp
        actions/WidgetActionManager.cpp
        actions/FileActions.cpp
        actions/RenderActions.cpp
        actions/SearchActions.cpp
        dataset/io/FileSourceEditor.cpp
        dataset/io/FileSourcePlaybackRateEditor.cpp
        dataset/io/FileImporterEditor.cpp
        dataset/io/AttributeFileExporterEditor.cpp
        dataset/animation/controller/TCBInterpolationControllerEditor.cpp
        dataset/mesh/TriMeshVisEditor.cpp
        widgets/general/RolloutContainer.cpp
        widgets/general/RolloutContainerLayout.cpp
        widgets/general/SpinnerWidget.cpp
        widgets/general/ColorPickerWidget.cpp
        widgets/general/ElidedTextLabel.cpp
        widgets/general/AutocompleteEdit.cpp
        widgets/general/AutocompleteLineEdit.cpp
        widgets/general/AutocompleteTextEdit.cpp
        widgets/general/HtmlListWidget.cpp
        widgets/general/ViewportModeButton.cpp
        widgets/general/StatusBar.cpp
        widgets/general/MenuToolButton.cpp
        widgets/general/PopupUpdateComboBox.cpp
        widgets/general/CopyableTableView.cpp
        widgets/general/StableComboBox.cpp
        widgets/general/EnterLineEdit.cpp
        widgets/animation/AnimationTimeSpinner.cpp
        widgets/animation/AnimationTimeSlider.cpp
        widgets/animation/AnimationTrackBar.cpp
        widgets/rendering/FrameBufferWidget.cpp
        widgets/rendering/FrameBufferWindow.cpp
        widgets/selection/SceneNodesListModel.cpp
        widgets/selection/SceneNodeSelectionBox.cpp
        widgets/display/CoordinateDisplayWidget.cpp
        widgets/display/StatusWidget.cpp
        properties/PropertiesEditor.cpp
        properties/PropertiesPanel.cpp
        properties/ParameterUI.cpp
        properties/PropertyParameterUI.cpp
        properties/NumericalParameterUI.cpp
        properties/AffineTransformationParameterUI.cpp
        properties/BooleanActionParameterUI.cpp
        properties/BooleanGroupBoxParameterUI.cpp
        properties/BooleanParameterUI.cpp
        properties/BooleanRadioButtonParameterUI.cpp
        properties/ColorParameterUI.cpp
        properties/CustomParameterUI.cpp
        properties/DefaultPropertiesEditor.cpp
        properties/FilenameParameterUI.cpp
        properties/FloatParameterUI.cpp
        properties/FontParameterUI.cpp
        properties/IntegerParameterUI.cpp
        properties/IntegerRadioButtonParameterUI.cpp
        properties/IntegerCheckBoxParameterUI.cpp
        properties/ModifierDelegateParameterUI.cpp
        properties/ModifierDelegateFixedListParameterUI.cpp
        properties/ModifierDelegateVariableListParameterUI.cpp
        properties/ObjectStatusDisplay.cpp
        properties/RefTargetListParameterUI.cpp
        properties/StringParameterUI.cpp
        properties/SubObjectParameterUI.cpp
        properties/VariantComboBoxParameterUI.cpp
        properties/VectorParameterUI.cpp
        properties/OpenDataInspectorButton.cpp
        properties/ModifierGroupEditor.cpp
        properties/PipelineSelectionParameterUI.cpp
        properties/DataObjectReferenceParameterUI.cpp
        rendering/RenderSettingsEditor.cpp
        rendering/BaseSceneRendererEditor.cpp
        viewport/ViewportMenu.cpp
        viewport/input/XFormModes.cpp
        viewport/overlays/CoordinateTripodOverlayEditor.cpp
        viewport/overlays/TextLabelOverlayEditor.cpp
        viewport/overlays/MoveOverlayInputMode.cpp
        app/GuiApplication.cpp
        app/GuiApplicationService.cpp
        utilities/concurrent/ProgressDialog.cpp
        resources/gui.qrc
    PLUGIN_DEPENDENCIES
        GuiBase
        ViewportWidget
    LIB_DEPENDENCIES
        Qt6::Widgets
        Qt6::Network
    PRECOMPILED_HEADERS
        GUI.h
)
IF(LINUX)
    TARGET_LINK_LIBRARIES(Gui PRIVATE Qt6::DBus)
ENDIF()

IF(APPLE)
    TARGET_LINK_LIBRARIES(Gui PRIVATE "-framework ApplicationServices")
ENDIF()

# Inject copyright notice text into the source code.
SET_PROPERTY(SOURCE actions/FileActions.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
    "OVITO_COPYRIGHT_NOTICE=\"${OVITO_COPYRIGHT_NOTICE}\"")

# Let the GUI module know if we are building a preview/development version of OVITO.
# A notice will be shown in the main window reminding the user to switch to the
# final release once it becomes available.
IF(OVITO_USE_GIT_REVISION_NUMBER)
    STRING(TIMESTAMP current_date "%d-%b-%Y")
    TARGET_COMPILE_DEFINITIONS(Gui PRIVATE "OVITO_DEVELOPMENT_BUILD_DATE=\"${current_date}\"")
ENDIF()

# Workaround for missing image files in the Qt 6.2 library on macOS.
# qtbase/src/plugins/styles/mac/qmacstyle_mac.mm, line 6702 references the embedded image resource "toolbar-ext.png", which
# doesn't exist. Probably it has been renamed to "toolbar-ext-macstyle.png" without updating the source code.
# We include our own version of "toolbar-ext.png" here to silence the Qt warning message.
IF(APPLE)
    qt_add_resources(Gui "qt_missing_images" PREFIX "/qt-project.org/styles/macstyle/images/"
        BASE "resources/mainwin/qt/"
        FILES "resources/mainwin/qt/toolbar-ext.png" "resources/mainwin/qt/toolbar-ext@2x.png")
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
