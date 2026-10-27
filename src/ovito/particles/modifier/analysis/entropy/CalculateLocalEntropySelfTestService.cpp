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


#include <ovito/particles/Particles.h>
#include <ovito/particles/modifier/analysis/entropy/CalculateLocalEntropyModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "CalculateLocalEntropySelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CalculateLocalEntropySelfTestService);

namespace {

constexpr double A = 4.08;
constexpr int N = 4;
constexpr double TOLERANCE = 1e-6;

/// Frame 0: ideal fcc. Frame 1: atom id gets (0.05 sin(1.3 id), 0.05 sin(2.1 id + 0.5), 0.05 cos(0.7 id)).
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(15);
    const double basis[4][3] = {{0, 0, 0}, {0.5, 0.5, 0}, {0.5, 0, 0.5}, {0, 0.5, 0.5}};
    for(int frame = 0; frame < 2; frame++) {
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n" << 4 * N * N * N << "\n";
        out << "ITEM: BOX BOUNDS pp pp pp\n" << QStringLiteral("0 %1\n").arg(N * A, 0, 'g', 15).repeated(3);
        out << "ITEM: ATOMS id type x y z\n";
        int id = 1;
        for(int i = 0; i < N; i++) for(int j = 0; j < N; j++) for(int k = 0; k < N; k++) for(const auto& b : basis) {
            double x = (i + b[0]) * A, y = (j + b[1]) * A, z = (k + b[2]) * A;
            if(frame == 1) {
                x += 0.05 * std::sin(1.3 * id);
                y += 0.05 * std::sin(2.1 * id + 0.5);
                z += 0.05 * std::cos(0.7 * id);
            }
            out << id++ << " 1 " << x << " " << y << " " << z << "\n";
        }
    }
}

bool check(const QString& name, double actual, double expected)
{
    const bool ok = std::abs(actual - expected) <= TOLERANCE * std::max(1.0, std::abs(expected));
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << QString::number(actual, 'g', 12);
    else
        qCritical().noquote() << "  FAIL" << name << ": got" << QString::number(actual, 'g', 12) << "expected" << QString::number(expected, 'g', 12);
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void CalculateLocalEntropySelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("local-entropy-selftest")},
        tr("Run the in-process regression test for the Calculate local entropy modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void CalculateLocalEntropySelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("local-entropy-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Local entropy self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Local entropy self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Local entropy self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool CalculateLocalEntropySelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("local_entropy_test.dump"));
    writeDump(dumpPath);

    QUrl url = QUrl::fromLocalFile(dumpPath);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);

    OORef<CalculateLocalEntropyModifier> entropy = OORef<CalculateLocalEntropyModifier>::create();
    entropy->setCutoff(5.3);
    entropy->setSigma(0.2);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, entropy);

    // Returns the entropy values ordered by particle id.
    auto evaluate = [&](int frame) {
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
        const Particles* particles = state.getObject<Particles>();
        const Property* property = particles ? particles->getProperty(QStringLiteral("Entropy")) : nullptr;
        if(!property)
            throw Exception(QStringLiteral("The Entropy property is missing from the pipeline output: %1").arg(state.status().text()));
        BufferReadAccess<FloatType> values(property);
        BufferReadAccess<IdentifierIntType> ids(particles->expectProperty(Particles::IdentifierProperty));
        std::vector<double> byId(values.size());
        for(size_t i = 0; i < values.size(); i++)
            byId[ids[i] - 1] = values[i];
        return byId;
    };
    auto spread = [](const std::vector<double>& v) { auto [lo, hi] = std::ranges::minmax(v); return hi - lo; };
    auto sum = [](const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0); };

    // Expected values measured with OVITO Pro 3.16.1 (Calculate local entropy) on the same dump.
    bool ok = true;

    qInfo().noquote() << "Local entropy self-test: ideal fcc, global density";
    std::vector<double> s = evaluate(0);
    ok &= check(QStringLiteral("atom 1"), s[0], -8.556850321982283);
    ok &= check(QStringLiteral("spread over atoms"), spread(s), 0);

    qInfo().noquote() << "Local entropy self-test: ideal fcc, local density";
    entropy->setUseLocalDensity(true);
    s = evaluate(0);
    ok &= check(QStringLiteral("atom 1"), s[0], -8.509461565970561);
    ok &= check(QStringLiteral("spread over atoms"), spread(s), 0);

    qInfo().noquote() << "Local entropy self-test: displaced fcc, global density";
    entropy->setUseLocalDensity(false);
    s = evaluate(1);
    ok &= check(QStringLiteral("sum"), sum(s), -2076.0219615823294);
    ok &= check(QStringLiteral("atom 1"), s[0], -8.105013805586802);
    ok &= check(QStringLiteral("atom 50"), s[49], -7.96212737930723);
    ok &= check(QStringLiteral("atom 200"), s[199], -8.312648536840088);

    qInfo().noquote() << "Local entropy self-test: displaced fcc, local density, average within 3.5";
    entropy->setUseLocalDensity(true);
    entropy->setComputeAverage(true);
    entropy->setAverageCutoff(3.5);
    s = evaluate(1);
    ok &= check(QStringLiteral("sum"), sum(s), -2066.3132075454505);
    ok &= check(QStringLiteral("atom 1"), s[0], -8.058364663061672);
    ok &= check(QStringLiteral("atom 50"), s[49], -8.105867126347809);
    ok &= check(QStringLiteral("atom 200"), s[199], -8.051878638175898);

    qInfo().noquote() << (ok ? "Local entropy self-test: ALL PASSED" : "Local entropy self-test: FAILED");
    return ok;
}

}   // End of namespace
