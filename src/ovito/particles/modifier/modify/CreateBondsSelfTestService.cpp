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
#include <ovito/particles/modifier/modify/CreateBondsModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "CreateBondsSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CreateBondsSelfTestService);

namespace {

/// A bond as (lower index, higher index, periodic image seen from the lower index).
using BondKey = std::array<int64_t, 5>;

/// Bonds created by OVITO Pro 3.16.1 (CreateBondsModifier, mode CovalentRadius) for the file of writeRandomDataFile().
const std::vector<BondKey> proCovalentBonds = {
    {0,13,0,0,0},{1,78,0,0,0},{2,23,0,0,0},{3,23,0,0,0},{3,27,0,0,0},{4,37,0,0,-1},{4,62,0,0,0},{5,7,0,0,0},{5,29,0,0,0},
    {6,48,0,0,0},{6,75,0,0,0},{7,44,0,0,0},{7,65,0,0,0},{7,66,0,0,0},{10,24,0,0,0},{11,62,0,0,1},{13,20,0,0,0},{13,75,0,0,0},
    {14,21,0,0,0},{15,61,0,0,0},{15,70,0,0,0},{17,60,0,0,0},{18,49,0,0,0},{19,55,0,0,0},{20,62,-1,0,0},{22,40,0,0,0},
    {22,67,0,0,0},{24,40,0,0,0},{24,46,0,0,0},{24,64,0,0,0},{25,49,0,0,0},{25,73,0,0,0},{26,75,1,0,0},{27,67,1,0,0},
    {28,32,0,0,0},{28,69,0,0,0},{31,33,0,0,0},{31,41,0,0,0},{31,78,0,0,0},{32,69,0,0,0},{32,78,0,0,0},{32,79,0,0,0},
    {33,69,0,0,0},{34,43,0,0,0},{36,52,0,0,0},{36,74,0,0,0},{37,43,0,0,0},{37,45,0,0,0},{37,62,0,0,1},{38,44,0,0,0},
    {39,51,0,0,0},{39,65,0,0,0},{39,66,0,0,0},{40,46,0,0,0},{40,64,0,0,0},{41,51,0,0,0},{41,72,0,0,0},{42,55,0,0,0},
    {43,45,0,0,0},{43,58,0,0,0},{45,58,0,0,0},{48,67,0,0,0},{48,75,0,0,0},{48,76,0,0,0},{49,56,0,0,0},{49,73,0,0,0},
    {51,65,0,0,0},{51,66,0,0,0},{52,74,0,0,0},{55,74,0,0,0},{56,73,0,0,0},{60,68,-1,0,0},{60,70,-1,-1,0},{60,72,0,0,0},
    {61,63,1,1,0},{61,70,0,0,0},{65,66,0,0,0},{67,76,0,0,0},{68,71,0,0,0},{70,72,1,1,0},{78,79,0,0,0}};

/// 80 atoms of ten species from a 64-bit LCG, uniform in a periodic 9^3 box.
/// The same generator in Python produced the input of the OVITO Pro reference run.
void writeRandomDataFile(const QString& path)
{
    uint64_t state = 20261102;
    auto next = [&]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double)(state >> 11) / (double)(1ULL << 53);
    };
    const char* elements[] = {"C", "H", "N", "O", "S", "P", "B", "Al", "Si", "He"};
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    out << "Create bonds self-test\n\n80 atoms\n10 atom types\n\n0 9.0 xlo xhi\n0 9.0 ylo yhi\n0 9.0 zlo zhi\n\nMasses\n\n";
    for(int t = 0; t < 10; t++)
        out << (t + 1) << " 1.0 # " << elements[t] << "\n";
    out << "\nAtoms # atomic\n\n";
    for(int i = 1; i <= 80; i++) {
        const int type = (int)(next() * 10) + 1;
        const double x = 9.0 * next(), y = 9.0 * next(), z = 9.0 * next();
        out << QString::asprintf("%d %d %.10f %.10f %.10f\n", i, type, x, y, z);
    }
}

/// Two atoms of the given type names at distance d along x in a non-periodic box.
void writePairDataFile(const QString& path, const char* nameA, const char* nameB, double d)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    out << "Create bonds pair test\n\n2 atoms\n2 atom types\n\n0 30 xlo xhi\n0 30 ylo yhi\n0 30 zlo zhi\n\nMasses\n\n"
        << "1 1.0 # " << nameA << "\n2 1.0 # " << nameB << "\n\nAtoms # atomic\n\n"
        << QString::asprintf("1 1 10 10 10\n2 2 %.10f 10 10\n", 10.0 + d);
}

OORef<Pipeline> makePipeline(const QString& path)
{
    const QUrl url = QUrl::fromLocalFile(path);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test data file."));
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    return pipeline;
}

/// Creates a Create bonds modifier with all memorized fields set explicitly, so that GUI settings do not leak in.
OORef<CreateBondsModifier> createBonds(CreateBondsModifier::CutoffMode mode, bool discard)
{
    OORef<CreateBondsModifier> modifier = OORef<CreateBondsModifier>::create();
    modifier->setCutoffMode(mode);
    modifier->setUniformCutoff(1.0);
    modifier->setDiscardExistingBonds(discard);
    modifier->setOnlyIntraMoleculeBonds(false);
    return modifier;
}

void appendModifier(Pipeline* pipeline, OORef<CreateBondsModifier> modifier)
{
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, modifier);
}

PipelineFlowState evaluate(Pipeline* pipeline)
{
    return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
}

std::vector<BondKey> bondList(const PipelineFlowState& state)
{
    const Particles* particles = state.getObject<Particles>();
    if(!particles || !particles->bonds())
        throw Exception(QStringLiteral("No bonds in the pipeline output: %1").arg(state.status().text()));
    BufferReadAccess<ParticleIndexPair> topology(particles->bonds()->expectProperty(Bonds::TopologyProperty));
    BufferReadAccess<Vector3I> images(particles->bonds()->getProperty(Bonds::PeriodicImageProperty));
    std::vector<BondKey> list;
    for(size_t i = 0; i < topology.size(); i++) {
        int64_t a = topology[i][0], b = topology[i][1];
        Vector3I image = images ? images[i] : Vector3I::Zero();
        if(a > b) {
            std::swap(a, b);
            image = -image;
        }
        list.push_back({a, b, image.x(), image.y(), image.z()});
    }
    std::sort(list.begin(), list.end());
    return list;
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void CreateBondsSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("create-bonds-selftest")},
        tr("Run the in-process regression test for the covalent radius mode of the Create bonds modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void CreateBondsSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("create-bonds-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Create bonds self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Create bonds self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Create bonds self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool CreateBondsSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString randomPath = dir.filePath(QStringLiteral("random.data"));
    writeRandomDataFile(randomPath);

    bool ok = true;

    qInfo().noquote() << "Create bonds self-test: covalent radii, 80 random atoms (168 candidates pruned to 81 bonds)";
    {
        OORef<Pipeline> pipeline = makePipeline(randomPath);
        appendModifier(pipeline, createBonds(CreateBondsModifier::CovalentRadiusCutoff, false));
        PipelineFlowState state = evaluate(pipeline);
        std::vector<BondKey> bonds = bondList(state);
        size_t mismatches = 0;
        for(const BondKey& b : bonds)
            if(!std::binary_search(proCovalentBonds.begin(), proCovalentBonds.end(), b)) mismatches++;
        ok &= check(QStringLiteral("bond list identical to OVITO Pro"), bonds == proCovalentBonds,
            QStringLiteral("%1 bonds (Pro: %2), %3 not in Pro's list").arg(bonds.size()).arg(proCovalentBonds.size()).arg(mismatches));
        ok &= check(QStringLiteral("CreateBonds.num_bonds"), state.getAttributeValue(QStringLiteral("CreateBonds.num_bonds")).toLongLong() == 81,
            state.getAttributeValue(QStringLiteral("CreateBonds.num_bonds")).toString());
    }

    qInfo().noquote() << "Create bonds self-test: discard existing bonds (uniform 1.0 pass first; Pro: 88 kept, 81 discarded)";
    for(bool discard : {false, true}) {
        OORef<Pipeline> pipeline = makePipeline(randomPath);
        appendModifier(pipeline, createBonds(CreateBondsModifier::UniformCutoff, false));
        appendModifier(pipeline, createBonds(CreateBondsModifier::CovalentRadiusCutoff, discard));
        std::vector<BondKey> bonds = bondList(evaluate(pipeline));
        if(discard)
            ok &= check(QStringLiteral("discard = true"), bonds == proCovalentBonds, QStringLiteral("%1 bonds").arg(bonds.size()));
        else
            ok &= check(QStringLiteral("discard = false"), bonds.size() == 88, QStringLiteral("%1 bonds").arg(bonds.size()));
    }

    qInfo().noquote() << "Create bonds self-test: atom pairs (Pro: He-He 0.3 bonded, He-H 1.0 not, C-C 1.5 bonded, Foo = error)";
    struct PairCase { const char* a; const char* b; double d; int expected; };   // expected = -1: error
    for(const PairCase& c : {PairCase{"He", "He", 0.3, 1}, PairCase{"He", "H", 1.0, 0}, PairCase{"C", "C", 1.5, 1}, PairCase{"Foo", "C", 1.0, -1}}) {
        const QString path = dir.filePath(QStringLiteral("pair.data"));
        writePairDataFile(path, c.a, c.b, c.d);
        OORef<Pipeline> pipeline = makePipeline(path);
        appendModifier(pipeline, createBonds(CreateBondsModifier::CovalentRadiusCutoff, false));
        const QString name = QStringLiteral("%1-%2 at %3").arg(c.a).arg(c.b).arg(c.d);
        if(c.expected < 0) {
            QString message;
            try { evaluate(pipeline); }
            catch(const Exception& ex) { message = ex.messages().join(' '); }
            ok &= check(name, message.contains(QStringLiteral("Unknown particle type 'Foo'")), message.isEmpty() ? QStringLiteral("no error") : message);
        }
        else {
            PipelineFlowState state = evaluate(pipeline);
            const Particles* particles = state.getObject<Particles>();
            const size_t count = (particles && particles->bonds()) ? particles->bonds()->elementCount() : 0;
            ok &= check(name, count == (size_t)c.expected, QStringLiteral("%1 bonds").arg(count));
        }
    }

    qInfo().noquote() << (ok ? "Create bonds self-test: ALL PASSED" : "Create bonds self-test: FAILURES");
    return ok;
}

}   // End of namespace
