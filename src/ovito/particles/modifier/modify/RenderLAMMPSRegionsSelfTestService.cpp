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
#include <ovito/particles/modifier/modify/RenderLAMMPSRegionsModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshReadAccess.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "RenderLAMMPSRegionsSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(RenderLAMMPSRegionsSelfTestService);

namespace {

/// 20000 particles from a 64-bit LCG, uniform in [-50, 50)^3; box [-50, 50]^3.
void writeDataFile(const QString& path)
{
    constexpr int N = 20000;
    uint64_t state = 12345;
    auto next = [&]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double)(state >> 11) / (double)(1ULL << 53);
    };
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test file %1").arg(path));
    QTextStream out(&file);
    out << "regions test\n\n" << N << " atoms\n1 atom types\n\n-50 50 xlo xhi\n-50 50 ylo yhi\n-50 50 zlo zhi\n\nAtoms # atomic\n\n";
    for(int i = 1; i <= N; i++) {
        const double x = -50 + 100 * next(), y = -50 + 100 * next(), z = -50 + 100 * next();
        out << QString::asprintf("%d 1 %.10f %.10f %.10f\n", i, x, y, z);
    }
}

/// Region commands evaluated by the test (all with "Select interior atoms" and "Color by region").
const char* const testCases[] = {
    "region 1 sphere 0 0 0 20",
    "region 2 block -10 10 -5 5 -20 20",
    "region 3 cylinder z 0 0 10 -20 20",
    "region 4 cone x 0 0 0 15 -20 20",
    "region 5 ellipsoid 0 0 0 10 20 30",
    "region 6 plane 0 0 0 1 1 1",
    "region 7 plane 10 0 0 1 1 0 side out",
    "region 8 sphere 0 0 0 20 side out",
    "region 9 block -10 10 -5 5 -20 20 open 1 open 3",
    "region 10 cylinder y 5 -5 10 -20 20 open 3",
    "region 11 cone z 0 0 5 15 -20 20 open 1",
    "region 12 block INF 10 EDGE 5 -20 20",
    "region 13 sphere 10 0 0 10 move 0 10 0 rotate 1.5707963267948966 0 0 0 0 0 1",
    "region 14 block -10 10 -5 5 -20 20 rotate 0.7 1 2 3 1 1 1 move 3 NULL -2",
    "region 15 cylinder x 0 0 10 INF INF units lattice",
    "region a sphere 0 0 0 10\nregion b block 0 20 0 20 0 20 # comment\n\nregion c cylinder y 0 0 5 -40 40\nregion d cone z 0 0 5 1 0 10 &\n   side out",
    "region 16 prism -10 10 -10 10 -10 10 5 3 2",
    "region 17 prism -10 10 -5 5 -20 20 0 0 0",
};

/// Invalid commands that must raise an error.
const char* const errorCases[] = {
    "region 1 torus 0 0 0 1 2",
    "fix 1 all nve",
    "region 1 sphere 0 0 0 v_r",
    "region 1 sphere 0 0 0",
    "region 1 sphere 0 0 0 10\nregion 1 sphere 1 1 1 10",
    "region 1 sphere 0 0 0 10 rotate 1 0 0 0 0 0 0",
    "region 1 sphere 0 0 0 10 open 1",
    "region 1 block 10 -10 0 1 0 1",
    "region 1 cylinder q 0 0 1 0 1",
    "region 1 block 0 1 0 1 0 1 open 7",
    "region 1 sphere 0 0 0 10 side sideways",
};

/// Result of one test case.
struct Result {
    size_t vertices = 0, faces = 0;
    double signedVolume = 0;
    Box3 bbox;
    bool closed = false;
    qlonglong selected = 0;
    QStringList names;
    std::vector<Color> colors;
    QStringList status;
};

/// Signed volume enclosed by the mesh faces (fan triangulation).
double signedVolume(const SurfaceMesh* mesh)
{
    SurfaceMeshReadAccess access(mesh);
    BufferReadAccess<Point3> positions(mesh->vertices()->expectProperty(SurfaceMeshVertices::PositionProperty));
    double volume = 0;
    for(auto face : access.facesRange()) {
        const auto e0 = access.firstFaceEdge(face);
        const Point3 p0 = positions[access.vertex1(e0)];
        for(auto e = access.nextFaceEdge(e0); access.nextFaceEdge(e) != e0; e = access.nextFaceEdge(e)) {
            const Point3 p1 = positions[access.vertex1(e)], p2 = positions[access.vertex2(e)];
            volume += (p0 - Point3::Origin()).dot((p1 - Point3::Origin()).cross(p2 - Point3::Origin())) / 6;
        }
    }
    return volume;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void RenderLAMMPSRegionsSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("lammps-regions-selftest")},
        tr("Run the in-process regression test for the Render LAMMPS regions modifier and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void RenderLAMMPSRegionsSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("lammps-regions-selftest")))
        return;
    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "LAMMPS regions self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("LAMMPS regions self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool RenderLAMMPSRegionsSelfTestService::runTest()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("regions_test.data"));
    writeDataFile(path);
    const QUrl url = QUrl::fromLocalFile(path);

    auto evaluate = [&](const QString& commands) {
        OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, std::move(importer), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        OORef<RenderLAMMPSRegionsModifier> modifier = OORef<RenderLAMMPSRegionsModifier>::create();
        modifier->setCommands(commands);
        modifier->setSelectParticles(true);
        modifier->setColorByRegion(true);
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, modifier);
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    };

    bool ok = true;
    auto fail = [&](const QString& msg) { qCritical().noquote() << "LAMMPS regions self-test:" << msg; ok = false; };

    std::vector<Result> results;
    QJsonObject dump;
    for(const char* commands : testCases) {
        PipelineFlowState state = evaluate(QString::fromLatin1(commands));
        if(state.status().type() == PipelineStatus::Error) {
            fail(QStringLiteral("[%1] evaluation failed: %2").arg(commands).arg(state.status().text()));
            results.emplace_back();
            continue;
        }
        Result r;
        ConstDataObjectPath meshPath = state.data()->getObject(SurfaceMesh::OOClass(), QString::fromLatin1(RenderLAMMPSRegionsModifier::meshIdentifier));
        const SurfaceMesh* mesh = meshPath.empty() ? nullptr : static_object_cast<SurfaceMesh>(meshPath.back());
        if(!mesh) {
            fail(QStringLiteral("[%1] no surface mesh").arg(commands));
            results.emplace_back();
            continue;
        }
        mesh->verifyMeshIntegrity();
        SurfaceMeshReadAccess access(mesh);
        r.vertices = access.vertexCount();
        r.faces = access.faceCount();
        r.closed = mesh->topology()->isClosed();
        r.signedVolume = signedVolume(mesh);
        r.bbox.addPoints(BufferReadAccess<Point3>(mesh->vertices()->expectProperty(SurfaceMeshVertices::PositionProperty)));
        if(const Property* ids = mesh->regions()->getProperty(QStringLiteral("Region ID")))
            for(const ElementType* type : ids->elementTypes()) r.names.push_back(type->name());
        if(BufferReadAccess<ColorG> colors = mesh->regions()->getProperty(SurfaceMeshRegions::ColorProperty))
            for(const ColorG& c : colors) r.colors.push_back(c.toDataType<FloatType>());
        r.selected = state.getAttributeValue(QStringLiteral("RenderLAMMPSRegions.selected_count")).toLongLong();
        if(BufferReadAccess<SelectionIntType> selection = state.expectObject<Particles>()->getProperty(Particles::SelectionProperty))
            if((qlonglong)std::ranges::count_if(selection, [](SelectionIntType s) { return s != 0; }) != r.selected)
                fail(QStringLiteral("[%1] selection property and attribute disagree").arg(commands));
        if(state.status().type() == PipelineStatus::Warning)
            r.status.push_back(state.status().text());

        QJsonObject entry;
        entry.insert(QStringLiteral("verts"), (qint64)r.vertices);
        entry.insert(QStringLiteral("faces"), (qint64)r.faces);
        entry.insert(QStringLiteral("closed"), r.closed);
        entry.insert(QStringLiteral("signedvol"), r.signedVolume);
        entry.insert(QStringLiteral("bbox"), QJsonArray{r.bbox.minc.x(), r.bbox.minc.y(), r.bbox.minc.z(), r.bbox.maxc.x(), r.bbox.maxc.y(), r.bbox.maxc.z()});
        entry.insert(QStringLiteral("selected"), r.selected);
        entry.insert(QStringLiteral("names"), QJsonArray::fromStringList(r.names));
        QJsonArray colors;
        for(const Color& c : r.colors) colors.append(QJsonArray{c.r(), c.g(), c.b()});
        entry.insert(QStringLiteral("colors"), colors);
        entry.insert(QStringLiteral("warning"), r.status.join(QChar('\n')));
        dump.insert(QString::fromLatin1(commands), entry);
        results.push_back(std::move(r));
    }

    for(const char* commands : errorCases) {
        QString message;
        try {
            PipelineFlowState state = evaluate(QString::fromLatin1(commands));
            if(state.status().type() == PipelineStatus::Error)
                message = state.status().text();
        }
        catch(const Exception& ex) {
            message = ex.messages().join(QChar('\n'));
        }
        if(message.isEmpty())
            fail(QStringLiteral("[%1] expected an error, got none").arg(commands));
        else
            dump.insert(QStringLiteral("error: ") + QString::fromLatin1(commands), message);
    }

    if(qEnvironmentVariableIsSet("OVITO_RLR_DUMP")) {
        QFile out(qEnvironmentVariable("OVITO_RLR_DUMP"));
        if(!out.open(QIODevice::WriteOnly))
            throw Exception(QStringLiteral("Cannot write %1").arg(out.fileName()));
        out.write(QJsonDocument(dump).toJson(QJsonDocument::Indented));
        qInfo().noquote() << "LAMMPS regions self-test: results written to" << out.fileName();
        return ok;
    }

    // Reference values, one row per test case: vertex and face counts, closed mesh, signed volume, bounding box,
    // selected particles. They agree with OVITO Pro 3.16.1 for the same input except where noted:
    // - Pro orients cones and y cylinders inwards (negative volume); here all faces point outwards (cases 4, 10, 11, 16).
    // - Case 13: Pro's mesh applies rotate before move (as LAMMPS) but its selection does the opposite (70 particles);
    //   73 is the count for the sphere at (0, 20, 0), checked with numpy. Case 14: Pro does not accept NULL; with
    //   "move 3 0 -2" its mesh is identical and its selection again uses the opposite order (188 instead of 187, numpy).
    // - Prisms: Pro fails to compute the selection. Case 17 (no tilt) must equal case 2, case 16 was checked with numpy.
    struct Reference { size_t vertices, faces; bool closed; double signedVolume; double bbox[6]; qlonglong selected; };
    const Reference references[] = {
        {2562, 5120, true, 33437.91158, {-20, -20, -20, 20, 20, 20}, 679},
        {8, 6, true, 8000, {-10, -5, -20, 10, 5, 20}, 162},
        {100, 52, true, 12533.32336, {-10, -9.98026728, -20, 10, 9.98026728, 20}, 247},
        {100, 52, true, 9399.992517, {-20, -15, -14.9704009, 20, 15, 14.9704009}, 207},
        {10242, 20480, true, 25119.1497, {-10, -20, -30, 10, 20, 30}, 485},
        {6, 1, false, 0, {-50, -50, -50, 50, 50, 50}, 9952},
        {4, 1, false, -30000, {-40, -40, -50, 50, 50, 50}, 11910},
        {2562, 5120, true, -33437.91158, {-20, -20, -20, 20, 20, 20}, 19321},
        {8, 4, false, 5333.333333, {-10, -5, -20, 10, 5, 20}, 162},
        {100, 2, false, 4177.774452, {-5, -20, -14.9802673, 15, 20, 4.98026728}, 247},
        {100, 51, false, 13055.54516, {-15, -14.9704009, -20, 15, 14.9704009, 20}, 261},
        {8, 6, true, 212525.5888, {-86.6025404, -50, -20, 10, 5, 20}, 2686},
        {2562, 5120, true, 4179.738948, {-10, 10, -10, 10, 30, 10}, 73},
        {8, 6, true, 8000, {-16.5136488, -13.8465804, -24.1885031, 21.2994546, 15.3343374, 19.9149402}, 187},
        {100, 52, true, 54270.8821, {-86.6025404, -10, -9.98026728, 86.6025404, 10, 9.98026728}, 661},
        {2770, 5230, true, 18122.62311, {-10, -40, -10, 20, 40, 20}, 20000},
        {8, 6, true, 8000, {-10, -10, -10, 18, 12, 10}, 168},
        {8, 6, true, 8000, {-10, -5, -20, 10, 5, 20}, 162},
    };
    static_assert(std::size(references) == std::size(testCases));
    auto close = [](double a, double b) { return std::abs(a - b) <= 1e-6 * std::max(1.0, std::abs(b)); };
    for(size_t i = 0; i < results.size(); i++) {
        const Result& r = results[i];
        const Reference& ref = references[i];
        const QString tag = QStringLiteral("[case %1: %2]").arg(i).arg(QString::fromLatin1(testCases[i]).replace(QChar('\n'), QStringLiteral(" | ")));
        if(r.vertices != ref.vertices || r.faces != ref.faces)
            fail(QStringLiteral("%1 mesh has %2 vertices and %3 faces, expected %4 and %5").arg(tag).arg(r.vertices).arg(r.faces).arg(ref.vertices).arg(ref.faces));
        if(r.closed != ref.closed)
            fail(QStringLiteral("%1 mesh closed = %2, expected %3").arg(tag).arg(r.closed).arg(ref.closed));
        if(!close(r.signedVolume, ref.signedVolume))
            fail(QStringLiteral("%1 signed volume %2, expected %3").arg(tag).arg(r.signedVolume, 0, 'g', 12).arg(ref.signedVolume, 0, 'g', 12));
        const double bbox[6] = {r.bbox.minc.x(), r.bbox.minc.y(), r.bbox.minc.z(), r.bbox.maxc.x(), r.bbox.maxc.y(), r.bbox.maxc.z()};
        for(int d = 0; d < 6; d++)
            if(!close(bbox[d], ref.bbox[d]))
                fail(QStringLiteral("%1 bounding box component %2 is %3, expected %4").arg(tag).arg(d).arg(bbox[d], 0, 'g', 12).arg(ref.bbox[d], 0, 'g', 12));
        if(r.selected != ref.selected)
            fail(QStringLiteral("%1 selected %2 particles, expected %3").arg(tag).arg(r.selected).arg(ref.selected));
    }

    // Region names, standard type palette, and the warning about lattice units.
    const QStringList expectedNames{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")};
    const Color expectedColors[] = {{1.0, 0.4, 0.4}, {0.4, 0.4, 1.0}, {1.0, 1.0, 0.0}, {1.0, 0.4, 1.0}};
    if(results[15].names != expectedNames)
        fail(QStringLiteral("[case 15] region names are '%1'").arg(results[15].names.join(QChar(','))));
    if(results[15].colors.size() != 4)
        fail(QStringLiteral("[case 15] expected 4 region colors"));
    else
        for(int i = 0; i < 4; i++)
            if(!results[15].colors[i].equals(expectedColors[i], 1e-6))
                fail(QStringLiteral("[case 15] wrong color for region %1").arg(i));
    if(results[0].names != QStringList{QStringLiteral("1")})
        fail(QStringLiteral("[case 0] region name is '%1'").arg(results[0].names.join(QChar(','))));
    if(results[14].status.isEmpty() || !results[14].status.join(QChar(' ')).contains(QStringLiteral("lattice")))
        fail(QStringLiteral("[case 14] missing warning about lattice units"));

    qInfo().noquote() << (ok ? "LAMMPS regions self-test: ALL PASSED" : "LAMMPS regions self-test: FAILED");
    return ok;
}

}   // End of namespace
