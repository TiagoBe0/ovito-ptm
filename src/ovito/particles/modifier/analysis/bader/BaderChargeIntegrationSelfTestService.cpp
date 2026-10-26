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
#include <ovito/particles/modifier/analysis/bader/BaderChargeIntegrationModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "BaderChargeIntegrationSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BaderChargeIntegrationSelfTestService);

namespace {

constexpr double Bohr = 0.529177210903;

struct Atom { double pos[3]; double amplitude, width; int z; };

/// Gaussian cube file (Bohr units) with the density sum_a A_a exp(-|r - r_a|^2 / (2 w_a^2)) (minimum image) at the grid points.
void writeCube(const QString& path, const double cell[3][3], const int n[3], const std::vector<Atom>& atoms)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test file %1").arg(path));
    QTextStream out(&file);
    out << "Bader test\nanalytic gaussians\n" << QString::asprintf("%5d %12.6f %12.6f %12.6f\n", (int)atoms.size(), 0.0, 0.0, 0.0);
    for(int d = 0; d < 3; d++)
        out << QString::asprintf("%5d %12.6f %12.6f %12.6f\n", n[d], cell[d][0] / n[d] / Bohr, cell[d][1] / n[d] / Bohr, cell[d][2] / n[d] / Bohr);
    for(const Atom& a : atoms)
        out << QString::asprintf("%5d %12.6f %12.6f %12.6f %12.6f\n", a.z, 0.0, a.pos[0] / Bohr, a.pos[1] / Bohr, a.pos[2] / Bohr);
    // Inverse of the matrix whose columns are the cell vectors.
    Matrix3 m;
    for(int r = 0; r < 3; r++) for(int c = 0; c < 3; c++) m(r, c) = cell[c][r];
    const Matrix3 inv = m.inverse();
    std::vector<double> values;
    for(int i = 0; i < n[0]; i++) for(int j = 0; j < n[1]; j++) for(int k = 0; k < n[2]; k++) {
        Vector3 p;
        for(int d = 0; d < 3; d++) p[d] = (double)i / n[0] * cell[0][d] + (double)j / n[1] * cell[1][d] + (double)k / n[2] * cell[2][d];
        double rho = 0;
        for(const Atom& a : atoms) {
            Vector3 f = inv * (p - Vector3(a.pos[0], a.pos[1], a.pos[2]));
            for(int d = 0; d < 3; d++) f[d] -= std::nearbyint(f[d]);
            const Vector3 delta = m * f;
            rho += a.amplitude * std::exp(-delta.squaredLength() / (2 * a.width * a.width));
        }
        values.push_back(rho);
    }
    for(size_t s = 0; s < values.size(); s += 6) {
        QStringList line;
        for(size_t t = s; t < std::min(values.size(), s + 6); t++) line << QString::asprintf("%.10e", values[t]);
        out << line.join(' ') << '\n';
    }
}

/// Per-region and per-atom results.
struct Result {
    std::vector<double> density, volume, distance, atomDensity, atomVolume;
    std::vector<int64_t> particle;
    std::vector<Point3> position;
    std::vector<int64_t> regionHistogram;
    double weightSum = 0;
};

template<typename T>
std::vector<T> column(const PropertyContainer* container, const QString& name)
{
    const Property* p = container->getProperty(name);
    if(!p) throw Exception(QStringLiteral("Missing property '%1'").arg(name));
    std::vector<T> v(p->size());
    p->copyTo(v.begin());
    return v;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void BaderChargeIntegrationSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("bader-selftest")},
        tr("Run the in-process regression test for the Bader charge integration modifier and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void BaderChargeIntegrationSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("bader-selftest")))
        return;
    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Bader self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Bader self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool BaderChargeIntegrationSelfTestService::runTest()
{
    QTemporaryDir dir;
    const double orthoCell[3][3] = {{10, 0, 0}, {0, 11, 0}, {0, 0, 12}};
    const int orthoShape[3] = {20, 22, 24};
    const std::vector<Atom> orthoAtoms = {{{2.0, 3.0, 4.0}, 1.0, 1.0, 8}, {{6.5, 6.0, 7.5}, 2.0, 1.3, 1}, {{8.7, 1.2, 10.4}, 0.7, 0.8, 1}};
    const double tricCell[3][3] = {{10, 0, 0}, {3, 10, 0}, {1, 2, 11}};
    const int tricShape[3] = {24, 24, 26};
    const std::vector<Atom> tricAtoms = {{{2.0, 3.0, 4.0}, 1.0, 1.0, 8}, {{8.5, 6.0, 7.5}, 2.0, 1.3, 1}, {{5.7, 1.2, 9.4}, 0.7, 0.8, 1}, {{1.0, 8.0, 2.0}, 1.5, 1.1, 6}};
    writeCube(dir.filePath(QStringLiteral("ortho.cube")), orthoCell, orthoShape, orthoAtoms);
    writeCube(dir.filePath(QStringLiteral("tric.cube")), tricCell, tricShape, tricAtoms);

    auto evaluate = [&](const QString& name) {
        const QUrl url = QUrl::fromLocalFile(dir.filePath(name));
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult()), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        OORef<BaderChargeIntegrationModifier> bader = OORef<BaderChargeIntegrationModifier>::create();
        bader->setSubject(PropertyContainerReference(&VoxelGrid::OOClass(), QStringLiteral("imported")));
        bader->setDensityProperty(PropertyReference(QStringLiteral("Property")));
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, bader);
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
        if(state.status().type() == PipelineStatus::Error)
            throw Exception(QStringLiteral("%1: %2").arg(name).arg(state.status().text()));
        ConstDataObjectPath tablePath = state.data()->getObject(DataTable::OOClass(), QString::fromLatin1(BaderChargeIntegrationModifier::tableIdentifier));
        if(tablePath.empty())
            throw Exception(QStringLiteral("%1: table missing").arg(name));
        const DataTable* table = static_object_cast<DataTable>(tablePath.back());
        Result r;
        r.density = column<double>(table, QStringLiteral("Integrated Density"));
        r.volume = column<double>(table, QStringLiteral("Integrated Volume"));
        r.distance = column<double>(table, QStringLiteral("Mapped Particle Distance"));
        r.particle = column<int64_t>(table, QStringLiteral("Mapped Particle Index"));
        {
            BufferReadAccess<Point3> positions(table->getProperty(QStringLiteral("Position")));
            r.position.assign(positions.begin(), positions.end());
        }
        const Particles* particles = state.expectObject<Particles>();
        r.atomDensity = column<double>(particles, QStringLiteral("Integrated Density"));
        r.atomVolume = column<double>(particles, QStringLiteral("Integrated Volume"));
        ConstDataObjectPath gridPath = state.data()->getObject(VoxelGrid::OOClass(), QStringLiteral("imported"));
        const VoxelGrid* grid = static_object_cast<VoxelGrid>(gridPath.back());
        for(int32_t region : column<int32_t>(grid, QStringLiteral("Region"))) {
            if((size_t)region >= r.regionHistogram.size()) r.regionHistogram.resize(region + 1, 0);
            r.regionHistogram[region]++;
        }
        for(double w : column<double>(grid, QStringLiteral("Maximum Region Weight"))) r.weightSum += w;
        return r;
    };

    bool ok = true;
    auto fail = [&](const QString& msg) { qCritical().noquote() << "Bader self-test:" << msg; ok = false; };
    auto close = [](double a, double b, double tol) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); };

    // Results of OVITO Pro 3.16.1 for the same cube files: per region the integrated density, volume, distance and index
    // of the mapped particle and the position of the maximum; per atom the integrated density; the voxel histogram of
    // "Region" and the sum of "Maximum Region Weight". Pro's cube importer (3.16) converts the density from e/bohr^3 to
    // e/A^3 and this one (3.14) does not, so Pro's densities are scaled by bohr^3 (the importer's 0.52917721067 A).
    struct Reference {
        QString file;
        std::vector<double> density, volume, distance;
        std::vector<int64_t> particle;
        std::vector<Point3> position;
        std::vector<double> atomDensity;
        std::vector<int64_t> regionHistogram;
        double weightSum;
    };
    const Reference references[] = {
        {QStringLiteral("ortho.cube"),
         {466.25036181481011, 106.63775926113011, 38.415463246023222},
         {797.50336494823773, 346.84133688943751, 175.65503526022528},
         {9.165618137600071e-07, 4.4408920985006262e-16, 0.2999999095277151},
         {1, 0, 2},
         {{6.4999995684687475, 5.9999996016634594, 7.4999995020793238}, {1.999999867221153, 2.9999998008317297, 3.9999997344423059}, {8.4999994356899009, 0.99999993361057649, 10.499999302911053}},
         {106.63775926113011, 466.25036181481011, 38.415463246023222},
         {6383, 2772, 1405},
         10181.010365986123},
        {QStringLiteral("tric.cube"),
         {465.00440148796611, 212.38122992535119, 106.89970684131879, 39.104347636827804},
         {503.79488590749912, 291.76357823199697, 209.06740772622462, 95.375115573439729},
         {0.21844105306398456, 0.2522108457575466, 0.28095607351852314, 0.12120110404299274},
         {1, 3, 0, 2},
         {{8.317312116427507, 5.9679536881397048, 7.6153892387519706}, {11.192311793263611, 7.8846176698701234, 2.115385899653325}, {1.9294891206082181, 3.1923102076701566, 3.8076946193759853}, {8.7211600548768438, 11.275647678754343, 9.3076979584746304}},
         {106.89970684131879, 465.00440148796611, 39.104347636827804, 212.38122992535119},
         {6899, 3967, 2832, 1278},
         14240.689034529656},
    };
    const double densityScale = std::pow(0.52917721067, 3);

    for(const Reference& ref : references) {
        const QString& name = ref.file;
        Result r = evaluate(name);
        if(!qEnvironmentVariableIsSet("OVITO_BADER_DUMP")) {
            if(r.density.size() != ref.density.size() || r.atomDensity.size() != ref.atomDensity.size()) {
                fail(QStringLiteral("%1: %2 regions, expected %3").arg(name).arg(r.density.size()).arg(ref.density.size()));
                continue;
            }
            for(size_t i = 0; i < ref.density.size(); i++) {
                if(!close(r.density[i], ref.density[i] * densityScale, 1e-9))
                    fail(QStringLiteral("%1 region %2: density %3, expected %4").arg(name).arg(i).arg(r.density[i], 0, 'g', 15).arg(ref.density[i] * densityScale, 0, 'g', 15));
                if(!close(r.volume[i], ref.volume[i], 1e-9))
                    fail(QStringLiteral("%1 region %2: volume %3, expected %4").arg(name).arg(i).arg(r.volume[i], 0, 'g', 15).arg(ref.volume[i], 0, 'g', 15));
                if(!close(r.distance[i], ref.distance[i], 1e-9) || r.particle[i] != ref.particle[i])
                    fail(QStringLiteral("%1 region %2: mapped to particle %3 at %4, expected %5 at %6").arg(name).arg(i).arg(r.particle[i]).arg(r.distance[i]).arg(ref.particle[i]).arg(ref.distance[i]));
                if(!r.position[i].equals(ref.position[i], 1e-9))
                    fail(QStringLiteral("%1 region %2: wrong position of the maximum").arg(name).arg(i));
            }
            for(size_t i = 0; i < ref.atomDensity.size(); i++)
                if(!close(r.atomDensity[i], ref.atomDensity[i] * densityScale, 1e-9))
                    fail(QStringLiteral("%1 atom %2: density %3, expected %4").arg(name).arg(i).arg(r.atomDensity[i], 0, 'g', 15).arg(ref.atomDensity[i] * densityScale, 0, 'g', 15));
            if(r.regionHistogram != ref.regionHistogram)
                fail(QStringLiteral("%1: voxel region histogram differs").arg(name));
            if(!close(r.weightSum, ref.weightSum, 1e-9))
                fail(QStringLiteral("%1: sum of maximum region weights %2, expected %3").arg(name).arg(r.weightSum, 0, 'g', 15).arg(ref.weightSum, 0, 'g', 15));
            // The regions partition the cell.
            const double totalVolume = std::accumulate(r.volume.begin(), r.volume.end(), 0.0);
            const double cellVolume = (name == QStringLiteral("ortho.cube")) ? 1320.0 : 1100.0;
            if(!close(totalVolume, cellVolume, 1e-5))
                fail(QStringLiteral("%1: total region volume %2, cell volume %3").arg(name).arg(totalVolume).arg(cellVolume));
        }
        if(qEnvironmentVariableIsSet("OVITO_BADER_DUMP")) {
            auto list = [](const auto& v) { QStringList s; for(auto x : v) s << QString::number(x, 'g', 17); return s.join(QStringLiteral(", ")); };
            QStringList pos;
            for(const Point3& p : r.position) pos << QStringLiteral("{%1, %2, %3}").arg(p.x(), 0, 'g', 17).arg(p.y(), 0, 'g', 17).arg(p.z(), 0, 'g', 17);
            qInfo().noquote() << name << "\n  density" << list(r.density) << "\n  volume" << list(r.volume) << "\n  distance" << list(r.distance)
                << "\n  particle" << list(r.particle) << "\n  position" << pos.join(QStringLiteral(", ")) << "\n  atomDensity" << list(r.atomDensity)
                << "\n  atomVolume" << list(r.atomVolume) << "\n  hist" << list(r.regionHistogram) << "\n  weightSum" << QString::number(r.weightSum, 'g', 17);
        }
    }
    if(qEnvironmentVariableIsSet("OVITO_BADER_DUMP"))
        return ok;

    qInfo().noquote() << (ok ? "Bader self-test: ALL PASSED" : "Bader self-test: FAILED");
    return ok;
}

}   // End of namespace
