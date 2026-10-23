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


#include <ovito/particles/Particles.h>
#include <ovito/core/app/ApplicationService.h>
#include <ovito/core/dataset/pipeline/PipelineStatus.h>
#include "VacancyAnalysisModifier.h"

#include <QVector>

namespace Ovito {

/**
 * Command-line batch runner for the VacancyAnalysisModifier.
 *
 * This service intentionally lives in the native Particles plugin. It builds and
 * evaluates a normal OVITO pipeline, so parameter sweeps exercise the same C++
 * modifier implementation that the GUI uses.
 */
class OVITO_PARTICLES_EXPORT VacancyAnalysisSweepService : public ApplicationService
{
    OVITO_CLASS(VacancyAnalysisSweepService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the requested sweep after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    struct SweepParameters {
        QString inputPath;
        QString referencePath;
        QString outputPath;
        QString dumpSitesPath;   ///< Optional CSV with per-reference-site classification (for ground-truth validation).
        QString multiSomDumpPath; ///< Optional LAMMPS-dump path pattern with the per-site descriptors, for MultiSOM.
        bool multiSomCandidatesOnly = true; ///< Write only Wigner-Seitz candidate sites (occupancy == 0) to the MultiSOM dump.
        // Upstream deformation signals the sweep computes itself before the vacancy analysis, so the
        // local-context descriptors do not depend on the input dump carrying the right LAMMPS computes.
        bool computePtm = false;
        bool computeStrain = false;
        bool computeVoronoi = false;
        FloatType strainCutoff = 3.0; ///< Neighbor cutoff of the AtomicStrain modifier, in simulation units.
        QString identifierColumn;    ///< Optional per-atom column promoted to Particle Identifier, so AtomicStrain
                                     ///< can map atoms to reference sites when the file provides no 'id' column.
        QVector<VacancyAnalysisModifier::MaskSignalType> maskSignals;
        QVector<FloatType> maskThresholds;
        QVector<FloatType> cavityRadiusRatios;
        QVector<FloatType> recombinationCutoffs;
        bool frenkelRecombination = true;
        bool requireFreeVolume = true;
        bool maskDisordered = true;
        bool autodetectFileSequences = true;
        int firstFrame = 0;
        int maxFrames = -1;
        QString mlModelPath;         ///< Optional GBDT model file; non-empty enables the ML classification mode.
        FloatType mlThreshold = 0.5; ///< Probability threshold of the ML vacancy verdict.
    };

    static SweepParameters parseCommandLine(const QCommandLineParser& parser);
    static OORef<Pipeline> createFilePipeline(const QString& path, bool autodetectFileSequences);
    static QVector<FloatType> parseFloatList(const QString& text, std::initializer_list<FloatType> defaultValues, const QString& optionName);
    static QVector<FloatType> parseCutoffList(const QString& text);
    static QVector<VacancyAnalysisModifier::MaskSignalType> parseMaskSignalList(const QString& text);
    static VacancyAnalysisModifier::MaskSignalType parseMaskSignal(const QString& text);
    static QString maskSignalName(VacancyAnalysisModifier::MaskSignalType signal);
    static QString statusTypeName(PipelineStatus::StatusType statusType);
    static QString defectTypeName(int defectType);
    static void writeSiteDump(QTextStream& stream, const PipelineFlowState& state, const QStringList& idFields);
    static void writeMultiSomDump(const QString& path, const PipelineFlowState& state, int timestep, bool candidatesOnly);
    static QString toLocalPath(const QString& path);
    static QString variantToString(const QVariant& value);
    static QString csvField(const QString& value);
    static void writeCsvRow(QTextStream& stream, const QStringList& fields);
    static void runSweep(const SweepParameters& params);
};

}   // End of namespace
