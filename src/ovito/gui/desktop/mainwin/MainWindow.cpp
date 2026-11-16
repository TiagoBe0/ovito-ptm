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
#include <ovito/gui/desktop/app/GuiApplication.h>
#include <ovito/gui/desktop/app/GuiApplicationService.h>
#include <ovito/gui/desktop/widgets/animation/AnimationTimeSpinner.h>
#include <ovito/gui/desktop/widgets/animation/AnimationTimeSlider.h>
#include <ovito/gui/desktop/widgets/animation/AnimationTrackBar.h>
#include <ovito/gui/desktop/widgets/rendering/FrameBufferWindow.h>
#include <ovito/gui/desktop/widgets/display/CoordinateDisplayWidget.h>
#include <ovito/gui/desktop/widgets/general/StatusBar.h>
#include <ovito/gui/desktop/widgets/selection/SceneNodeSelectionBox.h>
#include <ovito/gui/desktop/dialogs/MessageDialog.h>
#include <ovito/gui/desktop/dialogs/ImportFileDialog.h>
#include <ovito/gui/desktop/dialogs/HistoryFileDialog.h>
#include <ovito/gui/desktop/actions/WidgetActionManager.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <ovito/gui/desktop/dataset/io/FileImporterEditor.h>
#include <ovito/gui/desktop/utilities/concurrent/ProgressDialog.h>
#include <ovito/gui/base/viewport/ViewportInputManager.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/app/undo/UndoStack.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/viewport/ViewportConfiguration.h>
#include <ovito/core/viewport/ViewportWindow.h>
#include <ovito/core/utilities/concurrent/TaskProgress.h>
#include "MainWindow.h"
#include "ViewportsPanel.h"
#include "TaskDisplayWidget.h"
#include "cmdpanel/CommandPanel.h"
#include "data_inspector/DataInspectorPanel.h"

#ifdef Q_OS_MAC
#include <ApplicationServices/ApplicationServices.h> // Needed in MainWindow::checkAccessibilityAccess()
#endif

namespace Ovito {

/******************************************************************************
* Initializes the main window.
******************************************************************************/
void MainWindow::initializeWindow()
{
    _baseWindowTitle = tr("%1 (Open Visualization Tool)").arg(Application::applicationName());
#if defined(OVITO_DEVELOPMENT_BUILD_DATE)
    _baseWindowTitle += tr(" - Development build %1 created on %2").arg(Application::applicationVersionString()).arg(QStringLiteral(OVITO_DEVELOPMENT_BUILD_DATE));
#endif
    setWindowTitle(_baseWindowTitle);

    // Set up the layout of docking widgets.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // Disable context menus in toolbars.
    setContextMenuPolicy(Qt::NoContextMenu);

    // Let GUI application services register their actions.
    for(const auto& service : StandaloneApplication::instance()->applicationServices()) {
        if(auto gui_service = dynamic_object_cast<GuiApplicationService>(service))
            gui_service->registerActions(ui());
    }

    // Create the main menu
    createMainMenu();

    // Create the main toolbar.
    createMainToolbar();

    // Create the viewports panel and the data inspector panel.
    QSplitter* dataInspectorSplitter = new QSplitter();
    dataInspectorSplitter->setOrientation(Qt::Vertical);
    dataInspectorSplitter->setChildrenCollapsible(false);
    dataInspectorSplitter->setHandleWidth(0);
    _viewportsPanel = new ViewportsPanel(*this);
    dataInspectorSplitter->addWidget(_viewportsPanel);
    _dataInspector = new DataInspectorPanel(ui());
    dataInspectorSplitter->addWidget(_dataInspector);
    dataInspectorSplitter->setStretchFactor(0, 1);
    dataInspectorSplitter->setStretchFactor(1, 0);
    // Start with the data inspector collapsed to its tab bar, so that the viewports get all the vertical space.
    // Otherwise the splitter sizes the panel to the preferred height of its largest tab page.
    // The user expands it with the button next to the tabs or by clicking a tab.
    dataInspectorSplitter->setSizes({ QWIDGETSIZE_MAX, 0 });
    setCentralWidget(dataInspectorSplitter);
    _viewportsPanel->setFocus(Qt::OtherFocusReason);

    // Create the animation panel below the viewports.
    QWidget* animationPanel = new QWidget();
    QVBoxLayout* animationPanelLayout = new QVBoxLayout();
    animationPanelLayout->setSpacing(0);
    animationPanelLayout->setContentsMargins(0, 1, 0, 0);
    animationPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    animationPanel->setLayout(animationPanelLayout);

    // Create animation time slider
    AnimationTimeSlider* timeSlider = new AnimationTimeSlider(ui());
    animationPanelLayout->addWidget(timeSlider);
    AnimationTrackBar* trackBar = new AnimationTrackBar(ui(), timeSlider);
    animationPanelLayout->addWidget(trackBar);

    // Create status bar.
    QWidget* statusBarContainer = new QWidget();
    statusBarContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _statusBarLayout = new QHBoxLayout(statusBarContainer);
    _statusBarLayout->setContentsMargins(2,0,0,0);
    _statusBarLayout->setSpacing(2);
    animationPanelLayout->addWidget(statusBarContainer, 1);

    _statusBar = new StatusBar();
    _statusBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _statusBarLayout->addWidget(_statusBar);
    _statusBar->overflowWidget()->setParent(animationPanel);

    TaskDisplayWidget* taskDisplay = new TaskDisplayWidget(this);
    taskDisplay->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    _statusBarLayout->addWidget(taskDisplay, 1);

    _coordinateDisplay = new CoordinateDisplayWidget(ui(), animationPanel);
    _coordinateDisplay->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    _statusBarLayout->addWidget(_coordinateDisplay);
    _statusBarLayout->addStrut(std::max(_coordinateDisplay->sizeHint().height(), taskDisplay->sizeHint().height()));

    // Create the animation control toolbar.
    QToolBar* animationControlBar1 = new QToolBar();
    animationControlBar1->addAction(actionManager()->getAction(ACTION_GOTO_START_OF_ANIMATION));
    animationControlBar1->addSeparator();
    animationControlBar1->addAction(actionManager()->getAction(ACTION_GOTO_PREVIOUS_FRAME));
    animationControlBar1->addAction(actionManager()->getAction(ACTION_TOGGLE_ANIMATION_PLAYBACK));
    animationControlBar1->addAction(actionManager()->getAction(ACTION_GOTO_NEXT_FRAME));
    animationControlBar1->addSeparator();
    animationControlBar1->addAction(actionManager()->getAction(ACTION_GOTO_END_OF_ANIMATION));
    QToolBar* animationControlBar2 = new QToolBar();
    animationControlBar2->addAction(actionManager()->getAction(ACTION_AUTO_KEY_MODE_TOGGLE));
    QWidget* animationTimeSpinnerContainer = new QWidget();
    QHBoxLayout* animationTimeSpinnerLayout = new QHBoxLayout(animationTimeSpinnerContainer);
    animationTimeSpinnerLayout->setContentsMargins(0,0,0,0);
    animationTimeSpinnerLayout->setSpacing(0);
    class TimeEditBox : public QLineEdit {
    public:
        virtual QSize sizeHint() const { return minimumSizeHint(); }
    };
    QLineEdit* timeEditBox = new TimeEditBox();
    timeEditBox->setToolTip(tr("Current Animation Time"));
    AnimationTimeSpinner* currentTimeSpinner = new AnimationTimeSpinner(ui());
    currentTimeSpinner->setTextBox(timeEditBox);
    animationTimeSpinnerLayout->addWidget(timeEditBox, 1);
    animationTimeSpinnerLayout->addWidget(currentTimeSpinner);
    animationControlBar2->addWidget(animationTimeSpinnerContainer);
    animationControlBar2->addAction(actionManager()->getAction(ACTION_ANIMATION_SETTINGS));
    animationControlBar2->addWidget(new QWidget());

    QWidget* animationControlPanel = new QWidget();
    QVBoxLayout* animationControlPanelLayout = new QVBoxLayout(animationControlPanel);
    animationControlPanelLayout->setSpacing(0);
    animationControlPanelLayout->setContentsMargins(0, 1, 0, 0);
    animationControlPanelLayout->addWidget(animationControlBar1);
    animationControlPanelLayout->addWidget(animationControlBar2);
    animationControlPanelLayout->addStretch(1);
    animationControlPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    animationTimeSpinnerContainer->setStyle(QApplication::style());

    // Create the viewport control toolbar.
    QToolBar* viewportControlBar1 = new QToolBar();
    viewportControlBar1->addAction(actionManager()->getAction(ACTION_VIEWPORT_ZOOM));
    viewportControlBar1->addAction(actionManager()->getAction(ACTION_VIEWPORT_PAN));
    viewportControlBar1->addAction(actionManager()->getAction(ACTION_VIEWPORT_ORBIT));
    QToolBar* viewportControlBar2 = new QToolBar();
    viewportControlBar2->addAction(actionManager()->getAction(ACTION_VIEWPORT_ZOOM_SCENE_EXTENTS));
    viewportControlBar2->addAction(actionManager()->getAction(ACTION_VIEWPORT_FOV));
    viewportControlBar2->addAction(actionManager()->getAction(ACTION_VIEWPORT_MAXIMIZE));
    QWidget* viewportControlPanel = new QWidget();
    QVBoxLayout* viewportControlPanelLayout = new QVBoxLayout(viewportControlPanel);
    viewportControlPanelLayout->setSpacing(0);
    viewportControlPanelLayout->setContentsMargins(0, 1, 0, 0);
    viewportControlPanelLayout->addWidget(viewportControlBar1);
    QHBoxLayout* sublayout = new QHBoxLayout();
    sublayout->addStretch(1);
    sublayout->addWidget(viewportControlBar2);
    viewportControlPanelLayout->addLayout(sublayout);
    viewportControlPanelLayout->addStretch(1);
    viewportControlPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // Create the command panel.
    _commandPanel = new CommandPanel(ui(), this);

    // Create the bottom docking widget.
    QWidget* bottomDockWidget = new QWidget();
    bottomDockWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QGridLayout* bottomDockLayout = new QGridLayout(bottomDockWidget);
    bottomDockLayout->setContentsMargins(0,0,0,0);
    bottomDockLayout->setSpacing(0);
    QFrame* separatorLine = new QFrame();
    QPalette pal = separatorLine->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Mid));
    separatorLine->setFrameShape(QFrame::HLine);
    separatorLine->setFrameShadow(QFrame::Plain);
    separatorLine->setPalette(pal);
    bottomDockLayout->addWidget(separatorLine, 1, 0, 1, 5);
    bottomDockLayout->addWidget(animationPanel, 2, 0);
    separatorLine = new QFrame();
    separatorLine->setFrameShape(QFrame::VLine);
    separatorLine->setFrameShadow(QFrame::Plain);
    separatorLine->setPalette(pal);
    bottomDockLayout->addWidget(separatorLine, 2, 1);
    bottomDockLayout->addWidget(animationControlPanel, 2, 2);
    separatorLine = new QFrame();
    separatorLine->setFrameShape(QFrame::VLine);
    separatorLine->setFrameShadow(QFrame::Plain);
    separatorLine->setPalette(pal);
    bottomDockLayout->addWidget(separatorLine, 2, 3);
    bottomDockLayout->addWidget(viewportControlPanel, 2, 4);

    // Create docking widgets.
    createDockPanel(tr("Bottom panel"), "BottomPanel", Qt::BottomDockWidgetArea, Qt::BottomDockWidgetArea, bottomDockWidget);
    createDockPanel(tr("Command Panel"), "CommandPanel", Qt::RightDockWidgetArea, Qt::DockWidgetAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea), _commandPanel);

    // Update window title when document path changes.
    connect(&datasetContainer(), &DataSetContainer::filePathChanged, this, [this](const QString& filePath) { setWindowFilePath(filePath); });
    connect(undoStack(), &UndoStack::cleanChanged, this, [this](bool isClean) { setWindowModified(!isClean); });

    // Accept files via drag & drop.
    setAcceptDrops(true);
}

/******************************************************************************
* Destructor.
******************************************************************************/
MainWindow::~MainWindow()
{
    OVITO_ASSERT(ui().mainWindow() == this);
    OVITO_ASSERT(dataset() == nullptr);

    // Detach widget from UI object.
    ui()._mainWindow = nullptr;
    ui().setViewportInputManager(nullptr);
    ui().setActionManager(nullptr);
    ui().setUndoStack(nullptr);
}

/******************************************************************************
* Creates a dock panel.
******************************************************************************/
QDockWidget* MainWindow::createDockPanel(const QString& caption, const QString& objectName, Qt::DockWidgetArea dockArea, Qt::DockWidgetAreas allowedAreas, QWidget* contents)
{
    QDockWidget* dockWidget = new QDockWidget(caption, this);
    dockWidget->setObjectName(objectName);
    dockWidget->setAllowedAreas(allowedAreas);
    dockWidget->setFeatures(QDockWidget::DockWidgetClosable);
    dockWidget->setWidget(contents);
    dockWidget->setTitleBarWidget(new QWidget());
    addDockWidget(dockArea, dockWidget);
    return dockWidget;
}

/******************************************************************************
* Restores a previously saved maximized/non-maximized state and shows the window.
******************************************************************************/
void MainWindow::restoreMainWindowGeometry()
{
    QSettings settings;
    settings.beginGroup("app/mainwindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    show();
}

/******************************************************************************
* Saves the maximized/non-maximized state of the window in the settings store.
******************************************************************************/
void MainWindow::saveMainWindowGeometry()
{
    QSettings settings;
    settings.beginGroup("app/mainwindow");
    settings.setValue("geometry", saveGeometry());
}

/******************************************************************************
* Loads the layout of the docked widgets from the settings store.
******************************************************************************/
void MainWindow::restoreLayout()
{
    QSettings settings;
    settings.beginGroup("app/mainwindow");
    QVariant state = settings.value("state");
    if(state.canConvert<QByteArray>())
        restoreState(state.toByteArray());
    commandPanel()->restoreLayout();
}

/******************************************************************************
* Saves the layout of the docked widgets to the settings store.
******************************************************************************/
void MainWindow::saveLayout()
{
    QSettings settings;
    settings.beginGroup("app/mainwindow");
    settings.setValue("state", saveState());
    commandPanel()->saveLayout();
}

/******************************************************************************
* Creates the main menu.
******************************************************************************/
void MainWindow::createMainMenu()
{
    QMenuBar* menuBar = this->menuBar();

    // Build the file menu.
    QMenu* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->setObjectName(QStringLiteral("FileMenu"));
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_IMPORT));
#ifdef OVITO_SSH_CLIENT
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_REMOTE_IMPORT));
#endif
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_EXPORT));
    fileMenu->addSeparator();
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_OPEN));
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_SAVE));
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_SAVEAS));
    fileMenu->addSeparator();
    if(QAction* runScriptFileAction = actionManager()->findAction(ACTION_SCRIPTING_RUN_FILE))
        fileMenu->addAction(runScriptFileAction);
    if(QAction* generateScriptFileAction = actionManager()->findAction(ACTION_SCRIPTING_GENERATE_CODE))
        fileMenu->addAction(generateScriptFileAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actionManager()->getAction(ACTION_FILE_NEW_WINDOW));
    fileMenu->addSeparator();
    fileMenu->addAction(actionManager()->getAction(ACTION_QUIT));

    // Build the edit menu.
    QMenu* editMenu = menuBar->addMenu(tr("&Edit"));
    editMenu->setObjectName(QStringLiteral("EditMenu"));
    editMenu->addAction(actionManager()->getAction(ACTION_EDIT_UNDO));
    editMenu->addAction(actionManager()->getAction(ACTION_EDIT_REDO));
#ifdef OVITO_DEBUG
    editMenu->addAction(actionManager()->getAction(ACTION_EDIT_CLEAR_UNDO_STACK));
#endif
    editMenu->addSeparator();
    editMenu->addAction(actionManager()->getAction(ACTION_SETTINGS_DIALOG));
    if(QAction* extensionGalleryAction = actionManager()->findAction(ACTION_SCRIPTING_EXTENSIONS_GALLERY))
        editMenu->addAction(extensionGalleryAction);

    // Build the help menu.
    QMenu* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("HelpMenu"));
    helpMenu->addAction(actionManager()->getAction(ACTION_HELP_SHOW_ONLINE_HELP));
    helpMenu->addAction(actionManager()->getAction(ACTION_HELP_SHOW_SCRIPTING_HELP));
    helpMenu->addAction(actionManager()->getAction(ACTION_HELP_REQUEST_FEATURE));
    helpMenu->addSeparator();
    helpMenu->addAction(actionManager()->getAction(ACTION_HELP_GRAPHICS_SYSINFO));
#ifndef  Q_OS_MACOS
    helpMenu->addSeparator();
#endif
    helpMenu->addAction(actionManager()->getAction(ACTION_HELP_ABOUT));
}

/******************************************************************************
* Creates the main toolbar.
******************************************************************************/
void MainWindow::createMainToolbar()
{
    _mainToolbar = addToolBar(tr("Main Toolbar"));
    _mainToolbar->setObjectName("MainToolbar");
    _mainToolbar->setMovable(false);

    _mainToolbar->addAction(actionManager()->getAction(ACTION_FILE_IMPORT));
    _mainToolbar->addAction(actionManager()->getAction(ACTION_FILE_REMOTE_IMPORT));

    _mainToolbar->addSeparator();

    _mainToolbar->addAction(actionManager()->getAction(ACTION_FILE_OPEN));
    _mainToolbar->addAction(actionManager()->getAction(ACTION_FILE_SAVE));

    _mainToolbar->addSeparator();

    _mainToolbar->addAction(actionManager()->getAction(ACTION_EDIT_UNDO));
    _mainToolbar->addAction(actionManager()->getAction(ACTION_EDIT_REDO));

    _mainToolbar->addSeparator();

    _mainToolbar->addAction(actionManager()->getAction(ACTION_SELECTION_MODE));
    _mainToolbar->addAction(actionManager()->getAction(ACTION_XFORM_MOVE_MODE));
    _mainToolbar->addAction(actionManager()->getAction(ACTION_XFORM_ROTATE_MODE));

    _mainToolbar->addSeparator();

    _mainToolbar->addAction(actionManager()->getAction(ACTION_RENDER_ACTIVE_VIEWPORT));

    _mainToolbar->addSeparator();

    _mainToolbar->addAction(actionManager()->getAction(ACTION_COMMAND_QUICKSEARCH));

    QLabel* pipelinesLabel = new QLabel(tr("  Pipelines: "));
    pipelinesLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pipelinesLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _mainToolbar->addWidget(pipelinesLabel);
    _mainToolbar->addWidget(new SceneNodeSelectionBox(*this));
}

/******************************************************************************
* Is called when the window receives an event.
******************************************************************************/
bool MainWindow::event(QEvent* event)
{
    if(event->type() == QEvent::StatusTip) {
        ui().showStatusBarMessage(static_cast<QStatusTipEvent*>(event)->tip());
        return true;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    else if(event->type() == QEvent::ThemeChange) {
        // Switch icon theme to match with the current UI theme.
        QIcon::setThemeName(GuiApplication::instance()->usingDarkTheme() ? QStringLiteral("ovito-dark") : QStringLiteral("ovito-light"));
    }
#endif
    return QMainWindow::event(event);
}

/******************************************************************************
* Handles global key input.
******************************************************************************/
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if(!static_cast<ViewportsPanel*>(_viewportsPanel)->onKeyShortcut(event))
        QMainWindow::keyPressEvent(event);
}

/******************************************************************************
* Is called by the system when the user tries to close the window.
******************************************************************************/
void MainWindow::closeEvent(QCloseEvent* event)
{
    bool successfulShutdown = handleExceptions<true>([&] {
        // Let the user save changes made to the current dataset.
        if(isVisible())
            ui().askForSaveChanges();

        // Don't allow user to interact with the window anymore.
        setEnabled(false);

#ifdef OVITO_DEBUG
        // Check that the shutdown task does not get canceled (just for correctness).
        detail::FunctionTaskCallback taskCallback(this_task::get(), [](int state) noexcept {
            OVITO_ASSERT(!(state & Task::Canceled));
        });
#endif

        // Stop all running tasks in this window and release program session objects.
        if(!ui().shutdown()) {
            setEnabled(true); // Reenable window if shutdown has been canceled.
            this_task::cancelAndThrow();
        }
    });

    // Swallow close event if the user chose to cancel the shutdown.
    if(successfulShutdown)
        event->accept();
    else
        event->ignore();
}

/******************************************************************************
 * Called by the system when the window is moved. Redraws the viewports when the screen is changed
 * Avoids some small visual glitches
 ******************************************************************************/
void MainWindow::moveEvent(QMoveEvent* event)
{
    // Get the current screen the window is located on.
    QScreen* newScreen = screen();

    // If the screen changed - redraw the viewports to account for possible changes in DPI scaling.
    if(newScreen && newScreen != _currentScreen) {
        // Remember the current screen.
        _currentScreen = newScreen;
        ui().updateViewports();
    }

    QMainWindow::moveEvent(event);
}

/******************************************************************************
* Returns the page of the command panel that is currently visible.
******************************************************************************/
MainWindow::CommandPanelPage MainWindow::currentCommandPanelPage() const
{
    return _commandPanel->currentPage();
}

/******************************************************************************
* Sets the page of the command panel that is currently visible.
******************************************************************************/
void MainWindow::setCurrentCommandPanelPage(CommandPanelPage page)
{
    _commandPanel->setCurrentPage(page);
}

/******************************************************************************
* Sets the file path associated with this window and updates the window's title.
******************************************************************************/
void MainWindow::setWindowFilePath(const QString& filePath)
{
    if(filePath.isEmpty())
        setWindowTitle(_baseWindowTitle + QStringLiteral(" [*]"));
    else
        setWindowTitle(_baseWindowTitle + QStringLiteral(" - %1[*]").arg(QFileInfo(filePath).fileName()));
    QMainWindow::setWindowFilePath(filePath);
}

/******************************************************************************
* Called by the system when a drag is in progress and the mouse enters this
* window.
******************************************************************************/
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

/******************************************************************************
* Called by the system when the drag is dropped on this window.
******************************************************************************/
void MainWindow::dropEvent(QDropEvent* event)
{
    event->acceptProposedAction();
    std::vector<QUrl> importUrls;
    bool success = handleExceptions([&] {
        for(const QUrl& url : event->mimeData()->urls()) {
            if(url.fileName().endsWith(".ovito", Qt::CaseInsensitive)) {
                if(url.isLocalFile()) {
                    ui().askForSaveChanges();
                    OORef<DataSet> dataset = DataSet::createFromFile(url.toLocalFile());
                    if(ui().checkLoadedDataset(dataset))
                        datasetContainer().setCurrentSet(std::move(dataset));
                    importUrls.clear();
                    return;
                }
            }
            else {
                importUrls.push_back(url);
            }
        }
    });
    if(success && !importUrls.empty()) {
        performTransaction(tr("Import data"), [&] {
            ui().importFiles(std::move(importUrls));
        });
    }
}

/******************************************************************************
* Opens the data inspector panel and shows the data object generated by the
* given data pipeline node.
******************************************************************************/
bool MainWindow::openDataInspector(PipelineNode* createdByNode, const QString& objectNameHint, const QVariant& modeHint)
{
    if(_dataInspector->selectDataObject(createdByNode, objectNameHint, modeHint)) {
        _dataInspector->open();
        return true;
    }
    return false;
}

/******************************************************************************
* Creates a frame buffer of the requested size and displays it as a window in the user interface.
******************************************************************************/
std::shared_ptr<FrameBuffer> MainWindow::createAndShowFrameBuffer(int width, int height)
{
    // Create the frame buffer window.
    if(!_frameBufferWindow) {
        _frameBufferWindow = new FrameBufferWindow(*this, this);
        _frameBufferWindow->setWindowTitle(tr("Render output"));
    }

    std::shared_ptr<FrameBuffer> fb = _frameBufferWindow->createFrameBuffer(width, height);
    _frameBufferWindow->showAndActivateWindow();

    return fb;
}

/******************************************************************************
* Shows a progress bar or a similar UI to indicate the current rendering progress
* and let the user cancel the operation if necessary.
******************************************************************************/
void MainWindow::showRenderingProgress(const std::shared_ptr<FrameBuffer>& frameBuffer, SharedFuture<void> renderingFuture)
{
    // Assuming that the frame buffer window is already showing the same frame buffer provided by the caller.
    OVITO_ASSERT(_frameBufferWindow && _frameBufferWindow->frameBuffer() == frameBuffer);
    if(!_frameBufferWindow)
        return;

    _frameBufferWindow->showRenderingProgress(std::move(renderingFuture));
}

/******************************************************************************
* Handler function for exceptions.
******************************************************************************/
void MainWindow::reportError(const Exception& ex, bool blocking)
{
    if(!blocking) {
        // Deferred display of the error message after execution returns to the main event loop.
        if(_errorList.empty())
            QMetaObject::invokeMethod(this, "showErrorMessages", Qt::QueuedConnection);

        // Queue error messages.
        _errorList.push_back(ex);
    }
    else {
        _errorList.push_back(ex);
        showErrorMessages();
    }
}

/******************************************************************************
* Displays an error message to the user that is associated with a particular child window or dialog.
******************************************************************************/
void MainWindow::reportError(const Exception& exception, QWidget* window)
{
    // If the exception is associated with additional message strings,
    // show them in the Details section of the message box dialog.
    QString detailText;
    if(exception.messages().size() > 1) {
        for(int i = 1; i < exception.messages().size(); i++)
            detailText += exception.messages()[i] + QStringLiteral("\n");
    }
    // Also show traceback information.
    if(!exception.traceback().isEmpty()) {
        if(!detailText.isEmpty())
            detailText += QChar('\n');
        detailText += exception.traceback();
    }

    // Display the message box to the user.
    showMessageBoxImpl(
        window,
        UserInterface::MessageBoxIcon::CriticalIcon,
        tr("Error - %1").arg(Application::applicationName()),
        exception.message(),
        UserInterface::MessageBoxButton::Ok,
        UserInterface::MessageBoxButton::NoButton,
        detailText);
}

/******************************************************************************
* Displays an error message box. This slot is called by reportError().
******************************************************************************/
void MainWindow::showErrorMessages()
{
    // For detection destruction of the main window.
    QPointer<MainWindow> self = this;

    while(!_errorList.empty() && !self.isNull()) {
        // Show next exception from queue.
        reportError(_errorList.front(), this);
        _errorList.pop_front();
    }
}

/******************************************************************************
* Displays a modal message box to the user. Blocks until the user closes the message box.
* This method wraps the QMessageBox class of the Qt library.
******************************************************************************/
UserInterface::MessageBoxButton MainWindow::showMessageBoxImpl(QWidget* window, UserInterface::MessageBoxIcon icon, const QString& title, const QString& text, int buttons, UserInterface::MessageBoxButton defaultButton, const QString& detailedText)
{
    OVITO_ASSERT(QThread::currentThread() == this->thread());

    // Verify that our enum values match the corresponding values of the QMessageBox class, which allows us to perform a direct cast.
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Ok) == QMessageBox::Ok);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Cancel) == QMessageBox::Cancel);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Discard) == QMessageBox::Discard);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Yes) == QMessageBox::Yes);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::No) == QMessageBox::No);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Apply) == QMessageBox::Apply);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Abort) == QMessageBox::Abort);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Retry) == QMessageBox::Retry);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::StandardButtons>(UserInterface::MessageBoxButton::Ignore) == QMessageBox::Ignore);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::Icon>(UserInterface::MessageBoxIcon::NoIcon) == QMessageBox::NoIcon);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::Icon>(UserInterface::MessageBoxIcon::InformationIcon) == QMessageBox::Information);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::Icon>(UserInterface::MessageBoxIcon::WarningIcon) == QMessageBox::Warning);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::Icon>(UserInterface::MessageBoxIcon::CriticalIcon) == QMessageBox::Critical);
    OVITO_STATIC_ASSERT(static_cast<QMessageBox::Icon>(UserInterface::MessageBoxIcon::QuestionIcon) == QMessageBox::Question);

    // Prepare a message box dialog.
    QPointer<MessageDialog> msgbox = new MessageDialog();
    msgbox->setWindowTitle(title);
    msgbox->setStandardButtons(static_cast<QMessageBox::StandardButtons>(buttons));
    msgbox->setDefaultButton(static_cast<QMessageBox::StandardButton>(defaultButton));
    msgbox->setText(text);
    msgbox->setDetailedText(detailedText);
    msgbox->setIcon(static_cast<QMessageBox::Icon>(icon));
    msgbox->setTextInteractionFlags(Qt::TextBrowserInteraction);

    // Stop animation playback when showing a message box.
    QAction* playbackAction = actionManager()->getAction(ACTION_TOGGLE_ANIMATION_PLAYBACK);
    if(playbackAction && playbackAction->isChecked())
        playbackAction->trigger();

    // Honor the parent window in case it was explicitly specified by the caller.
    if(window && window->isVisible()) {
        // If there currently is floating window being shown (e.g. the FrameBufferWindow),
        // make the error message dialog a child of this floating window to show it in front.
        for(QMainWindow* floatingChildWindow : window->findChildren<QMainWindow*>(Qt::FindDirectChildrenOnly)) {
            if(floatingChildWindow->isVisible() && floatingChildWindow->isActiveWindow()) {
                window = floatingChildWindow;
                break;
            }
        }

        // If there currently is a modal dialog box being shown,
        // make the error message dialog a child of this dialog to prevent a UI dead-lock.
        for(QDialog* dialog : window->findChildren<QDialog*>(Qt::FindChildrenRecursively)) {
            if(dialog->isVisible() && dialog->isModal() && !qobject_cast<ProgressDialog*>(dialog)) {
                window = dialog;
                dialog->show();
                break;
            }
        }
        msgbox->setParent(window);
        msgbox->setWindowModality(Qt::WindowModal);
    }

    // Show message box.
    int result = msgbox->exec();

    // Discard message box.
    delete msgbox.data();

    return static_cast<UserInterface::MessageBoxButton>(result);
}

/******************************************************************************
* Opens another main window (in addition to the existing windows) and
* optionally loads a file in the new window.
******************************************************************************/
void MainWindow::openNewWindow(const QStringList& arguments)
{
    // This is a workaround for a bug in Qt 6.4 on macOS platform. The displayed menu bar does not automatically follow
    // the main window that is currently active. That's why we launch another independent instance of the app instead.
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    // Get the path to the ovito executable.
    QString execPath = Application::instance()->applicationFilePath();

    // If we are currently running ovitos in graphical mode, start ovito instead.
    if(execPath.endsWith("ovitos"))
        execPath.chop(1);

    // Start another instance of the program.
    if(!QProcess::startDetached(execPath, arguments))
        throw Exception(tr("Failed to start another instance of the program. Executable path: %1").arg(execPath));
#else
    OORef<MainWindowUI> mainWinUI = OORef<MainWindowUI>::create();
    mainWinUI->mainWindow()->show();
    mainWinUI->mainWindow()->restoreMainWindowGeometry();
    mainWinUI->mainWindow()->restoreLayout();
    if(!mainWinUI->handleExceptions([&]() {
        GuiApplication::initializeUserInterface(*mainWinUI, arguments);
    })) {
        mainWinUI->shutdown();
    }
#endif
}

/******************************************************************************
* Checks if the current application has accessability access. This is required on macOS to move the cursor using QCursor::setPos().
* This method will prompt the user the first time it is called (for each ovito version). Returns true on non macOS.
******************************************************************************/
bool MainWindow::checkAccessibilityAccess(QWidget* parent) const
{
#ifdef Q_OS_MAC
    QSettings settings;
    settings.beginGroup("app/mainwindow");

    // Get program version for which permission was already requested.
    const int major = settings.value("AccessibilityDialogMajor", 0).toInt();
    const int minor = settings.value("AccessibilityDialogMinor", 0).toInt();
    const int revision = settings.value("AccessibilityDialogRevision", 0).toInt();

    // Ovito version changed from the last time we requested permission?
    if(QT_VERSION_CHECK(Application::applicationVersionMajor(), Application::applicationVersionMinor(),
                        Application::applicationVersionRevision()) != QT_VERSION_CHECK(major, minor, revision) &&
       !AXIsProcessTrusted()) {
        // Present the user with a info dialog explaining the accessibility requirement
        MessageDialog msgBox(parent);
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.setText(tr("%1 needs macOS accessibility access").arg(Application::applicationName()));
        msgBox.setInformativeText(
            tr("MacOS accessibility permission is required for %1 to enable infinite scrolling while dragging the spinner widget. "
               "The permission is needed by the application to reposition the mouse cursor when it leaves the screen.\n\n"
               "Click 'Help' for more information. Click 'Next' to proceed to the macOS permission dialog.").arg(Application::applicationName()));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Help | QMessageBox::Cancel | QMessageBox::Ignore);
        msgBox.button(QMessageBox::Ok)->setText(tr("Next"));
        msgBox.button(QMessageBox::Ignore)->setText(tr("Don't ask again"));
        const int msgBoxRet = msgBox.exec();
        if(msgBoxRet == QMessageBox::Help) {
            actionManager()->openHelpTopic("manual:usage.spinner_widgets");
            return false;
        }
        else if(msgBoxRet == QMessageBox::Cancel) {
            return false;
        }

        // Update stored value
        settings.setValue("AccessibilityDialogMajor", Application::applicationVersionMajor());
        settings.setValue("AccessibilityDialogMinor", Application::applicationVersionMinor());
        settings.setValue("AccessibilityDialogRevision", Application::applicationVersionRevision());

        if(msgBoxRet == QMessageBox::Ignore) {
            return false;
        }

        const CFStringRef keys[] = {kAXTrustedCheckOptionPrompt};
        const CFTypeRef values[] = {kCFBooleanTrue};
        const CFDictionaryRef options =
            CFDictionaryCreate(nullptr, (const void**)&keys, (const void**)&values, sizeof(keys) / sizeof(keys[0]),
                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        // Ask for permission
        AXIsProcessTrustedWithOptions(options);
        CFRelease(options);
    }
    settings.endGroup();

    // Return access state
    return static_cast<bool>(AXIsProcessTrusted());
#else
    return true;
#endif
}

}   // End of namespace
