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

#pragma once


#include <ovito/aiagent/AIAgent.h>

namespace Ovito {

/**
 * \brief Runs a child process on a pseudo-terminal.
 *
 * Coding agent CLIs are full-screen terminal applications: they query the terminal size,
 * switch to the alternate screen buffer and expect line editing to happen on their side.
 * A plain QProcess with pipes is not enough for that, because the child would see a pipe
 * instead of a TTY and fall back to non-interactive behavior. This class therefore
 * allocates a pty pair, runs the child on the slave end and exposes the master end as a
 * byte stream. It is Unix-only; on other platforms start() reports an error.
 */
class PtyProcess : public QObject
{
    Q_OBJECT

public:

    /// Constructor.
    explicit PtyProcess(QObject* parent = nullptr) : QObject(parent) {}

    /// Destructor. Terminates the child process if it is still running.
    virtual ~PtyProcess();

    /// Starts the given program on a new pseudo-terminal.
    /// Returns false and sets errorString() if the child process could not be started.
    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
            const QProcessEnvironment& environment, int columns, int rows);

    /// Returns whether a child process is currently running.
    bool isRunning() const { return _masterFd != -1; }

    /// Returns the error message of the last failed operation.
    const QString& errorString() const { return _errorString; }

    /// Sends the given bytes to the standard input of the child process.
    void write(const QByteArray& data);

    /// Informs the child process about a new terminal window size.
    void setWindowSize(int columns, int rows);

    /// Asks the child process to terminate, first politely, then forcefully.
    void terminate();

Q_SIGNALS:

    /// Emitted whenever the child process has written data to the terminal.
    void dataReceived(const QByteArray& data);

    /// Emitted after the child process has exited and the terminal has been closed.
    void finished(int exitCode);

private Q_SLOTS:

    /// Reads pending output from the pseudo-terminal.
    void readFromPty();

private:

    /// Closes the master file descriptor and reaps the child process.
    void cleanup(int exitCode);

    /// File descriptor of the master side of the pty, or -1 if no process is running.
    int _masterFd = -1;

    /// Process ID of the child process, or -1 if no process is running.
    qint64 _childPid = -1;

    /// Watches the master file descriptor for incoming data.
    QSocketNotifier* _notifier = nullptr;

    /// Error message of the last failed operation.
    QString _errorString;
};

}   // End of namespace
