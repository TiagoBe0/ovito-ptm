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

#pragma once


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/modifier/dxa/StructureAnalysis.h>
#include <ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.h>
#include <ovito/core/app/ApplicationService.h>
#include <ovito/core/dataset/pipeline/PipelineStatus.h>

namespace Ovito {

/**
 * Command-line batch runner for the DislocationTrackingModifier.
 *
 * It builds a normal OVITO pipeline (FileSource -> Dislocation analysis (DXA) -> Dislocation
 * tracking), evaluates it non-interactively so the tracker performs its whole-trajectory scan,
 * and writes the per-frame tracking metrics (track count, births/deaths, densities and the
 * per-track drift velocities) to a CSV file. This exercises the exact C++ modifier used by the
 * GUI, so it can validate the velocity computation end-to-end without a graphical session.
 *
 * NOTE: the tracker always scans ALL source frames of the trajectory (tracks are trajectory-wide),
 * regardless of the output frame range, because DXA has to run on every frame to build the tracks.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationTrackingSweepService : public ApplicationService
{
    OVITO_CLASS(DislocationTrackingSweepService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the requested batch after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    struct RunParameters {
        QString inputPath;
        QString outputPath;
        QString tracksOutputPath;        ///< Per-track CSV; empty = derived from outputPath ("<base>_tracks.csv").
        QString assignmentsOutputPath;   ///< Per-segment assignment CSV; empty = "<base>_assignments.csv".
        bool skipDxa = false;            ///< Input already contains dislocation lines (.ca): do not run DXA.
        // Ablation switches; true = the full algorithm.
        bool projectPerpendicular = true;
        bool matchSpatialBurgers = true;
        bool pbcClosedExemption = true;
        bool driftVelocity = true;
        StructureAnalysis::LatticeStructureType structure = StructureAnalysis::LATTICE_FCC;
        bool usePTM = true;   ///< PTM (true) or CNA (false) for the structure identification inside DXA.
        FloatType timePerFrame = 1;
        FloatType maxMatchingDistance = 0;   ///< 0 = auto.
        FloatType burgersTolerance = FloatType(0.25);
        int maxBridgeGap = 2;
        int minTrackLength = 1;
        int firstFrame = 0;
        int maxFrames = -1;
        bool autodetectFileSequences = true;
    };

    static RunParameters parseCommandLine(const QCommandLineParser& parser);
    static OORef<Pipeline> createFilePipeline(const QString& path, bool autodetectFileSequences);
    static StructureAnalysis::LatticeStructureType parseStructure(const QString& text);
    static QString structureName(StructureAnalysis::LatticeStructureType structure);
    static QString statusTypeName(PipelineStatus::StatusType statusType);
    static QString toLocalPath(const QString& path);
    static QString variantToString(const QVariant& value);
    static QString csvField(const QString& value);
    static void writeCsvRow(QTextStream& stream, const QStringList& fields);
    static void runSweep(const RunParameters& params);

    /// Compact description of which algorithm ingredients were disabled, stamped into every CSV row.
    static QString ablationLabel(const RunParameters& params);

    /// Derives the path of a companion CSV (per-track, per-segment) from the per-frame output path.
    static QString companionPath(const QString& explicitPath, const QString& metricsPath, const QString& suffix);
    static QString assignmentsPath(const RunParameters& params, const QString& metricsPath);

    /// Writes one row per track (one physical dislocation) with its lifetime and drift velocity.
    static void writeTrackTable(const RunParameters& params, const QString& metricsPath, const DislocationTrackingModificationNode* node);
};

}   // End of namespace
