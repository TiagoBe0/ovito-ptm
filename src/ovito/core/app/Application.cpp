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

#include <ovito/core/Core.h>
#include <ovito/core/utilities/io/FileManager.h>
#include "Application.h"

#include <QLoggingCategory>
#include <QSurfaceFormat>
#ifndef Q_OS_WASM
    #include <QNetworkProxyFactory>
#endif
#ifdef Q_OS_MACOS
    #include <CoreFoundation/CFBundle.h> // Needed for implementation of ovitoAppFileName()
#endif
#ifdef Q_OS_WIN
    #ifndef NOMINMAX
        #define NOMINMAX // Prevent <Windows.h> from defining min() and max() macros
    #endif
    #include <Windows.h> // Needed for implementation of ovitoAppFileName()
#endif

// Called from Application::initialize() to register the embedded Qt resource files
// when running a statically linked executable. The Qt documentation says this
// needs to be placed outside of any C++ namespace.
static void registerQtResources()
{
#ifdef OVITO_BUILD_MONOLITHIC
    Q_INIT_RESOURCE(core);
#endif
}

namespace Ovito {

/// Determines the path of the OVITO main executable.
static QString ovitoAppFileName()
{
#if defined(Q_OS_WIN)
    QVarLengthArray<wchar_t, MAX_PATH + 1> space;
    DWORD v;
    size_t size = 1;
    do {
        size += MAX_PATH;
        space.resize(int(size));
        v = ::GetModuleFileName(NULL, space.data(), DWORD(space.size()));
    }
    while(v >= size);
    return QString::fromWCharArray(space.data(), v);
#elif defined(Q_OS_MACOS)
    static QString appFileName;
    if (appFileName.isEmpty()) {
        CFBundleRef mainBundle = CFBundleGetMainBundle();
        CFURLRef executableURL = CFBundleCopyExecutableURL(mainBundle);
        if(executableURL) {
            CFStringRef cfStr = CFURLCopyFileSystemPath(executableURL, kCFURLPOSIXPathStyle);
            appFileName = QString::fromCFString(cfStr);
            CFRelease(cfStr);
            CFRelease(executableURL);
        }
    }
    return appFileName;
#elif defined(Q_OS_LINUX)
    const char *path = "/proc/self/exe";
    QByteArray buf(256, Qt::Uninitialized);
    ssize_t len = ::readlink(path, buf.data(), buf.size());
    while(len == buf.size()) {
        // readlink(2) will fill our buffer and not necessarily terminate with NUL;
        if(buf.size() >= 4096)
            return {};

        // double the size and try again
        buf.resize(buf.size() * 2);
        len = ::readlink(path, buf.data(), buf.size());
    }
    if(len == -1)
        return {};

    buf.resize(len);
    return QFile::decodeName(buf);
#else
    return {};
#endif
}

/// The one and only instance of the class.
Application* Application::_instance = nullptr;

/// Indicates what kind of application is running (GUI, console, Python module, etc.)
Application::RunMode Application::_runMode = Application::AppMode;

/// Stores a pointer to the original Qt message handler function, which has been replaced with our own handler.
QtMessageHandler Application::defaultQtMessageHandler = nullptr;

/******************************************************************************
* Handler method for Qt log messages.
* This can be used to set a debugger breakpoint for the OVITO_ASSERT macros.
******************************************************************************/
void Application::qtMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Forward message to default handler.
    if(defaultQtMessageHandler) defaultQtMessageHandler(type, context, msg);
    else std::cerr << qPrintable(qFormatLogMessage(type, context, msg)) << std::endl;
}

/******************************************************************************
* Handler method for Qt log messages that should be redirected to a file.
******************************************************************************/
static void qtMessageLogFile(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Format the message string to be written to the log file.
    QString formattedMsg = qFormatLogMessage(type, context, msg);

    // The log file object.
    static QFile logFile(QDir::fromNativeSeparators(qEnvironmentVariable("OVITO_LOG_FILE", QStringLiteral("ovito.log"))));

    // Synchronize concurrent access to the log file.
    static QMutex ioMutex;
    QMutexLocker mutexLocker(&ioMutex);

    // Open the log file for writing if it is not open yet.
    if(!logFile.isOpen()) {
        if(!logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::cerr << "WARNING: Failed to open log file '" << qPrintable(logFile.fileName()) << "' for writing: ";
            std::cerr << qPrintable(logFile.errorString()) << std::endl;
            Application::qtMessageOutput(type, context, msg);
            return;
        }
    }

    // Write to the text stream.
    static QTextStream stream(&logFile);
    stream << formattedMsg << '\n';
    stream.flush();
}

/******************************************************************************
* Constructor.
******************************************************************************/
Application::Application()
{
    // Set global application pointer.
    OVITO_ASSERT(_instance == nullptr); // Only allowed to create one Application class instance.
    _instance = this;
}

/******************************************************************************
* Destructor.
******************************************************************************/
Application::~Application()
{
    OVITO_ASSERT(taskManager().isShuttingDown()); // Make sure this UserInterface was properly shut down before being deleted.
    _instance = nullptr;
}

/******************************************************************************
* Returns the major version number of the application.
******************************************************************************/
int Application::applicationVersionMajor()
{
    // This compile-time constant is defined by the CMake build script.
    return OVITO_VERSION_MAJOR;
}

/******************************************************************************
* Returns the minor version number of the application.
******************************************************************************/
int Application::applicationVersionMinor()
{
    // This compile-time constant is defined by the CMake build script.
    return OVITO_VERSION_MINOR;
}

/******************************************************************************
* Returns the revision version number of the application.
******************************************************************************/
int Application::applicationVersionRevision()
{
    // This compile-time constant is defined by the CMake build script.
    return OVITO_VERSION_REVISION;
}

/******************************************************************************
* Returns the complete version string of the application release.
******************************************************************************/
QString Application::applicationVersionString()
{
    // This compile-time constant is defined by the CMake build script.
    return QStringLiteral(OVITO_VERSION_STRING);
}

/******************************************************************************
* Returns the human-readable name of the application.
******************************************************************************/
QString Application::applicationName()
{
    // This compile-time constant is defined by the CMake build script.
    return QStringLiteral(OVITO_APPLICATION_NAME);
}

/******************************************************************************
* This is called on program startup.
******************************************************************************/
bool Application::initialize(int& argc, char** argv)
{
    // Store command line arguments for later use.
    _argc = &argc;
    _argv = argv;

    // Install custom Qt error message handler to catch fatal errors in debug mode
    // or redirect log output to file instead of the console if requested by the user.
    if(qEnvironmentVariableIsSet("OVITO_LOG_FILE")) {
        // Install a message handler that writes log output to a text file.
        defaultQtMessageHandler = qInstallMessageHandler(qtMessageLogFile);
        // QDebugStateSaver saver(qInfo());
        qInfo().noquote() << "#" << applicationName() << applicationVersionString() << "started on" << QDateTime::currentDateTime().toString();
    }
    else {
        // Install message handler that forwards to the default Qt handler or writes to stderr stream.
        defaultQtMessageHandler = qInstallMessageHandler(qtMessageOutput);
    }

    // Force "C" locale, which will be used to parse numbers, e.g. from simulation data files.
    // QCoreApplication requires a UTF-8 locale and will print a warning to the terminal during initialization otherwise.
    // Note: On macOS, C.UTF-8 is not available, so we use en_US.UTF-8 instead.
#ifndef Q_OS_WIN
#if defined(Q_OS_MACOS)
    qputenv("LC_ALL", "en_US.UTF-8");
#else
    qputenv("LC_ALL", "C.UTF-8");
#endif
    // Activate the locale selected by the LC_ALL environment variable.
    std::setlocale(LC_ALL, "");
#else
    // Reactivate the "C" locale (just in case someone else has changed it).
    std::setlocale(LC_ALL, "C");
#endif
    QLocale::setDefault(QLocale::c());

    // Double-check if a "C"-like locale is active. Otherwise, parsing of floating-point numbers from text files
    // using the sscanf() function may fail.
    FloatType d;
    if(sscanf("1234.56", FLOATTYPE_SCANF_STRING, &d) != 1 || d != FloatType(1234.56)) {
        qWarning() << "Failed to set the C locale for parsing floating-point numbers with decimal points. "
                "Please check your system's locale settings or set the LC_ALL environment variable to C.UTF-8. "
                "Otherwise, OVITO is not able to read & write simulation data files correctly.";
    }

    // Suppress console messages "qt.network.ssl: QSslSocket: cannot resolve ..."
    QLoggingCategory::setFilterRules(QStringLiteral("qt.network.ssl.warning=false"));

    // Register our floating-point data type with the Qt type system.
    qRegisterMetaType<FloatType>("FloatType");

    // Register generic object reference type with the Qt type system.
    qRegisterMetaType<OORef<OvitoObject>>("OORef<OvitoObject>");

    // Register Qt conversion operators for custom types.
    QMetaType::registerConverter<QColor, Color>();
    QMetaType::registerConverter<Color, QColor>();
    QMetaType::registerConverter<QColor, ColorA>();
    QMetaType::registerConverter<ColorA, QColor>();
    QMetaType::registerConverter<Vector2, QVector2D>(&Vector2::operator QVector2D);
    QMetaType::registerConverter<QVector2D, Vector2>();
    QMetaType::registerConverter<Vector3, QVector3D>(&Vector3::operator QVector3D);
    QMetaType::registerConverter<QVector3D, Vector3>();
    QMetaType::registerConverter<Color, Vector3>();
    QMetaType::registerConverter<Color, QString>(&Color::toString);
    QMetaType::registerConverter<Vector3, Color>();
    QMetaType::registerConverter<QVector3D, Color>();
    QMetaType::registerConverter<Vector3, QString>(&Vector3::toString);
    QMetaType::registerConverter<Color, QVector3D>(&Color::operator QVector3D);
    QMetaType::registerConverter<AffineTransformation, QMatrix4x4>();
    QMetaType::registerConverter<QMatrix4x4, AffineTransformation>();

    // Note: Setting the AA_ShareOpenGLContexts and AA_UseDesktopOpenGL flags is only valid BEFORE
    // the global Qt application object is created. When running in a Python interpreter environment, THE Qt
    // application object may have already been created externally before the ovito module is imported.
    if(QCoreApplication::startingUp()) {
        // Enable OpenGL context sharing globally.
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
        // Use desktop OpenGL (avoid ANGLE on Windows), unless the user explicitly selected an implementation
        // through Qt's QT_OPENGL variable, e.g. QT_OPENGL=software on a Windows machine without a GPU driver.
        if(!qEnvironmentVariableIsSet("QT_OPENGL"))
            QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    }

    // Specify default OpenGL surface format.
    QSurfaceFormat format;
#ifndef Q_OS_WASM
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(1);
#ifdef Q_OS_MACOS
    // macOS only supports core profile contexts.
    format.setMajorVersion(4);
    format.setMinorVersion(3);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif
#else
    // When running in a web browser, try to request a context that supports OpenGL ES 2.0 (WebGL 1).
    format.setMajorVersion(2);
    format.setMinorVersion(0);
#endif

    // Enable this to display debug log messages from the OpenGL driver.
    if(qEnvironmentVariableIntValue("OVITO_OPENGL_DEBUG_CONTEXT") )
        format.setOption(QSurfaceFormat::DebugContext);

    QSurfaceFormat::setDefaultFormat(format);

    // Register Qt resources.
    ::registerQtResources();

    return true;
}

/******************************************************************************
* Create the global Qt application object.
******************************************************************************/
void Application::createQtApplication(bool supportGui)
{
    // Let the user override the application type with the OVITO_GUI_MODE environment variable.
    if(qEnvironmentVariableIsSet("OVITO_GUI_MODE")) {
        if(qEnvironmentVariableIntValue("OVITO_GUI_MODE")) {
            supportGui = true;
        }
        else if(supportGui) {
            throw Exception(tr("Cannot use this program function, which requires a GUI-enabled Qt application (a connection to the platform's windowing system): "
                    "The OVITO_GUI_MODE environment variable is set to 0, which forces OVITO to run in non-GUI mode."));
        }
    }

    // Do nothing if a global Qt application object already exists. Just check that it is of the right type.
    if(QCoreApplication::instance()) {
        if(supportGui) {
            QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
            if(!guiApp || guiApp->platformName() == QStringLiteral("minimal")) {
                throw Exception(tr("This function requires a GUI-enabled Qt application object, i.e. a connection to a windowing system, but it cannot be initialized at this point: A process-wide, non-GUI Qt application object already exists. "
                        "To solve this issue, you can explicitly create a PySide6 QApplication object before importing the ovito Python module, invoke this function sooner in the Python script to initialize a "
                        "connection to the windowing system (e.g. X Windows or Wayland), or set the environment variable OVITO_GUI_MODE=1."));
            }
        }
        return;
    }

    // The Qt application must be created in the main thread.
    if(this_task::isMainThread()) {
        // Let the derived class create the right QCoreApplication object.
        QCoreApplication* qtApp = createQtApplicationImpl(supportGui, *_argc, _argv);

        // Make the Qt application a child of OVITO's Application object to destroy it on shutdown.
        if(!qtApp->parent())
            qtApp->setParent(this);

        // Keep Qt event loop running indefinitely in Jupyter kernel mode, such that
        // new main windows can be opened and closed more than once.
        if(runMode() == KernelMode) {
            qtApp->setQuitLockEnabled(false);
            if(QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(qtApp)) {
                guiApp->setQuitOnLastWindowClosed(false);
            }
        }

        // OVITO prefers the "C" locale over the system's default locale.
        QLocale::setDefault(QLocale::c());

        // Double-check if a "C"-like locale is active. Otherwise, parsing of floating-point numbers from text files
        // using the sscanf() function may fail.
        FloatType d;
        if(sscanf("1234.56", FLOATTYPE_SCANF_STRING, &d) != 1 || d != FloatType(1234.56)) {
            throw Exception(tr("Failed to set the C locale for parsing floating-point numbers with decimal points. "
                    "Please check your system's locale settings or set the LC_ALL environment variable to C.UTF-8. "
                    "Otherwise, OVITO is not able to read & write simulation data files correctly."));
        }
    }
    else {
        // If called from a worker thread, we need to perform the creation of the Qt app in the main thread
        // and block the worker thread until the Qt app has been created.
        launchAsync(DeferredObjectExecutor(Application::instance()), [supportGui]() noexcept {
            Application::instance()->createQtApplication(supportGui);
        }).waitForFinished();
    }
}

/******************************************************************************
* Cancels all running tasks and closes the user interface as soon as possible
* (without asking user to save changes).
******************************************************************************/
bool Application::shutdown()
{
    // Close the user interface.
    if(!UserInterface::shutdown())
        return false;

    // Wait for all running tasks to stop, empty the deferred work queue, and leave all nested event loops.
    taskManager().requestShutdown();

    return true;
}

#ifndef Q_OS_WASM
/******************************************************************************
* Returns the application-wide network manager object.
******************************************************************************/
QNetworkAccessManager* Application::networkAccessManager()
{
    if(!_networkAccessManager) {
        if(qEnvironmentVariableIntValue("OVITO_ENABLE_SYSTEM_PROXY")) {
            QNetworkProxyFactory::setUseSystemConfiguration(true);
        }
        _networkAccessManager = new QNetworkAccessManager(this);
    }
    return _networkAccessManager;
}
#endif

/******************************************************************************
* Reports task progress on the console (from any thread).
******************************************************************************/
void Application::logTaskActivity(const QString& text)
{
    // Print task messages to the console if task logging is enabled (via Python method ovito.enable_logging()).
    if(taskConsoleLoggingEnabled() && !text.isEmpty())
        qInfo().noquote() << "OVITO:" << text;
}

/******************************************************************************
* Similar to QCoreApplication::applicationDirPath() but doesn't require a Qt application.
******************************************************************************/
QString Application::applicationDirPath() const
{
    return qApp ? QCoreApplication::applicationDirPath() : QFileInfo(ovitoAppFileName()).path();
}

/******************************************************************************
* Similar to QCoreApplication::applicationFilePath() but doesn't require a Qt application.
******************************************************************************/
QString Application::applicationFilePath() const
{
    return qApp ? QCoreApplication::applicationFilePath() : ovitoAppFileName();
}

}   // End of namespace
