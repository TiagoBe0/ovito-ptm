////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonModifier.h>
#include <ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifier.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "AmorphousCarbonTrackingSweepService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonTrackingSweepService);

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
void AmorphousCarbonTrackingSweepService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track"),
        tr("Run Amorphous carbon cluster analysis + tracking over a trajectory in batch mode and exit.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-input"),
        tr("Input trajectory/data file or wildcard pattern for the carbon-tracking run (e.g. 'cycle*.data')."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-output"),
        tr("CSV output path for the per-frame cluster and tracking metrics."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-coordination-cutoff"),
        tr("C-C neighbor cutoff used to determine coordination/hybridization, in angstrom."),
        QStringLiteral("dist"),
        QStringLiteral("2.064")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-bond-cutoff"),
        tr("C-C cutoff used to connect atoms of the selected hybridization state, in angstrom. "
           "Defaults to the coordination cutoff; shorter values discard real bonds."),
        QStringLiteral("dist"),
        QStringLiteral("2.064")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-species"),
        tr("Which hybridization state's connected domains to track: sp2 or sp3."),
        QStringLiteral("type"),
        QStringLiteral("sp2")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-min-cluster-size"),
        tr("Minimum cluster size to count as scientifically relevant."),
        QStringLiteral("N"),
        QStringLiteral("3")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-min-same-phase-neighbors"),
        tr("Cluster only atoms having at least this many same-phase neighbors. 0 clusters the whole "
           "phase, which is degenerate for a percolating network; 3 clusters its core."),
        QStringLiteral("N"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-genealogy-threshold"),
        tr("Minimum overlap fraction (of either cluster's own size) for a shared-atom overlap to "
           "count as a genealogical edge in the bidirectional Stable/Fragmented/Merged/Created/"
           "Disappeared cluster classification. Validated robust across 0.05-0.30."),
        QStringLiteral("fraction"),
        QStringLiteral("0.10")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-rings"),
        tr("Also detect closed primitive (shortest-path) rings in the same-phase bond graph "
           "and report per-frame ring counts. Costs meaningfully more than clustering alone.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-max-ring-size"),
        tr("Largest ring size (in atoms) the ring search looks for. Only used with --carbon-track-rings."),
        QStringLiteral("N"),
        QStringLiteral("8")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-min-ring-size"),
        tr("Smallest ring size (in atoms) the ring search reports; raise to exclude small rings "
           "such as triangles. Only used with --carbon-track-rings."),
        QStringLiteral("N"),
        QStringLiteral("3")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-reference-frame"),
        tr("Source frame number to use as the fixed reference configuration (typically cycle 0)."),
        QStringLiteral("N"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-first-frame"),
        tr("First source frame index to write to the CSV."),
        QStringLiteral("N"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-max-frames"),
        tr("Maximum number of source frames to write to the CSV. Negative means all frames."),
        QStringLiteral("N"),
        QStringLiteral("-1")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("carbon-track-no-sequence-detection"),
        tr("Do not auto-detect numbered file sequences for the input trajectory.")));
}

/******************************************************************************
* Runs the requested batch after the application has entered the main event loop.
******************************************************************************/
void AmorphousCarbonTrackingSweepService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("carbon-track")))
        return;

    try {
        runSweep(parseCommandLine(parser));
        QCoreApplication::exit(0);
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Amorphous carbon tracking run failed:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        QCoreApplication::exit(1);
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Amorphous carbon tracking run failed:" << QString::fromLocal8Bit(ex.what());
        QCoreApplication::exit(1);
    }
}

/******************************************************************************
* Parses command-line parameters.
******************************************************************************/
AmorphousCarbonTrackingSweepService::RunParameters AmorphousCarbonTrackingSweepService::parseCommandLine(const QCommandLineParser& parser)
{
    RunParameters params;
    params.inputPath = parser.value(QStringLiteral("carbon-track-input")).trimmed();
    params.outputPath = parser.value(QStringLiteral("carbon-track-output")).trimmed();
    if(params.inputPath.isEmpty())
        throw Exception(QStringLiteral("--carbon-track-input is required."));
    if(params.outputPath.isEmpty())
        throw Exception(QStringLiteral("--carbon-track-output is required."));

    bool ok = false;
    params.coordinationCutoff = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("carbon-track-coordination-cutoff")), &ok);
    if(!ok || params.coordinationCutoff <= 0)
        throw Exception(QStringLiteral("--carbon-track-coordination-cutoff must be a positive number."));
    params.bondCutoff = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("carbon-track-bond-cutoff")), &ok);
    if(!ok || params.bondCutoff <= 0)
        throw Exception(QStringLiteral("--carbon-track-bond-cutoff must be a positive number."));

    const QString speciesText = parser.value(QStringLiteral("carbon-track-species")).trimmed().toLower();
    if(speciesText == QStringLiteral("sp2")) params.clusterSpecies = 2;
    else if(speciesText == QStringLiteral("sp3")) params.clusterSpecies = 3;
    else throw Exception(QStringLiteral("Unknown --carbon-track-species '%1'. Use sp2 or sp3.").arg(speciesText));

    params.minimumClusterSize = parser.value(QStringLiteral("carbon-track-min-cluster-size")).toInt(&ok);
    if(!ok || params.minimumClusterSize < 1)
        throw Exception(QStringLiteral("--carbon-track-min-cluster-size must be a positive integer."));
    params.minSamePhaseNeighbors = parser.value(QStringLiteral("carbon-track-min-same-phase-neighbors")).toInt(&ok);
    if(!ok || params.minSamePhaseNeighbors < 0)
        throw Exception(QStringLiteral("--carbon-track-min-same-phase-neighbors must be a non-negative integer."));
    params.genealogyOverlapFraction = (FloatType)QLocale::c().toDouble(parser.value(QStringLiteral("carbon-track-genealogy-threshold")), &ok);
    if(!ok || params.genealogyOverlapFraction <= 0 || params.genealogyOverlapFraction > 1)
        throw Exception(QStringLiteral("--carbon-track-genealogy-threshold must be a number in (0, 1]."));
    params.ringAnalysisEnabled = parser.isSet(QStringLiteral("carbon-track-rings"));
    params.maxRingSize = parser.value(QStringLiteral("carbon-track-max-ring-size")).toInt(&ok);
    if(!ok || params.maxRingSize < 3)
        throw Exception(QStringLiteral("--carbon-track-max-ring-size must be an integer of at least 3."));
    params.minRingSize = parser.value(QStringLiteral("carbon-track-min-ring-size")).toInt(&ok);
    if(!ok || params.minRingSize < 3)
        throw Exception(QStringLiteral("--carbon-track-min-ring-size must be an integer of at least 3."));
    if(params.minRingSize > params.maxRingSize)
        throw Exception(QStringLiteral("--carbon-track-min-ring-size cannot exceed --carbon-track-max-ring-size."));
    params.referenceFrame = parser.value(QStringLiteral("carbon-track-reference-frame")).toInt(&ok);
    if(!ok || params.referenceFrame < 0)
        throw Exception(QStringLiteral("--carbon-track-reference-frame must be a non-negative integer."));
    params.firstFrame = parser.value(QStringLiteral("carbon-track-first-frame")).toInt(&ok);
    if(!ok || params.firstFrame < 0)
        throw Exception(QStringLiteral("--carbon-track-first-frame must be a non-negative integer."));
    params.maxFrames = parser.value(QStringLiteral("carbon-track-max-frames")).toInt(&ok);
    if(!ok)
        throw Exception(QStringLiteral("--carbon-track-max-frames must be an integer."));

    params.autodetectFileSequences = !parser.isSet(QStringLiteral("carbon-track-no-sequence-detection"));
    return params;
}

/******************************************************************************
* Creates a file-source pipeline without using GUI import actions.
******************************************************************************/
OORef<Pipeline> AmorphousCarbonTrackingSweepService::createFilePipeline(const QString& path, bool autodetectFileSequences)
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
* Returns a stable command-line name for a pipeline status type.
******************************************************************************/
QString AmorphousCarbonTrackingSweepService::statusTypeName(PipelineStatus::StatusType statusType)
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
QString AmorphousCarbonTrackingSweepService::toLocalPath(const QString& path)
{
    QFileInfo info(path);
    if(info.isAbsolute())
        return QDir::cleanPath(path);
    return QDir::cleanPath(QDir::current().absoluteFilePath(path));
}

QString AmorphousCarbonTrackingSweepService::variantToString(const QVariant& value)
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

QString AmorphousCarbonTrackingSweepService::csvField(const QString& value)
{
    QString field = value;
    field.replace('"', QStringLiteral("\"\""));
    if(field.contains(',') || field.contains('"') || field.contains('\n') || field.contains('\r'))
        return QStringLiteral("\"%1\"").arg(field);
    return field;
}

void AmorphousCarbonTrackingSweepService::writeCsvRow(QTextStream& stream, const QStringList& fields)
{
    for(int i = 0; i < fields.size(); i++) {
        if(i != 0)
            stream << ',';
        stream << csvField(fields[i]);
    }
    stream << '\n';
}

/******************************************************************************
* Runs the native amorphous-carbon cluster analysis + tracking batch over the trajectory.
******************************************************************************/
void AmorphousCarbonTrackingSweepService::runSweep(const RunParameters& params)
{
    qInfo().noquote() << "Amorphous carbon tracking: loading input" << params.inputPath;
    OORef<Pipeline> pipeline = createFilePipeline(params.inputPath, params.autodetectFileSequences);

    // Build the pipeline: FileSource -> Amorphous carbon cluster analysis -> Amorphous carbon cluster tracking.
    OORef<AmorphousCarbonModifier> clusterMod = OORef<AmorphousCarbonModifier>::create();
    clusterMod->setCoordinationCutoff(params.coordinationCutoff);
    clusterMod->setBondCutoff(params.bondCutoff);
    clusterMod->setClusterSpecies(static_cast<AmorphousCarbonModifier::ClusterSpecies>(params.clusterSpecies));
    clusterMod->setMinimumClusterSize(params.minimumClusterSize);
    clusterMod->setMinSamePhaseNeighbors(params.minSamePhaseNeighbors);
    clusterMod->setColorMode(AmorphousCarbonModifier::NoColoring);   // No rendering in batch mode.
    clusterMod->setRingAnalysisEnabled(params.ringAnalysisEnabled);
    clusterMod->setMaxRingSize(params.maxRingSize);
    clusterMod->setMinRingSize(params.minRingSize);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, clusterMod);

    // Reference configuration is intentionally left unset: with no external reference assigned,
    // ReferenceConfigurationModifier evaluates this same pipeline at referenceFrameNumber() instead
    // (its default "upstream pipeline, constant frame" mode) -- exactly the fixed cycle-0 baseline
    // this sweep needs, with no separate reference pipeline to build.
    OORef<AmorphousCarbonTrackingModifier> trackMod = OORef<AmorphousCarbonTrackingModifier>::create();
    trackMod->setColorMode(AmorphousCarbonTrackingModifier::NoColoring);
    trackMod->setGenealogyOverlapFraction(params.genealogyOverlapFraction);
    trackMod->setUseReferenceFrameOffset(false);
    trackMod->setReferenceFrameNumber(params.referenceFrame);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, trackMod);

    const int sourceFrameCount = pipeline->head()->numberOfSourceFrames();
    if(sourceFrameCount <= 0)
        throw Exception(QStringLiteral("Input source contains no frames."));
    if(params.referenceFrame >= sourceFrameCount)
        throw Exception(QStringLiteral("Reference frame %1 is outside the input frame range 0..%2.").arg(params.referenceFrame).arg(sourceFrameCount - 1));
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
        QStringLiteral("reference_frame"),
        QStringLiteral("species"),
        QStringLiteral("sp_count"),
        QStringLiteral("sp2_count"),
        QStringLiteral("sp3_count"),
        QStringLiteral("other_count"),
        QStringLiteral("sp2_percent"),
        QStringLiteral("sp3_percent"),
        QStringLiteral("cluster_count"),
        QStringLiteral("relevant_cluster_count"),
        QStringLiteral("largest_cluster_size"),
        QStringLiteral("largest_cluster_fraction"),
        QStringLiteral("clustered_fraction"),
        QStringLiteral("relevant_clustered_fraction"),
        QStringLiteral("atoms_with_discarded_bonds"),
        QStringLiteral("atoms_with_uncounted_bonds"),
        QStringLiteral("min_same_phase_neighbors"),
        QStringLiteral("clustered_atom_count"),
        QStringLiteral("largest_cluster_core_fraction"),
        QStringLiteral("clustered_core_fraction"),
        QStringLiteral("largest_cluster_percolates"),
        QStringLiteral("second_largest_cluster_size"),
        QStringLiteral("largest_cluster_radius_of_gyration"),
        QStringLiteral("mean_cluster_size"),
        QStringLiteral("median_cluster_size"),
        QStringLiteral("cluster_size_p90"),
        QStringLiteral("cluster_number_density"),
        QStringLiteral("gap_estimate_eV"),
        QStringLiteral("ring_analysis_enabled"),
        QStringLiteral("ring_min_size_searched"),
        QStringLiteral("ring_max_size_searched"),
        QStringLiteral("ring_count_total"),
        QStringLiteral("ring_mean_size"),
        QStringLiteral("reference_largest_cluster_size"),
        QStringLiteral("main_fragment_size"),
        QStringLiteral("main_fragment_fraction"),
        QStringLiteral("retained_fraction"),
        QStringLiteral("left_phase_fraction"),
        QStringLiteral("left_core_fraction"),
        QStringLiteral("dispersed_fraction"),
        QStringLiteral("vanished_fraction"),
        QStringLiteral("fragment_count"),
        QStringLiteral("unmapped_atom_count"),
        QStringLiteral("vanished_reference_atom_count"),
        QStringLiteral("relevant_reference_atom_count"),
        QStringLiteral("relevant_main_fragment_size"),
        QStringLiteral("relevant_main_fragment_fraction"),
        QStringLiteral("relevant_retained_fraction"),
        QStringLiteral("relevant_left_phase_fraction"),
        QStringLiteral("relevant_left_core_fraction"),
        QStringLiteral("relevant_dispersed_fraction"),
        QStringLiteral("relevant_vanished_fraction"),
        QStringLiteral("relevant_fragment_count"),
        QStringLiteral("relevant_mean_fragments_per_cluster"),
        QStringLiteral("genealogy_overlap_fraction"),
        QStringLiteral("reference_clusters_relevant"),
        QStringLiteral("reference_clusters_stable"),
        QStringLiteral("reference_clusters_fragmented"),
        QStringLiteral("reference_clusters_disappeared"),
        QStringLiteral("current_clusters_relevant"),
        QStringLiteral("current_clusters_stable"),
        QStringLiteral("current_clusters_merged"),
        QStringLiteral("current_clusters_created"),
        QStringLiteral("status_type"),
        QStringLiteral("status_text")
    });

    qInfo().noquote() << "Amorphous carbon tracking: scanning" << (endFrame - params.firstFrame)
                      << "of" << sourceFrameCount << "frames against reference frame" << params.referenceFrame << "...";

    const QString speciesName = params.clusterSpecies == 3 ? QStringLiteral("sp3") : QStringLiteral("sp2");
    qsizetype rowsWritten = 0;
    bool discardedBondsWarned = false;
    for(int frame = params.firstFrame; frame < endFrame; frame++) {
        const AnimationTime time = pipeline->head()->sourceFrameToAnimationTime(frame);
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(time, true, false)).blockForResult();
        const PipelineStatus& status = state.status();
        writeCsvRow(out, {
            QString::number(attr(state, QStringLiteral("SourceFrame"), frame).toInt()),
            attr(state, QStringLiteral("Timestep"), QVariant()).toString(),
            QString::number(params.referenceFrame),
            speciesName,
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.sp_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.sp2_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.sp3_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.other_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.sp2_percent"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.sp3_percent"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.cluster_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.relevant_cluster_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.largest_cluster_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.largest_cluster_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.clustered_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.relevant_clustered_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.atoms_with_discarded_bonds"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.atoms_with_uncounted_bonds"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.min_same_phase_neighbors"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.clustered_atom_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.largest_cluster_core_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.clustered_core_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.largest_cluster_percolates"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.second_largest_cluster_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.largest_cluster_radius_of_gyration"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.mean_cluster_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.median_cluster_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.cluster_size_p90"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.cluster_number_density"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.gap_estimate_eV"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.ring_analysis_enabled"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.ring_min_size_searched"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.ring_max_size_searched"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.ring_count_total"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbon.ring_mean_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.reference_largest_cluster_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.main_fragment_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.main_fragment_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.retained_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.left_phase_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.left_core_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.dispersed_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.vanished_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.fragment_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.unmapped_atom_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.vanished_reference_atom_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_reference_atom_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_size"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_retained_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_left_phase_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_left_core_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_dispersed_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_vanished_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_fragment_count"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.relevant_mean_fragments_per_cluster"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.genealogy_overlap_fraction"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.reference_clusters_relevant"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.reference_clusters_stable"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.reference_clusters_fragmented"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.reference_clusters_disappeared"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.current_clusters_relevant"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.current_clusters_stable"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.current_clusters_merged"))),
            variantToString(attr(state, QStringLiteral("AmorphousCarbonTracking.current_clusters_created"))),
            statusTypeName(status.type()),
            status.text()
        });
        rowsWritten++;

        // The cluster-analysis modifier raises this as a pipeline warning, but the tracking
        // modifier downstream replaces the status, so the CSV's status column never shows it.
        // Surface it once on the console instead, where a batch user will actually see it.
        if(!discardedBondsWarned) {
            const qlonglong discarded = attr(state, QStringLiteral("AmorphousCarbon.atoms_with_discarded_bonds"), 0).toLongLong();
            const qlonglong uncounted = attr(state, QStringLiteral("AmorphousCarbon.atoms_with_uncounted_bonds"), 0).toLongLong();
            if(discarded > 0) {
                discardedBondsWarned = true;
                qWarning().noquote() << "Amorphous carbon tracking: WARNING - bond cutoff" << params.bondCutoff
                                     << "is shorter than the coordination cutoff" << params.coordinationCutoff
                                     << ";" << discarded << speciesName
                                     << "atoms lose bonds that defined their hybridization, inflating cluster counts.";
            }
            else if(uncounted > 0) {
                discardedBondsWarned = true;
                qWarning().noquote() << "Amorphous carbon tracking: WARNING - bond cutoff" << params.bondCutoff
                                     << "is longer than the coordination cutoff" << params.coordinationCutoff
                                     << ";" << uncounted
                                     << "atoms hold a bond in the gap between them: those bonds connect clusters "
                                        "without counting towards any hybridization, and the short coordination "
                                        "cutoff demotes exactly those atoms out of the phase.";
            }
        }

        // A full mCRA series is ~100 frames of several seconds each. Flush every row so the CSV
        // grows as the sweep runs: progress is visible from outside, and an interrupted or failed
        // run still leaves the frames already computed on disk instead of an empty file.
        out.flush();
        qInfo().noquote() << "Amorphous carbon tracking: frame" << frame << "of" << (endFrame - 1)
                          << "->" << statusTypeName(status.type());
    }

    qInfo().noquote() << "Amorphous carbon tracking: wrote" << rowsWritten << "rows to" << outputInfo.absoluteFilePath();
}

}   // End of namespace
