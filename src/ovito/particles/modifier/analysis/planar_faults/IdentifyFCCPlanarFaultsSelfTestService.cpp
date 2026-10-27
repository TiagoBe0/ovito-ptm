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
#include <ovito/particles/modifier/analysis/planar_faults/IdentifyFCCPlanarFaultsModifier.h>
#include <ovito/particles/modifier/analysis/ptm/PolyhedralTemplateMatchingModifier.h>
#include <ovito/particles/modifier/analysis/cna/CommonNeighborAnalysisModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "IdentifyFCCPlanarFaultsSelfTestService.h"
#include <QCommandLineParser>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <random>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(IdentifyFCCPlanarFaultsSelfTestService);

namespace {

constexpr const char* STACKING = "ABCABCACBACBABACBACACABC";
constexpr int NUM_LAYERS = 24;
constexpr int NX = 6;           // atoms per row
constexpr int NY = 4;           // pairs of rows (the rectangular in-plane cell holds 2 rows per a*sqrt(3))
constexpr double D = 2.88;      // nearest-neighbor distance
constexpr double JITTER = 0.04 * D;

double layerSpacing() { return D * std::sqrt(2.0 / 3.0); }
double cellX() { return NX * D; }
double cellY() { return NY * D * std::sqrt(3.0); }

/// Expected fault type of each layer: a layer is hcp-like iff its two neighbor layers carry the same letter.
int expectedType(int layer)
{
    switch(layer) {
        case 6: return IdentifyFCCPlanarFaultsModifier::CoherentTwinBoundary;
        case 12: case 13: return IdentifyFCCPlanarFaultsModifier::IntrinsicStackingFault;
        case 18: case 19: case 20: return IdentifyFCCPlanarFaultsModifier::MultiLayerStackingFault;
        default: return IdentifyFCCPlanarFaultsModifier::NonHCP;
    }
}

/// Frame 0 is the ideal lattice, frame 1 has uniform random displacements of up to JITTER per component.
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(10);
    const int perLayer = NX * 2 * NY;
    const double h = layerSpacing();
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> jitter(-JITTER, JITTER);
    for(int frame = 0; frame < 2; frame++) {
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n" << perLayer * NUM_LAYERS << "\n";
        out << "ITEM: BOX BOUNDS pp pp pp\n0 " << cellX() << "\n0 " << cellY() << "\n0 " << NUM_LAYERS * h << "\n";
        out << "ITEM: ATOMS id type x y z\n";
        int id = 1;
        for(int layer = 0; layer < NUM_LAYERS; layer++) {
            // Triangular lattice a1 = (D, 0), a2 = (D/2, D*sqrt(3)/2); B and C are shifted by (a1+a2)/3 and 2(a1+a2)/3.
            const int shift = STACKING[layer] - 'A';
            const double ox = shift * D / 2, oy = shift * D * std::sqrt(3.0) / 6;
            for(int j = 0; j < 2 * NY; j++) {
                for(int i = 0; i < NX; i++) {
                    double x = std::fmod(i * D + j * D / 2 + ox, cellX());
                    double y = j * D * std::sqrt(3.0) / 2 + oy;
                    double z = layer * h;
                    if(frame == 1) { x += jitter(rng); y += jitter(rng); z += jitter(rng); }
                    out << id++ << " 1 " << x << " " << y << " " << z << "\n";
                }
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

/// Evaluates the pipeline with the given structure identification modifier and checks the classification.
bool checkCase(const QString& label, const QUrl& url, OORef<Modifier> structureModifier, int frame)
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
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, structureModifier);
    OORef<IdentifyFCCPlanarFaultsModifier> faults = OORef<IdentifyFCCPlanarFaultsModifier>::create();
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, faults);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    const Particles* particles = state.getObject<Particles>();
    const Property* faultProperty = particles ? particles->getProperty(QStringLiteral("Planar Fault Type")) : nullptr;
    if(!faultProperty)
        throw Exception(QStringLiteral("The Planar Fault Type property is missing from the pipeline output: %1").arg(state.status().text()));

    qInfo().noquote() << "Planar faults self-test:" << label;
    bool ok = true;

    // Every atom must carry the type of the layer it was generated in (ids are assigned layer by layer).
    BufferReadAccess<int32_t> types(faultProperty);
    BufferReadAccess<IdentifierIntType> ids(particles->expectProperty(Particles::IdentifierProperty));
    const int perLayer = NX * 2 * NY;
    std::array<int64_t, IdentifyFCCPlanarFaultsModifier::NUM_FAULT_TYPES> counts{};
    size_t mismatches = 0;
    QString firstMismatch;
    for(size_t i = 0; i < types.size(); i++) {
        const int layer = (ids[i] - 1) / perLayer;
        counts[types[i]]++;
        if(types[i] != expectedType(layer)) {
            if(mismatches++ == 0)
                firstMismatch = QStringLiteral(", first: id %1 in layer %2 has type %3, expected %4").arg(ids[i]).arg(layer).arg(types[i]).arg(expectedType(layer));
        }
    }
    ok &= check(QStringLiteral("per-layer type"), mismatches == 0,
        QStringLiteral("%1 mismatches; counts %2 %3 %4 %5 %6%7").arg(mismatches)
            .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3]).arg(counts[4]).arg(firstMismatch));

    // Output table as in OVITO Pro: the non-hcp row is 0 and the fractions are relative to the hcp-like atoms
    // (2 ISF layers, 1 TB layer and 3 multilayer layers of the 6 hcp-like layers).
    const DataTable* table = state.getObjectBy<DataTable>(node, QStringLiteral("planar_faults"));
    const Property* countColumn = table ? table->getProperty(QStringLiteral("Atom Count")) : nullptr;
    const Property* fractionColumn = table ? table->getProperty(QStringLiteral("Atom Fraction")) : nullptr;
    if(!countColumn || !fractionColumn)
        throw Exception(QStringLiteral("The planar_faults table or its Atom Count / Atom Fraction columns are missing."));
    std::vector<double> tableCounts(countColumn->size()), fractions(fractionColumn->size());
    countColumn->copyTo(tableCounts.begin());
    fractionColumn->copyTo(fractions.begin());
    const std::vector<double> expectedFractions = {0, 0, 2.0 / 6, 1.0 / 6, 3.0 / 6};
    bool tableOk = tableCounts.size() == 5 && tableCounts[0] == 0 && fractions.size() == 5;
    for(size_t t = 0; tableOk && t < 5; t++)
        tableOk = std::abs(fractions[t] - expectedFractions[t]) < 1e-12;
    ok &= check(QStringLiteral("planar_faults table"), tableOk, QStringLiteral("fractions %1").arg([&] { QStringList l; for(double f : fractions) l << QString::number(f); return l.join(' '); }()));

    // One ISF and one TB, each spanning the full cell cross-section.
    const double crossSection = cellX() * cellY();
    const double tolerance = (frame == 0) ? 1e-6 : 0.02;
    for(const auto& [name, attr] : {std::pair{QStringLiteral("ISF area"), QStringLiteral("PlanarFaults.isf_area")},
                                    std::pair{QStringLiteral("TB area"), QStringLiteral("PlanarFaults.tb_area")}}) {
        double area = state.getAttributeValue(node, attr).toDouble();
        ok &= check(name, std::abs(area / crossSection - 1) < tolerance,
            QStringLiteral("%1 (expected %2, tolerance %3)").arg(area).arg(crossSection).arg(tolerance));
    }
    return ok;
}

/// Compares the per-atom classification with a reference computed by OVITO Pro (PTM + Identify fcc planar faults),
/// stored in the "ProFaultType" column of a LAMMPS dump. Atoms are matched by their identifiers.
bool checkReference(const QString& path)
{
    const QUrl url = QUrl::fromLocalFile(path);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the reference file %1.").arg(path));
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<PolyhedralTemplateMatchingModifier> ptm = OORef<PolyhedralTemplateMatchingModifier>::create();
    ptm->setOutputOrientation(true);  // As required by OVITO Pro.
    OORef<IdentifyFCCPlanarFaultsModifier> faults = OORef<IdentifyFCCPlanarFaultsModifier>::create();
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, ptm);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, faults);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    const Particles* particles = state.getObject<Particles>();
    const Property* faultProperty = particles ? particles->getProperty(QStringLiteral("Planar Fault Type")) : nullptr;
    // The LAMMPS dump importer lowercases the names of non-standard columns.
    const Property* referenceProperty = particles ? particles->getProperty(QStringLiteral("profaulttype")) : nullptr;
    if(!faultProperty || !referenceProperty)
        throw Exception(QStringLiteral("Missing Planar Fault Type or reference ProFaultType column in %1: %2").arg(path).arg(state.status().text()));

    std::vector<int> actual(faultProperty->size()), expected(referenceProperty->size());
    faultProperty->copyTo(actual.begin());
    referenceProperty->copyTo(expected.begin());
    constexpr int N = IdentifyFCCPlanarFaultsModifier::NUM_FAULT_TYPES;
    std::array<std::array<int64_t, N>, N> confusion{};
    size_t mismatches = 0;
    for(size_t i = 0; i < actual.size(); i++) {
        if(actual[i] < 0 || actual[i] >= N || expected[i] < 0 || expected[i] >= N)
            throw Exception(QStringLiteral("Invalid planar fault type in %1.").arg(path));
        confusion[expected[i]][actual[i]]++;
        if(actual[i] != expected[i]) mismatches++;
    }
    QStringList rows;
    for(int e = 0; e < N; e++) {
        QStringList row;
        for(int a = 0; a < N; a++) row << QString::number(confusion[e][a]);
        rows << row.join(' ');
    }
    return check(QStringLiteral("reference %1").arg(QFileInfo(path).fileName()), mismatches == 0,
        QStringLiteral("%1 of %2 atoms differ; confusion (rows = OVITO Pro, columns = fork): [%3]").arg(mismatches).arg(actual.size()).arg(rows.join(" | ")));
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void IdentifyFCCPlanarFaultsSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("planar-faults-selftest")},
        tr("Run the in-process regression test for the Identify fcc planar faults modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void IdentifyFCCPlanarFaultsSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("planar-faults-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Planar faults self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Planar faults self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Planar faults self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool IdentifyFCCPlanarFaultsSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("planar_faults_test.dump"));
    writeDump(dumpPath);
    const QUrl url = QUrl::fromLocalFile(dumpPath);

    bool ok = true;
    for(int frame = 0; frame < 2; frame++) {
        const QString lattice = frame == 0 ? QStringLiteral("ideal lattice") : QStringLiteral("random displacements");
        ok &= checkCase(QStringLiteral("PTM, %1").arg(lattice), url, OORef<PolyhedralTemplateMatchingModifier>::create(), frame);
        ok &= checkCase(QStringLiteral("adaptive CNA, %1").arg(lattice), url, OORef<CommonNeighborAnalysisModifier>::create(), frame);
    }

    // Optional comparison with classifications exported from OVITO Pro (comma-separated list of LAMMPS dumps).
    const QString references = qEnvironmentVariable("OVITO_PLANAR_FAULTS_REFERENCE");
    if(!references.isEmpty()) {
        qInfo().noquote() << "Planar faults self-test: comparison with OVITO Pro references";
        for(const QString& path : references.split(QChar(','), Qt::SkipEmptyParts))
            ok &= checkReference(path);
    }

    qInfo().noquote() << (ok ? "Planar faults self-test: ALL PASSED" : "Planar faults self-test: FAILED");
    return ok;
}

}   // End of namespace
