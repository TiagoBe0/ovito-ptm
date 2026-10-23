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

#include <ovito/stdmod/StdMod.h>
#include <ovito/stdmod/modifiers/ExpressionSelectionModifier.h>
#include <ovito/stdmod/modifiers/HistogramModifier.h>
#include <ovito/stdmod/modifiers/TimeSeriesModifier.h>
#include <ovito/stdmod/modifiers/TimeAveragingModifier.h>
#include <ovito/stdmod/modifiers/ReducePropertyModifier.h>
#include <ovito/stdmod/modifiers/ComputePropertyModifier.h>
#include <ovito/stdmod/modifiers/DifferenceBetweenFramesModifier.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "AnalysisModifiersSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AnalysisModifiersSelfTestService);

namespace {

constexpr int numFrames = 5;
constexpr int timestepStride = 100;

/// Writes a 4-atom, 5-frame dump in which atom i sits at x = i + frame, so the number of atoms
/// with x > 2.5 is 1, 2, 3, 4, 4 over the frames, and the timestep is 100 * frame. The cell length
/// along x is 20 + frame, and the atoms are listed in reverse order on odd frames, so per-atom
/// averages only come out right if atoms are matched by their IDs.
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    for(int frame = 0; frame < numFrames; frame++) {
        out << "ITEM: TIMESTEP\n" << frame * timestepStride << "\n";
        out << "ITEM: NUMBER OF ATOMS\n4\n";
        out << "ITEM: BOX BOUNDS ff ff ff\n0 " << (20 + frame) << "\n0 20\n0 20\n";
        out << "ITEM: ATOMS id type x y z\n";
        for(int k = 0; k < 4; k++) {
            int i = (frame % 2) ? 3 - k : k;
            out << (i + 1) << " 1 " << (i + frame) << " 1 1\n";
        }
    }
}

OORef<Pipeline> createPipeline(const QString& path)
{
    QUrl url = QUrl::fromLocalFile(path);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    return pipeline;
}

/// Reads one column (component) of a table property.
std::vector<double> column(const Property* property, size_t component)
{
    std::vector<double> values;
    if(!property)
        return values;
    // Converts from whatever data type the column has (e.g. Int64 histogram counts).
    values.resize(property->size());
    property->copyComponentTo(values.begin(), component);
    return values;
}

bool check(const QString& name, const std::vector<double>& actual, const std::vector<double>& expected)
{
    bool ok = actual.size() == expected.size();
    for(size_t i = 0; ok && i < actual.size(); i++)
        ok = std::isnan(expected[i]) ? std::isnan(actual[i]) : std::abs(actual[i] - expected[i]) < 1e-9;
    QStringList a, e;
    for(double v : actual) a << QString::number(v);
    for(double v : expected) e << QString::number(v);
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << a.join(' ');
    else
        qCritical().noquote() << "  FAIL" << name << ": got [" << a.join(' ') << "] expected [" << e.join(' ') << "]";
    return ok;
}

bool checkValue(const QString& name, const QVariant& actual, double expected)
{
    bool ok = false;
    double x = actual.toDouble(&ok);
    return check(name, ok ? std::vector<double>{x} : std::vector<double>{}, {expected});
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void AnalysisModifiersSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("time-series-selftest")},
        tr("Run the in-process regression test for the Time series modifier and exit "
           "(0 on success, 1 on any failed check).")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("time-averaging-selftest")},
        tr("Run the in-process regression test for the Time averaging modifier and exit "
           "(0 on success, 1 on any failed check).")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("reduce-property-selftest")},
        tr("Run the in-process regression test for the Reduce property modifier and exit "
           "(0 on success, 1 on any failed check).")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("difference-between-frames-selftest")},
        tr("Run the in-process regression test for the Difference between frames modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void AnalysisModifiersSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    const bool timeSeries = parser.isSet(QStringLiteral("time-series-selftest"));
    const bool timeAveraging = parser.isSet(QStringLiteral("time-averaging-selftest"));
    const bool reduceProperty = parser.isSet(QStringLiteral("reduce-property-selftest"));
    const bool differenceBetweenFrames = parser.isSet(QStringLiteral("difference-between-frames-selftest"));
    if(!timeSeries && !timeAveraging && !reduceProperty && !differenceBetweenFrames)
        return;

    bool ok = false;
    try {
        ok = true;
        if(timeSeries)
            ok &= runTimeSeriesTest();
        if(timeAveraging)
            ok &= runTimeAveragingTest();
        if(reduceProperty)
            ok &= runReducePropertyTest();
        if(differenceBetweenFrames)
            ok &= runDifferenceBetweenFramesTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Analysis modifiers self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Analysis modifiers self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Analysis modifiers self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs the TimeSeriesModifier checks.
******************************************************************************/
bool AnalysisModifiersSelfTestService::runTimeSeriesTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("time_series_test.dump"));
    writeDump(dumpPath);

    OORef<Pipeline> pipeline = createPipeline(dumpPath);
    if(pipeline->head()->numberOfSourceFrames() != numFrames)
        throw Exception(QStringLiteral("Expected %1 frames in the test dump, found %2.").arg(numFrames).arg(pipeline->head()->numberOfSourceFrames()));

    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("Position.X > 2.5"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);

    OORef<TimeSeriesModifier> timeSeries = OORef<TimeSeriesModifier>::create();
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, timeSeries);

    // Evaluates the pipeline non-interactively (at the last frame, to make sure the result
    // doesn't depend on the current frame) and returns the generated table.
    auto evaluate = [&]() -> DataOORef<const DataTable> {
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(numFrames - 1), true, false)).blockForResult();
        const DataTable* table = state.getObjectBy<DataTable>(node, QString::fromLatin1(TimeSeriesModifier::tableIdentifier));
        if(!table)
            throw Exception(QStringLiteral("The time series table is missing from the pipeline output."));
        return table;
    };

    const double nan = std::numeric_limits<double>::quiet_NaN();
    bool ok = true;

    qInfo().noquote() << "Time series self-test: default frame axis, two attributes";
    timeSeries->setSourceAttributes({QStringLiteral("ExpressionSelection.count"), QStringLiteral("Timestep")});
    {
        auto table = evaluate();
        ok &= check(QStringLiteral("x = frame"), column(table->x(), 0), {0, 1, 2, 3, 4});
        ok &= check(QStringLiteral("y[count]"), column(table->y(), 0), {1, 2, 3, 4, 4});
        ok &= check(QStringLiteral("y[Timestep]"), column(table->y(), 1), {0, 100, 200, 300, 400});
    }

    qInfo().noquote() << "Time series self-test: Timestep axis, every 2nd frame";
    timeSeries->setSourceAttributes({QStringLiteral("ExpressionSelection.count")});
    timeSeries->setTimeAttribute(QStringLiteral("Timestep"));
    timeSeries->setSamplingFrequency(2);
    {
        auto table = evaluate();
        ok &= check(QStringLiteral("x = Timestep"), column(table->x(), 0), {0, 200, 400});
        ok &= check(QStringLiteral("y[count]"), column(table->y(), 0), {1, 3, 4});
    }

    qInfo().noquote() << "Time series self-test: custom interval 1..3";
    timeSeries->setTimeAttribute({});
    timeSeries->setSamplingFrequency(1);
    timeSeries->setUseCustomInterval(true);
    timeSeries->setCustomIntervalStart(1);
    timeSeries->setCustomIntervalEnd(3);
    {
        auto table = evaluate();
        ok &= check(QStringLiteral("x = frame"), column(table->x(), 0), {1, 2, 3});
        ok &= check(QStringLiteral("y[count]"), column(table->y(), 0), {2, 3, 4});
    }

    qInfo().noquote() << "Time series self-test: missing attribute yields NaN";
    timeSeries->setUseCustomInterval(false);
    timeSeries->setSourceAttributes({QStringLiteral("NoSuchAttribute")});
    {
        auto table = evaluate();
        ok &= check(QStringLiteral("y[missing]"), column(table->y(), 0), {nan, nan, nan, nan, nan});
    }

    qInfo().noquote() << (ok ? "Time series self-test: ALL PASSED" : "Time series self-test: FAILED");
    return ok;
}

/******************************************************************************
* Runs the TimeAveragingModifier checks.
******************************************************************************/
bool AnalysisModifiersSelfTestService::runTimeAveragingTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("time_averaging_test.dump"));
    writeDump(dumpPath);

    OORef<Pipeline> pipeline = createPipeline(dumpPath);

    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("Position.X > 2.5"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);

    // Five bins of width 2 over [0, 10]: the mean counts are 0.6 1.4 1.4 0.6 0.
    OORef<HistogramModifier> histogram = OORef<HistogramModifier>::create();
    histogram->setSourceProperty(PropertyReference(QStringLiteral("Position.X")));
    histogram->setNumberOfBins(5);
    histogram->setNormalizationMode(HistogramModifier::AbsoluteCount);  // Memorized field: do not inherit the GUI setting.
    histogram->setFixXAxisRange(true);
    histogram->setXAxisRangeStart(0);
    histogram->setXAxisRangeEnd(10);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, histogram);

    OORef<TimeAveragingModifier> averaging = OORef<TimeAveragingModifier>::create();
    averaging->setOperateOn({
        QStringLiteral("attribute:ExpressionSelection.count"),
        QStringLiteral("attribute:Timestep"),
        QStringLiteral("property:particles/Position"),
        QStringLiteral("table:histogram[Position.X]"),
        QStringLiteral("cell")});
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, averaging);

    // Evaluate at frame 3, where the atoms are listed in reverse order (IDs 4 3 2 1).
    auto evaluate = [&]() {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(3), true, false)).blockForResult();
    };
    auto table = [](const PipelineFlowState& state, const QString& id) -> const DataTable* {
        ConstDataObjectPath path = state.data()->getObject(DataTable::OOClass(), id);
        if(path.empty())
            throw Exception(QStringLiteral("Data table '%1' is missing from the pipeline output.").arg(id));
        return static_object_cast<DataTable>(path.back());
    };
    auto particleColumn = [](const PipelineFlowState& state, const QString& name, size_t component) {
        const PropertyContainer* particles = static_object_cast<PropertyContainer>(state.data()->getObject(PropertyContainer::OOClass(), QStringLiteral("particles")).back());
        return column(particles->getProperty(name), component);
    };

    bool ok = true;

    qInfo().noquote() << "Time averaging self-test: create new values";
    {
        PipelineFlowState state = evaluate();
        ok &= checkValue(QStringLiteral("ExpressionSelection.count Average"), state.getAttributeValue(QStringLiteral("ExpressionSelection.count Average")), 2.8);
        ok &= checkValue(QStringLiteral("Timestep Average"), state.getAttributeValue(QStringLiteral("Timestep Average")), 200);
        ok &= check(QStringLiteral("Position Average.X (current order, by ID)"), particleColumn(state, QStringLiteral("Position Average"), 0), {5, 4, 3, 2});
        ok &= check(QStringLiteral("Position Average.Y"), particleColumn(state, QStringLiteral("Position Average"), 1), {1, 1, 1, 1});
        ok &= check(QStringLiteral("Position.X unchanged"), particleColumn(state, QStringLiteral("Position"), 0), {6, 5, 4, 3});
        ok &= check(QStringLiteral("histogram average"), column(table(state, QStringLiteral("histogram[Position.X]-average"))->y(), 0), {0.6, 1.4, 1.4, 0.6, 0});
        ok &= check(QStringLiteral("cell average (vector 1)"), column(table(state, QStringLiteral("cell-average"))->y(), 0), {22, 0, 0, 0});
    }

    qInfo().noquote() << "Time averaging self-test: replace original values";
    averaging->setOverwrite(true);
    {
        PipelineFlowState state = evaluate();
        ok &= checkValue(QStringLiteral("Timestep"), state.getAttributeValue(QStringLiteral("Timestep")), 200);
        ok &= check(QStringLiteral("Position.X (current order, by ID)"), particleColumn(state, QStringLiteral("Position"), 0), {5, 4, 3, 2});
        ok &= check(QStringLiteral("histogram counts (rounded integers)"), column(table(state, QStringLiteral("histogram[Position.X]"))->y(), 0), {1, 1, 1, 1, 0});
        const SimulationCell* cell = state.getObject<SimulationCell>();
        ok &= check(QStringLiteral("cell length X"), {cell ? (double)cell->cellMatrix()(0, 0) : -1.0}, {22});
    }

    qInfo().noquote() << "Time averaging self-test: every 2nd frame (0, 2, 4)";
    averaging->setOverwrite(false);
    averaging->setSamplingFrequency(2);
    {
        PipelineFlowState state = evaluate();
        ok &= checkValue(QStringLiteral("ExpressionSelection.count Average"), state.getAttributeValue(QStringLiteral("ExpressionSelection.count Average")), 8.0 / 3.0);
    }

    // With an automatic range the histogram covers [min x, max x] = [f, f + 3] at frame f: the averaged
    // table must carry the mean range [2, 5] (not the range of the current frame) and warn about it.
    qInfo().noquote() << "Time averaging self-test: histogram with automatic range";
    averaging->setSamplingFrequency(1);
    histogram->setFixXAxisRange(false);
    {
        PipelineFlowState state = evaluate();
        const DataTable* averaged = table(state, QStringLiteral("histogram[Position.X]-average"));
        ok &= check(QStringLiteral("averaged x range"), {(double)averaged->intervalStart(), (double)averaged->intervalEnd()}, {2, 5});
        ok &= check(QStringLiteral("warning status"), {state.status().type() == PipelineStatus::Warning ? 1.0 : 0.0}, {1});
    }
    histogram->setFixXAxisRange(true);

    qInfo().noquote() << (ok ? "Time averaging self-test: ALL PASSED" : "Time averaging self-test: FAILED");
    return ok;
}

/******************************************************************************
* Runs the ReducePropertyModifier checks.
******************************************************************************/
bool AnalysisModifiersSelfTestService::runReducePropertyTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("reduce_property_test.dump"));
    writeDump(dumpPath);

    OORef<Pipeline> pipeline = createPipeline(dumpPath);
    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("Position.X > 2.5"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);
    OORef<ReducePropertyModifier> reduce = OORef<ReducePropertyModifier>::create();
    reduce->setSourceProperty(PropertyReference(QStringLiteral("Position")));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, reduce);

    auto evaluate = [&](int frame) {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    };

    bool ok = true;

    // Frame 3 lists the atoms in reverse order: Position.X = 6 5 4 3, Position.Y = 1 1 1 1.
    qInfo().noquote() << "Reduce property self-test: all operations on Position (frame 3, X = 6 5 4 3)";
    const std::vector<std::pair<ReducePropertyModifier::ReductionOperation, double>> expectedX = {
        {ReducePropertyModifier::Min, 3}, {ReducePropertyModifier::Max, 6}, {ReducePropertyModifier::Sum, 18},
        {ReducePropertyModifier::NonzeroCount, 4}, {ReducePropertyModifier::Mean, 4.5}, {ReducePropertyModifier::Median, 4.5},
        {ReducePropertyModifier::Variance, 5.0 / 3.0}, {ReducePropertyModifier::StdDev, std::sqrt(5.0 / 3.0)},
        {ReducePropertyModifier::Argmin, 3}, {ReducePropertyModifier::Argmax, 0}};
    for(const auto& [op, expected] : expectedX) {
        reduce->setOperation(op);
        PipelineFlowState state = evaluate(3);
        QString name = QStringLiteral("Position.X.") + ReducePropertyModifier::operationKey(op);
        ok &= checkValue(name, state.getAttributeValue(name), expected);
    }
    reduce->setOperation(ReducePropertyModifier::Mean);
    {
        PipelineFlowState state = evaluate(3);
        ok &= checkValue(QStringLiteral("Position.Y.mean"), state.getAttributeValue(QStringLiteral("Position.Y.mean")), 1);
    }

    qInfo().noquote() << "Reduce property self-test: single component";
    reduce->setSourceProperty(PropertyReference(QStringLiteral("Position.X")));
    reduce->setOperation(ReducePropertyModifier::Max);
    {
        PipelineFlowState state = evaluate(3);
        ok &= checkValue(QStringLiteral("Position.X.max"), state.getAttributeValue(QStringLiteral("Position.X.max")), 6);
        ok &= check(QStringLiteral("no Position.Y attribute"), {state.getAttributeValue(QStringLiteral("Position.Y.max")).isValid() ? 1.0 : 0.0}, {0});
    }

    qInfo().noquote() << "Reduce property self-test: only selected elements (frame 0, X = 0 1 2 3)";
    reduce->setOnlySelectedElements(true);
    reduce->setOperation(ReducePropertyModifier::Argmax);
    {
        PipelineFlowState state = evaluate(0);
        // Only the atom at x = 3 (index 3) is selected; argmax indexes into the full list.
        ok &= checkValue(QStringLiteral("Position.X.argmax"), state.getAttributeValue(QStringLiteral("Position.X.argmax")), 3);
    }
    selection->setExpression(QStringLiteral("Position.X > 0.5"));
    reduce->setOperation(ReducePropertyModifier::Median);
    {
        PipelineFlowState state = evaluate(0);
        ok &= checkValue(QStringLiteral("Position.X.median (selected 1 2 3)"), state.getAttributeValue(QStringLiteral("Position.X.median")), 2);
    }

    // A property with a NaN: sqrt(X - 4) at frame 3 (X = 6 5 4 3) is sqrt(2) 1 0 NaN.
    qInfo().noquote() << "Reduce property self-test: NaN values (NumPy semantics)";
    OORef<Pipeline> nanPipeline = createPipeline(dumpPath);
    OORef<ComputePropertyModifier> compute = OORef<ComputePropertyModifier>::create();
    compute->setOutputProperty(PropertyReference(QStringLiteral("Root")));
    compute->setExpressions({QStringLiteral("sqrt(Position.X - 4)")});
    nanPipeline->applyModifier(AnimationTime::fromFrame(0), false, compute);
    reduce = OORef<ReducePropertyModifier>::create();
    reduce->setSourceProperty(PropertyReference(QStringLiteral("Root")));
    nanPipeline->applyModifier(AnimationTime::fromFrame(0), false, reduce);
    pipeline = nanPipeline;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for(const auto& [op, expected] : std::vector<std::pair<ReducePropertyModifier::ReductionOperation, double>>{
            {ReducePropertyModifier::Min, nan}, {ReducePropertyModifier::Max, nan}, {ReducePropertyModifier::Median, nan},
            {ReducePropertyModifier::Mean, nan}, {ReducePropertyModifier::Argmin, 3}, {ReducePropertyModifier::Argmax, 3},
            {ReducePropertyModifier::NonzeroCount, 3}}) {
        reduce->setOperation(op);
        PipelineFlowState state = evaluate(3);
        QString name = QStringLiteral("Root.") + ReducePropertyModifier::operationKey(op);
        ok &= checkValue(name, state.getAttributeValue(name), expected);
    }

    qInfo().noquote() << (ok ? "Reduce property self-test: ALL PASSED" : "Reduce property self-test: FAILED");
    return ok;
}

/******************************************************************************
* Runs the DifferenceBetweenFramesModifier checks.
******************************************************************************/
bool AnalysisModifiersSelfTestService::runDifferenceBetweenFramesTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("difference_test.dump"));
    writeDump(dumpPath);

    OORef<Pipeline> pipeline = createPipeline(dumpPath);

    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("Position.X > 2.5"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);

    // Five bins of width 2 over [0, 10]: counts 2 2 0 0 0 at frame 0, 0 1 2 1 0 at frame 3.
    OORef<HistogramModifier> histogram = OORef<HistogramModifier>::create();
    histogram->setSourceProperty(PropertyReference(QStringLiteral("Position.X")));
    histogram->setNumberOfBins(5);
    histogram->setNormalizationMode(HistogramModifier::AbsoluteCount);  // Memorized field: do not inherit the GUI setting.
    histogram->setFixXAxisRange(true);
    histogram->setXAxisRangeStart(0);
    histogram->setXAxisRangeEnd(10);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, histogram);

    OORef<DifferenceBetweenFramesModifier> difference = OORef<DifferenceBetweenFramesModifier>::create();
    difference->setOperateOn({
        QStringLiteral("attribute:ExpressionSelection.count"),
        QStringLiteral("attribute:Timestep"),
        QStringLiteral("property:particles/Position"),
        QStringLiteral("table:histogram[Position.X]")});
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, difference);

    auto evaluate = [&](int frame) {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    };
    auto particleColumn = [](const PipelineFlowState& state, const QString& name, size_t component) {
        const PropertyContainer* particles = static_object_cast<PropertyContainer>(state.data()->getObject(PropertyContainer::OOClass(), QStringLiteral("particles")).back());
        return column(particles->getProperty(name), component);
    };
    auto histogramColumn = [](const PipelineFlowState& state, const QString& name) {
        const DataTable* table = static_object_cast<DataTable>(state.data()->getObject(DataTable::OOClass(), QStringLiteral("histogram[Position.X]")).back());
        return column(table->getProperty(name), 0);
    };
    auto expectError = [&](const QString& name, int frame) {
        try {
            PipelineFlowState state = evaluate(frame);
            if(state.status().type() == PipelineStatus::Error) {
                qInfo().noquote() << "  PASS" << name << ": error status" << state.status().text();
                return true;
            }
        }
        catch(const Exception& ex) {
            qInfo().noquote() << "  PASS" << name << ":" << ex.messages().join(' ');
            return true;
        }
        qCritical().noquote() << "  FAIL" << name << ": expected an error";
        return false;
    };

    bool ok = true;

    // Frame 3 lists the atoms in reverse order (IDs 4 3 2 1); matching by index would give X differences 6 4 2 0.
    qInfo().noquote() << "Difference between frames self-test: constant reference frame 0, evaluated at frame 3";
    {
        PipelineFlowState state = evaluate(3);
        ok &= checkValue(QStringLiteral("ExpressionSelection.count (Difference)"), state.getAttributeValue(QStringLiteral("ExpressionSelection.count (Difference)")), 3);
        QVariant timestep = state.getAttributeValue(QStringLiteral("Timestep (Difference)"));
        ok &= checkValue(QStringLiteral("Timestep (Difference)"), timestep, 300);
        ok &= check(QStringLiteral("Timestep (Difference) is an integer"), {timestep.metaType().id() == QMetaType::LongLong ? 1.0 : 0.0}, {1});
        ok &= check(QStringLiteral("Position (Difference).X (matched by ID)"), particleColumn(state, QStringLiteral("Position (Difference)"), 0), {3, 3, 3, 3});
        ok &= check(QStringLiteral("Position (Difference).Y"), particleColumn(state, QStringLiteral("Position (Difference)"), 1), {0, 0, 0, 0});
        ok &= check(QStringLiteral("Position.X unchanged"), particleColumn(state, QStringLiteral("Position"), 0), {6, 5, 4, 3});
        ok &= check(QStringLiteral("histogram Count (Difference)"), histogramColumn(state, QStringLiteral("Count (Difference)")), {-2, -1, 2, 1, 0});
    }

    qInfo().noquote() << "Difference between frames self-test: constant reference frame 4, evaluated at frame 1";
    difference->setReferenceFrameNumber(4);
    {
        PipelineFlowState state = evaluate(1);
        ok &= checkValue(QStringLiteral("Timestep (Difference)"), state.getAttributeValue(QStringLiteral("Timestep (Difference)")), -300);
        ok &= check(QStringLiteral("Position (Difference).X"), particleColumn(state, QStringLiteral("Position (Difference)"), 0), {-3, -3, -3, -3});
    }

    qInfo().noquote() << "Difference between frames self-test: relative offset -1, evaluated at frames 3 and 1";
    difference->setUseReferenceFrameOffset(true);
    difference->setReferenceFrameOffset(-1);
    {
        PipelineFlowState state = evaluate(3);
        ok &= checkValue(QStringLiteral("ExpressionSelection.count (Difference)"), state.getAttributeValue(QStringLiteral("ExpressionSelection.count (Difference)")), 1);
        ok &= checkValue(QStringLiteral("Timestep (Difference)"), state.getAttributeValue(QStringLiteral("Timestep (Difference)")), 100);
        ok &= check(QStringLiteral("Position (Difference).X"), particleColumn(state, QStringLiteral("Position (Difference)"), 0), {1, 1, 1, 1});
    }
    {
        PipelineFlowState state = evaluate(1);
        ok &= checkValue(QStringLiteral("ExpressionSelection.count (Difference) at frame 1"), state.getAttributeValue(QStringLiteral("ExpressionSelection.count (Difference)")), 1);
    }
    ok &= expectError(QStringLiteral("reference frame -1 out of range at frame 0"), 0);

    // With an automatic range, the histogram bins move between frames and must not be subtracted.
    histogram->setFixXAxisRange(false);
    ok &= expectError(QStringLiteral("histogram with automatic x range"), 3);
    histogram->setFixXAxisRange(true);

    // Second trajectory: 3 atoms at frame 0, a 4th atom (ID 4) appears at frame 1, listed first.
    qInfo().noquote() << "Difference between frames self-test: newly appearing particles";
    const QString growingPath = dir.filePath(QStringLiteral("difference_growing.dump"));
    {
        QFile file(growingPath);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
            throw Exception(QStringLiteral("Cannot write test dump %1").arg(growingPath));
        QTextStream out(&file);
        out << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n3\nITEM: BOX BOUNDS ff ff ff\n0 20\n0 20\n0 20\n";
        out << "ITEM: ATOMS id type x y z\n1 1 1 1 1\n2 1 2 1 1\n3 1 3 1 1\n";
        out << "ITEM: TIMESTEP\n100\nITEM: NUMBER OF ATOMS\n4\nITEM: BOX BOUNDS ff ff ff\n0 20\n0 20\n0 20\n";
        out << "ITEM: ATOMS id type x y z\n4 1 9 1 1\n3 1 3.5 1 1\n2 1 2.5 1 1\n1 1 1.5 1 1\n";
    }
    OORef<Pipeline> growing = createPipeline(growingPath);
    OORef<DifferenceBetweenFramesModifier> growingDifference = OORef<DifferenceBetweenFramesModifier>::create();
    growingDifference->setOperateOn({QStringLiteral("property:particles/Position")});
    growing->applyModifier(AnimationTime::fromFrame(0), false, growingDifference);
    auto evaluateGrowing = [&]() {
        return growing->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(1), true, false)).blockForResult();
    };
    {
        bool raised = false;
        try {
            PipelineFlowState state = evaluateGrowing();
            raised = state.status().type() == PipelineStatus::Error;
        }
        catch(const Exception&) {
            raised = true;
        }
        ok &= check(QStringLiteral("new particle raises an error by default"), {raised ? 1.0 : 0.0}, {1});
    }
    growingDifference->setTolerateNewElements(true);
    growingDifference->setSelectNewElements(true);
    {
        PipelineFlowState state = evaluateGrowing();
        ok &= check(QStringLiteral("Position (Difference).X (new particle = 0)"), particleColumn(state, QStringLiteral("Position (Difference)"), 0), {0, 0.5, 0.5, 0.5});
        ok &= check(QStringLiteral("Selection marks the new particle"), particleColumn(state, QStringLiteral("Selection"), 0), {1, 0, 0, 0});
    }

    qInfo().noquote() << (ok ? "Difference between frames self-test: ALL PASSED" : "Difference between frames self-test: FAILED");
    return ok;
}

}   // End of namespace
