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
#include <ovito/particles/modifier/analysis/binning/SpatialBinningModifier.h>
#include <ovito/stdmod/modifiers/ExpressionSelectionModifier.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "SpatialBinningSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SpatialBinningSelfTestService);

namespace {

/// Writes 8 atoms in a 10x10x10 box. Frame 0 is fully periodic, frame 1 is non-periodic along x.
/// Atom 5 sits at x = -0.5: it wraps into the last x bin on frame 0 and is discarded on frame 1.
/// Frame 2 shifts the cell origin to (-5,-5,-5); frame 3 is triclinic with tilt xy = 4 (|b| = sqrt(116)).
///
///   id   x    y    z    vx
///    1  0.5  0.5  0.5   1
///    2  1.5  0.5  0.5   3
///    3  2.5  0.5  0.5   5
///    4  9.5  0.5  0.5   7
///    5 -0.5  0.5  0.5   9
///    6  4.9  7.0  3.0   2
///    7  5.1  2.0  8.0   4
///    8  7.5  9.0  1.0   6
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    const char* atoms =
        "1 1 0.5 0.5 0.5 1 0 0\n"
        "2 1 1.5 0.5 0.5 3 0 0\n"
        "3 1 2.5 0.5 0.5 5 0 0\n"
        "4 1 9.5 0.5 0.5 7 0 0\n"
        "5 1 -0.5 0.5 0.5 9 0 0\n"
        "6 1 4.9 7.0 3.0 2 0 0\n"
        "7 1 5.1 2.0 8.0 4 0 0\n"
        "8 1 7.5 9.0 1.0 6 0 0\n";
    for(int frame = 0; frame < 4; frame++) {
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n8\n";
        if(frame < 2)
            out << "ITEM: BOX BOUNDS " << (frame == 0 ? "pp" : "ff") << " pp pp\n0 10\n0 10\n0 10\n";
        else if(frame == 2)
            out << "ITEM: BOX BOUNDS pp pp pp\n-5 5\n-5 5\n-5 5\n";
        else
            out << "ITEM: BOX BOUNDS xy xz yz pp pp pp\n0 14 4\n0 10 0\n0 10 0\n";
        out << "ITEM: ATOMS id type x y z vx vy vz\n" << atoms;
    }
}

std::vector<double> column(const Property* property, size_t component = 0)
{
    std::vector<double> values;
    if(!property)
        return values;
    values.resize(property->size());
    property->copyComponentTo(values.begin(), component);
    return values;
}

bool check(const QString& name, const std::vector<double>& actual, const std::vector<double>& expected)
{
    bool ok = actual.size() == expected.size();
    for(size_t i = 0; ok && i < actual.size(); i++)
        ok = std::abs(actual[i] - expected[i]) < 1e-9;
    QStringList a, e;
    for(double v : actual) a << QString::number(v);
    for(double v : expected) e << QString::number(v);
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << a.join(' ');
    else
        qCritical().noquote() << "  FAIL" << name << ": got [" << a.join(' ') << "] expected [" << e.join(' ') << "]";
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void SpatialBinningSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("spatial-binning-selftest")},
        tr("Run the in-process regression test for the Spatial binning modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void SpatialBinningSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("spatial-binning-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Spatial binning self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Spatial binning self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Spatial binning self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool SpatialBinningSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("spatial_binning_test.dump"));
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

    // Only consulted when the binning is restricted to selected particles.
    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("ParticleIdentifier <= 4"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);

    OORef<SpatialBinningModifier> binning = OORef<SpatialBinningModifier>::create();
    // Memorized fields: set every one explicitly so GUI defaults cannot leak into the test.
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setBinDirection(SpatialBinningModifier::X);
    binning->setNumberOfBinsX(2);
    binning->setNumberOfBinsY(2);
    binning->setNumberOfBinsZ(2);
    binning->setReductionOperation(SpatialBinningModifier::Mean);
    binning->setFixPropertyAxisRange(false);
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, binning);

    auto evaluate = [&](int frame) {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    };
    auto table = [&](int frame) -> std::pair<std::vector<double>, std::vector<double>> {
        PipelineFlowState state = evaluate(frame);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        if(!t)
            throw Exception(QStringLiteral("The binning table is missing from the pipeline output: %1").arg(state.status().text()));
        ConstPropertyPtr x = t->getXValues();
        return { column(x), column(t->y()) };
    };
    auto grid = [&](int frame) -> std::pair<std::vector<double>, std::vector<double>> {
        PipelineFlowState state = evaluate(frame);
        const VoxelGrid* g = state.getObjectBy<VoxelGrid>(node, QStringLiteral("binning"));
        if(!g || g->properties().empty())
            throw Exception(QStringLiteral("The binning grid is missing from the pipeline output: %1").arg(state.status().text()));
        return { {(double)g->shape()[0], (double)g->shape()[1], (double)g->shape()[2]}, column(g->properties().back()) };
    };

    bool ok = true;

    // Two x bins [0,5) and [5,10): atoms 1 2 3 6 (vx 1 3 5 2) and 4 5 7 8 (vx 7 9 4 6, atom 5 wrapped).
    qInfo().noquote() << "Spatial binning self-test: 1D reductions of Velocity.X (periodic, 2 bins)";
    const std::vector<std::pair<SpatialBinningModifier::ReductionOperation, std::vector<double>>> expected1D = {
        {SpatialBinningModifier::Mean, {2.75, 6.5}},
        {SpatialBinningModifier::Sum, {11, 26}},
        {SpatialBinningModifier::SumVol, {11.0 / 500, 26.0 / 500}},
        {SpatialBinningModifier::Min, {1, 4}},
        {SpatialBinningModifier::Max, {5, 9}}};
    for(const auto& [op, values] : expected1D) {
        binning->setReductionOperation(op);
        ok &= check(QStringLiteral("op %1").arg((int)op), table(0).second, values);
    }
    ok &= check(QStringLiteral("bin centers"), table(0).first, {2.5, 7.5});

    qInfo().noquote() << "Spatial binning self-test: particle count without input property";
    binning->setSourceProperty({});
    binning->setReductionOperation(SpatialBinningModifier::Sum);
    ok &= check(QStringLiteral("count (periodic)"), table(0).second, {4, 4});
    ok &= check(QStringLiteral("count (non-periodic x drops atom 5)"), table(1).second, {4, 3});

    qInfo().noquote() << "Spatial binning self-test: only selected particles (ids 1-4)";
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setOnlySelectedElements(true);
    ok &= check(QStringLiteral("sum of selected"), table(0).second, {9, 7});
    binning->setOnlySelectedElements(false);

    // Five bins of width 2: mean vx = 2 5 3 6 8; periodic central differences with h = 2.
    qInfo().noquote() << "Spatial binning self-test: periodic first derivative (5 bins)";
    binning->setNumberOfBinsX(5);
    binning->setReductionOperation(SpatialBinningModifier::Mean);
    ok &= check(QStringLiteral("mean profile"), table(0).second, {2, 5, 3, 6, 8});
    binning->setFirstDerivative(true);
    ok &= check(QStringLiteral("derivative"), table(0).second, {-0.75, 0.25, 0.25, 1.25, -1});
    // Non-periodic x (frame 1): one-sided differences at the ends. Values measured in OVITO Pro 3.16.1.
    ok &= check(QStringLiteral("derivative (non-periodic)"), table(1).second, {1.5, 0.25, 0.25, 1.0, 0.5});
    {
        PipelineFlowState state = evaluate(0);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        ok &= check(QStringLiteral("derivative column named as in Pro"), {t && t->y()->name() == QStringLiteral("d(Velocity.X)/d(Position)") ? 1.0 : 0.0}, {1});
    }
    binning->setFirstDerivative(false);
    {
        PipelineFlowState state = evaluate(0);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        ok &= check(QStringLiteral("table title and x label as in Pro"),
            {t && t->title() == QStringLiteral("Velocity.X") && t->axisLabelX() == QStringLiteral("Position") && t->plotMode() == DataTable::Histogram ? 1.0 : 0.0}, {1});
    }
    binning->setNumberOfBinsX(2);

    // As in OVITO Pro, a 1D binning along Y takes its bin count from the first field (2 bins, not 4).
    qInfo().noquote() << "Spatial binning self-test: 1D along Y uses the first bin count";
    binning->setBinDirection(SpatialBinningModifier::Y);
    binning->setNumberOfBinsY(4);
    ok &= check(QStringLiteral("Y mean (2 bins)"), table(0).second, {29.0 / 6.0, 4});
    binning->setNumberOfBinsY(2);
    binning->setBinDirection(SpatialBinningModifier::X);

    // As in OVITO Pro 3.16.1 (measured): the x-axis runs from 0 to the length of the binned cell vector,
    // ignoring the cell origin, and the derivative step is that length divided by the bin count.
    qInfo().noquote() << "Spatial binning self-test: x-axis range with shifted origin and triclinic cell";
    auto interval = [&](int frame) -> std::vector<double> {
        PipelineFlowState state = evaluate(frame);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        if(!t)
            throw Exception(QStringLiteral("The binning table is missing from the pipeline output: %1").arg(state.status().text()));
        return { t->intervalStart(), t->intervalEnd() };
    };
    ok &= check(QStringLiteral("range (shifted origin)"), interval(2), {0, 10});
    binning->setBinDirection(SpatialBinningModifier::Y);
    ok &= check(QStringLiteral("range along tilted b"), interval(3), {0, std::sqrt(116.0)});
    binning->setBinDirection(SpatialBinningModifier::X);

    // Voxel index = z*4 + y*2 + x.
    qInfo().noquote() << "Spatial binning self-test: 2D (XY) max and 3D count";
    binning->setBinDirection(SpatialBinningModifier::XY);
    binning->setReductionOperation(SpatialBinningModifier::Max);
    {
        auto [shape, values] = grid(0);
        ok &= check(QStringLiteral("2D shape"), shape, {2, 2, 1});
        ok &= check(QStringLiteral("2D max"), values, {5, 9, 2, 6});
    }
    binning->setBinDirection(SpatialBinningModifier::XYZ);
    binning->setSourceProperty({});
    binning->setReductionOperation(SpatialBinningModifier::Sum);
    {
        auto [shape, values] = grid(0);
        ok &= check(QStringLiteral("3D shape"), shape, {2, 2, 2});
        ok &= check(QStringLiteral("3D count"), values, {3, 2, 1, 1, 0, 1, 0, 0});
    }

    // XZ with bin counts (2, 4, 3): the grid is 2 x 4 x 1 (x along the first axis, z along the second),
    // the derivative option is ignored, and the domain is a 2D cell. Values measured in OVITO Pro 3.16.1.
    qInfo().noquote() << "Spatial binning self-test: 2D (XZ) grid layout as in OVITO Pro";
    binning->setBinDirection(SpatialBinningModifier::XZ);
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setNumberOfBinsY(4);
    binning->setNumberOfBinsZ(3);
    binning->setFirstDerivative(true);
    for(const auto& [op, values] : std::vector<std::pair<SpatialBinningModifier::ReductionOperation, std::vector<double>>>{
            {SpatialBinningModifier::Min, {1, 6, 2, 0, 0, 0, 0, 4}},
            {SpatialBinningModifier::Mean, {3, 22.0 / 3.0, 2, 0, 0, 0, 0, 4}},
            {SpatialBinningModifier::Sum, {9, 22, 2, 0, 0, 0, 0, 4}}}) {
        binning->setReductionOperation(op);
        auto [shape, actual] = grid(0);
        ok &= check(QStringLiteral("XZ shape"), shape, {2, 4, 1});
        ok &= check(QStringLiteral("XZ op %1").arg((int)op), actual, values);
    }
    {
        PipelineFlowState state = evaluate(0);
        const VoxelGrid* g = state.getObjectBy<VoxelGrid>(node, QStringLiteral("binning"));
        const AffineTransformation& m = g->domain()->cellMatrix();
        ok &= check(QStringLiteral("XZ domain vectors"), {m(0, 0), m(2, 1), m(2, 2), g->domain()->is2D() ? 1.0 : 0.0}, {10, 10, 1, 1});
    }

    qInfo().noquote() << (ok ? "Spatial binning self-test: ALL PASSED" : "Spatial binning self-test: FAILED");
    return ok;
}

}   // End of namespace
