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
#include <ovito/particles/modifier/analysis/surface/ConstructSurfaceModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "ConstructSurfaceSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <numbers>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ConstructSurfaceSelfTestService);

namespace {

constexpr double A = 4.08;
constexpr int N = 8;
constexpr double L = N * A;
constexpr double OFFSET = 0.25;                       // Atom coordinates span [OFFSET, OFFSET + (N - 0.5) A].
constexpr double CRYSTAL_MAX = OFFSET + (N - 0.5) * A;
constexpr double PORE_RADIUS = 5;
// Pores at 0.32 L and 0.68 L along the diagonal: at least 5 A of crystal between a pore and a cell face,
// so the wall stays closed with open boundaries for both surface methods.
const double poreCenters[2][3] = {{0.32 * L, 0.32 * L, 0.32 * L}, {0.68 * L, 0.68 * L, 0.68 * L}};

/// fcc crystal with all atoms within PORE_RADIUS of the two pore centers removed, in a cell [lo, hi]^3
/// per direction (the crystal itself does not depend on the cell). Returns the atom count.
int writeDump(const QString& path, const char* bounds, const std::array<double, 3>& lo, const std::array<double, 3>& hi, double poreRadius)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    const double basis[4][3] = {{0, 0, 0}, {0.5, 0.5, 0}, {0.5, 0, 0.5}, {0, 0.5, 0.5}};
    QString atoms;
    QTextStream atomStream(&atoms);
    atomStream.setRealNumberPrecision(10);
    int id = 0;
    for(int i = 0; i < N; i++) for(int j = 0; j < N; j++) for(int k = 0; k < N; k++) for(const auto& b : basis) {
        double p[3] = {(i + b[0]) * A + OFFSET, (j + b[1]) * A + OFFSET, (k + b[2]) * A + OFFSET};
        bool inPore = false;
        for(const auto& c : poreCenters) {
            double d2 = 0;
            for(int x = 0; x < 3; x++) d2 += (p[x] - c[x]) * (p[x] - c[x]);
            inPore |= d2 < poreRadius * poreRadius;
        }
        if(!inPore)
            atomStream << ++id << " 1 " << p[0] << " " << p[1] << " " << p[2] << "\n";
    }
    QTextStream out(&file);
    out.setRealNumberPrecision(15);
    out << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n" << id << "\n";
    out << "ITEM: BOX BOUNDS " << bounds << "\n";
    for(int d = 0; d < 3; d++)
        out << lo[d] << " " << hi[d] << "\n";
    out << "ITEM: ATOMS id type x y z\n" << atoms;
    return id;
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

bool relEq(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); }

/// Result of one evaluation of the modifier.
struct Result {
    PipelineFlowState state;
    double attr(const char* name) const {
        QVariant v = state.getAttributeValue(QStringLiteral("ConstructSurfaceMesh.") + QLatin1String(name));
        if(!v.isValid())
            throw Exception(QStringLiteral("Attribute ConstructSurfaceMesh.%1 is missing: %2").arg(name).arg(state.status().text()));
        return v.toDouble();
    }
};

/// Builds the crystal in the given cell and evaluates the modifier with region identification.
Result evaluate(const QTemporaryDir& dir, ConstructSurfaceModifier::SurfaceMethod method, const char* bounds,
                const std::array<double, 3>& lo, const std::array<double, 3>& hi, int* atomCount = nullptr, double poreRadius = PORE_RADIUS,
                int smoothingLevel = 0)
{
    static int counter = 0;
    const QString dumpPath = dir.filePath(QStringLiteral("crystal_%1.dump").arg(counter++));
    int n = writeDump(dumpPath, bounds, lo, hi, poreRadius);
    if(atomCount) *atomCount = n;

    QUrl url = QUrl::fromLocalFile(dumpPath);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);

    OORef<ConstructSurfaceModifier> surface = OORef<ConstructSurfaceModifier>::create();
    // Memorized fields: do not inherit GUI settings.
    surface->setMethod(method);
    surface->setProbeSphereRadius(4);
    surface->setSmoothingLevel(smoothingLevel);
    surface->setGridResolution(50);
    surface->setRadiusFactor(1);
    surface->setIsoValue(0.6);
    surface->setIdentifyRegions(true);
    surface->setMapParticlesToRegions(true);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, surface);
    return {pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult()};
}

QString methodName(ConstructSurfaceModifier::SurfaceMethod method)
{
    return method == ConstructSurfaceModifier::AlphaShape ? QStringLiteral("alpha shape") : QStringLiteral("Gaussian density");
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void ConstructSurfaceSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("construct-surface-selftest")},
        tr("Run the in-process regression test for the region analysis of the Construct surface mesh modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void ConstructSurfaceSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("construct-surface-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Construct surface self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Construct surface self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Construct surface self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool ConstructSurfaceSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));

    // Bounds for one pore: at least the removed sphere minus a nearest-neighbor shell, at most the
    // sphere grown by one nearest-neighbor distance.
    const double nn = A / std::sqrt(2.0);
    const double minPore = 4.0 / 3.0 * std::numbers::pi * std::pow(PORE_RADIUS - nn, 3);
    const double maxPore = 4.0 / 3.0 * std::numbers::pi * std::pow(PORE_RADIUS + nn, 3);
    using Method = ConstructSurfaceModifier::SurfaceMethod;

    bool ok = true;

    // 1. The crystal in its own cell [0, L]^3, periodic and open: all attributes and their mutual consistency.
    for(Method method : {ConstructSurfaceModifier::AlphaShape, ConstructSurfaceModifier::GaussianDensity}) {
        for(bool periodic : {true, false}) {
            qInfo().noquote() << "Construct surface self-test:" << methodName(method) << (periodic ? "- periodic cell" : "- open cell");
            int atomCount = 0;
            Result r = evaluate(dir, method, periodic ? "pp pp pp" : "ff ff ff", {0, 0, 0}, {L, L, L}, &atomCount);
            const double cellVolume = L * L * L;
            for(const char* name : {"surface_area", "specific_surface_area", "filled_volume", "empty_volume", "void_volume", "filled_fraction",
                                    "empty_fraction", "filled_region_count", "empty_region_count", "void_region_count", "cell_volume",
                                    "box_volume", "filled_volume_in_cell", "empty_volume_in_cell", "filled_fraction_in_cell",
                                    "empty_fraction_in_cell", "specific_surface_area_in_cell"})
                qInfo().noquote() << "    " << name << "=" << r.attr(name);

            // The standard attributes behave exactly like OVITO Pro 3.16.1 (measured with ovitos): for a non-periodic
            // cell the exterior region is infinite and the cell-based quantities are NaN. For a periodic cell the
            // additional *_in_cell attributes coincide with the standard ones.
            if(periodic) {
                ok &= check(QStringLiteral("periodic: standard attributes = *_in_cell attributes"),
                    r.attr("cell_volume") == r.attr("box_volume") && r.attr("empty_volume") == r.attr("empty_volume_in_cell")
                    && r.attr("filled_fraction") == r.attr("filled_fraction_in_cell") && r.attr("empty_fraction") == r.attr("empty_fraction_in_cell")
                    && r.attr("specific_surface_area") == r.attr("specific_surface_area_in_cell"), {});
            }
            else {
                ok &= check(QStringLiteral("open cell, as in OVITO Pro: empty_volume = inf; cell_volume, fractions, specific area = NaN"),
                    std::isinf(r.attr("empty_volume")) && std::isnan(r.attr("cell_volume")) && std::isnan(r.attr("filled_fraction"))
                    && std::isnan(r.attr("empty_fraction")) && std::isnan(r.attr("specific_surface_area")), {});
            }
            const double filled = r.attr("filled_volume"), empty = r.attr("empty_volume_in_cell"), voids = r.attr("void_volume");

            ok &= check(QStringLiteral("filled_region_count = 1"), r.attr("filled_region_count") == 1, QString::number(r.attr("filled_region_count")));
            ok &= check(QStringLiteral("void_region_count = 2"), r.attr("void_region_count") == 2, QString::number(r.attr("void_region_count")));
            ok &= check(QStringLiteral("empty_region_count"), r.attr("empty_region_count") == (periodic ? 2 : 3), QString::number(r.attr("empty_region_count")));
            ok &= check(QStringLiteral("each pore within geometric bounds"), voids > 2 * minPore && voids < 2 * maxPore,
                QStringLiteral("%1 in (%2, %3)").arg(voids).arg(2 * minPore).arg(2 * maxPore));
            ok &= check(QStringLiteral("box_volume = cell volume"), relEq(r.attr("box_volume"), cellVolume), QString::number(r.attr("box_volume")));
            ok &= check(QStringLiteral("filled_fraction_in_cell = filled_in_cell / cell volume, filled_in_cell + empty_in_cell = cell volume"), relEq(r.attr("filled_fraction_in_cell"), r.attr("filled_volume_in_cell") / cellVolume) && relEq(r.attr("filled_volume_in_cell") + empty, cellVolume), QString::number(r.attr("filled_fraction_in_cell")));
            ok &= check(QStringLiteral("empty_fraction_in_cell = empty / cell volume"), relEq(r.attr("empty_fraction_in_cell"), empty / cellVolume), QString::number(r.attr("empty_fraction_in_cell")));
            ok &= check(QStringLiteral("specific_surface_area_in_cell = area / cell volume"), relEq(r.attr("specific_surface_area_in_cell"), r.attr("surface_area") / cellVolume), QString::number(r.attr("specific_surface_area_in_cell")));
            if(periodic) {
                ok &= check(QStringLiteral("void volume = empty volume"), relEq(voids, empty), QStringLiteral("%1 vs %2").arg(voids).arg(empty));
                ok &= check(QStringLiteral("filled + empty = cell volume"), relEq(filled + empty, cellVolume), QStringLiteral("%1 + %2").arg(filled).arg(empty));
            }
            else {
                ok &= check(QStringLiteral("empty volume finite and > void volume"), std::isfinite(empty) && empty > voids, QStringLiteral("%1 vs %2").arg(empty).arg(voids));
                if(method == ConstructSurfaceModifier::AlphaShape)    // All atoms, hence the whole alpha shape, lie inside the cell.
                    ok &= check(QStringLiteral("filled + empty = cell volume"), relEq(filled + empty, cellVolume), QStringLiteral("%1 + %2").arg(filled).arg(empty));
                else                                                  // The Gaussian surface sticks out of the cell.
                    ok &= check(QStringLiteral("filled + empty > cell volume (filled region extends beyond the cell)"), filled + empty > cellVolume * (1 + 1e-6),
                        QStringLiteral("%1 + %2 vs %3").arg(filled).arg(empty).arg(cellVolume));
            }
            if(method == ConstructSurfaceModifier::AlphaShape) {
                // Every atom belongs to the single filled region (particle mapping exists for the alpha shape only).
                const Property* regions = r.state.expectObject<Particles>()->getProperty(QStringLiteral("Region"));
                bool sameRegion = regions && regions->size() == (size_t)atomCount;
                if(sameRegion) {
                    BufferReadAccess<int32_t> ids(regions);
                    sameRegion = std::all_of(ids.begin(), ids.end(), [&](int32_t id) { return id == ids[0] && id >= 0; });
                }
                ok &= check(QStringLiteral("all atoms in one filled region"), sameRegion, regions ? QStringLiteral("Region property present") : QStringLiteral("missing"));
                ok &= check(QStringLiteral("region_memberships attribute"), r.state.getAttributeValue(QStringLiteral("ConstructSurfaceMesh.region_memberships")).isValid(), {});
            }
        }
    }

    // 2. Alpha shape in an open cell that cuts the crystal at z = 2.5 and z = 30: the filled volume outside the
    // cell consists of two slabs of the atom-center bounding box, so empty = V_cell - (filled - slabs).
    // The cuts stay clear of the pore cavities (which extend ~6 A from the pore centers), so both remain voids.
    {
        qInfo().noquote() << "Construct surface self-test: alpha shape - open cell cutting the crystal along z";
        const double zlo = 2.5, zhi = 30;
        Result r = evaluate(dir, ConstructSurfaceModifier::AlphaShape, "ff ff ff", {0, 0, zlo}, {L, L, zhi});
        const double side = CRYSTAL_MAX - OFFSET;
        const double slabs = side * side * ((zlo - OFFSET) + (CRYSTAL_MAX - zhi));
        const double cellVolume = L * L * (zhi - zlo);
        const double expected = cellVolume - (r.attr("filled_volume") - slabs);
        // The fcc block lacks some corner sites, which shaves a few A^3 off the slabs.
        ok &= check(QStringLiteral("empty_volume = V_cell - (filled - outside slabs)"), std::abs(r.attr("empty_volume_in_cell") - expected) < 0.005 * slabs,
            QStringLiteral("%1 vs %2 (slabs %3)").arg(r.attr("empty_volume_in_cell")).arg(expected).arg(slabs));
        ok &= check(QStringLiteral("void_region_count = 2"), r.attr("void_region_count") == 2, QString::number(r.attr("void_region_count")));
    }

    // 3. Additivity: splitting a large open cell at z = h (through the first pore) into two halves, the empty
    // volumes of the halves add up to that of the whole cell. Each half clips the boundary of the filled
    // region differently, so this exercises the clipping for both methods.
    for(Method method : {ConstructSurfaceModifier::AlphaShape, ConstructSurfaceModifier::GaussianDensity}) {
        qInfo().noquote() << "Construct surface self-test:" << methodName(method) << "- additivity of empty volume under splitting the cell";
        const double lo = -20, hi = L + 20, h = poreCenters[0][2];
        Result whole = evaluate(dir, method, "ff ff ff", {lo, lo, lo}, {hi, hi, hi});
        Result lower = evaluate(dir, method, "ff ff ff", {lo, lo, lo}, {hi, hi, h});
        Result upper = evaluate(dir, method, "ff ff ff", {lo, lo, h}, {hi, hi, hi});
        // Filled volume inside each cell, as implied by the reported empty volume.
        auto filledInside = [](const Result& r, double cellVolume) { return cellVolume - r.attr("empty_volume_in_cell"); };
        const double wholeVolume = std::pow(hi - lo, 3), lowerVolume = std::pow(hi - lo, 2) * (h - lo), upperVolume = std::pow(hi - lo, 2) * (hi - h);
        const double sum = filledInside(lower, lowerVolume) + filledInside(upper, upperVolume);
        ok &= check(QStringLiteral("whole cell contains everything: filled + empty = cell volume"), relEq(whole.attr("filled_volume") + whole.attr("empty_volume_in_cell"), wholeVolume),
            QString::number(whole.attr("filled_volume") + whole.attr("empty_volume_in_cell")));
        ok &= check(QStringLiteral("each half: 0 < filled inside < filled"),
            filledInside(lower, lowerVolume) > 0 && filledInside(lower, lowerVolume) < lower.attr("filled_volume") &&
            filledInside(upper, upperVolume) > 0 && filledInside(upper, upperVolume) < upper.attr("filled_volume"),
            QStringLiteral("%1 / %2").arg(filledInside(lower, lowerVolume)).arg(filledInside(upper, upperVolume)));
        // The alpha shape does not depend on the cell. The Gaussian density surface of the upstream code changes
        // very slightly when atoms lie outside the cell; allow exactly that difference of the meshes.
        const double meshDifference = std::abs(lower.attr("filled_volume") - whole.attr("filled_volume")) + std::abs(upper.attr("filled_volume") - whole.attr("filled_volume"));
        ok &= check(QStringLiteral("filled inside lower + filled inside upper = filled (whole)"),
            std::abs(sum - whole.attr("filled_volume")) <= meshDifference + 1e-6 * whole.attr("filled_volume"),
            QStringLiteral("%1 vs %2 (mesh difference %3)").arg(sum).arg(whole.attr("filled_volume")).arg(meshDifference));
    }

    // 4. A cell entirely inside the crystal: no part of the exterior lies inside the cell, so the empty volume must
    // equal the void volume exactly, although the filled region sticks out of every open face. Open in all three
    // directions exercises faces, edges and corners (inclusion-exclusion); periodic in x and y with an open z
    // exercises mesh edges that cross periodic boundaries.
    for(Method method : {ConstructSurfaceModifier::AlphaShape, ConstructSurfaceModifier::GaussianDensity}) {
        for(const char* bounds : {"ff ff ff", "pp pp ff"}) {
            qInfo().noquote() << "Construct surface self-test:" << methodName(method) << "- cell inside the crystal, boundaries" << bounds;
            const bool slab = bounds[0] == 'p';
            Result r = evaluate(dir, method, bounds, {slab ? 0 : 2.5, slab ? 0 : 2.5, 2.5}, {slab ? L : 30, slab ? L : 30, 30});
            const double cellVolume = slab ? L * L * 27.5 : 27.5 * 27.5 * 27.5;
            ok &= check(QStringLiteral("void_region_count = 2"), r.attr("void_region_count") == 2, QString::number(r.attr("void_region_count")));
            ok &= check(QStringLiteral("empty_volume = void_volume (no exterior inside the cell)"),
                std::abs(r.attr("empty_volume_in_cell") - r.attr("void_volume")) < 1e-6 * cellVolume,
                QStringLiteral("%1 vs %2").arg(r.attr("empty_volume_in_cell")).arg(r.attr("void_volume")));
        }
    }

    // Slab (periodic in x and y, open in z) with all atoms inside the cell: the empty layers above and below the
    // slab touch the open boundary and must be exterior, not voids. Regression test for MarchingCubes, which
    // used to check only the grid vertex that created a region for lying on an open boundary.
    for(Method method : {ConstructSurfaceModifier::AlphaShape, ConstructSurfaceModifier::GaussianDensity}) {
        qInfo().noquote() << "Construct surface self-test:" << methodName(method) << "- slab, open z boundary, atoms inside the cell";
        // The margins must exceed the distance of the surface from the outermost atomic plane, which is ~1.3 A for the
        // Gaussian density surface (isolevel 0.6); with a 1 A margin it would lie outside the tight cell.
        Result tight = evaluate(dir, method, "pp pp ff", {0, 0, -3}, {L, L, L + 3});
        Result padded = evaluate(dir, method, "pp pp ff", {0, 0, -8}, {L, L, L + 8});
        ok &= check(QStringLiteral("void_region_count = 2, empty_region_count = 4"),
            tight.attr("void_region_count") == 2 && tight.attr("empty_region_count") == 4,
            QStringLiteral("%1 / %2").arg(tight.attr("void_region_count")).arg(tight.attr("empty_region_count")));
        // Growing the cell along the open direction only adds empty space (the surface lies inside both cells).
        ok &= check(QStringLiteral("padding the open direction by 2 x 5 A adds exactly that volume to empty_volume"),
            relEq(padded.attr("empty_volume_in_cell") - tight.attr("empty_volume_in_cell"), L * L * 10, 1e-6),
            QStringLiteral("%1 vs %2").arg(padded.attr("empty_volume_in_cell") - tight.attr("empty_volume_in_cell")).arg(L * L * 10));
    }

    // Mesh smoothing (default level 8 in the GUI) moves the alpha-shape vertices but not the region volumes, which
    // stem from the Delaunay tetrahedra. The volume attributes must not depend on it, also when the filled region
    // sticks out of the cell.
    {
        qInfo().noquote() << "Construct surface self-test: alpha shape - volumes independent of mesh smoothing";
        for(auto [lo, hi] : {std::pair{0.0, L}, std::pair{2.5, 30.0}}) {
            Result plain = evaluate(dir, ConstructSurfaceModifier::AlphaShape, "ff ff ff", {lo, lo, lo}, {hi, hi, hi}, nullptr, PORE_RADIUS, 0);
            Result smooth = evaluate(dir, ConstructSurfaceModifier::AlphaShape, "ff ff ff", {lo, lo, lo}, {hi, hi, hi}, nullptr, PORE_RADIUS, 8);
            ok &= check(QStringLiteral("cell [%1, %2]^3: smoothing 8 gives the same filled/empty/void volumes").arg(lo).arg(hi),
                relEq(smooth.attr("filled_volume"), plain.attr("filled_volume")) && relEq(smooth.attr("empty_volume_in_cell"), plain.attr("empty_volume_in_cell"))
                    && relEq(smooth.attr("void_volume"), plain.attr("void_volume")),
                QStringLiteral("empty %1 vs %2").arg(smooth.attr("empty_volume_in_cell")).arg(plain.attr("empty_volume_in_cell")));
        }
    }

    // 5. No particles at all: everything in the cell is empty.
    for(Method method : {ConstructSurfaceModifier::AlphaShape, ConstructSurfaceModifier::GaussianDensity}) {
        qInfo().noquote() << "Construct surface self-test:" << methodName(method) << "- no particles, open cell";
        int atomCount = -1;
        Result r = evaluate(dir, method, "ff ff ff", {0, 0, 0}, {L, L, L}, &atomCount, 1000);
        ok &= check(QStringLiteral("no atoms written"), atomCount == 0, QString::number(atomCount));
        ok &= check(QStringLiteral("filled_volume = 0, empty_volume = cell volume"),
            r.attr("filled_volume") == 0 && relEq(r.attr("empty_volume_in_cell"), L * L * L),
            QStringLiteral("%1 / %2").arg(r.attr("filled_volume")).arg(r.attr("empty_volume_in_cell")));
    }

    qInfo().noquote() << (ok ? "Construct surface self-test: ALL PASSED" : "Construct surface self-test: FAILED");
    return ok;
}

}   // End of namespace
