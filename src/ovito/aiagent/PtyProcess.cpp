////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
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

#include <ovito/aiagent/AIAgent.h>
#include "PtyProcess.h"

#ifdef Q_OS_UNIX
    #include <termios.h>
    #include <unistd.h>
    #include <signal.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <sys/ioctl.h>
    #include <sys/wait.h>
    #ifdef Q_OS_MACOS
        #include <util.h>
    #else
        #include <pty.h>
    #endif
#endif

namespace Ovito {

/******************************************************************************
* Destructor.
******************************************************************************/
PtyProcess::~PtyProcess()
{
    terminate();
}

/******************************************************************************
* Starts the given program on a new pseudo-terminal.
******************************************************************************/
bool PtyProcess::start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
        const QProcessEnvironment& environment, int columns, int rows)
{
#ifndef Q_OS_UNIX
    Q_UNUSED(program); Q_UNUSED(arguments); Q_UNUSED(workingDirectory);
    Q_UNUSED(environment); Q_UNUSED(columns); Q_UNUSED(rows);
    _errorString = tr("Running an interactive terminal session is only supported on Unix-like systems.");
    return false;
#else
    OVITO_ASSERT(!isRunning());

    // Resolve the executable. execvp() would search PATH itself, but doing it here lets us
    // report a helpful error message before forking instead of having the child die silently.
    QString executable = program;
    if(!executable.contains(QChar('/'))) {
        QString resolved = QStandardPaths::findExecutable(executable, environment.value(QStringLiteral("PATH")).split(QChar(':'), Qt::SkipEmptyParts));
        if(resolved.isEmpty())
            resolved = QStandardPaths::findExecutable(executable);
        if(resolved.isEmpty()) {
            _errorString = tr("The executable '%1' could not be found. Make sure the coding agent is installed and reachable through the PATH seen by OVITO.").arg(program);
            return false;
        }
        executable = resolved;
    }
    else if(!QFileInfo(executable).isExecutable()) {
        _errorString = tr("The file '%1' does not exist or is not executable.").arg(executable);
        return false;
    }

    // Everything the child needs must be prepared before forking: after fork() only
    // async-signal-safe calls are allowed, so no memory allocation may happen there.
    QByteArray executableUtf8 = executable.toLocal8Bit();
    QByteArray workingDirUtf8 = workingDirectory.toLocal8Bit();
    std::vector<QByteArray> argStorage;
    argStorage.push_back(executableUtf8);
    for(const QString& arg : arguments)
        argStorage.push_back(arg.toLocal8Bit());
    std::vector<char*> argv;
    for(QByteArray& arg : argStorage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::vector<QByteArray> envStorage;
    for(const QString& key : environment.keys())
        envStorage.push_back((key + QChar('=') + environment.value(key)).toLocal8Bit());
    std::vector<char*> envp;
    for(QByteArray& entry : envStorage)
        envp.push_back(entry.data());
    envp.push_back(nullptr);

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)std::max(columns, 1);
    ws.ws_row = (unsigned short)std::max(rows, 1);

    int masterFd = -1;
    pid_t pid = forkpty(&masterFd, nullptr, nullptr, &ws);
    if(pid < 0) {
        _errorString = tr("Failed to allocate a pseudo-terminal: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if(pid == 0) {
        // We are the child process. Detach from OVITO's signal handling and start the agent.
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        if(!workingDirUtf8.isEmpty())
            (void)::chdir(workingDirUtf8.constData());
        ::execve(executableUtf8.constData(), argv.data(), envp.data());
        // execve() only returns on failure. Report it on the terminal the parent is reading.
        const char* msg = "\r\nOVITO: failed to launch the coding agent process.\r\n";
        (void)::write(STDERR_FILENO, msg, strlen(msg));
        ::_exit(127);
    }

    // We are the parent process.
    _masterFd = masterFd;
    _childPid = pid;
    _errorString.clear();
    ::fcntl(_masterFd, F_SETFL, ::fcntl(_masterFd, F_GETFL, 0) | O_NONBLOCK);
    _notifier = new QSocketNotifier(_masterFd, QSocketNotifier::Read, this);
    connect(_notifier, &QSocketNotifier::activated, this, &PtyProcess::readFromPty);
    return true;
#endif
}

/******************************************************************************
* Reads pending output from the pseudo-terminal.
******************************************************************************/
void PtyProcess::readFromPty()
{
#ifdef Q_OS_UNIX
    if(_masterFd == -1)
        return;
    // Drain the pty in bounded chunks. A busy agent can produce output faster than we render it,
    // so we hand control back to the event loop after a while instead of looping until EAGAIN.
    char buffer[8192];
    for(int chunk = 0; chunk < 16; chunk++) {
        ssize_t n = ::read(_masterFd, buffer, sizeof(buffer));
        if(n > 0) {
            Q_EMIT dataReceived(QByteArray(buffer, (int)n));
            continue;
        }
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        // n == 0 or EIO: the slave side was closed, i.e. the child process is gone.
        int exitCode = 0;
        int status = 0;
        if(_childPid > 0 && ::waitpid((pid_t)_childPid, &status, 0) > 0)
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        cleanup(exitCode);
        return;
    }
#endif
}

/******************************************************************************
* Sends the given bytes to the standard input of the child process.
******************************************************************************/
void PtyProcess::write(const QByteArray& data)
{
#ifdef Q_OS_UNIX
    if(_masterFd == -1 || data.isEmpty())
        return;
    qint64 offset = 0;
    while(offset < data.size()) {
        ssize_t n = ::write(_masterFd, data.constData() + offset, data.size() - offset);
        if(n > 0) {
            offset += n;
            continue;
        }
        if(n < 0 && errno == EINTR)
            continue;
        // The pty buffer is full. Wait for it to drain rather than dropping keystrokes.
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(_masterFd, &writeSet);
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            if(::select(_masterFd + 1, nullptr, &writeSet, nullptr, &timeout) > 0)
                continue;
        }
        break;
    }
#else
    Q_UNUSED(data);
#endif
}

/******************************************************************************
* Informs the child process about a new terminal window size.
******************************************************************************/
void PtyProcess::setWindowSize(int columns, int rows)
{
#ifdef Q_OS_UNIX
    if(_masterFd == -1)
        return;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)std::max(columns, 1);
    ws.ws_row = (unsigned short)std::max(rows, 1);
    ::ioctl(_masterFd, TIOCSWINSZ, &ws);
    // SIGWINCH is delivered to the foreground process group by the kernel, but only for
    // processes that have the pty as their controlling terminal, which is the case here.
#else
    Q_UNUSED(columns); Q_UNUSED(rows);
#endif
}

/******************************************************************************
* Asks the child process to terminate, first politely, then forcefully.
******************************************************************************/
void PtyProcess::terminate()
{
#ifdef Q_OS_UNIX
    if(_masterFd == -1)
        return;
    pid_t pid = (pid_t)_childPid;
    if(pid > 0) {
        // Signal the whole foreground process group so that helper processes started by the
        // agent (language servers, shells) go away as well.
        ::kill(-pid, SIGHUP);
        ::kill(-pid, SIGTERM);
        for(int i = 0; i < 50; i++) {
            int status = 0;
            pid_t result = ::waitpid(pid, &status, WNOHANG);
            if(result == pid || (result < 0 && errno == ECHILD))
                break;
            QThread::msleep(10);
            if(i == 30)
                ::kill(-pid, SIGKILL);
        }
    }
    cleanup(0);
#endif
}

/******************************************************************************
* Closes the master file descriptor and reaps the child process.
******************************************************************************/
void PtyProcess::cleanup(int exitCode)
{
#ifdef Q_OS_UNIX
    if(_masterFd == -1)
        return;
    if(_notifier) {
        _notifier->setEnabled(false);
        _notifier->deleteLater();
        _notifier = nullptr;
    }
    ::close(_masterFd);
    _masterFd = -1;
    _childPid = -1;
    Q_EMIT finished(exitCode);
#else
    Q_UNUSED(exitCode);
#endif
}

}   // End of namespace
