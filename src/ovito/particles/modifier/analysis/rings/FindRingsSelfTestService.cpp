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
#include <ovito/particles/modifier/analysis/rings/FindRingsModifier.h>
#include <ovito/particles/modifier/modify/CreateBondsModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "FindRingsSelfTestService.h"
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(FindRingsSelfTestService);

namespace {

using Ring = FindRingsModifier::Ring;
using BondList = std::vector<std::tuple<size_t, size_t, Vector3I>>;

BondList bondsOf(std::initializer_list<std::pair<size_t, size_t>> pairs)
{
    BondList bonds;
    for(auto [a, b] : pairs)
        bonds.emplace_back(a, b, Vector3I::Zero());
    return bonds;
}

QString toString(const std::vector<Ring>& rings)
{
    QStringList parts;
    for(const Ring& ring : rings) {
        QStringList ids;
        for(size_t i : ring) ids << QString::number(i);
        parts << QStringLiteral("(%1)").arg(ids.join(' '));
    }
    return parts.join(' ');
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

bool checkRings(const QString& name, const std::vector<Ring>& actual, const std::vector<Ring>& expected)
{
    return check(name, actual == expected, QStringLiteral("got [%1] expected [%2]").arg(toString(actual)).arg(toString(expected)));
}

/// Writes a periodic crystal (basis in reduced coordinates, n x n x n unit cells) as a LAMMPS dump.
void writeCrystal(const QString& path, const std::vector<std::array<double, 3>>& basis, const std::array<double, 3>& cellSize, const std::array<int, 3>& repeat)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(12);
    out << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n" << basis.size() * repeat[0] * repeat[1] * repeat[2] << "\n";
    out << "ITEM: BOX BOUNDS pp pp pp\n";
    for(int d = 0; d < 3; d++) out << "0 " << cellSize[d] * repeat[d] << "\n";
    out << "ITEM: ATOMS id type x y z\n";
    int id = 1;
    for(int i = 0; i < repeat[0]; i++) for(int j = 0; j < repeat[1]; j++) for(int k = 0; k < repeat[2]; k++)
        for(const auto& b : basis)
            out << id++ << " 1 " << (i + b[0]) * cellSize[0] << " " << (j + b[1]) * cellSize[1] << " " << (k + b[2]) * cellSize[2] << "\n";
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void FindRingsSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("find-rings-selftest")},
        tr("Run the in-process regression test for the Find rings modifier and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void FindRingsSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("find-rings-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Find rings self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Find rings self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool FindRingsSelfTestService::runTest()
{
    bool ok = true;

    qInfo().noquote() << "Find rings self-test: small graphs";
    // Naphthalene-like: hexagons 0-1-2-3-4-5 and 0-1-9-8-7-6 share the bond 0-1.
    const BondList fused = bondsOf({{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{6,7},{7,8},{8,9},{9,1}});
    ok &= checkRings(QStringLiteral("two fused hexagons: two 6-rings, no 10-ring"), FindRingsModifier::findRings(10, fused, 3, 12),
        {{0,1,2,3,4,5}, {0,1,9,8,7,6}});
    ok &= checkRings(QStringLiteral("two fused hexagons, minimum size 7: nothing"), FindRingsModifier::findRings(10, fused, 7, 12), {});
    ok &= checkRings(QStringLiteral("two fused hexagons, maximum size 5: nothing"), FindRingsModifier::findRings(10, fused, 3, 5), {});
    ok &= checkRings(QStringLiteral("square with diagonal: two triangles, no 4-ring"),
        FindRingsModifier::findRings(4, bondsOf({{0,1},{1,2},{2,3},{3,0},{0,2}}), 3, 12), {{0,1,2}, {0,2,3}});
    ok &= checkRings(QStringLiteral("pentagon"), FindRingsModifier::findRings(5, bondsOf({{0,1},{1,2},{2,3},{3,4},{4,0}}), 3, 12), {{0,1,2,3,4}});
    BondList wrapping = bondsOf({{0,1},{1,2},{2,3}});
    wrapping.emplace_back(3, 0, Vector3I(1, 0, 0));
    ok &= checkRings(QStringLiteral("4-cycle closing through the periodic boundary: no ring"), FindRingsModifier::findRings(4, wrapping, 3, 12), {});
    ok &= checkRings(QStringLiteral("same 4-cycle closing inside the cell: one ring"),
        FindRingsModifier::findRings(4, bondsOf({{0,1},{1,2},{2,3},{3,0}}), 3, 12), {{0,1,2,3}});

    // Full pipelines over periodic crystals.
    QTemporaryDir dir;
    auto runCrystal = [&](const QString& name, const std::vector<std::array<double, 3>>& basis, const std::array<double, 3>& cellSize,
                          const std::array<int, 3>& repeat, FloatType bondCutoff, int ringSize, size_t expectedRings, bool includeWrapping = false) {
        qInfo().noquote() << "Find rings self-test:" << name;
        const QString path = dir.filePath(name + QStringLiteral(".dump"));
        writeCrystal(path, basis, cellSize, repeat);
        QUrl url = QUrl::fromLocalFile(path);
        OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, std::move(importer), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        OORef<CreateBondsModifier> createBonds = OORef<CreateBondsModifier>::create();
        createBonds->setCutoffMode(CreateBondsModifier::UniformCutoff);
        createBonds->setUniformCutoff(bondCutoff);
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, createBonds);
        OORef<FindRingsModifier> findRings = OORef<FindRingsModifier>::create();
        findRings->setMinRingSize(3);
        findRings->setMaxRingSize(12);
        findRings->setIncludeWrappingRings(includeWrapping);
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, findRings);
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();

        auto table = [&](const QString& id) -> const DataTable* {
            ConstDataObjectPath p = state.data()->getObject(DataTable::OOClass(), id);
            if(p.empty()) throw Exception(QStringLiteral("Table %1 missing: %2").arg(id).arg(state.status().text()));
            return static_object_cast<DataTable>(p.back());
        };
        // Histogram: only the expected ring size occurs.
        const DataTable* histogram = table(QString::fromLatin1(FindRingsModifier::histogramTableIdentifier));
        std::vector<double> counts(histogram->elementCount());
        histogram->y()->copyTo(counts.begin());
        std::vector<double> expectedCounts(10, 0);
        expectedCounts[ringSize - 3] = expectedRings;
        ok &= check(QStringLiteral("histogram (sizes 3..12)"), counts == expectedCounts,
            QStringLiteral("%1 %2-rings").arg(counts.size() > size_t(ringSize - 3) ? counts[ringSize - 3] : -1).arg(ringSize));
        // The N-rings table has one row per ring with N indices, each ring a closed path of distinct atoms.
        const DataTable* rings = table(QStringLiteral("%1-rings").arg(ringSize));
        ok &= check(QStringLiteral("%1-rings table: rows x columns").arg(ringSize),
            rings->elementCount() == expectedRings && rings->getProperty(QStringLiteral("Particle Indices"))->componentCount() == (size_t)ringSize,
            QStringLiteral("%1 x %2").arg(rings->elementCount()).arg(rings->getProperty(QStringLiteral("Particle Indices"))->componentCount()));
        ok &= check(QStringLiteral("FindRings.count"), state.getAttributeValue(QStringLiteral("FindRings.count")).toULongLong() == expectedRings,
            state.getAttributeValue(QStringLiteral("FindRings.count")).toString());
        ConstDataObjectPath meshPath = state.data()->getObject(SurfaceMesh::OOClass(), QStringLiteral("rings"));
        const SurfaceMesh* mesh = meshPath.empty() ? nullptr : static_object_cast<SurfaceMesh>(meshPath.back());
        ok &= check(QStringLiteral("ring mesh: two faces (both sides) per ring"), mesh && mesh->topology()->faceCount() == 2 * expectedRings,
            mesh ? QString::number(mesh->topology()->faceCount()) : QStringLiteral("missing"));
    };

    // Diamond (a = 3.567, bond length 1.545): every atom lies on 12 six-rings, 6 atoms per ring -> 2 rings per atom.
    runCrystal(QStringLiteral("diamond"),
        {{0,0,0},{0,0.5,0.5},{0.5,0,0.5},{0.5,0.5,0},{0.25,0.25,0.25},{0.25,0.75,0.75},{0.75,0.25,0.75},{0.75,0.75,0.25}},
        {3.567, 3.567, 3.567}, {3, 3, 3}, 1.8, 6, 2 * 216);
    // Graphene (C-C 1.42, rectangular cell 3a x sqrt(3)a, 4 atoms): one hexagon per two atoms.
    const double a = 1.42;
    runCrystal(QStringLiteral("graphene"),
        {{0, 0, 0}, {1.0/3, 0, 0}, {0.5, 0.5, 0}, {5.0/6, 0.5, 0}},
        {3 * a, std::sqrt(3.0) * a, 10}, {6, 8, 1}, 1.6, 6, 6 * 8 * 4 / 2);
    // The graphene sheet is wide enough that no cycle of up to 12 bonds wraps around the cell: same result.
    runCrystal(QStringLiteral("graphene, wrapping rings included"),
        {{0, 0, 0}, {1.0/3, 0, 0}, {0.5, 0.5, 0}, {5.0/6, 0.5, 0}},
        {3 * a, std::sqrt(3.0) * a, 10}, {6, 8, 1}, 1.6, 6, 6 * 8 * 4 / 2, true);

    // Optional comparison with ring size histograms exported from OVITO Pro: a JSON file with the input file,
    // the bond cutoff, the ring size range and, per frame, the histogram {"size": count}.
    const QString referencePath = qEnvironmentVariable("OVITO_FIND_RINGS_REFERENCE");
    if(!referencePath.isEmpty()) {
        QFile referenceFile(referencePath);
        if(!referenceFile.open(QIODevice::ReadOnly))
            throw Exception(QStringLiteral("Cannot read %1").arg(referencePath));
        const QJsonObject reference = QJsonDocument::fromJson(referenceFile.readAll()).object();
        qInfo().noquote() << "Find rings self-test: comparison with OVITO Pro histograms from" << referencePath;
        QUrl url = QUrl::fromLocalFile(reference.value(QStringLiteral("file")).toString());
        OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
        if(!importer)
            throw Exception(QStringLiteral("Could not detect the format of %1").arg(url.toLocalFile()));
        importer->setMultiTimestepFile(true);
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, std::move(importer), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        OORef<CreateBondsModifier> createBonds = OORef<CreateBondsModifier>::create();
        createBonds->setCutoffMode(CreateBondsModifier::UniformCutoff);
        createBonds->setUniformCutoff(reference.value(QStringLiteral("cutoff")).toDouble());
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, createBonds);
        OORef<FindRingsModifier> findRings = OORef<FindRingsModifier>::create();
        findRings->setIncludeWrappingRings(false);
        findRings->setMinRingSize(reference.value(QStringLiteral("min")).toInt());
        findRings->setMaxRingSize(reference.value(QStringLiteral("max")).toInt());
        ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, findRings);
        // "hist": Pro's rings that close inside the cell. With wrapping rings included, the expected histogram is
        // "hist_quotient", the shortest-path rings of the bond graph without periodic images enumerated by brute
        // force: Pro's rings ("hist_pro_all") are always a subset of it, but Pro misses some of the larger odd
        // wrapping rings, and which ones changes between frames with the same bond topology.
        for(bool includeWrapping : {false, true}) {
            findRings->setIncludeWrappingRings(includeWrapping);
            for(const QJsonValue& entry : reference.value(QStringLiteral("frames")).toArray()) {
                const int frame = entry.toObject().value(QStringLiteral("frame")).toInt();
                const QJsonObject expected = entry.toObject().value(includeWrapping ? QStringLiteral("hist_quotient") : QStringLiteral("hist")).toObject();
                PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
                const DataTable* histogram = state.getObjectBy<DataTable>(node, QString::fromLatin1(FindRingsModifier::histogramTableIdentifier));
                if(!histogram)
                    throw Exception(QStringLiteral("Ring histogram missing at frame %1: %2").arg(frame).arg(state.status().text()));
                std::vector<int64_t> sizes(histogram->elementCount()), counts(histogram->elementCount());
                histogram->x()->copyTo(sizes.begin());
                histogram->y()->copyTo(counts.begin());
                QStringList actualText, expectedText;
                bool same = (qsizetype)sizes.size() == expected.size();
                for(size_t i = 0; i < sizes.size(); i++) {
                    const int64_t e = expected.value(QString::number(sizes[i])).toInteger(-1);
                    same &= (e == counts[i]);
                    actualText << QString::number(counts[i]);
                    expectedText << QString::number(e);
                }
                ok &= check(QStringLiteral("frame %1%2").arg(frame).arg(includeWrapping ? QStringLiteral(" (wrapping rings included)") : QString()), same, QStringLiteral("fork [%1] reference [%2]").arg(actualText.join(' '), expectedText.join(' ')));
            }
        }
    }

    qInfo().noquote() << (ok ? "Find rings self-test: ALL PASSED" : "Find rings self-test: FAILED");
    return ok;
}

}   // End of namespace
