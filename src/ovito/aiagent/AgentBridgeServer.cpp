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
#include <ovito/gui/desktop/mainwin/MainWindowUI.h>
#include <ovito/gui/desktop/mainwin/ViewportsPanel.h>
#include <ovito/gui/desktop/utilities/concurrent/ProgressDialog.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/app/PluginManager.h>
#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/ModifierClass.h>
#include <ovito/core/dataset/scene/Scene.h>
#include <ovito/core/dataset/scene/SceneNode.h>
#include <ovito/core/dataset/scene/SelectionSet.h>
#include <ovito/core/viewport/Viewport.h>
#include <ovito/core/viewport/ViewportConfiguration.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/properties/PropertyContainer.h>
#include <ovito/stdobj/table/DataTable.h>
#include "AgentBridgeServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
AgentBridgeServer::AgentBridgeServer(MainWindowUI& ui, QObject* parent) : QObject(parent), _ui(ui)
{
}

/******************************************************************************
* Destructor.
******************************************************************************/
AgentBridgeServer::~AgentBridgeServer()
{
    stop();
}

/******************************************************************************
* Starts listening on a private local socket.
******************************************************************************/
bool AgentBridgeServer::start()
{
    if(_server)
        return true;

    // The socket name embeds the process id so that several OVITO windows can each run
    // their own agent session without fighting over the same socket file.
    QString name = QStringLiteral("ovito-agent-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));

    _server = new QLocalServer(this);
    // Only the user running OVITO may connect: the bridge executes scene modifications
    // on behalf of whoever is on the other end.
    _server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    if(!_server->listen(name)) {
        _errorString = _server->errorString();
        delete _server;
        _server = nullptr;
        return false;
    }
    _socketPath = _server->fullServerName();
    connect(_server, &QLocalServer::newConnection, this, &AgentBridgeServer::onNewConnection);
    return true;
}

/******************************************************************************
* Stops the server and disconnects all clients.
******************************************************************************/
void AgentBridgeServer::stop()
{
    if(!_server)
        return;
    _server->close();
    _server->deleteLater();
    _server = nullptr;
    _socketPath.clear();
}

/******************************************************************************
* Accepts an incoming connection from the MCP server process.
******************************************************************************/
void AgentBridgeServer::onNewConnection()
{
    while(QLocalSocket* socket = _server->nextPendingConnection()) {
        // Requests are newline-delimited JSON objects, one response per request, in order.
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            int newlinePos;
            while((newlinePos = buffer->indexOf('\n')) >= 0) {
                QByteArray line = buffer->left(newlinePos);
                buffer->remove(0, newlinePos + 1);
                if(line.trimmed().isEmpty())
                    continue;
                QJsonObject response = handleRequestLine(line);
                socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
                socket->write("\n");
                socket->flush();
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

/******************************************************************************
* Parses and dispatches one request line.
******************************************************************************/
QJsonObject AgentBridgeServer::handleRequestLine(const QByteArray& line)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    QJsonObject response;
    if(parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), tr("Malformed request: %1").arg(parseError.errorString()));
        return response;
    }

    const QJsonObject request = document.object();
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    try {
        response = dispatch(method, params);
        response.insert(QStringLiteral("ok"), true);
        Q_EMIT requestHandled(method, true, QString());
    }
    catch(const Exception& ex) {
        response = QJsonObject();
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), ex.messages().join(QStringLiteral(" ")));
        Q_EMIT requestHandled(method, false, ex.messages().join(QStringLiteral(" ")));
    }
    catch(const std::exception& ex) {
        response = QJsonObject();
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QString::fromLatin1(ex.what()));
        Q_EMIT requestHandled(method, false, QString::fromLatin1(ex.what()));
    }

    if(request.contains(QStringLiteral("id")))
        response.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
    return response;
}

/******************************************************************************
* Dispatches a single request to the method implementation.
******************************************************************************/
QJsonObject AgentBridgeServer::dispatch(const QString& method, const QJsonObject& params)
{
    static const std::map<QString, QJsonObject (AgentBridgeServer::*)(const QJsonObject&)> methods = {
        { QStringLiteral("session.info"),         &AgentBridgeServer::methodSessionInfo },
        { QStringLiteral("scene.list"),           &AgentBridgeServer::methodSceneList },
        { QStringLiteral("pipeline.describe"),    &AgentBridgeServer::methodPipelineDescribe },
        { QStringLiteral("pipeline.select"),      &AgentBridgeServer::methodPipelineSelect },
        { QStringLiteral("pipeline.delete"),      &AgentBridgeServer::methodPipelineDelete },
        { QStringLiteral("modifier.catalog"),     &AgentBridgeServer::methodModifierCatalog },
        { QStringLiteral("modifier.insert"),      &AgentBridgeServer::methodModifierInsert },
        { QStringLiteral("modifier.delete"),      &AgentBridgeServer::methodModifierDelete },
        { QStringLiteral("modifier.set_params"),  &AgentBridgeServer::methodModifierSetParams },
        { QStringLiteral("modifier.set_enabled"), &AgentBridgeServer::methodModifierSetEnabled },
        { QStringLiteral("data.summary"),         &AgentBridgeServer::methodDataSummary },
        { QStringLiteral("data.attributes"),      &AgentBridgeServer::methodDataAttributes },
        { QStringLiteral("data.table"),           &AgentBridgeServer::methodDataTable },
        { QStringLiteral("data.elements"),        &AgentBridgeServer::methodDataElements },
        { QStringLiteral("file.import"),          &AgentBridgeServer::methodFileImport },
        { QStringLiteral("animation.info"),       &AgentBridgeServer::methodAnimationInfo },
        { QStringLiteral("animation.goto"),       &AgentBridgeServer::methodAnimationGoto },
        { QStringLiteral("viewport.list"),        &AgentBridgeServer::methodViewportList },
        { QStringLiteral("viewport.camera"),      &AgentBridgeServer::methodViewportCamera },
        { QStringLiteral("viewport.screenshot"),  &AgentBridgeServer::methodViewportScreenshot },
    };

    auto iter = methods.find(method);
    if(iter == methods.end())
        throw Exception(tr("Unknown bridge method '%1'.").arg(method));
    return (this->*(iter->second))(params);
}

/******************************************************************************
* Returns the pipeline addressed by the request.
******************************************************************************/
Pipeline* AgentBridgeServer::resolvePipeline(const QJsonObject& params) const
{
    Scene* scene = _ui.datasetContainer().activeScene();
    if(!scene)
        throw Exception(tr("There is no active scene."));

    // Collect the pipelines in scene order, which is the order the agent sees them in.
    std::vector<SceneNode*> nodes;
    for(SceneNode* node : scene->children()) {
        if(node->pipeline())
            nodes.push_back(node);
    }
    if(nodes.empty())
        throw Exception(tr("The scene contains no pipeline. Import a data file first."));

    const QJsonValue selector = params.value(QStringLiteral("pipeline"));
    if(selector.isUndefined() || selector.isNull()) {
        // Default to what the user has selected in the pipeline selector, so that the agent
        // and the user talk about the same object when nothing else is said.
        if(SelectionSet* selection = scene->selection()) {
            for(SceneNode* node : selection->nodes()) {
                if(node->pipeline())
                    return node->pipeline();
            }
        }
        return nodes.front()->pipeline();
    }
    if(selector.isDouble()) {
        int index = selector.toInt();
        if(index < 0 || index >= (int)nodes.size())
            throw Exception(tr("There is no pipeline with index %1. The scene contains %2 pipeline(s).").arg(index).arg(nodes.size()));
        return nodes[index]->pipeline();
    }
    const QString name = selector.toString();
    for(SceneNode* node : nodes) {
        if(node->sceneNodeName() == name || node->pipeline()->objectTitle() == name)
            return node->pipeline();
    }
    throw Exception(tr("There is no pipeline named '%1' in the scene.").arg(name));
}

/******************************************************************************
* Returns the modification node at the given index of a pipeline.
******************************************************************************/
ModificationNode* AgentBridgeServer::resolveModifierNode(Pipeline* pipeline, int index) const
{
    int current = 0;
    PipelineNode* node = pipeline->head();
    while(node) {
        if(ModificationNode* modNode = dynamic_object_cast<ModificationNode>(node)) {
            if(current == index)
                return modNode;
            current++;
            node = modNode->input();
        }
        else break;
    }
    throw Exception(tr("The pipeline has no modifier at index %1. It contains %2 modifier(s), counted from the top.").arg(index).arg(current));
}

/******************************************************************************
* Returns the pipeline output at the frame given in the request.
******************************************************************************/
PipelineFlowState AgentBridgeServer::evaluatePipeline(Pipeline* pipeline, const QJsonObject& params) const
{
    AnimationSettings* animationSettings = _ui.datasetContainer().activeAnimationSettings();
    AnimationTime time = animationSettings ? animationSettings->currentTime() : AnimationTime::fromFrame(0);
    if(params.contains(QStringLiteral("frame")))
        time = AnimationTime::fromFrame(params.value(QStringLiteral("frame")).toInt());

    // Use the cached result when the requested frame is the one already on screen; otherwise
    // run the pipeline and block, showing the same progress dialog the user gets elsewhere.
    PipelineFlowState state = pipeline->getCachedPipelineOutput(time, false);
    if(!state) {
        PipelineEvaluationRequest request(time, true, false);
        state = ProgressDialog::blockForFuture(pipeline->evaluatePipeline(request), _ui, tr("Evaluating pipeline"));
    }
    if(!state)
        throw Exception(tr("The pipeline produced no output at the requested frame."));
    return state;
}

/******************************************************************************
* Converts a property value to JSON.
******************************************************************************/
QJsonValue AgentBridgeServer::variantToJson(const QVariant& value)
{
    switch(value.userType()) {
    case QMetaType::Bool:       return QJsonValue(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:  return QJsonValue((double)value.toLongLong());
    case QMetaType::Float:
    case QMetaType::Double:     return QJsonValue(value.toDouble());
    case QMetaType::QString:    return QJsonValue(value.toString());
    default: break;
    }
    // Vector-like parameter types are reported as JSON arrays, which is how the agent
    // gets to write them back, too.
    if(value.canConvert<Vector3>() && value.userType() == qMetaTypeId<Vector3>()) {
        Vector3 v = value.value<Vector3>();
        return QJsonArray{ (double)v.x(), (double)v.y(), (double)v.z() };
    }
    if(value.userType() == qMetaTypeId<Point3>()) {
        Point3 p = value.value<Point3>();
        return QJsonArray{ (double)p.x(), (double)p.y(), (double)p.z() };
    }
    if(value.userType() == qMetaTypeId<Color>()) {
        Color c = value.value<Color>();
        return QJsonArray{ (double)c.r(), (double)c.g(), (double)c.b() };
    }
    if(value.userType() == qMetaTypeId<Vector3I>()) {
        Vector3I v = value.value<Vector3I>();
        return QJsonArray{ v.x(), v.y(), v.z() };
    }
    if(value.canConvert<QString>())
        return QJsonValue(value.toString());
    return QJsonValue(QStringLiteral("<%1>").arg(QString::fromLatin1(value.typeName())));
}

/******************************************************************************
* Converts a JSON value to a QVariant matching the type of the current value.
******************************************************************************/
QVariant AgentBridgeServer::jsonToVariant(const QJsonValue& value, const QVariant& currentValue, bool& ok)
{
    ok = true;
    const int targetType = currentValue.userType();

    if(targetType == qMetaTypeId<Vector3>() || targetType == qMetaTypeId<Point3>() ||
            targetType == qMetaTypeId<Color>() || targetType == qMetaTypeId<Vector3I>()) {
        QJsonArray array = value.toArray();
        if(array.size() != 3) { ok = false; return {}; }
        if(targetType == qMetaTypeId<Vector3>())
            return QVariant::fromValue(Vector3((FloatType)array[0].toDouble(), (FloatType)array[1].toDouble(), (FloatType)array[2].toDouble()));
        if(targetType == qMetaTypeId<Point3>())
            return QVariant::fromValue(Point3((FloatType)array[0].toDouble(), (FloatType)array[1].toDouble(), (FloatType)array[2].toDouble()));
        if(targetType == qMetaTypeId<Color>())
            return QVariant::fromValue(Color((FloatType)array[0].toDouble(), (FloatType)array[1].toDouble(), (FloatType)array[2].toDouble()));
        return QVariant::fromValue(Vector3I(array[0].toInt(), array[1].toInt(), array[2].toInt()));
    }

    switch(targetType) {
    case QMetaType::Bool:
        if(value.isBool()) return QVariant(value.toBool());
        if(value.isDouble()) return QVariant(value.toDouble() != 0.0);
        ok = false;
        return {};
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        if(!value.isDouble()) { ok = false; return {}; }
        return QVariant((int)value.toDouble());
    case QMetaType::Float:
    case QMetaType::Double:
        if(!value.isDouble()) { ok = false; return {}; }
        return QVariant(value.toDouble());
    case QMetaType::QString:
        if(!value.isString()) { ok = false; return {}; }
        return QVariant(value.toString());
    default:
        break;
    }

    // Enumeration parameters are stored as their own registered type but accept an integer.
    if(value.isDouble()) {
        QVariant converted = QVariant((int)value.toDouble());
        if(converted.convert(QMetaType(targetType)))
            return converted;
        // Assigning the plain integer still works for Q_ENUM fields.
        return QVariant((int)value.toDouble());
    }
    if(value.isString()) {
        QVariant converted = QVariant(value.toString());
        if(converted.convert(QMetaType(targetType)))
            return converted;
    }
    ok = false;
    return {};
}

/******************************************************************************
* Serializes the parameters of an object using its property field reflection.
******************************************************************************/
QJsonObject AgentBridgeServer::serializeParameters(const RefTarget* object) const
{
    QJsonObject result;
    if(!object)
        return result;
    // Walk up the class hierarchy so that inherited parameters are reported as well.
    for(OvitoClassPtr clazz = &object->getOOClass(); clazz != nullptr; clazz = clazz->superClass()) {
        const RefMakerClass* refMakerClass = dynamic_cast<const RefMakerClass*>(clazz);
        if(!refMakerClass)
            continue;
        for(const PropertyFieldDescriptor* field : refMakerClass->propertyFields()) {
            const QString identifier = QString::fromLatin1(field->identifier());
            if(result.contains(identifier))
                continue;
            if(field->isReferenceField()) {
                // Sub-objects are reported by name only: the agent addresses them through
                // their own methods rather than through the parent's parameter list.
                if(field->isVector())
                    continue;
                const RefTarget* target = const_cast<RefMaker*>(static_cast<const RefMaker*>(object))->getReferenceFieldTarget(field);
                QJsonObject reference;
                reference.insert(QStringLiteral("class"), target ? target->getOOClass().name() : QString());
                reference.insert(QStringLiteral("title"), target ? target->objectTitle() : QString());
                result.insert(identifier, reference);
                continue;
            }
            QVariant value = const_cast<RefMaker*>(static_cast<const RefMaker*>(object))->getPropertyFieldValue(field);
            result.insert(identifier, variantToJson(value));
        }
    }
    return result;
}

/******************************************************************************
* Applies a set of parameter values to an object.
******************************************************************************/
QStringList AgentBridgeServer::applyParameters(RefTarget* object, const QJsonObject& values) const
{
    QStringList rejected;
    for(auto iter = values.begin(); iter != values.end(); ++iter) {
        const QByteArray identifier = iter.key().toLatin1();
        const RefMakerClass* refMakerClass = dynamic_cast<const RefMakerClass*>(&object->getOOClass());
        const PropertyFieldDescriptor* field = refMakerClass ? refMakerClass->findPropertyField(identifier.constData(), true) : nullptr;
        if(!field || field->isReferenceField()) {
            rejected.push_back(tr("%1: no such parameter").arg(iter.key()));
            continue;
        }
        QVariant currentValue = object->getPropertyFieldValue(field);
        bool ok = false;
        QVariant newValue = jsonToVariant(iter.value(), currentValue, ok);
        if(!ok) {
            rejected.push_back(tr("%1: expected a value of type %2").arg(iter.key(), QString::fromLatin1(currentValue.typeName())));
            continue;
        }
        object->setPropertyFieldValue(field, newValue);
    }
    return rejected;
}

/******************************************************************************
* Reports basic information about the running OVITO session.
******************************************************************************/
QJsonObject AgentBridgeServer::methodSessionInfo(const QJsonObject& params)
{
    QJsonObject result;
    result.insert(QStringLiteral("application"), QStringLiteral("OVITO"));
    result.insert(QStringLiteral("version"), Application::applicationVersionString());
    result.insert(QStringLiteral("pid"), (double)QCoreApplication::applicationPid());
    result.insert(QStringLiteral("socket"), _socketPath);
    result.insert(QStringLiteral("bridge_api"), 1);
    if(DataSet* dataset = _ui.datasetContainer().currentSet())
        result.insert(QStringLiteral("session_file"), dataset->filePath());
    result.insert(QStringLiteral("note"), tr(
        "This build has no embedded Python interpreter. Scene inspection and modification go "
        "through this bridge API, not through Python code."));
    return result;
}

/******************************************************************************
* Lists the pipelines in the scene.
******************************************************************************/
QJsonObject AgentBridgeServer::methodSceneList(const QJsonObject& params)
{
    Scene* scene = _ui.datasetContainer().activeScene();
    if(!scene)
        throw Exception(tr("There is no active scene."));

    // The selected pipeline is what the user is looking at in the command panel.
    std::set<const Pipeline*> selectedPipelines;
    if(SelectionSet* selection = scene->selection()) {
        for(SceneNode* node : selection->nodes())
            if(node->pipeline()) selectedPipelines.insert(node->pipeline());
    }

    QJsonArray pipelines;
    int index = 0;
    for(SceneNode* node : scene->children()) {
        Pipeline* pipeline = node->pipeline();
        if(!pipeline)
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("index"), index++);
        entry.insert(QStringLiteral("name"), node->sceneNodeName());
        entry.insert(QStringLiteral("title"), pipeline->objectTitle());
        entry.insert(QStringLiteral("selected"), selectedPipelines.count(pipeline) != 0);

        QJsonArray modifiers;
        int modifierIndex = 0;
        PipelineNode* pipelineNode = pipeline->head();
        while(ModificationNode* modNode = dynamic_object_cast<ModificationNode>(pipelineNode)) {
            if(Modifier* modifier = modNode->modifier()) {
                QJsonObject modifierEntry;
                modifierEntry.insert(QStringLiteral("index"), modifierIndex);
                modifierEntry.insert(QStringLiteral("class"), modifier->getOOClass().name());
                modifierEntry.insert(QStringLiteral("title"), modifier->objectTitle());
                modifierEntry.insert(QStringLiteral("enabled"), modifier->isEnabled());
                modifierEntry.insert(QStringLiteral("status"), modNode->status().text());
                modifiers.append(modifierEntry);
            }
            modifierIndex++;
            pipelineNode = modNode->input();
        }
        entry.insert(QStringLiteral("modifiers"), modifiers);
        entry.insert(QStringLiteral("source"), pipelineNode ? pipelineNode->objectTitle() : QString());
        pipelines.append(entry);
    }

    QJsonObject result;
    result.insert(QStringLiteral("pipelines"), pipelines);
    result.insert(QStringLiteral("note"), tr("Modifier index 0 is the topmost modifier, the one applied last."));
    return result;
}

/******************************************************************************
* Describes one pipeline, including the parameters of all its modifiers.
******************************************************************************/
QJsonObject AgentBridgeServer::methodPipelineDescribe(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);

    QJsonArray modifiers;
    int modifierIndex = 0;
    PipelineNode* pipelineNode = pipeline->head();
    while(ModificationNode* modNode = dynamic_object_cast<ModificationNode>(pipelineNode)) {
        if(Modifier* modifier = modNode->modifier()) {
            QJsonObject entry;
            entry.insert(QStringLiteral("index"), modifierIndex);
            entry.insert(QStringLiteral("class"), modifier->getOOClass().name());
            entry.insert(QStringLiteral("title"), modifier->objectTitle());
            entry.insert(QStringLiteral("enabled"), modifier->isEnabled());
            entry.insert(QStringLiteral("status"), modNode->status().text());
            entry.insert(QStringLiteral("params"), serializeParameters(modifier));
            modifiers.append(entry);
        }
        modifierIndex++;
        pipelineNode = modNode->input();
    }

    QJsonObject result;
    result.insert(QStringLiteral("title"), pipeline->objectTitle());
    result.insert(QStringLiteral("modifiers"), modifiers);
    if(pipelineNode) {
        QJsonObject source;
        source.insert(QStringLiteral("class"), pipelineNode->getOOClass().name());
        source.insert(QStringLiteral("title"), pipelineNode->objectTitle());
        source.insert(QStringLiteral("params"), serializeParameters(pipelineNode));
        result.insert(QStringLiteral("source"), source);
    }
    return result;
}

/******************************************************************************
* Makes a pipeline the selected one in the user interface.
******************************************************************************/
QJsonObject AgentBridgeServer::methodPipelineSelect(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    Scene* scene = _ui.datasetContainer().activeScene();
    SceneNode* targetNode = nullptr;
    for(SceneNode* node : scene->children())
        if(node->pipeline() == pipeline) { targetNode = node; break; }
    if(!targetNode)
        throw Exception(tr("The pipeline is not part of the scene."));

    bool success = _ui.performTransaction(tr("Select pipeline"), [&]() {
        scene->selection()->setNode(targetNode);
    });
    if(!success)
        throw Exception(tr("Changing the selection failed."));

    QJsonObject result;
    result.insert(QStringLiteral("selected"), targetNode->sceneNodeName());
    return result;
}

/******************************************************************************
* Deletes a pipeline from the scene.
******************************************************************************/
QJsonObject AgentBridgeServer::methodPipelineDelete(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    Scene* scene = _ui.datasetContainer().activeScene();
    QString name;
    bool success = _ui.performTransaction(tr("Delete pipeline"), [&]() {
        for(SceneNode* node : scene->children().toVector()) {
            if(node->pipeline() == pipeline) {
                name = node->sceneNodeName();
                node->requestObjectDeletion();
                break;
            }
        }
    });
    if(!success)
        throw Exception(tr("Deleting the pipeline failed."));

    QJsonObject result;
    result.insert(QStringLiteral("deleted"), name);
    return result;
}

/******************************************************************************
* Lists the modifier classes available in this OVITO build.
******************************************************************************/
QJsonObject AgentBridgeServer::methodModifierCatalog(const QJsonObject& params)
{
    // Optionally restrict the list to the modifiers that are applicable to the data
    // currently flowing through a pipeline, which is what the modifier list in the GUI shows.
    const DataCollection* inputData = nullptr;
    PipelineFlowState state;
    if(params.value(QStringLiteral("applicable_only")).toBool(false)) {
        state = evaluatePipeline(resolvePipeline(params), params);
        inputData = state.data();
    }
    const bool includeParameters = params.value(QStringLiteral("include_params")).toBool(false);
    const QString filter = params.value(QStringLiteral("filter")).toString();

    QJsonArray modifiers;
    for(OvitoClassPtr clazz : PluginManager::instance().listClasses(Modifier::OOClass())) {
        const ModifierClass* modifierClass = static_cast<const ModifierClass*>(clazz);
        if(inputData && !modifierClass->isApplicableTo(*inputData))
            continue;
        if(!filter.isEmpty() && !clazz->displayName().contains(filter, Qt::CaseInsensitive) && !clazz->name().contains(filter, Qt::CaseInsensitive))
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("class"), clazz->name());
        entry.insert(QStringLiteral("display_name"), clazz->displayName());
        entry.insert(QStringLiteral("category"), modifierClass->modifierCategory());
        entry.insert(QStringLiteral("plugin"), clazz->pluginId());
        if(includeParameters) {
            // Instantiating the modifier is the only way to learn its default parameter
            // values; the instance is discarded right away and never enters the scene.
            OORef<Modifier> instance = static_object_cast<Modifier>(clazz->createInstance());
            entry.insert(QStringLiteral("params"), serializeParameters(instance));
        }
        modifiers.append(entry);
    }

    QJsonObject result;
    result.insert(QStringLiteral("modifiers"), modifiers);
    return result;
}

/******************************************************************************
* Inserts a new modifier at the top of a pipeline.
******************************************************************************/
QJsonObject AgentBridgeServer::methodModifierInsert(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    const QString className = params.value(QStringLiteral("class")).toString();
    if(className.isEmpty())
        throw Exception(tr("Missing parameter 'class': the name of the modifier class to insert."));

    OvitoClassPtr modifierClass = nullptr;
    for(OvitoClassPtr clazz : PluginManager::instance().listClasses(Modifier::OOClass())) {
        if(clazz->name() == className || clazz->displayName().compare(className, Qt::CaseInsensitive) == 0) {
            modifierClass = clazz;
            break;
        }
    }
    if(!modifierClass)
        throw Exception(tr("There is no modifier class named '%1'. Use modifier.catalog to list the available ones.").arg(className));

    AnimationSettings* animationSettings = _ui.datasetContainer().activeAnimationSettings();
    const AnimationTime time = animationSettings ? animationSettings->currentTime() : AnimationTime::fromFrame(0);

    QStringList rejected;
    QString title;
    bool success = _ui.performTransaction(tr("Insert modifier"), [&]() {
        OORef<Modifier> modifier = static_object_cast<Modifier>(modifierClass->createInstance());
        const QJsonObject values = params.value(QStringLiteral("params")).toObject();
        if(!values.isEmpty())
            rejected = applyParameters(modifier, values);
        pipeline->applyModifier(time, true, modifier);
        title = modifier->objectTitle();
    });
    if(!success)
        throw Exception(tr("Inserting the modifier failed."));

    QJsonObject result;
    result.insert(QStringLiteral("inserted"), title);
    result.insert(QStringLiteral("index"), 0);
    if(!rejected.isEmpty())
        result.insert(QStringLiteral("rejected_params"), QJsonArray::fromStringList(rejected));
    result.insert(QStringLiteral("note"), tr("New modifiers are always inserted at the top of the pipeline."));
    return result;
}

/******************************************************************************
* Deletes a modifier from a pipeline.
******************************************************************************/
QJsonObject AgentBridgeServer::methodModifierDelete(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    OORef<ModificationNode> node = resolveModifierNode(pipeline, params.value(QStringLiteral("index")).toInt(0));
    const QString title = node->modifier() ? node->modifier()->objectTitle() : node->objectTitle();

    bool success = _ui.performTransaction(tr("Delete modifier"), [&]() {
        // Unlink the node by making its consumer point at its input, which is how the
        // pipeline editor removes a modifier.
        node->visitDependents([&](RefMaker* dependent) {
            if(ModificationNode* precedingNode = dynamic_object_cast<ModificationNode>(dependent)) {
                if(precedingNode->input() == node)
                    precedingNode->setInput(node->input());
            }
            else if(Pipeline* owner = dynamic_object_cast<Pipeline>(dependent)) {
                if(owner->head() == node)
                    owner->setHead(node->input());
            }
        });
        node->requestObjectDeletion();
    });
    if(!success)
        throw Exception(tr("Deleting the modifier failed."));

    QJsonObject result;
    result.insert(QStringLiteral("deleted"), title);
    return result;
}

/******************************************************************************
* Changes parameters of an existing modifier.
******************************************************************************/
QJsonObject AgentBridgeServer::methodModifierSetParams(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    ModificationNode* node = resolveModifierNode(pipeline, params.value(QStringLiteral("index")).toInt(0));
    Modifier* modifier = node->modifier();
    if(!modifier)
        throw Exception(tr("The pipeline entry at this index holds no modifier."));

    const QJsonObject values = params.value(QStringLiteral("params")).toObject();
    if(values.isEmpty())
        throw Exception(tr("Missing parameter 'params': an object mapping parameter names to new values."));

    QStringList rejected;
    bool success = _ui.performTransaction(tr("Change modifier parameters"), [&]() {
        rejected = applyParameters(modifier, values);
    });
    if(!success)
        throw Exception(tr("Changing the modifier parameters failed."));

    QJsonObject result;
    result.insert(QStringLiteral("modifier"), modifier->objectTitle());
    result.insert(QStringLiteral("params"), serializeParameters(modifier));
    if(!rejected.isEmpty())
        result.insert(QStringLiteral("rejected_params"), QJsonArray::fromStringList(rejected));
    return result;
}

/******************************************************************************
* Enables or disables a modifier.
******************************************************************************/
QJsonObject AgentBridgeServer::methodModifierSetEnabled(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    ModificationNode* node = resolveModifierNode(pipeline, params.value(QStringLiteral("index")).toInt(0));
    Modifier* modifier = node->modifier();
    if(!modifier)
        throw Exception(tr("The pipeline entry at this index holds no modifier."));
    const bool enabled = params.value(QStringLiteral("enabled")).toBool(true);

    bool success = _ui.performTransaction(enabled ? tr("Enable modifier") : tr("Disable modifier"), [&]() {
        modifier->setEnabled(enabled);
    });
    if(!success)
        throw Exception(tr("Changing the modifier state failed."));

    QJsonObject result;
    result.insert(QStringLiteral("modifier"), modifier->objectTitle());
    result.insert(QStringLiteral("enabled"), modifier->isEnabled());
    return result;
}

/******************************************************************************
* Describes the contents of a data collection.
******************************************************************************/
QJsonObject AgentBridgeServer::describeDataCollection(const DataCollection* data) const
{
    QJsonObject result;
    QJsonArray objects;
    QJsonObject attributes;

    if(data) {
        for(const DataObject* object : data->objects()) {
            if(const AttributeDataObject* attribute = dynamic_object_cast<AttributeDataObject>(object)) {
                attributes.insert(attribute->identifier(), variantToJson(attribute->value()));
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("class"), object->getOOClass().name());
            entry.insert(QStringLiteral("title"), object->objectTitle());
            entry.insert(QStringLiteral("identifier"), object->identifier());

            // Property containers -- particles, bonds, data tables, voxel grids and the
            // property-based objects added by this fork -- all describe themselves the same way.
            if(const PropertyContainer* container = dynamic_object_cast<PropertyContainer>(object)) {
                entry.insert(QStringLiteral("element_count"), (double)container->elementCount());
                QJsonArray properties;
                for(const Property* property : container->properties()) {
                    QJsonObject propertyEntry;
                    propertyEntry.insert(QStringLiteral("name"), property->name());
                    propertyEntry.insert(QStringLiteral("components"), (int)property->componentCount());
                    propertyEntry.insert(QStringLiteral("data_type"),
                        property->dataType() == Property::Int32 ? QStringLiteral("int32") :
                        property->dataType() == Property::Int64 ? QStringLiteral("int64") :
                        property->dataType() == Property::Float32 ? QStringLiteral("float32") :
                        property->dataType() == Property::Float64 ? QStringLiteral("float64") : QStringLiteral("other"));
                    properties.append(propertyEntry);
                }
                entry.insert(QStringLiteral("properties"), properties);
            }
            objects.append(entry);
        }
    }

    result.insert(QStringLiteral("objects"), objects);
    result.insert(QStringLiteral("attributes"), attributes);
    return result;
}

/******************************************************************************
* Summarizes the data produced by a pipeline.
******************************************************************************/
QJsonObject AgentBridgeServer::methodDataSummary(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    PipelineFlowState state = evaluatePipeline(pipeline, params);
    QJsonObject result = describeDataCollection(state.data());
    result.insert(QStringLiteral("pipeline"), pipeline->objectTitle());
    result.insert(QStringLiteral("status"), state.status().text());
    return result;
}

/******************************************************************************
* Returns the global attributes produced by a pipeline.
******************************************************************************/
QJsonObject AgentBridgeServer::methodDataAttributes(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    PipelineFlowState state = evaluatePipeline(pipeline, params);
    QJsonObject attributes;
    if(const DataCollection* data = state.data()) {
        for(const DataObject* object : data->objects()) {
            if(const AttributeDataObject* attribute = dynamic_object_cast<AttributeDataObject>(object))
                attributes.insert(attribute->identifier(), variantToJson(attribute->value()));
        }
    }
    QJsonObject result;
    result.insert(QStringLiteral("attributes"), attributes);
    return result;
}

/******************************************************************************
* Reads the values of a property for a range of elements.
******************************************************************************/
static QJsonArray readPropertyValues(const Property* property, size_t firstElement, size_t count)
{
    QJsonArray values;
    const size_t componentCount = property->componentCount();
    for(size_t i = firstElement; i < std::min(firstElement + count, property->size()); i++) {
        if(componentCount == 1) {
            // Scalar properties are reported as plain numbers rather than one-element arrays.
            property->forAnyType([&](auto type) {
                BufferReadAccess<decltype(type)*> accessor(property);
                values.append((double)accessor.get(i, 0));
            });
        }
        else {
            QJsonArray components;
            property->forAnyType([&](auto type) {
                BufferReadAccess<decltype(type)*> accessor(property);
                for(size_t c = 0; c < componentCount; c++)
                    components.append((double)accessor.get(i, c));
            });
            values.append(components);
        }
    }
    return values;
}

/******************************************************************************
* Returns the contents of a data table produced by a pipeline.
******************************************************************************/
QJsonObject AgentBridgeServer::methodDataTable(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    PipelineFlowState state = evaluatePipeline(pipeline, params);
    const DataCollection* data = state.data();
    if(!data)
        throw Exception(tr("The pipeline produced no data."));

    const QString name = params.value(QStringLiteral("name")).toString();
    const DataTable* table = nullptr;
    QStringList availableTables;
    for(const DataObject* object : data->objects()) {
        if(const DataTable* candidate = dynamic_object_cast<DataTable>(object)) {
            availableTables.push_back(candidate->identifier());
            if(name.isEmpty() || candidate->identifier() == name || candidate->objectTitle().compare(name, Qt::CaseInsensitive) == 0) {
                if(!table) table = candidate;
            }
        }
    }
    if(!table) {
        throw Exception(availableTables.isEmpty() ?
            tr("The pipeline output contains no data tables.") :
            tr("There is no data table named '%1'. Available tables: %2").arg(name, availableTables.join(QStringLiteral(", "))));
    }

    const int maxRows = params.value(QStringLiteral("max_rows")).toInt(1000);
    QJsonObject result;
    result.insert(QStringLiteral("identifier"), table->identifier());
    result.insert(QStringLiteral("title"), table->objectTitle());
    result.insert(QStringLiteral("row_count"), (double)table->elementCount());
    result.insert(QStringLiteral("axis_label_x"), table->axisLabelX());
    result.insert(QStringLiteral("axis_label_y"), table->axisLabelY());
    if(ConstPropertyPtr xValues = table->getXValues())
        result.insert(QStringLiteral("x"), readPropertyValues(xValues.get(), 0, maxRows));
    QJsonObject columns;
    for(const Property* property : table->properties())
        columns.insert(property->name(), readPropertyValues(property, 0, maxRows));
    result.insert(QStringLiteral("columns"), columns);
    if((int)table->elementCount() > maxRows)
        result.insert(QStringLiteral("truncated"), true);
    return result;
}

/******************************************************************************
* Reads per-element property values from a property container.
******************************************************************************/
QJsonObject AgentBridgeServer::methodDataElements(const QJsonObject& params)
{
    Pipeline* pipeline = resolvePipeline(params);
    PipelineFlowState state = evaluatePipeline(pipeline, params);
    const DataCollection* data = state.data();
    if(!data)
        throw Exception(tr("The pipeline produced no data."));

    const QString containerName = params.value(QStringLiteral("container")).toString();
    const PropertyContainer* container = nullptr;
    QStringList availableContainers;
    for(const DataObject* object : data->objects()) {
        if(const PropertyContainer* candidate = dynamic_object_cast<PropertyContainer>(object)) {
            availableContainers.push_back(candidate->identifier().isEmpty() ? candidate->getOOClass().name() : candidate->identifier());
            if(containerName.isEmpty() ||
                    candidate->identifier().compare(containerName, Qt::CaseInsensitive) == 0 ||
                    candidate->getOOClass().name().compare(containerName, Qt::CaseInsensitive) == 0 ||
                    candidate->objectTitle().compare(containerName, Qt::CaseInsensitive) == 0) {
                if(!container) container = candidate;
            }
        }
    }
    if(!container) {
        throw Exception(availableContainers.isEmpty() ?
            tr("The pipeline output contains no property containers.") :
            tr("There is no property container named '%1'. Available: %2").arg(containerName, availableContainers.join(QStringLiteral(", "))));
    }

    const size_t start = (size_t)std::max(params.value(QStringLiteral("start")).toInt(0), 0);
    // The cap keeps a careless request from serializing millions of particles into the
    // agent's context window.
    const size_t count = (size_t)std::clamp(params.value(QStringLiteral("count")).toInt(100), 1, 10000);

    QStringList requested;
    for(const QJsonValue& value : params.value(QStringLiteral("properties")).toArray())
        requested.push_back(value.toString());

    QJsonObject values;
    QStringList missing = requested;
    for(const Property* property : container->properties()) {
        if(!requested.isEmpty() && !requested.contains(property->name(), Qt::CaseInsensitive))
            continue;
        missing.removeAll(property->name());
        values.insert(property->name(), readPropertyValues(property, start, count));
    }

    QJsonObject result;
    result.insert(QStringLiteral("container"), container->objectTitle());
    result.insert(QStringLiteral("element_count"), (double)container->elementCount());
    result.insert(QStringLiteral("start"), (double)start);
    result.insert(QStringLiteral("values"), values);
    if(!requested.isEmpty() && !missing.isEmpty())
        result.insert(QStringLiteral("missing_properties"), QJsonArray::fromStringList(missing));
    return result;
}

/******************************************************************************
* Imports a data file into the scene.
******************************************************************************/
QJsonObject AgentBridgeServer::methodFileImport(const QJsonObject& params)
{
    const QString path = params.value(QStringLiteral("path")).toString();
    if(path.isEmpty())
        throw Exception(tr("Missing parameter 'path': the file to import."));
    const QFileInfo fileInfo(path);
    if(!fileInfo.exists())
        throw Exception(tr("The file '%1' does not exist.").arg(path));

    Scene* scene = _ui.datasetContainer().activeScene();
    if(!scene)
        throw Exception(tr("There is no active scene."));

    const QString modeName = params.value(QStringLiteral("mode")).toString(QStringLiteral("add"));
    FileImporter::ImportMode importMode =
        (modeName == QStringLiteral("reset")) ? FileImporter::ResetScene :
        (modeName == QStringLiteral("replace")) ? FileImporter::ReplaceSelected : FileImporter::AddToScene;

    const QUrl url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
    OORef<FileImporter> importer = ProgressDialog::blockForFuture(
        FileImporter::autodetectFileFormat(url), _ui, tr("Detecting file format"));
    if(!importer)
        throw Exception(tr("Could not detect the format of the file '%1'.").arg(path));

    std::vector<std::pair<QUrl, OORef<FileImporter>>> urlImporters;
    urlImporters.emplace_back(url, importer);
    OORef<Pipeline> pipeline = ProgressDialog::blockForFuture(
        importer->importFileSet(scene, std::move(urlImporters), importMode,
            params.value(QStringLiteral("autodetect_sequence")).toBool(true),
            FileImporter::ImportAsTrajectory),
        _ui, tr("Importing data"));

    QJsonObject result;
    result.insert(QStringLiteral("imported"), fileInfo.absoluteFilePath());
    result.insert(QStringLiteral("importer"), importer->getOOClass().displayName());
    result.insert(QStringLiteral("pipeline"), pipeline ? pipeline->objectTitle() : QString());
    if(AnimationSettings* animationSettings = _ui.datasetContainer().activeAnimationSettings())
        result.insert(QStringLiteral("frame_count"), animationSettings->numberOfFrames());
    return result;
}

/******************************************************************************
* Reports the animation interval of the scene.
******************************************************************************/
QJsonObject AgentBridgeServer::methodAnimationInfo(const QJsonObject& params)
{
    AnimationSettings* animationSettings = _ui.datasetContainer().activeAnimationSettings();
    if(!animationSettings)
        throw Exception(tr("The scene has no animation settings."));
    QJsonObject result;
    result.insert(QStringLiteral("current_frame"), animationSettings->currentFrame());
    result.insert(QStringLiteral("first_frame"), animationSettings->firstFrame());
    result.insert(QStringLiteral("last_frame"), animationSettings->lastFrame());
    result.insert(QStringLiteral("frame_count"), animationSettings->numberOfFrames());
    return result;
}

/******************************************************************************
* Jumps to a specific animation frame.
******************************************************************************/
QJsonObject AgentBridgeServer::methodAnimationGoto(const QJsonObject& params)
{
    AnimationSettings* animationSettings = _ui.datasetContainer().activeAnimationSettings();
    if(!animationSettings)
        throw Exception(tr("The scene has no animation settings."));
    if(!params.contains(QStringLiteral("frame")))
        throw Exception(tr("Missing parameter 'frame'."));
    const int frame = std::clamp(params.value(QStringLiteral("frame")).toInt(), animationSettings->firstFrame(), animationSettings->lastFrame());
    animationSettings->setCurrentFrame(frame);

    QJsonObject result;
    result.insert(QStringLiteral("current_frame"), animationSettings->currentFrame());
    return result;
}

/******************************************************************************
* Lists the viewports of the current window.
******************************************************************************/
QJsonObject AgentBridgeServer::methodViewportList(const QJsonObject& params)
{
    ViewportConfiguration* viewportConfig = _ui.datasetContainer().currentSet() ? _ui.datasetContainer().currentSet()->viewportConfig() : nullptr;
    if(!viewportConfig)
        throw Exception(tr("There is no viewport configuration."));

    QJsonArray viewports;
    int index = 0;
    for(Viewport* viewport : viewportConfig->viewports()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("index"), index++);
        entry.insert(QStringLiteral("title"), viewport->viewportTitle());
        entry.insert(QStringLiteral("view_type"), (int)viewport->viewType());
        entry.insert(QStringLiteral("active"), viewport == viewportConfig->activeViewport());
        Point3 position = viewport->cameraPosition();
        Vector3 direction = viewport->cameraDirection();
        entry.insert(QStringLiteral("camera_position"), QJsonArray{ (double)position.x(), (double)position.y(), (double)position.z() });
        entry.insert(QStringLiteral("camera_direction"), QJsonArray{ (double)direction.x(), (double)direction.y(), (double)direction.z() });
        entry.insert(QStringLiteral("field_of_view"), (double)viewport->fieldOfView());
        entry.insert(QStringLiteral("render_preview"), viewport->renderPreviewMode());
        viewports.append(entry);
    }

    QJsonObject result;
    result.insert(QStringLiteral("viewports"), viewports);
    result.insert(QStringLiteral("view_types"), QStringLiteral(
        "0=none, 1=top, 2=bottom, 3=front, 4=back, 5=left, 6=right, 7=ortho, 8=perspective, 9=scene node"));
    return result;
}

/******************************************************************************
* Returns the viewport addressed by a request.
******************************************************************************/
static Viewport* resolveViewport(ViewportConfiguration* viewportConfig, const QJsonValue& selector)
{
    if(selector.isUndefined() || selector.isNull())
        return viewportConfig->activeViewport();
    if(selector.isDouble()) {
        int index = selector.toInt();
        if(index < 0 || index >= viewportConfig->viewports().size())
            throw Exception(AgentBridgeServer::tr("There is no viewport with index %1.").arg(index));
        return viewportConfig->viewports()[index];
    }
    const QString title = selector.toString();
    for(Viewport* viewport : viewportConfig->viewports())
        if(viewport->viewportTitle().compare(title, Qt::CaseInsensitive) == 0)
            return viewport;
    throw Exception(AgentBridgeServer::tr("There is no viewport named '%1'.").arg(title));
}

/******************************************************************************
* Reads or changes the camera of a viewport.
******************************************************************************/
QJsonObject AgentBridgeServer::methodViewportCamera(const QJsonObject& params)
{
    ViewportConfiguration* viewportConfig = _ui.datasetContainer().currentSet() ? _ui.datasetContainer().currentSet()->viewportConfig() : nullptr;
    if(!viewportConfig)
        throw Exception(tr("There is no viewport configuration."));
    Viewport* viewport = resolveViewport(viewportConfig, params.value(QStringLiteral("viewport")));
    if(!viewport)
        throw Exception(tr("There is no active viewport."));

    auto readVector = [&](const QString& key, bool& present) -> std::array<FloatType, 3> {
        QJsonArray array = params.value(key).toArray();
        present = (array.size() == 3);
        if(!present) return { FloatType(0), FloatType(0), FloatType(0) };
        return { (FloatType)array[0].toDouble(), (FloatType)array[1].toDouble(), (FloatType)array[2].toDouble() };
    };

    bool changed = false;
    if(params.contains(QStringLiteral("view_type"))) {
        viewport->setViewType((Viewport::ViewType)params.value(QStringLiteral("view_type")).toInt(), true, true);
        changed = true;
    }
    bool present = false;
    if(auto position = readVector(QStringLiteral("camera_position"), present); present) {
        viewport->setCameraPosition(Point3(position[0], position[1], position[2]));
        changed = true;
    }
    if(auto direction = readVector(QStringLiteral("camera_direction"), present); present) {
        viewport->setCameraDirection(Vector3(direction[0], direction[1], direction[2]));
        changed = true;
    }
    if(params.contains(QStringLiteral("field_of_view"))) {
        viewport->setFieldOfView((FloatType)params.value(QStringLiteral("field_of_view")).toDouble());
        changed = true;
    }
    if(params.contains(QStringLiteral("render_preview"))) {
        viewport->setRenderPreviewMode(params.value(QStringLiteral("render_preview")).toBool());
        changed = true;
    }
    if(params.value(QStringLiteral("zoom_all")).toBool(false)) {
        // Framing the whole scene needs the pipelines to be evaluated first, so this is
        // deferred rather than applied synchronously.
        viewportConfig->zoomToSceneExtentsWhenReady();
        changed = true;
    }

    QJsonObject result;
    result.insert(QStringLiteral("viewport"), viewport->viewportTitle());
    result.insert(QStringLiteral("changed"), changed);
    Point3 position = viewport->cameraPosition();
    Vector3 direction = viewport->cameraDirection();
    result.insert(QStringLiteral("camera_position"), QJsonArray{ (double)position.x(), (double)position.y(), (double)position.z() });
    result.insert(QStringLiteral("camera_direction"), QJsonArray{ (double)direction.x(), (double)direction.y(), (double)direction.z() });
    result.insert(QStringLiteral("field_of_view"), (double)viewport->fieldOfView());
    result.insert(QStringLiteral("view_type"), (int)viewport->viewType());
    return result;
}

/******************************************************************************
* Captures what the user currently sees and writes it to an image file.
******************************************************************************/
QJsonObject AgentBridgeServer::methodViewportScreenshot(const QJsonObject& params)
{
    MainWindow* mainWindow = _ui.mainWindow();
    if(!mainWindow)
        throw Exception(tr("The main window is not available."));

    // Grabbing the on-screen widget, rather than rendering the scene offscreen, is what
    // makes the screenshot show exactly what the user is looking at, overlays included.
    QWidget* widget = nullptr;
    const QJsonValue selector = params.value(QStringLiteral("viewport"));
    if(selector.isString() && selector.toString() == QStringLiteral("window")) {
        widget = mainWindow;
    }
    else if(selector.isString() && selector.toString() == QStringLiteral("all")) {
        widget = mainWindow->viewportsPanel();
    }
    else {
        ViewportConfiguration* viewportConfig = _ui.datasetContainer().currentSet() ? _ui.datasetContainer().currentSet()->viewportConfig() : nullptr;
        if(!viewportConfig)
            throw Exception(tr("There is no viewport configuration."));
        Viewport* viewport = resolveViewport(viewportConfig, selector);
        widget = viewport ? mainWindow->viewportsPanel()->viewportWidget(viewport) : nullptr;
        if(!widget)
            widget = mainWindow->viewportsPanel();
    }
    if(!widget)
        throw Exception(tr("The requested viewport has no window to capture."));

    // Let pending pipeline results reach the screen before the frame is grabbed.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);

    QPixmap pixmap = widget->grab();
    if(pixmap.isNull())
        throw Exception(tr("Capturing the viewport failed."));

    QString path = params.value(QStringLiteral("path")).toString();
    if(path.isEmpty())
        path = QDir(QDir::tempPath()).filePath(QStringLiteral("ovito-agent-screenshot-%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
    if(!pixmap.save(path, "PNG"))
        throw Exception(tr("Could not write the screenshot to '%1'.").arg(path));

    QJsonObject result;
    result.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
    result.insert(QStringLiteral("width"), pixmap.width());
    result.insert(QStringLiteral("height"), pixmap.height());
    return result;
}

}   // End of namespace
