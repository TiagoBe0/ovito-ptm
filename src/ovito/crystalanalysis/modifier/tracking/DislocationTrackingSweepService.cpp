////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
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
#include <ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "DislocationTrackingSweepService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationTrackingSweepService);

namespace {

QCommandLineOption makeOption(const QString& name, const QString& description, const QString& valueName = {}, const QString& defaultValue = {})
{
    if(valueName.isEmpty())
        return QCommandLineOption(QStringList{name}, description);
    return QCommandLineOption(QStringList{name}, description, valueName, defaultValue);
}

QVariant attr(const PipelineFlowState& state, const QString& name, const QVariant& defaultValue = {})
{
    return state.getAttributeValue(name, defaultValue);
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void DislocationTrackingSweepService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track"),
        tr("Run the native Dislocation tracking modifier over a trajectory in batch mode and exit.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-input"),
        tr("Input trajectory/data file or wildcard pattern for the dislocation-tracking run."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-output"),
        tr("CSV output path for the per-frame dislocation-tracking metrics."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-structure"),
        tr("Input crystal structure for the DXA analysis: fcc, bcc or hcp."),
        QStringLiteral("type"),
        QStringLiteral("fcc")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-time-per-frame"),
        tr("Physical time between two consecutive trajectory frames (used to convert drift into velocity)."),
        QStringLiteral("dt"),
        QStringLiteral("1")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-max-matching-distance"),
        tr("Maximum centroid distance for matching segments between frames. 0 = automatic (10% of the smallest cell edge)."),
        QStringLiteral("dist"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-burgers-tol"),
        tr("Relative tolerance for comparing the Burgers vectors of candidate segments."),
        QStringLiteral("rel"),
        QStringLiteral("0.25")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-max-bridge"),
        tr("Maximum number of consecutive frames a dislocation may be missing and still be bridged. 0 disables bridging."),
        QStringLiteral("N"),
        QStringLiteral("2")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-min-track-length"),
        tr("Minimum number of frames a track must span to be kept."),
        QStringLiteral("N"),
        QStringLiteral("1")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-first-frame"),
        tr("First source frame index to write to the CSV. The tracker always scans the whole trajectory."),
        QStringLiteral("N"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-max-frames"),
        tr("Maximum number of source frames to write to the CSV. Negative means all frames."),
        QStringLiteral("N"),
        QStringLiteral("-1")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("dislocation-track-no-sequence-detection"),
        tr("Do not auto-detect numbered file sequences for the input trajectory.")));
}

/******************************************************************************
* Runs the requested batch after the application has entered the main event loop.
******************************************************************************/
void DislocationTrackingSweepService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("dislocation-track")))
        return;

    try {
        runSweep(parseCommandLine(parser));
        QCoreApplication::exit(0);
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Dislocation tracking run failed:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        QCoreApplication::exit(1);
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Dislocation tracking run failed:" << QString::fromLocal8Bit(ex.what());
        QCoreApplication::exit(1);
    }
}

/******************************************************************************
* Parses command-line parameters.
******************************************************************************/
DislocationTrackingSweepService::RunParameters DislocationTrackingSweepService::parseCommandLine(const QCommandLineParser& parser)
{
    RunParameters params;
    params.inputPath = parser.value(QStringLiteral("dislocation-track-input")).trimmed();
    params.outputPath = parser.value(QStringLiteral("dislocation-track-output")).trimmed();
    if(params.inputPath.isEmpty())
        throw Exception(QStringLiteral("--dislocation-track-input is required."));
    if(params.outputPath.isEmpty())
        throw Exception(QStringLiteral("--dislocation-track-output is required."));

    params.structure = parseStructure(parser.value(QStringLiteral("dislocation-track-structure")));

    bool ok = false;
    params.timePerFrame = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("dislocation-track-time-per-frame")), &ok);
    if(!ok || params.timePerFrame <= 0)
        throw Exception(QStringLiteral("--dislocation-track-time-per-frame must be a positive number."));
    params.maxMatchingDistance = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("dislocation-track-max-matching-distance")), &ok);
    if(!ok || params.maxMatchingDistance < 0)
        throw Exception(QStringLiteral("--dislocation-track-max-matching-distance must be a non-negative number."));
    params.burgersTolerance = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("dislocation-track-burgers-tol")), &ok);
    if(!ok || params.burgersTolerance < 0 || params.burgersTolerance > 1)
        throw Exception(QStringLiteral("--dislocation-track-burgers-tol must be in the [0, 1] range."));

    params.maxBridgeGap = parser.value(QStringLiteral("dislocation-track-max-bridge")).toInt(&ok);
    if(!ok || params.maxBridgeGap < 0)
        throw Exception(QStringLiteral("--dislocation-track-max-bridge must be a non-negative integer."));
    params.minTrackLength = parser.value(QStringLiteral("dislocation-track-min-track-length")).toInt(&ok);
    if(!ok || params.minTrackLength < 1)
        throw Exception(QStringLiteral("--dislocation-track-min-track-length must be a positive integer."));
    params.firstFrame = parser.value(QStringLiteral("dislocation-track-first-frame")).toInt(&ok);
    if(!ok || params.firstFrame < 0)
        throw Exception(QStringLiteral("--dislocation-track-first-frame must be a non-negative integer."));
    params.maxFrames = parser.value(QStringLiteral("dislocation-track-max-frames")).toInt(&ok);
    if(!ok)
        throw Exception(QStringLiteral("--dislocation-track-max-frames must be an integer."));

    params.autodetectFileSequences = !parser.isSet(QStringLiteral("dislocation-track-no-sequence-detection"));
    return params;
}

/******************************************************************************
* Creates a file-source pipeline without using GUI import actions.
******************************************************************************/
OORef<Pipeline> DislocationTrackingSweepService::createFilePipeline(const QString& path, bool autodetectFileSequences)
{
    const QString localPath = toLocalPath(path);
    QUrl sourceUrl = QUrl::fromLocalFile(localPath);
    QUrl detectionUrl = sourceUrl;

    const bool isWildcard = FileSourceImporter::isWildcardPattern(sourceUrl);
    if(isWildcard) {
        std::vector<QUrl> matches = FileSourceImporter::findWildcardMatchesResolved(sourceUrl).blockForResult();
        if(matches.empty())
            throw Exception(QStringLiteral("Wildcard pattern did not match any files: %1").arg(localPath));
        detectionUrl = matches.front();
    }

    OORef<FileImporter> genericImporter = FileImporter::autodetectFileFormat(detectionUrl).blockForResult();
    if(!genericImporter)
        throw Exception(QStringLiteral("Could not detect file format: %1").arg(detectionUrl.toString(QUrl::PreferLocalFile)));

    OORef<FileSourceImporter> sourceImporter = dynamic_object_cast<FileSourceImporter>(genericImporter);
    if(!sourceImporter)
        throw Exception(QStringLiteral("Detected importer cannot be used as a pipeline file source: %1").arg(detectionUrl.toString(QUrl::PreferLocalFile)));

    // A single trajectory file holds many timesteps. Unlike numbered file sequences, the importer
    // does not scan a lone file for frames unless multi-timestep mode is enabled, so a whole
    // trajectory would otherwise be read as a single frame. Enable it for non-wildcard inputs; an
    // explicit wildcard already provides the frame sequence through the individual files.
    if(!isWildcard)
        sourceImporter->setMultiTimestepFile(true);

    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({std::move(sourceUrl)}, std::move(sourceImporter), autodetectFileSequences);
    (void) source->updateListOfFrames().blockForResult();

    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    return pipeline;
}

/******************************************************************************
* Parses the input crystal structure name.
******************************************************************************/
StructureAnalysis::LatticeStructureType DislocationTrackingSweepService::parseStructure(const QString& text)
{
    const QString key = text.trimmed().toLower();
    if(key == QStringLiteral("fcc")) return StructureAnalysis::LATTICE_FCC;
    if(key == QStringLiteral("bcc")) return StructureAnalysis::LATTICE_BCC;
    if(key == QStringLiteral("hcp")) return StructureAnalysis::LATTICE_HCP;
    throw Exception(QStringLiteral("Unknown crystal structure '%1'. Use fcc, bcc or hcp.").arg(text.trimmed()));
}

QString DislocationTrackingSweepService::structureName(StructureAnalysis::LatticeStructureType structure)
{
    switch(structure) {
    case StructureAnalysis::LATTICE_FCC: return QStringLiteral("fcc");
    case StructureAnalysis::LATTICE_BCC: return QStringLiteral("bcc");
    case StructureAnalysis::LATTICE_HCP: return QStringLiteral("hcp");
    default: return QStringLiteral("other");
    }
}

/******************************************************************************
* Returns a stable command-line name for a pipeline status type.
******************************************************************************/
QString DislocationTrackingSweepService::statusTypeName(PipelineStatus::StatusType statusType)
{
    switch(statusType) {
    case PipelineStatus::Success: return QStringLiteral("success");
    case PipelineStatus::Warning: return QStringLiteral("warning");
    case PipelineStatus::Error:   return QStringLiteral("error");
    }
    return {};
}

/******************************************************************************
* Path and CSV helpers.
******************************************************************************/
QString DislocationTrackingSweepService::toLocalPath(const QString& path)
{
    QFileInfo info(path);
    if(info.isAbsolute())
        return QDir::cleanPath(path);
    return QDir::cleanPath(QDir::current().absoluteFilePath(path));
}

QString DislocationTrackingSweepService::variantToString(const QVariant& value)
{
    if(!value.isValid() || value.isNull())
        return {};
    switch(value.metaType().id()) {
    case QMetaType::Double:
        return QLocale::c().toString(value.toDouble(), 'g', 16);
    case QMetaType::Float:
        return QLocale::c().toString(value.toFloat(), 'g', 9);
    default:
        break;
    }
    return value.toString();
}

QString DislocationTrackingSweepService::csvField(const QString& value)
{
    QString field = value;
    field.replace('"', QStringLiteral("\"\""));
    if(field.contains(',') || field.contains('"') || field.contains('\n') || field.contains('\r'))
        return QStringLiteral("\"%1\"").arg(field);
    return field;
}

void DislocationTrackingSweepService::writeCsvRow(QTextStream& stream, const QStringList& fields)
{
    for(int i = 0; i < fields.size(); i++) {
        if(i != 0)
            stream << ',';
        stream << csvField(fields[i]);
    }
    stream << '\n';
}

/******************************************************************************
* Runs the native dislocation-tracking batch over the trajectory.
******************************************************************************/
void DislocationTrackingSweepService::runSweep(const RunParameters& params)
{
    qInfo().noquote() << "Dislocation tracking: loading input" << params.inputPath;
    OORef<Pipeline> pipeline = createFilePipeline(params.inputPath, params.autodetectFileSequences);

    // Build the pipeline: FileSource -> Dislocation analysis (DXA) -> Dislocation tracking.
    OORef<DislocationAnalysisModifier> dxa = OORef<DislocationAnalysisModifier>::create();
    dxa->setInputCrystalStructure(params.structure);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, dxa);

    OORef<DislocationTrackingModifier> tracker = OORef<DislocationTrackingModifier>::create();
    tracker->setTimePerFrame(params.timePerFrame);
    tracker->setMaxMatchingDistance(params.maxMatchingDistance);
    tracker->setBurgersTolerance(params.burgersTolerance);
    tracker->setMaxBridgeGap(params.maxBridgeGap);
    tracker->setMinTrackLength(params.minTrackLength);
    tracker->setColorByTrack(false);   // No rendering in batch mode.
    tracker->setShowTrails(false);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, tracker);

    const int sourceFrameCount = pipeline->head()->numberOfSourceFrames();
    if(sourceFrameCount <= 0)
        throw Exception(QStringLiteral("Input source contains no frames."));
    if(params.firstFrame >= sourceFrameCount)
        throw Exception(QStringLiteral("First frame %1 is outside the input frame range 0..%2.").arg(params.firstFrame).arg(sourceFrameCount - 1));

    int endFrame = sourceFrameCount;
    if(params.maxFrames >= 0)
        endFrame = std::min(endFrame, params.firstFrame + params.maxFrames);

    QFileInfo outputInfo(toLocalPath(params.outputPath));
    if(!outputInfo.dir().exists() && !outputInfo.dir().mkpath(QStringLiteral(".")))
        throw Exception(QStringLiteral("Could not create output directory: %1").arg(outputInfo.dir().absolutePath()));

    QFile file(outputInfo.absoluteFilePath());
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Could not open output CSV file for writing: %1").arg(outputInfo.absoluteFilePath()));

    QTextStream out(&file);
    writeCsvRow(out, {
        QStringLiteral("source_frame"),
        QStringLiteral("timestep"),
        QStringLiteral("structure"),
        QStringLiteral("time_per_frame"),
        QStringLiteral("track_count"),
        QStringLiteral("births"),
        QStringLiteral("deaths"),
        QStringLiteral("nucleation"),
        QStringLiteral("annihilation"),
        QStringLiteral("reactions"),
        QStringLiteral("total_density"),
        QStringLiteral("mean_velocity"),
        QStringLiteral("median_velocity"),
        QStringLiteral("max_velocity"),
        QStringLiteral("frank_junctions"),
        QStringLiteral("frank_max_residual"),
        QStringLiteral("frank_mean_residual"),
        QStringLiteral("status_type"),
        QStringLiteral("status_text")
    });

    qInfo().noquote() << "Dislocation tracking: scanning" << sourceFrameCount
                      << "frames (DXA runs on every frame; this can take a while)...";

    qsizetype rowsWritten = 0;
    for(int frame = params.firstFrame; frame < endFrame; frame++) {
        const AnimationTime time = pipeline->head()->sourceFrameToAnimationTime(frame);
        // Non-interactive evaluation: the first call triggers the whole-trajectory tracker scan,
        // subsequent frames reuse the cached tracks.
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(time, true, false)).blockForResult();
        const PipelineStatus& status = state.status();
        writeCsvRow(out, {
            QString::number(attr(state, QStringLiteral("SourceFrame"), frame).toInt()),
            attr(state, QStringLiteral("Timestep"), QVariant()).toString(),
            structureName(params.structure),
            QLocale::c().toString((double)params.timePerFrame, 'g', 9),
            variantToString(attr(state, QStringLiteral("DislocationTracking.track_count"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.births"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.deaths"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.nucleation"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.annihilation"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.reactions"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.total_density"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.mean_velocity"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.median_velocity"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.max_velocity"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.frank_junctions"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.frank_max_residual"))),
            variantToString(attr(state, QStringLiteral("DislocationTracking.frank_mean_residual"))),
            statusTypeName(status.type()),
            status.text()
        });
        rowsWritten++;
    }

    qInfo().noquote() << "Dislocation tracking: wrote" << rowsWritten << "rows to" << outputInfo.absoluteFilePath();
}

}   // End of namespace
