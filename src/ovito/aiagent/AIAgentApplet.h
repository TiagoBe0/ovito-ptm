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
#include <ovito/gui/desktop/mainwin/data_inspector/DataInspectionApplet.h>

namespace Ovito {

class PtyProcess;
class TerminalWidget;
class AgentBridgeServer;

/**
 * \brief Data inspector page that runs an external AI coding agent inside the OVITO window.
 *
 * The applet does two things: it runs the agent's command line interface on an embedded
 * pseudo-terminal, and it starts a local bridge server through which the agent can inspect
 * and modify the running OVITO session (see AgentBridgeServer). Which agent to run is up to
 * the user; OVITO only knows how to invoke a few common ones and lets the user spell out
 * the command line for anything else.
 */
class AIAgentApplet : public DataInspectionApplet
{
    OVITO_CLASS(AIAgentApplet)
    Q_OBJECT

public:

    /// Constructor.
    void initializeObject() { DataInspectionApplet::initializeObject(DataObject::OOClass()); }

    /// Returns the key value for this applet that is used for ordering the applet tabs.
    virtual int orderingKey() const override { return 200; }

    /// This applet is not tied to any data object, so it never applies to a data collection...
    virtual bool appliesTo(const DataCollection& data) override { return false; }

    /// ...but its tab is always shown, even when no pipeline is selected.
    virtual bool alwaysAvailable() const override { return true; }

    /// The applet does not display data objects.
    virtual std::vector<ConstDataObjectPath> getDataObjectPaths() override { return {}; }

    /// Lets the applet create the UI widget that is to be placed into the data inspector panel.
    virtual QWidget* createWidget() override;

    /// Updates the contents displayed in the inspector.
    virtual void updateDisplay() override {}

    /// Returns the help topic ID for the documentation page of this applet.
    virtual QString helpTopicId() const override { return QStringLiteral("manual:data_inspector.ai_agent"); }

private:

    /// Describes one coding agent that OVITO knows how to launch.
    struct AgentPreset {
        QString id;             ///< Stable identifier used in the settings store.
        QString displayName;    ///< Name shown in the drop-down list.
        QString commandLine;    ///< Command line containing the <PROMPT> placeholder.
    };

    /// Returns the built-in list of coding agents.
    static const std::vector<AgentPreset>& agentPresets();

    /// Starts the coding agent session.
    void startSession();

    /// Ends the coding agent session and returns to the launch form.
    void stopSession();

    /// Composes the initial prompt handed to the agent, which tells it how to reach OVITO.
    QString buildConnectionPrompt() const;

    /// Updates the enabled state and visibility of the widgets in the launch form.
    void updateLaunchForm();

    /// Loads the persistent settings of this applet.
    void loadSettings();

    /// Stores the persistent settings of this applet.
    void saveSettings() const;

    /// Switches between the launch form and the terminal.
    QStackedWidget* _stackedWidget = nullptr;

    /// Widgets of the launch form.
    QComboBox* _agentSelector = nullptr;
    QLineEdit* _commandLineField = nullptr;
    QLabel* _commandLineLabel = nullptr;
    QLabel* _workingDirectoryLabel = nullptr;
    QLabel* _statusLabel = nullptr;
    QPushButton* _startButton = nullptr;

    /// Widgets of the terminal page.
    TerminalWidget* _terminal = nullptr;
    QLabel* _sessionTitleLabel = nullptr;

    /// The running agent process, or null if no session is active.
    PtyProcess* _process = nullptr;

    /// The bridge that exposes the OVITO session to the agent.
    AgentBridgeServer* _bridge = nullptr;

    /// The directory in which the agent process is started.
    QString _workingDirectory;

    /// The custom command line entered by the user.
    QString _customCommandLine;
};

}   // End of namespace
