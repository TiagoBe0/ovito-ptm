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
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/pipeline/StaticSource.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/scene/Scene.h>
#include <ovito/core/dataset/scene/SceneNode.h>
#include <ovito/core/dataset/scene/SelectionSet.h>
#include <ovito/core/utilities/Exception.h>
#include "AgentBridgeServer.h"
#include "AgentBridgeSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonValue>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AgentBridgeSelfTestService);

namespace {

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

/// Adds a pipeline with a static source to the scene. An empty name leaves the node unnamed, so that its
/// title falls back to the title of the pipeline's source, as for an imported dump that was never renamed.
Pipeline* addPipeline(Scene* scene, const QString& name)
{
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(OORef<StaticSource>::create());
    OORef<SceneNode> node = OORef<SceneNode>::create();
    node->setPipeline(pipeline);
    if(!name.isEmpty())
        node->setSceneNodeName(name);
    scene->addChildNode(node);
    return pipeline.get(); // Kept alive by the scene node.
}

/// Calls selectPipeline() and returns the pipeline, or nullptr and the error message if it threw.
Pipeline* trySelect(Scene* scene, const QJsonValue& selector, QString& error)
{
    error.clear();
    try {
        return AgentBridgeServer::selectPipeline(scene, selector);
    }
    catch(const Exception& ex) {
        error = ex.messages().join(QStringLiteral(" "));
        return nullptr;
    }
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void AgentBridgeSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("agent-bridge-selftest")},
        tr("Run the in-process regression test for the pipeline selection of the AI agent bridge and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void AgentBridgeSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("agent-bridge-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Agent bridge self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Agent bridge self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Agent bridge self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool AgentBridgeSelfTestService::runTest()
{
    bool ok = true;
    QString error;
    qInfo().noquote() << "Agent bridge self-test: pipeline selection in a scene with several pipelines";

    // An empty scene has nothing to address.
    {
        OORef<Scene> scene = OORef<Scene>::create();
        Pipeline* p = trySelect(scene, QJsonValue(), error);
        ok &= check(QStringLiteral("empty scene: error"), !p && !error.isEmpty(), error);
    }

    // Scene: [0] "fcc", [1] "bcc", [2] "fcc" (a clone that kept its name),
    // [3] and [4] unnamed (two dumps whose source has the same title).
    OORef<Scene> scene = OORef<Scene>::create();
    Pipeline* fcc0 = addPipeline(scene, QStringLiteral("fcc"));
    Pipeline* bcc = addPipeline(scene, QStringLiteral("bcc"));
    Pipeline* fcc2 = addPipeline(scene, QStringLiteral("fcc"));
    Pipeline* unnamed3 = addPipeline(scene, QString());
    Pipeline* unnamed4 = addPipeline(scene, QString());
    const QString sourceTitle = unnamed3->objectTitle();
    (void)unnamed4;

    // Index in scene order.
    {
        Pipeline* p = trySelect(scene, QJsonValue(1), error);
        ok &= check(QStringLiteral("index 1 -> second pipeline"), p == bcc, error);
        p = trySelect(scene, QJsonValue(5), error);
        ok &= check(QStringLiteral("index out of range: error"), !p && !error.isEmpty(), error);
        p = trySelect(scene, QJsonValue(-1), error);
        ok &= check(QStringLiteral("negative index: error"), !p && !error.isEmpty(), error);
        // An index still addresses one of several pipelines that share a name.
        p = trySelect(scene, QJsonValue(2), error);
        ok &= check(QStringLiteral("index 2 -> duplicate-named pipeline"), p == fcc2, error);
    }

    // Unique name.
    {
        Pipeline* p = trySelect(scene, QJsonValue(QStringLiteral("bcc")), error);
        ok &= check(QStringLiteral("unique name 'bcc'"), p == bcc, error);
        p = trySelect(scene, QJsonValue(QStringLiteral("hcp")), error);
        ok &= check(QStringLiteral("unknown name: error"), !p && !error.isEmpty(), error);
    }

    // A pipeline whose user name equals its own source title matches once, not twice.
    {
        OORef<Scene> single = OORef<Scene>::create();
        Pipeline* only = addPipeline(single, sourceTitle);
        Pipeline* p = trySelect(single, QJsonValue(sourceTitle), error);
        ok &= check(QStringLiteral("name equal to own source title: one match"), p == only, error);
    }

    // Ambiguous names: refuse instead of silently taking the first one.
    {
        Pipeline* p = trySelect(scene, QJsonValue(QStringLiteral("fcc")), error);
        ok &= check(QStringLiteral("user name shared by two pipelines: error"), !p && !error.isEmpty(),
            p ? QStringLiteral("picked pipeline %1").arg(p == fcc0 ? 0 : -1) : error);
        ok &= check(QStringLiteral("ambiguity error names the matching indices"),
            error.contains(QStringLiteral("indices 0, 2")), error);
    }

    // Two unnamed pipelines (the same dump loaded twice): their source title is ambiguous.
    {
        OORef<Scene> twoDumps = OORef<Scene>::create();
        addPipeline(twoDumps, QString());
        addPipeline(twoDumps, QString());
        Pipeline* p = trySelect(twoDumps, QJsonValue(sourceTitle), error);
        ok &= check(QStringLiteral("source title shared by two unnamed pipelines: error"),
            !p && error.contains(QStringLiteral("indices 0, 1")), p ? QStringLiteral("picked a pipeline") : error);
    }

    // Default: the selected pipeline, then the first one.
    {
        Pipeline* p = trySelect(scene, QJsonValue(), error);
        ok &= check(QStringLiteral("no selector, nothing selected -> first"), p == fcc0, error);
        for(SceneNode* n : scene->children())
            if(n->pipeline() == bcc) scene->selection()->setNode(n);
        p = trySelect(scene, QJsonValue(), error);
        ok &= check(QStringLiteral("no selector -> selected pipeline"), p == bcc, error);
    }

    qInfo().noquote() << (ok ? "Agent bridge self-test: PASSED" : "Agent bridge self-test: FAILED");
    return ok;
}

}   // End of namespace
