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



#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/modifier/dxa/DislocationAnalysisModifier.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "DislocationAnalysisSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <numbers>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationAnalysisSelfTestService);

namespace {

using Vec = std::array<double, 3>;

struct Configuration {
    std::vector<Vec> positions;
    Vec boxLow, boxHigh;
    bool periodicXY;
};

/// Writes a single-frame LAMMPS dump. Non-periodic directions use shrink-wrapped bounds.
void writeDump(const QString& path, const Configuration& config)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(10);
    out << "ITEM: TIMESTEP\n0\n";
    out << "ITEM: NUMBER OF ATOMS\n" << config.positions.size() << "\n";
    out << "ITEM: BOX BOUNDS " << (config.periodicXY ? "pp pp pp" : "ss ss pp") << "\n";
    for(int dim = 0; dim < 3; dim++)
        out << config.boxLow[dim] << " " << config.boxHigh[dim] << "\n";
    out << "ITEM: ATOMS id type x y z\n";
    int id = 1;
    for(const Vec& p : config.positions)
        out << id++ << " 1 " << p[0] << " " << p[1] << " " << p[2] << "\n";
}

/// Cubic lattice (fcc or cubic diamond, lattice constant a) in the frame x=[001], y=[110], z=[-110],
/// with nx, ny, nz periods along the three axes (a, a/sqrt(2), a/sqrt(2)). If withScrew is set, a
/// screw dislocation with b = a/2[-110] along z is inserted through the center of the xy cross-section
/// and the x and y directions become free surfaces.
Configuration rotatedCubicCrystal(double a, bool diamond, int nx, int ny, int nz, bool withScrew)
{
    const double s = 1.0 / std::sqrt(2.0);
    const Vec L = {nx * a, ny * a * s, nz * a * s};
    std::vector<Vec> basis = {{0, 0, 0}, {0.5, 0.5, 0}, {0.5, 0, 0.5}, {0, 0.5, 0.5}};
    if(diamond) {
        for(int i = 0; i < 4; i++)
            basis.push_back({basis[i][0] + 0.25, basis[i][1] + 0.25, basis[i][2] + 0.25});
    }
    // Small offset keeps lattice sites away from the box faces.
    const Vec offset = {0.013 * a, 0.017 * a, 0.011 * a};
    const int range = nx + ny + nz + 2;
    Configuration config;
    for(int i = -range; i <= range; i++) for(int j = -range; j <= range; j++) for(int k = -range; k <= range; k++) {
        for(const Vec& b : basis) {
            const double cx = (i + b[0]) * a, cy = (j + b[1]) * a, cz = (k + b[2]) * a;
            Vec r = {cz + offset[0], (cx + cy) * s + offset[1], (cy - cx) * s + offset[2]};
            if(r[0] >= 0 && r[0] < L[0] && r[1] >= 0 && r[1] < L[1] && r[2] >= 0 && r[2] < L[2])
                config.positions.push_back(r);
        }
    }
    if(withScrew) {
        const double burgers = a * s;
        const double x0 = L[0] / 2 + 0.1 * a, y0 = L[1] / 2 + 0.07 * a;
        for(Vec& r : config.positions)
            r[2] = std::fmod(r[2] + burgers / (2 * std::numbers::pi) * std::atan2(r[1] - y0, r[0] - x0) + L[2], L[2]);
    }
    config.boxLow = {0, 0, 0};
    config.boxHigh = L;
    config.periodicXY = !withScrew;
    return config;
}

/// Perfect hexagonal diamond (lonsdaleite) in an orthorhombic cell (a, a*sqrt(3), c) per repeat unit.
Configuration hexagonalDiamondCrystal(double a, int nx, int ny, int nz)
{
    const double c = a * std::sqrt(8.0 / 3.0);
    const Vec cellSize = {a, a * std::sqrt(3.0), c};
    // Hcp sites in the orthorhombic cell, each with a bonded partner at +3/8 c.
    const std::vector<Vec> hcp = {{0, 0, 0}, {0.5, 0.5, 0}, {0.5, 1.0 / 6.0, 0.5}, {0, 2.0 / 3.0, 0.5}};
    Configuration config;
    for(int i = 0; i < nx; i++) for(int j = 0; j < ny; j++) for(int k = 0; k < nz; k++) {
        for(const Vec& h : hcp) {
            for(double dz : {0.0, 3.0 / 8.0}) {
                config.positions.push_back({(i + h[0] + 0.01) * cellSize[0], (j + h[1] + 0.01) * cellSize[1], (k + h[2] + dz + 0.01) * cellSize[2]});
            }
        }
    }
    config.boxLow = {0, 0, 0};
    config.boxHigh = {nx * cellSize[0], ny * cellSize[1], nz * cellSize[2]};
    config.periodicXY = true;
    return config;
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

struct Case {
    QString label;
    Configuration config;
    StructureAnalysis::LatticeStructureType lattice;
    QString countAttribute;
    double expectedLineLength;  // 0 = perfect crystal
};

/// Runs DXA on the case and checks the crystalline fraction and the dislocation content.
bool checkCase(const QDir& dir, const Case& c, bool usePTM)
{
    const QString dumpPath = dir.filePath(c.label + QStringLiteral(".dump"));
    writeDump(dumpPath, c.config);
    const QUrl url = QUrl::fromLocalFile(dumpPath);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<DislocationAnalysisModifier> dxa = OORef<DislocationAnalysisModifier>::create();
    dxa->setInputCrystalStructure(c.lattice);
    dxa->setUsePTM(usePTM);
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, dxa);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    const QVariant totalLength = state.getAttributeValue(node, QStringLiteral("DislocationAnalysis.total_line_length"));
    if(!totalLength.isValid())
        throw Exception(QStringLiteral("DXA produced no output for %1: %2").arg(c.label, state.status().text()));

    const QString label = QStringLiteral("%1 (%2)").arg(c.label, usePTM ? "PTM" : "CNA");
    const double numAtoms = c.config.positions.size();
    const double crystalline = state.getAttributeValue(node, c.countAttribute).toDouble() / numAtoms;
    // With a dislocation, the core and the free surfaces are not crystalline.
    const double minCrystalline = c.expectedLineLength > 0 ? 0.6 : 1.0;
    bool ok = check(label + QStringLiteral(" crystalline fraction"), crystalline >= minCrystalline,
        QStringLiteral("%1 (minimum %2)").arg(crystalline).arg(minCrystalline));

    // Collect the per-Burgers-vector line lengths.
    QStringList families;
    double knownLength = 0;
    for(const auto& [key, value] : state.buildAttributesMap().asKeyValueRange()) {
        if(!key.startsWith(QStringLiteral("DislocationAnalysis.length.")) || value.toDouble() == 0)
            continue;
        families << QStringLiteral("%1=%2").arg(key.mid(27)).arg(value.toDouble());
        if(!key.endsWith(QStringLiteral(".other")))
            knownLength += value.toDouble();
    }
    if(c.expectedLineLength == 0) {
        ok &= check(label + QStringLiteral(" line length"), totalLength.toDouble() == 0,
            QStringLiteral("%1 (expected 0)").arg(totalLength.toDouble()));
    }
    else {
        // One straight a/2<110> line spanning the periodic direction.
        const bool singleFamily = families.size() == 1 && families.front().startsWith(QStringLiteral("1/2<"));
        ok &= check(label + QStringLiteral(" Burgers vector"), singleFamily, families.join(QStringLiteral(", ")));
        ok &= check(label + QStringLiteral(" line length"), std::abs(knownLength / c.expectedLineLength - 1) < 0.05,
            QStringLiteral("%1 (expected %2)").arg(knownLength).arg(c.expectedLineLength));
    }
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void DislocationAnalysisSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("dxa-selftest")},
        tr("Run the in-process regression test for the Dislocation analysis (DXA) modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void DislocationAnalysisSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("dxa-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "DXA self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "DXA self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect here; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("DXA self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool DislocationAnalysisSelfTestService::runTest()
{
    QTemporaryDir tmp;
    if(!tmp.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QDir dir(tmp.path());

    const double aAu = 4.08, aSi = 5.431;
    const int nz = 8;
    const std::vector<Case> cases = {
        {QStringLiteral("fcc screw"), rotatedCubicCrystal(aAu, false, 10, 14, nz, true),
            StructureAnalysis::LATTICE_FCC, QStringLiteral("DislocationAnalysis.counts.FCC"), nz * aAu / std::sqrt(2.0)},
        {QStringLiteral("cubic diamond perfect"), rotatedCubicCrystal(aSi, true, 4, 6, 6, false),
            StructureAnalysis::LATTICE_CUBIC_DIAMOND, QStringLiteral("DislocationAnalysis.counts.CubicDiamond"), 0},
        {QStringLiteral("cubic diamond screw"), rotatedCubicCrystal(aSi, true, 8, 12, nz, true),
            StructureAnalysis::LATTICE_CUBIC_DIAMOND, QStringLiteral("DislocationAnalysis.counts.CubicDiamond"), nz * aSi / std::sqrt(2.0)},
        {QStringLiteral("hexagonal diamond perfect"), hexagonalDiamondCrystal(aSi / std::sqrt(2.0), 6, 4, 3),
            StructureAnalysis::LATTICE_HEX_DIAMOND, QStringLiteral("DislocationAnalysis.counts.HexagonalDiamond"), 0},
    };

    bool ok = true;
    for(const Case& c : cases) {
        qInfo().noquote() << "DXA self-test:" << c.label << QStringLiteral("(%1 atoms)").arg(c.config.positions.size());
        for(bool usePTM : {false, true})
            ok &= checkCase(dir, c, usePTM);
    }

    qInfo().noquote() << (ok ? "DXA self-test: ALL PASSED" : "DXA self-test: FAILED");
    return ok;
}

}   // End of namespace
