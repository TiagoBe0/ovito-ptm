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
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>

#include <QLocalServer>

namespace Ovito {

class MainWindowUI;

/**
 * \brief Exposes the running OVITO session to an external AI coding agent.
 *
 * OVITO Pro solves this by starting a Jupyter kernel inside its embedded Python interpreter
 * and letting the agent execute Python in the live session. This build has no embedded
 * interpreter, so the bridge speaks directly to OVITO's C++ object model instead: it serves
 * a small JSON-RPC API over a local socket, with one request and one response per line.
 *
 * The API is generic on purpose. Modifier parameters are read and written through OVITO's
 * property field reflection, so every modifier -- including the ones added by this fork --
 * is reachable without the bridge knowing anything about it.
 *
 * All requests are handled in the GUI thread, which is the only thread allowed to touch
 * the scene, and every scene-modifying request is recorded as an undoable operation so the
 * user can revert whatever the agent did.
 */
class AgentBridgeServer : public QObject
{
    Q_OBJECT

public:

    /// Constructor. Does not start listening yet.
    explicit AgentBridgeServer(MainWindowUI& ui, QObject* parent = nullptr);

    /// Destructor.
    virtual ~AgentBridgeServer();

    /// Starts listening on a private local socket.
    /// Returns false and sets errorString() if the socket could not be created.
    bool start();

    /// Stops the server and disconnects all clients.
    void stop();

    /// Returns the file system path of the socket the server is listening on.
    const QString& socketPath() const { return _socketPath; }

    /// Returns the error message of the last failed operation.
    const QString& errorString() const { return _errorString; }

Q_SIGNALS:

    /// Emitted whenever a request has been handled, for logging purposes.
    void requestHandled(const QString& method, bool success, const QString& message);

private Q_SLOTS:

    /// Accepts an incoming connection from the MCP server process.
    void onNewConnection();

private:

    /// Parses and dispatches one request line, returning the JSON response object.
    QJsonObject handleRequestLine(const QByteArray& line);

    /// Dispatches a single request to the method implementation.
    QJsonObject dispatch(const QString& method, const QJsonObject& params);

    // --- Method implementations. ---

    QJsonObject methodSessionInfo(const QJsonObject& params);
    QJsonObject methodSceneList(const QJsonObject& params);
    QJsonObject methodPipelineDescribe(const QJsonObject& params);
    QJsonObject methodPipelineSelect(const QJsonObject& params);
    QJsonObject methodPipelineDelete(const QJsonObject& params);
    QJsonObject methodModifierCatalog(const QJsonObject& params);
    QJsonObject methodModifierInsert(const QJsonObject& params);
    QJsonObject methodModifierDelete(const QJsonObject& params);
    QJsonObject methodModifierSetParams(const QJsonObject& params);
    QJsonObject methodModifierSetEnabled(const QJsonObject& params);
    QJsonObject methodDataSummary(const QJsonObject& params);
    QJsonObject methodDataAttributes(const QJsonObject& params);
    QJsonObject methodDataTable(const QJsonObject& params);
    QJsonObject methodDataElements(const QJsonObject& params);
    QJsonObject methodFileImport(const QJsonObject& params);
    QJsonObject methodAnimationInfo(const QJsonObject& params);
    QJsonObject methodAnimationGoto(const QJsonObject& params);
    QJsonObject methodViewportList(const QJsonObject& params);
    QJsonObject methodViewportCamera(const QJsonObject& params);
    QJsonObject methodViewportScreenshot(const QJsonObject& params);

    // --- Helpers. ---

    /// Returns the pipeline addressed by the request, or throws an Exception.
    Pipeline* resolvePipeline(const QJsonObject& params) const;

    /// Returns the modification node at the given index of a pipeline, counting from the
    /// end of the pipeline (index 0 is the topmost, most recently applied modifier).
    ModificationNode* resolveModifierNode(Pipeline* pipeline, int index) const;

    /// Returns the pipeline output at the frame given in the request, evaluating the
    /// pipeline and waiting for the result if it is not cached yet.
    PipelineFlowState evaluatePipeline(Pipeline* pipeline, const QJsonObject& params) const;

    /// Serializes the parameters of an object using its property field reflection.
    QJsonObject serializeParameters(const RefTarget* object) const;

    /// Applies a set of parameter values to an object, reporting the ones that were rejected.
    QStringList applyParameters(RefTarget* object, const QJsonObject& values) const;

    /// Converts a JSON value to a QVariant matching the type of the current property value.
    static QVariant jsonToVariant(const QJsonValue& value, const QVariant& currentValue, bool& ok);

    /// Converts a property value to JSON.
    static QJsonValue variantToJson(const QVariant& value);

    /// Describes the contents of a data collection.
    QJsonObject describeDataCollection(const DataCollection* data) const;

    /// The user interface this bridge is attached to.
    MainWindowUI& _ui;

    /// The local server accepting connections from the MCP process.
    QLocalServer* _server = nullptr;

    /// Path of the socket file.
    QString _socketPath;

    /// Error message of the last failed operation.
    QString _errorString;
};

}   // End of namespace
