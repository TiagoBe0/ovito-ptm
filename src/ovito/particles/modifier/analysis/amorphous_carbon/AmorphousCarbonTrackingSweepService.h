////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>
#include <ovito/core/app/ApplicationService.h>
#include <ovito/core/dataset/pipeline/PipelineStatus.h>

namespace Ovito {

/**
 * Command-line batch runner for AmorphousCarbonModifier + AmorphousCarbonTrackingModifier.
 *
 * Builds a headless OVITO pipeline (FileSource -> Amorphous carbon cluster analysis ->
 * Amorphous carbon cluster tracking), evaluates it non-interactively at every requested source
 * frame, and writes the per-frame AmorphousCarbon.* / AmorphousCarbonTracking.* attributes to a
 * CSV file. This runs the exact C++ modifiers used by the GUI over a full mCRA cycle series
 * (e.g. cycle0..cycle100) without opening a graphical session, so the CSV can feed directly into
 * the project's existing Python figure scripts.
 *
 * Unlike dislocation tracking, this does not need a whole-trajectory scan: each requested frame
 * is evaluated independently against a single fixed reference frame (frame 0 by default), which
 * is exactly what AmorphousCarbonTrackingModifier's default "upstream pipeline, frame 0"
 * reference-configuration mode already provides.
 */
class OVITO_PARTICLES_EXPORT AmorphousCarbonTrackingSweepService : public ApplicationService
{
    OVITO_CLASS(AmorphousCarbonTrackingSweepService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the requested batch after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    struct RunParameters {
        QString inputPath;
        QString outputPath;
        FloatType coordinationCutoff = FloatType(2.064);
        FloatType bondCutoff = FloatType(2.064);   ///< Same as the coordination cutoff by default; see AmorphousCarbonModifier.h.
        int clusterSpecies = 2;             ///< 2 = sp2, 3 = sp3 (matches AmorphousCarbonModifier::ClusterSpecies).
        int minimumClusterSize = 3;
        int minSamePhaseNeighbors = 0;   ///< 0 = cluster the whole phase; 3 = cluster its core.
        FloatType genealogyOverlapFraction = FloatType(0.10);   ///< Overlap threshold for the bidirectional cluster genealogy; see AmorphousCarbonTrackingModifier.h.
        bool ringAnalysisEnabled = false;
        int maxRingSize = 8;
        int minRingSize = 3;
        int referenceFrame = 0;
        int firstFrame = 0;
        int maxFrames = -1;
        bool autodetectFileSequences = true;
    };

    static RunParameters parseCommandLine(const QCommandLineParser& parser);
    static OORef<Pipeline> createFilePipeline(const QString& path, bool autodetectFileSequences);
    static QString statusTypeName(PipelineStatus::StatusType statusType);
    static QString toLocalPath(const QString& path);
    static QString variantToString(const QVariant& value);
    static QString csvField(const QString& value);
    static void writeCsvRow(QTextStream& stream, const QStringList& fields);
    static void runSweep(const RunParameters& params);
};

}   // End of namespace
