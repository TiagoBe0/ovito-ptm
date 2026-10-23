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
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include "AIAgentApplet.h"
#include "AgentBridgeServer.h"
#include "PtyProcess.h"
#include "TerminalWidget.h"

#include <QSettings>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AIAgentApplet);
OVITO_CLASSINFO(AIAgentApplet, "DisplayName", "AI Agent");

/******************************************************************************
* Returns the built-in list of coding agents.
******************************************************************************/
const std::vector<AIAgentApplet::AgentPreset>& AIAgentApplet::agentPresets()
{
    // The command lines mirror what each agent expects for a one-shot initial prompt in
    // interactive mode. They are only defaults: the "Custom" entry lets the user override
    // everything, which is the escape hatch when an agent changes its CLI.
    static const std::vector<AgentPreset> presets = {
        { QStringLiteral("claude"),   QStringLiteral("Claude Code"),        QStringLiteral("claude \"<PROMPT>\"") },
        { QStringLiteral("copilot"),  QStringLiteral("GitHub Copilot CLI"), QStringLiteral("copilot --prompt \"<PROMPT>\"") },
        { QStringLiteral("codex"),    QStringLiteral("OpenAI Codex"),       QStringLiteral("codex \"<PROMPT>\"") },
        { QStringLiteral("opencode"), QStringLiteral("OpenCode"),           QStringLiteral("opencode \"<PROMPT>\"") },
        { QStringLiteral("gemini"),   QStringLiteral("Gemini CLI"),         QStringLiteral("gemini -i \"<PROMPT>\"") },
        { QStringLiteral("custom"),   QStringLiteral("Custom..."),          QString() }
    };
    return presets;
}

/******************************************************************************
* Lets the applet create the UI widget that is to be placed into the data
* inspector panel.
******************************************************************************/
QWidget* AIAgentApplet::createWidget()
{
    _stackedWidget = new QStackedWidget();

    // --- Page 0: the launch form. ---

    QWidget* launchPage = new QWidget();
    QGridLayout* formLayout = new QGridLayout(launchPage);
    formLayout->setContentsMargins(12, 12, 12, 12);
    formLayout->setSpacing(6);
    formLayout->setColumnStretch(1, 1);
    int row = 0;

    QLabel* introLabel = new QLabel(tr(
        "<p>Run an AI coding agent inside this OVITO session. The agent sees the pipelines, "
        "modifiers and viewports you are working with, and can change them while you watch.</p>"));
    introLabel->setWordWrap(true);
    formLayout->addWidget(introLabel, row++, 0, 1, 3);

    formLayout->addWidget(new QLabel(tr("Coding agent:")), row, 0);
    _agentSelector = new QComboBox();
    for(const AgentPreset& preset : agentPresets())
        _agentSelector->addItem(preset.displayName, preset.id);
    formLayout->addWidget(_agentSelector, row++, 1, 1, 2);

    _commandLineLabel = new QLabel(tr("Command line:"));
    formLayout->addWidget(_commandLineLabel, row, 0);
    _commandLineField = new QLineEdit();
    _commandLineField->setToolTip(tr("The command used to start the agent. The placeholder <PROMPT> is replaced with the connection prompt."));
    formLayout->addWidget(_commandLineField, row++, 1, 1, 2);

    formLayout->addWidget(new QLabel(tr("Working directory:")), row, 0);
    _workingDirectoryLabel = new QLabel();
    _workingDirectoryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(_workingDirectoryLabel, row, 1);
    QPushButton* changeDirButton = new QPushButton(tr("Change..."));
    formLayout->addWidget(changeDirButton, row++, 2);

    _startButton = new QPushButton(tr("Start"));
    _startButton->setDefault(true);
    formLayout->addWidget(_startButton, row++, 1, Qt::AlignLeft);

    _statusLabel = new QLabel();
    _statusLabel->setWordWrap(true);
    _statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(_statusLabel, row++, 0, 1, 3);

    formLayout->setRowStretch(row, 1);
    _stackedWidget->addWidget(launchPage);

    // --- Page 1: the terminal running the agent. ---

    QWidget* terminalPage = new QWidget();
    QVBoxLayout* terminalLayout = new QVBoxLayout(terminalPage);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 2, 2, 2);
    _sessionTitleLabel = new QLabel();
    headerLayout->addWidget(_sessionTitleLabel, 1);
    QToolButton* closeButton = new QToolButton();
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setToolTip(tr("End the agent session"));
    closeButton->setAutoRaise(true);
    headerLayout->addWidget(closeButton);
    terminalLayout->addLayout(headerLayout);

    _terminal = new TerminalWidget();
    terminalLayout->addWidget(_terminal, 1);
    _stackedWidget->addWidget(terminalPage);

    loadSettings();
    updateLaunchForm();

    connect(_agentSelector, &QComboBox::currentIndexChanged, this, [this](int) {
        // Seed the custom command line from the preset last used, as a starting point.
        if(_agentSelector->currentData().toString() == QStringLiteral("custom") && _customCommandLine.isEmpty()) {
            int previous = std::max(_agentSelector->currentIndex() - 1, 0);
            _customCommandLine = agentPresets()[previous].commandLine;
            _commandLineField->setText(_customCommandLine);
        }
        updateLaunchForm();
        saveSettings();
    });
    connect(_commandLineField, &QLineEdit::textEdited, this, [this](const QString& text) {
        _customCommandLine = text;
        saveSettings();
    });
    connect(changeDirButton, &QPushButton::clicked, this, [this]() {
        QString directory = QFileDialog::getExistingDirectory(_stackedWidget, tr("Select working directory"), _workingDirectory);
        if(!directory.isEmpty()) {
            _workingDirectory = directory;
            _workingDirectoryLabel->setText(_workingDirectory);
            saveSettings();
        }
    });
    connect(_startButton, &QPushButton::clicked, this, &AIAgentApplet::startSession);
    connect(closeButton, &QToolButton::clicked, this, &AIAgentApplet::stopSession);
    connect(_terminal, &TerminalWidget::titleChanged, this, [this](const QString& title) {
        _sessionTitleLabel->setText(title);
    });

    return _stackedWidget;
}

/******************************************************************************
* Updates the enabled state and visibility of the widgets in the launch form.
******************************************************************************/
void AIAgentApplet::updateLaunchForm()
{
    const bool custom = (_agentSelector->currentData().toString() == QStringLiteral("custom"));
    _commandLineLabel->setVisible(custom);
    _commandLineField->setVisible(custom);
    _workingDirectoryLabel->setText(_workingDirectory);
}

/******************************************************************************
* Loads the persistent settings of this applet.
******************************************************************************/
void AIAgentApplet::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai_agent"));
    QString agentId = settings.value(QStringLiteral("agent"), QStringLiteral("claude")).toString();
    int index = _agentSelector->findData(agentId);
    _agentSelector->setCurrentIndex(index >= 0 ? index : 0);
    _customCommandLine = settings.value(QStringLiteral("command_line")).toString();
    _commandLineField->setText(_customCommandLine);
    _workingDirectory = settings.value(QStringLiteral("working_directory")).toString();
    if(_workingDirectory.isEmpty() || !QDir(_workingDirectory).exists())
        _workingDirectory = QDir::currentPath();
    settings.endGroup();
}

/******************************************************************************
* Stores the persistent settings of this applet.
******************************************************************************/
void AIAgentApplet::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai_agent"));
    settings.setValue(QStringLiteral("agent"), _agentSelector->currentData().toString());
    settings.setValue(QStringLiteral("command_line"), _customCommandLine);
    settings.setValue(QStringLiteral("working_directory"), _workingDirectory);
    settings.endGroup();
}

/******************************************************************************
* Composes the initial prompt handed to the agent.
******************************************************************************/
QString AIAgentApplet::buildConnectionPrompt() const
{
    // The prompt is the only channel through which OVITO can talk to an agent it did not
    // configure, so it states both what the session is and what to do if the tools are
    // missing. The socket path is also passed through the environment (OVITO_AGENT_SOCKET),
    // which is how the MCP server actually finds us.
    QString prompt = tr(
        "You are connected to a running OVITO session. Its live-session bridge is listening on the "
        "local socket %1, which the ovito-agent MCP server reads from the OVITO_AGENT_SOCKET "
        "environment variable. Use the ovito-agent tools to inspect and modify this session: "
        "they list the pipelines in the scene, read and write modifier parameters, evaluate the "
        "data, control the viewports and take screenshots of what the user sees. "
        "If those tools are not available, say so instead of guessing, and point the user to "
        "research/ai_agent/README.md for the one-time setup. "
        "Start by summarizing the current scene, then wait for instructions.");
    return prompt.arg(_bridge ? _bridge->socketPath() : tr("(bridge unavailable)"));
}

/******************************************************************************
* Starts the coding agent session.
******************************************************************************/
void AIAgentApplet::startSession()
{
    if(_process)
        return;

    _statusLabel->clear();

    // Start the bridge first: its socket path goes into the agent's environment.
    if(!_bridge) {
        _bridge = new AgentBridgeServer(ui(), this);
        if(!_bridge->start()) {
            _statusLabel->setText(tr("<b>The session bridge could not be started:</b> %1<br>"
                "The agent will run without access to this OVITO session.").arg(_bridge->errorString()));
            delete _bridge;
            _bridge = nullptr;
        }
    }

    // Work out the command line to run.
    const QString agentId = _agentSelector->currentData().toString();
    QString commandLine;
    for(const AgentPreset& preset : agentPresets()) {
        if(preset.id == agentId) {
            commandLine = preset.commandLine;
            break;
        }
    }
    if(agentId == QStringLiteral("custom"))
        commandLine = _commandLineField->text().trimmed();
    if(commandLine.isEmpty()) {
        _statusLabel->setText(tr("<b>Please enter the command line used to start the agent.</b>"));
        return;
    }
    if(!commandLine.contains(QStringLiteral("<PROMPT>"))) {
        _statusLabel->setText(tr("<b>The command line must contain the placeholder &lt;PROMPT&gt;</b>, "
            "which OVITO replaces with the connection prompt for the agent."));
        return;
    }

    // Substitute the prompt after splitting, so that a prompt containing spaces or quotes
    // stays a single argument no matter how it is quoted in the command line template.
    QStringList arguments = QProcess::splitCommand(commandLine);
    if(arguments.isEmpty()) {
        _statusLabel->setText(tr("<b>The command line could not be parsed.</b>"));
        return;
    }
    const QString prompt = buildConnectionPrompt();
    for(QString& argument : arguments)
        argument.replace(QStringLiteral("<PROMPT>"), prompt);
    const QString program = arguments.takeFirst();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.remove(QStringLiteral("LINES"));
    environment.remove(QStringLiteral("COLUMNS"));
    if(_bridge)
        environment.insert(QStringLiteral("OVITO_AGENT_SOCKET"), _bridge->socketPath());

    _terminal->reset();
    _process = new PtyProcess(this);
    connect(_process, &PtyProcess::dataReceived, _terminal, &TerminalWidget::feed);
    connect(_terminal, &TerminalWidget::dataToSend, _process, &PtyProcess::write);
    connect(_terminal, &TerminalWidget::windowSizeChanged, _process, &PtyProcess::setWindowSize);
    connect(_process, &PtyProcess::finished, this, [this](int exitCode) {
        _terminal->appendMessage(tr("[The agent process exited with code %1.]").arg(exitCode));
        // Give the user a moment to read the last screen before the form comes back.
        QTimer::singleShot(1500, this, &AIAgentApplet::stopSession);
    });

    if(!_process->start(program, arguments, _workingDirectory, environment, _terminal->columns(), _terminal->rows())) {
        _statusLabel->setText(tr("<b>Could not start the agent:</b> %1").arg(_process->errorString()));
        delete _process;
        _process = nullptr;
        return;
    }

    _sessionTitleLabel->setText(tr("%1 — %2").arg(_agentSelector->currentText(), _workingDirectory));
    _stackedWidget->setCurrentIndex(1);
    _terminal->setFocus();
    saveSettings();
}

/******************************************************************************
* Ends the coding agent session and returns to the launch form.
******************************************************************************/
void AIAgentApplet::stopSession()
{
    if(_process) {
        PtyProcess* process = _process;
        _process = nullptr;
        process->disconnect(this);
        process->terminate();
        process->deleteLater();
    }
    if(_bridge) {
        _bridge->stop();
        _bridge->deleteLater();
        _bridge = nullptr;
    }
    if(_stackedWidget)
        _stackedWidget->setCurrentIndex(0);
}

}   // End of namespace
