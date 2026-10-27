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
#include <ovito/particles/modifier/analysis/wignerseitz/WignerSeitzAnalysisModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "WignerSeitzSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <random>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(WignerSeitzSelfTestService);

namespace {

constexpr int N = 4;            // fcc unit cells per direction
constexpr double A = 4.08;      // lattice constant
constexpr double JITTER = 0.03 * A;

/// Frame 0 is the ideal lattice. In frame 1 every atom gets a small random displacement and atom 1
/// (at the origin) moves to the octahedral site (A/2, 0, 0), nudged off the six-fold tie.
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(10);
    const double basis[4][3] = {{0, 0, 0}, {0.5, 0.5, 0}, {0.5, 0, 0.5}, {0, 0.5, 0.5}};
    std::mt19937 rng(4242);
    std::uniform_real_distribution<double> jitter(-JITTER, JITTER);
    for(int frame = 0; frame < 2; frame++) {
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n" << 4 * N * N * N << "\n";
        out << "ITEM: BOX BOUNDS pp pp pp\n";
        for(int dim = 0; dim < 3; dim++)
            out << "0 " << N * A << "\n";
        out << "ITEM: ATOMS id type x y z\n";
        int id = 1;
        for(int i = 0; i < N; i++) for(int j = 0; j < N; j++) for(int k = 0; k < N; k++) {
            for(const auto& b : basis) {
                double x = (i + b[0]) * A, y = (j + b[1]) * A, z = (k + b[2]) * A;
                if(frame == 1) {
                    if(id == 1) { x = 0.5 * A + 0.05 * A; y = 0.02 * A; z = 0.01 * A; }
                    else { x += jitter(rng); y += jitter(rng); z += jitter(rng); }
                }
                out << id++ << " 1 " << x << " " << y << " " << z << "\n";
            }
        }
    }
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

/// Evaluates the Wigner-Seitz modifier on the given frame (reference = frame 0) and checks the defect counts.
bool checkCase(const QUrl& url, bool adaptive, int frame, qlonglong expectedDefects)
{
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<WignerSeitzAnalysisModifier> ws = OORef<WignerSeitzAnalysisModifier>::create();
    ws->setAdaptiveCell(adaptive);
    ws->setStrainSensitivity(1.0);
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, ws);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    const QVariant vacancies = state.getAttributeValue(node, QStringLiteral("WignerSeitz.vacancy_count"));
    const QVariant interstitials = state.getAttributeValue(node, QStringLiteral("WignerSeitz.interstitial_count"));
    if(!vacancies.isValid() || !interstitials.isValid())
        throw Exception(QStringLiteral("The Wigner-Seitz attributes are missing from the pipeline output: %1").arg(state.status().text()));

    const QString label = QStringLiteral("%1 cell, frame %2").arg(adaptive ? "adaptive" : "standard").arg(frame);
    bool ok = true;
    ok &= check(label + QStringLiteral(" vacancies"), vacancies.toLongLong() == expectedDefects,
        QStringLiteral("%1 (expected %2)").arg(vacancies.toLongLong()).arg(expectedDefects));
    ok &= check(label + QStringLiteral(" interstitials"), interstitials.toLongLong() == expectedDefects,
        QStringLiteral("%1 (expected %2)").arg(interstitials.toLongLong()).arg(expectedDefects));
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void WignerSeitzSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("wigner-seitz-selftest")},
        tr("Run the in-process regression test for the Wigner-Seitz defect analysis modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void WignerSeitzSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("wigner-seitz-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Wigner-Seitz self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Wigner-Seitz self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect here; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Wigner-Seitz self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool WignerSeitzSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("wigner_seitz_test.dump"));
    writeDump(dumpPath);
    const QUrl url = QUrl::fromLocalFile(dumpPath);

    bool ok = true;
    for(bool adaptive : {false, true}) {
        ok &= checkCase(url, adaptive, 0, 0);
        ok &= checkCase(url, adaptive, 1, 1);
    }

    qInfo().noquote() << (ok ? "Wigner-Seitz self-test: ALL PASSED" : "Wigner-Seitz self-test: FAILED");
    return ok;
}

}   // End of namespace
