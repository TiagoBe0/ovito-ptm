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

#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/modifier/analysis/ptm/PolyhedralTemplateMatchingModifier.h>
#include <ovito/particles/modifier/analysis/strain/AtomicStrainModifier.h>
#include <ovito/particles/modifier/analysis/voronoi/VoronoiAnalysisModifier.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdmod/modifiers/ComputePropertyModifier.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "VacancyAnalysisSweepService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(VacancyAnalysisSweepService);

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
void VacancyAnalysisSweepService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep"),
        tr("Run the native Vacancy Analysis modifier in batch sweep mode and exit.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-input"),
        tr("Input trajectory/data file or wildcard pattern for the vacancy sweep."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-reference"),
        tr("Reference configuration file used by the Vacancy Analysis modifier."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-output"),
        tr("CSV output path for the vacancy sweep."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-dump-sites"),
        tr("Optional CSV path: write the per-reference-site classification (Defect Type, Raw Occupancy, ...) for every evaluation. Intended for synthetic ground-truth validation."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-multisom-dump"),
        tr("Optional LAMMPS-dump path for MultiSOM. One file per evaluation; use * in the file name as the placeholder for the frame/parameter index."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-multisom-all-sites"),
        tr("Write every reference site to the MultiSOM dump instead of only the Wigner-Seitz candidates.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-features"),
        tr("Comma-separated upstream signals to compute before the vacancy analysis: ptm, strain, voronoi (or all). They provide the local deformation context descriptors."),
        QStringLiteral("list")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-identifier-column"),
        tr("Per-atom column to promote to the Particle Identifier property in both the input and the reference (e.g. site_id). Required by --vacancy-sweep-features=strain when the files carry no 'id' column."),
        QStringLiteral("name")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-strain-cutoff"),
        tr("Neighbor cutoff of the AtomicStrain modifier used by --vacancy-sweep-features=strain."),
        QStringLiteral("r"),
        QStringLiteral("3.0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-mask-signals"),
        tr("Comma-separated mask signals: structure, centrosymmetry, shear-strain, lammps-c-centro, lammps-c-coord."),
        QStringLiteral("list"),
        QStringLiteral("structure")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-mask-thresholds"),
        tr("Comma-separated structural mask disorder-fraction thresholds."),
        QStringLiteral("list"),
        QStringLiteral("0.5")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-cavity-ratios"),
        tr("Comma-separated minimum cavity radii (mean distance to the 6 nearest atoms) expressed as fractions of the current nearest-neighbor distance. Default: 0.85."),
        QStringLiteral("list"),
        QStringLiteral("0.85")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-recombination-cutoffs"),
        tr("Comma-separated Frenkel recombination cutoffs. Use 0 or auto for automatic cutoff."),
        QStringLiteral("list"),
        QStringLiteral("auto")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-first-frame"),
        tr("First source frame index to evaluate."),
        QStringLiteral("N"),
        QStringLiteral("0")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-max-frames"),
        tr("Maximum number of source frames to evaluate. Negative means all frames."),
        QStringLiteral("N"),
        QStringLiteral("-1")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-no-frenkel"),
        tr("Disable local Frenkel-pair recombination during the sweep.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-no-free-volume"),
        tr("Disable the local free-volume vacancy confirmation during the sweep.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-no-mask"),
        tr("Disable structural masking during the sweep.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-no-sequence-detection"),
        tr("Do not auto-detect numbered file sequences for the input trajectory.")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-ml-model"),
        tr("Path of a GBDT model file (export_ml_model.py). Enables the learned vacancy classification mode."),
        QStringLiteral("path")));
    cmdLineParser.addOption(makeOption(
        QStringLiteral("vacancy-sweep-ml-threshold"),
        tr("Probability threshold of the ML vacancy verdict."),
        QStringLiteral("x"),
        QStringLiteral("0.5")));
}

/******************************************************************************
* Runs the requested sweep after the application has entered the main event loop.
******************************************************************************/
void VacancyAnalysisSweepService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("vacancy-sweep")))
        return;

    try {
        runSweep(parseCommandLine(parser));
        QCoreApplication::exit(0);
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Vacancy sweep failed:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
        // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
        // exitWithFatalError(), which shuts the application down and yields exit code 1.
        throw Exception(tr("Vacancy sweep failed."));
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Vacancy sweep failed:" << QString::fromLocal8Bit(ex.what());
        throw Exception(tr("Vacancy sweep failed."));
    }
}

/******************************************************************************
* Parses command-line parameters.
******************************************************************************/
VacancyAnalysisSweepService::SweepParameters VacancyAnalysisSweepService::parseCommandLine(const QCommandLineParser& parser)
{
    SweepParameters params;
    params.inputPath = parser.value(QStringLiteral("vacancy-sweep-input")).trimmed();
    params.referencePath = parser.value(QStringLiteral("vacancy-sweep-reference")).trimmed();
    params.outputPath = parser.value(QStringLiteral("vacancy-sweep-output")).trimmed();
    params.dumpSitesPath = parser.value(QStringLiteral("vacancy-sweep-dump-sites")).trimmed();
    params.multiSomDumpPath = parser.value(QStringLiteral("vacancy-sweep-multisom-dump")).trimmed();
    params.multiSomCandidatesOnly = !parser.isSet(QStringLiteral("vacancy-sweep-multisom-all-sites"));
    if(params.inputPath.isEmpty())
        throw Exception(QStringLiteral("--vacancy-sweep-input is required."));
    if(params.referencePath.isEmpty())
        throw Exception(QStringLiteral("--vacancy-sweep-reference is required."));
    if(params.outputPath.isEmpty())
        throw Exception(QStringLiteral("--vacancy-sweep-output is required."));

    params.maskSignals = parseMaskSignalList(parser.value(QStringLiteral("vacancy-sweep-mask-signals")));
    params.maskThresholds = parseFloatList(parser.value(QStringLiteral("vacancy-sweep-mask-thresholds")), {FloatType(0.5)}, QStringLiteral("vacancy-sweep-mask-thresholds"));
    params.cavityRadiusRatios = parseFloatList(parser.value(QStringLiteral("vacancy-sweep-cavity-ratios")), {FloatType(0.85)}, QStringLiteral("vacancy-sweep-cavity-ratios"));
    params.recombinationCutoffs = parseCutoffList(parser.value(QStringLiteral("vacancy-sweep-recombination-cutoffs")));
    for(FloatType value : params.maskThresholds) {
        if(value < 0 || value > 1)
            throw Exception(QStringLiteral("--vacancy-sweep-mask-thresholds values must be in the [0, 1] range."));
    }
    for(FloatType value : params.cavityRadiusRatios) {
        if(value < 0 || value > 1)
            throw Exception(QStringLiteral("--vacancy-sweep-cavity-ratios values must be in the [0, 1] range."));
    }
    params.frenkelRecombination = !parser.isSet(QStringLiteral("vacancy-sweep-no-frenkel"));
    params.requireFreeVolume = !parser.isSet(QStringLiteral("vacancy-sweep-no-free-volume"));
    params.maskDisordered = !parser.isSet(QStringLiteral("vacancy-sweep-no-mask"));
    params.autodetectFileSequences = !parser.isSet(QStringLiteral("vacancy-sweep-no-sequence-detection"));

    for(QString token : parser.value(QStringLiteral("vacancy-sweep-features")).split(QChar(','), Qt::SkipEmptyParts)) {
        token = token.trimmed().toLower();
        if(token == QStringLiteral("all")) {
            params.computePtm = params.computeStrain = params.computeVoronoi = true;
        }
        else if(token == QStringLiteral("ptm")) params.computePtm = true;
        else if(token == QStringLiteral("strain")) params.computeStrain = true;
        else if(token == QStringLiteral("voronoi")) params.computeVoronoi = true;
        else
            throw Exception(QStringLiteral("Unknown value '%1' for --vacancy-sweep-features. Valid values: ptm, strain, voronoi, all.").arg(token));
    }

    params.identifierColumn = parser.value(QStringLiteral("vacancy-sweep-identifier-column")).trimmed();

    bool ok = false;
    params.strainCutoff =(FloatType)parser.value(QStringLiteral("vacancy-sweep-strain-cutoff")).toDouble(&ok);
    if(!ok || params.strainCutoff <= 0)
        throw Exception(QStringLiteral("--vacancy-sweep-strain-cutoff must be a positive number."));

    params.firstFrame = parser.value(QStringLiteral("vacancy-sweep-first-frame")).toInt(&ok);
    if(!ok || params.firstFrame < 0)
        throw Exception(QStringLiteral("--vacancy-sweep-first-frame must be a non-negative integer."));
    params.maxFrames = parser.value(QStringLiteral("vacancy-sweep-max-frames")).toInt(&ok);
    if(!ok)
        throw Exception(QStringLiteral("--vacancy-sweep-max-frames must be an integer."));

    params.mlModelPath = parser.value(QStringLiteral("vacancy-sweep-ml-model")).trimmed();
    params.mlThreshold = (FloatType)parser.value(QStringLiteral("vacancy-sweep-ml-threshold")).toDouble(&ok);
    if(!ok || params.mlThreshold < 0 || params.mlThreshold > 1)
        throw Exception(QStringLiteral("--vacancy-sweep-ml-threshold must be a number in the [0, 1] range."));

    return params;
}

/******************************************************************************
* Creates a file-source pipeline without using GUI import actions.
******************************************************************************/
OORef<Pipeline> VacancyAnalysisSweepService::createFilePipeline(const QString& path, bool autodetectFileSequences)
{
    const QString localPath = toLocalPath(path);
    QUrl sourceUrl = QUrl::fromLocalFile(localPath);
    QUrl detectionUrl = sourceUrl;

    if(FileSourceImporter::isWildcardPattern(sourceUrl)) {
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

    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({std::move(sourceUrl)}, std::move(sourceImporter), autodetectFileSequences);
    // Block until the frame list is available. The returned frame list is intentionally
    // discarded here (the FileSource keeps it); cast to void to silence the [[nodiscard]].
    (void) source->updateListOfFrames().blockForResult();

    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    return pipeline;
}

/******************************************************************************
* Parses comma-separated floating point values.
******************************************************************************/
QVector<FloatType> VacancyAnalysisSweepService::parseFloatList(const QString& text, std::initializer_list<FloatType> defaultValues, const QString& optionName)
{
    QVector<FloatType> values;
    const QString trimmed = text.trimmed();
    if(trimmed.isEmpty()) {
        for(FloatType value : defaultValues)
            values.push_back(value);
        return values;
    }

    for(const QString& token : trimmed.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const double value = QLocale::c().toDouble(token.trimmed(), &ok);
        if(!ok)
            throw Exception(QStringLiteral("Invalid numeric value '%1' in --%2.").arg(token.trimmed(), optionName));
        values.push_back((FloatType)value);
    }
    if(values.empty())
        throw Exception(QStringLiteral("--%1 must contain at least one value.").arg(optionName));
    return values;
}

/******************************************************************************
* Parses Frenkel cutoff values.
******************************************************************************/
QVector<FloatType> VacancyAnalysisSweepService::parseCutoffList(const QString& text)
{
    QVector<FloatType> values;
    const QString trimmed = text.trimmed();
    if(trimmed.isEmpty())
        return {FloatType(0)};

    for(QString token : trimmed.split(',', Qt::SkipEmptyParts)) {
        token = token.trimmed().toLower();
        if(token == QStringLiteral("auto")) {
            values.push_back(FloatType(0));
            continue;
        }

        bool ok = false;
        const double value = QLocale::c().toDouble(token, &ok);
        if(!ok || value < 0)
            throw Exception(QStringLiteral("Invalid recombination cutoff '%1'. Use a non-negative number or auto.").arg(token));
        values.push_back((FloatType)value);
    }
    if(values.empty())
        throw Exception(QStringLiteral("--vacancy-sweep-recombination-cutoffs must contain at least one value."));
    return values;
}

/******************************************************************************
* Parses comma-separated mask signal names.
******************************************************************************/
QVector<VacancyAnalysisModifier::MaskSignalType> VacancyAnalysisSweepService::parseMaskSignalList(const QString& text)
{
    QVector<VacancyAnalysisModifier::MaskSignalType> values;
    const QString trimmed = text.trimmed();
    if(trimmed.isEmpty())
        return {VacancyAnalysisModifier::StructureSignal};

    for(const QString& token : trimmed.split(',', Qt::SkipEmptyParts))
        values.push_back(parseMaskSignal(token));
    if(values.empty())
        throw Exception(QStringLiteral("--vacancy-sweep-mask-signals must contain at least one value."));
    return values;
}

/******************************************************************************
* Parses a mask signal name.
******************************************************************************/
VacancyAnalysisModifier::MaskSignalType VacancyAnalysisSweepService::parseMaskSignal(const QString& text)
{
    QString key = text.trimmed().toLower();
    key.replace('_', '-');
    key.replace('.', '-');
    key.replace(' ', '-');

    if(key == QStringLiteral("structure") || key == QStringLiteral("structure-type") || key == QStringLiteral("ptm") || key == QStringLiteral("cna"))
        return VacancyAnalysisModifier::StructureSignal;
    if(key == QStringLiteral("centrosymmetry") || key == QStringLiteral("centro") || key == QStringLiteral("csp"))
        return VacancyAnalysisModifier::CentrosymmetrySignal;
    if(key == QStringLiteral("shear") || key == QStringLiteral("shear-strain") || key == QStringLiteral("atomic-shear-strain"))
        return VacancyAnalysisModifier::ShearStrainSignal;
    if(key == QStringLiteral("lammps-c-centro") || key == QStringLiteral("c-centro"))
        return VacancyAnalysisModifier::LammpsCentroSignal;
    if(key == QStringLiteral("lammps-c-coord") || key == QStringLiteral("c-coord") || key == QStringLiteral("coord"))
        return VacancyAnalysisModifier::LammpsCoordSignal;

    throw Exception(QStringLiteral("Unknown vacancy mask signal '%1'.").arg(text.trimmed()));
}

/******************************************************************************
* Returns a stable command-line name for a mask signal.
******************************************************************************/
QString VacancyAnalysisSweepService::maskSignalName(VacancyAnalysisModifier::MaskSignalType signal)
{
    switch(signal) {
    case VacancyAnalysisModifier::StructureSignal:
        return QStringLiteral("structure");
    case VacancyAnalysisModifier::CentrosymmetrySignal:
        return QStringLiteral("centrosymmetry");
    case VacancyAnalysisModifier::ShearStrainSignal:
        return QStringLiteral("shear-strain");
    case VacancyAnalysisModifier::LammpsCentroSignal:
        return QStringLiteral("lammps-c-centro");
    case VacancyAnalysisModifier::LammpsCoordSignal:
        return QStringLiteral("lammps-c-coord");
    }
    return {};
}

/******************************************************************************
* Returns a stable command-line name for a pipeline status type.
******************************************************************************/
QString VacancyAnalysisSweepService::statusTypeName(PipelineStatus::StatusType statusType)
{
    switch(statusType) {
    case PipelineStatus::Success:
        return QStringLiteral("success");
    case PipelineStatus::Warning:
        return QStringLiteral("warning");
    case PipelineStatus::Error:
        return QStringLiteral("error");
    }
    return {};
}

/******************************************************************************
* Returns the human-readable name of a Defect Type id (mirrors the modifier enum).
******************************************************************************/
QString VacancyAnalysisSweepService::defectTypeName(int defectType)
{
    switch(defectType) {
    case VacancyAnalysisModifier::DefectOK:           return QStringLiteral("OK");
    case VacancyAnalysisModifier::DefectVacancy:      return QStringLiteral("Vacancy");
    case VacancyAnalysisModifier::DefectInterstitial: return QStringLiteral("Interstitial");
    case VacancyAnalysisModifier::DefectFrenkel:      return QStringLiteral("Frenkel pair");
    case VacancyAnalysisModifier::DefectNoCavity:     return QStringLiteral("No cavity");
    case VacancyAnalysisModifier::DefectStructural:   return QStringLiteral("Structural");
    case VacancyAnalysisModifier::DefectSurfaceOrVoid:return QStringLiteral("Surface/void");
    case VacancyAnalysisModifier::DefectMLRejected:   return QStringLiteral("ML rejected");
    }
    return QStringLiteral("Unknown");
}

/******************************************************************************
* Appends the per-reference-site classification of one evaluation to the dump stream.
******************************************************************************/
void VacancyAnalysisSweepService::writeSiteDump(QTextStream& stream, const PipelineFlowState& state, const QStringList& idFields)
{
    const Particles* particles = state.getObject<Particles>();
    if(!particles)
        return;

    const Property* defectType = particles->getProperty(QStringLiteral("Defect Type"));
    const Property* rawOccupancy = particles->getProperty(QStringLiteral("Raw Occupancy"));
    const Property* surplus = particles->getProperty(QStringLiteral("Corrected Interstitial Surplus"));
    const Property* confidence = particles->getProperty(QStringLiteral("Vacancy Confidence"));
    const Property* nearestDist = particles->getProperty(QStringLiteral("Nearest Atom Distance"));
    const Property* disorderFrac = particles->getProperty(QStringLiteral("Disorder Fraction"));
    const Property* frenkelPartner = particles->getProperty(QStringLiteral("Frenkel Partner Site"));
    const Property* siteId = particles->getProperty(QStringLiteral("site_id"));
    // ML descriptor block (computed for WS candidate sites; -1 sentinels elsewhere).
    const Property* knnDistances = particles->getProperty(QStringLiteral("Candidate KNN Distances"));
    const Property* shellCounts = particles->getProperty(QStringLiteral("Candidate Shell Counts"));
    const Property* nearestVacant = particles->getProperty(QStringLiteral("Nearest Vacant Site Distance"));
    const Property* vacantShell = particles->getProperty(QStringLiteral("Vacant Sites In Shell"));
    const Property* nearestInterstitial = particles->getProperty(QStringLiteral("Nearest Interstitial Site Distance"));
    const Property* interstitialSurplusNN = particles->getProperty(QStringLiteral("Nearest Interstitial Surplus"));
    const Property* interstitialShell = particles->getProperty(QStringLiteral("Interstitial Sites In Shell"));
    const Property* mlProbability = particles->getProperty(QStringLiteral("ML Vacancy Probability"));
    const Property* cavityRadius = particles->getProperty(QStringLiteral("Cavity Radius"));
    if(!defectType)
        return;

    BufferReadAccess<int32_t> defectArray(defectType);
    BufferReadAccess<int32_t> rawArray(rawOccupancy);
    BufferReadAccess<int32_t> surplusArray(surplus);
    BufferReadAccess<FloatType> confidenceArray(confidence);
    BufferReadAccess<FloatType> nearestArray(nearestDist);
    BufferReadAccess<FloatType> disorderArray(disorderFrac);
    BufferReadAccess<int64_t> partnerArray(frenkelPartner);
    BufferReadAccess<int32_t> siteIdArray(siteId);
    BufferReadAccess<FloatType*> knnArray(knnDistances);
    BufferReadAccess<int32_t*> shellArray(shellCounts);
    BufferReadAccess<FloatType> nearestVacantArray(nearestVacant);
    BufferReadAccess<int32_t> vacantShellArray(vacantShell);
    BufferReadAccess<FloatType> nearestInterstitialArray(nearestInterstitial);
    BufferReadAccess<int32_t> interstitialSurplusNNArray(interstitialSurplusNN);
    BufferReadAccess<int32_t> interstitialShellArray(interstitialShell);
    BufferReadAccess<FloatType> mlProbabilityArray(mlProbability);
    BufferReadAccess<FloatType> cavityRadiusArray(cavityRadius);

    const size_t n = defectType->size();
    for(size_t s = 0; s < n; s++) {
        QStringList fields = idFields;
        fields << (siteId ? QString::number(siteIdArray[s]) : QString::number((qulonglong)s));
        fields << QString::number(rawOccupancy ? rawArray[s] : -1);
        fields << QString::number(surplus ? surplusArray[s] : -1);
        fields << QString::number(defectArray[s]);
        fields << defectTypeName(defectArray[s]);
        fields << (confidence ? QLocale::c().toString((double)confidenceArray[s], 'g', 6) : QString());
        fields << (nearestDist ? QLocale::c().toString((double)nearestArray[s], 'g', 9) : QString());
        fields << (disorderFrac ? QLocale::c().toString((double)disorderArray[s], 'g', 6) : QString());
        fields << (frenkelPartner ? QString::number((qlonglong)partnerArray[s]) : QString());
        for(size_t c = 0; c < 6; c++)
            fields << (knnDistances ? QLocale::c().toString((double)knnArray.get(s, c), 'g', 9) : QString());
        for(size_t c = 0; c < 3; c++)
            fields << (shellCounts ? QString::number(shellArray.get(s, c)) : QString());
        fields << (nearestVacant ? QLocale::c().toString((double)nearestVacantArray[s], 'g', 9) : QString());
        fields << (vacantShell ? QString::number(vacantShellArray[s]) : QString());
        fields << (nearestInterstitial ? QLocale::c().toString((double)nearestInterstitialArray[s], 'g', 9) : QString());
        fields << (interstitialSurplusNN ? QString::number(interstitialSurplusNNArray[s]) : QString());
        fields << (interstitialShell ? QString::number(interstitialShellArray[s]) : QString());
        fields << (mlProbability ? QLocale::c().toString((double)mlProbabilityArray[s], 'g', 9) : QString());
        fields << (cavityRadius ? QLocale::c().toString((double)cavityRadiusArray[s], 'g', 9) : QString());
        writeCsvRow(stream, fields);
    }
}

/******************************************************************************
* Writes the per-site descriptors as a LAMMPS-style dump file for MultiSOM.
*
* MultiSOM takes the column names verbatim from the "ITEM: ATOMS" line and loads the rest with
* pandas using a single space as the separator, so every field must be numeric and rows must not
* carry a trailing separator. The site positions come from the reference configuration, which is
* what the vacancy analysis leaves in the output pipeline state.
******************************************************************************/
void VacancyAnalysisSweepService::writeMultiSomDump(const QString& path, const PipelineFlowState& state, int timestep, bool candidatesOnly)
{
    const Particles* particles = state.getObject<Particles>();
    if(!particles)
        return;
    const Property* positionProperty = particles->getProperty(Particles::PositionProperty);
    const Property* defectType = particles->getProperty(QStringLiteral("Defect Type"));
    if(!positionProperty || !defectType)
        return;

    QFileInfo info(toLocalPath(path));
    if(!info.dir().exists() && !info.dir().mkpath(QStringLiteral(".")))
        throw Exception(QStringLiteral("Could not create MultiSOM dump directory: %1").arg(info.dir().absolutePath()));
    QFile file(info.absoluteFilePath());
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Could not open MultiSOM dump file for writing: %1").arg(info.absoluteFilePath()));
    QTextStream stream(&file);

    // Scalar accessors. A missing property yields a zero column, keeping the layout fixed so that
    // one input_params.py works for every run regardless of which upstream signals were available.
    auto column = [&](QStringView name) { return particles->getProperty(name); };
    const Property* rawOccupancy = column(u"Raw Occupancy");
    const Property* surplus = column(u"Corrected Interstitial Surplus");
    const Property* confidence = column(u"Vacancy Confidence");
    const Property* nearestDist = column(u"Nearest Atom Distance");
    const Property* disorderFrac = column(u"Disorder Fraction");
    const Property* frenkelPartner = column(u"Frenkel Partner Site");
    const Property* knnDistances = column(u"Candidate KNN Distances");
    const Property* shellCounts = column(u"Candidate Shell Counts");
    const Property* nearestVacant = column(u"Nearest Vacant Site Distance");
    const Property* vacantShell = column(u"Vacant Sites In Shell");
    const Property* nearestInterstitial = column(u"Nearest Interstitial Site Distance");
    const Property* interstitialSurplusNN = column(u"Nearest Interstitial Surplus");
    const Property* interstitialShell = column(u"Interstitial Sites In Shell");
    const Property* mlProbability = column(u"ML Vacancy Probability");
    const Property* localShear = column(u"Local Shear Strain");
    const Property* localVolStrain = column(u"Local Volumetric Strain");
    const Property* localRmsd = column(u"Local RMSD");
    const Property* localAtomicVolume = column(u"Local Atomic Volume");
    const Property* localContextCount = column(u"Local Context Count");
    const Property* siteId = column(u"site_id");

    BufferReadAccess<Point3> positionArray(positionProperty);
    BufferReadAccess<int32_t> defectArray(defectType);
    BufferReadAccess<int32_t> rawArray(rawOccupancy);
    BufferReadAccess<int32_t> surplusArray(surplus);
    BufferReadAccess<FloatType> confidenceArray(confidence);
    BufferReadAccess<FloatType> nearestArray(nearestDist);
    BufferReadAccess<FloatType> disorderArray(disorderFrac);
    BufferReadAccess<int64_t> partnerArray(frenkelPartner);
    BufferReadAccess<FloatType*> knnArray(knnDistances);
    BufferReadAccess<int32_t*> shellArray(shellCounts);
    BufferReadAccess<FloatType> nearestVacantArray(nearestVacant);
    BufferReadAccess<int32_t> vacantShellArray(vacantShell);
    BufferReadAccess<FloatType> nearestInterstitialArray(nearestInterstitial);
    BufferReadAccess<int32_t> interstitialSurplusNNArray(interstitialSurplusNN);
    BufferReadAccess<int32_t> interstitialShellArray(interstitialShell);
    BufferReadAccess<FloatType> mlProbabilityArray(mlProbability);
    BufferReadAccess<FloatType*> localShearArray(localShear);
    BufferReadAccess<FloatType> localVolStrainArray(localVolStrain);
    BufferReadAccess<FloatType> localRmsdArray(localRmsd);
    BufferReadAccess<FloatType> localAtomicVolumeArray(localAtomicVolume);
    BufferReadAccess<int32_t> localContextCountArray(localContextCount);
    BufferReadAccess<int32_t> siteIdArray(siteId);

    const size_t numSites = defectType->size();
    std::vector<size_t> rows;
    rows.reserve(numSites);
    for(size_t s = 0; s < numSites; s++) {
        // A candidate is a site Wigner-Seitz left empty, whatever the consensus stages decided about
        // it afterwards. That is exactly the population the SOM has to separate.
        if(candidatesOnly && rawOccupancy && rawArray[s] != 0) continue;
        rows.push_back(s);
    }

    stream << "ITEM: TIMESTEP\n" << timestep << "\n";
    stream << "ITEM: NUMBER OF ATOMS\n" << (qulonglong)rows.size() << "\n";
    if(const SimulationCell* cell = state.getObject<SimulationCell>()) {
        const AffineTransformation& m = cell->matrix();
        const FloatType xy = m(0,1), xz = m(0,2), yz = m(1,2);
        const bool triclinic = (xy != 0 || xz != 0 || yz != 0);
        const FloatType xlo = m(0,3), ylo = m(1,3), zlo = m(2,3);
        const FloatType xhi = xlo + m(0,0), yhi = ylo + m(1,1), zhi = zlo + m(2,2);
        auto fmt = [](FloatType v) { return QLocale::c().toString((double)v, 'g', 9); };
        if(!triclinic) {
            stream << "ITEM: BOX BOUNDS pp pp pp\n";
            stream << fmt(xlo) << ' ' << fmt(xhi) << "\n";
            stream << fmt(ylo) << ' ' << fmt(yhi) << "\n";
            stream << fmt(zlo) << ' ' << fmt(zhi) << "\n";
        }
        else {
            // LAMMPS reports the bounding box of the tilted cell, not the cell vectors themselves.
            const FloatType xloBound = xlo + std::min({(FloatType)0, xy, xz, (FloatType)(xy + xz)});
            const FloatType xhiBound = xhi + std::max({(FloatType)0, xy, xz, (FloatType)(xy + xz)});
            const FloatType yloBound = ylo + std::min((FloatType)0, yz);
            const FloatType yhiBound = yhi + std::max((FloatType)0, yz);
            stream << "ITEM: BOX BOUNDS xy xz yz pp pp pp\n";
            stream << fmt(xloBound) << ' ' << fmt(xhiBound) << ' ' << fmt(xy) << "\n";
            stream << fmt(yloBound) << ' ' << fmt(yhiBound) << ' ' << fmt(xz) << "\n";
            stream << fmt(zlo) << ' ' << fmt(zhi) << ' ' << fmt(yz) << "\n";
        }
    }
    else {
        stream << "ITEM: BOX BOUNDS pp pp pp\n0 0\n0 0\n0 0\n";
    }

    stream << "ITEM: ATOMS id x y z raw_occupancy corrected_interstitial_surplus defect_type"
              " vacancy_confidence nearest_atom_distance disorder_fraction frenkel_partner_site"
              " knn_d1 knn_d2 knn_d3 knn_d4 knn_d5 knn_d6 count_075nn count_100nn count_125nn"
              " nearest_vacant_distance vacant_neighbors_150 nearest_interstitial_distance"
              " nearest_interstitial_surplus interstitial_neighbors_150 ml_probability"
              " local_shear_strain_mean local_shear_strain_max local_vol_strain local_rmsd"
              " local_atomic_volume local_context_count\n";

    for(size_t s : rows) {
        QStringList fields;
        fields << QString::number(siteId ? (qulonglong)siteIdArray[s] : (qulonglong)s);
        const Point3 p = positionArray[s];
        for(size_t c = 0; c < 3; c++)
            fields << QLocale::c().toString((double)p[c], 'g', 9);
        fields << QString::number(rawOccupancy ? rawArray[s] : 0);
        fields << QString::number(surplus ? surplusArray[s] : 0);
        fields << QString::number(defectArray[s]);
        fields << QLocale::c().toString(confidence ? (double)confidenceArray[s] : 0.0, 'g', 6);
        fields << QLocale::c().toString(nearestDist ? (double)nearestArray[s] : 0.0, 'g', 9);
        fields << QLocale::c().toString(disorderFrac ? (double)disorderArray[s] : 0.0, 'g', 6);
        fields << QString::number(frenkelPartner ? (qlonglong)partnerArray[s] : -1);
        for(size_t c = 0; c < 6; c++)
            fields << QLocale::c().toString(knnDistances ? (double)knnArray.get(s, c) : 0.0, 'g', 9);
        for(size_t c = 0; c < 3; c++)
            fields << QString::number(shellCounts ? shellArray.get(s, c) : 0);
        fields << QLocale::c().toString(nearestVacant ? (double)nearestVacantArray[s] : 0.0, 'g', 9);
        fields << QString::number(vacantShell ? vacantShellArray[s] : 0);
        fields << QLocale::c().toString(nearestInterstitial ? (double)nearestInterstitialArray[s] : 0.0, 'g', 9);
        fields << QString::number(interstitialSurplusNN ? interstitialSurplusNNArray[s] : 0);
        fields << QString::number(interstitialShell ? interstitialShellArray[s] : 0);
        fields << QLocale::c().toString(mlProbability ? (double)mlProbabilityArray[s] : 0.0, 'g', 9);
        fields << QLocale::c().toString(localShear ? (double)localShearArray.get(s, 0) : 0.0, 'g', 9);
        fields << QLocale::c().toString(localShear ? (double)localShearArray.get(s, 1) : 0.0, 'g', 9);
        fields << QLocale::c().toString(localVolStrain ? (double)localVolStrainArray[s] : 0.0, 'g', 9);
        fields << QLocale::c().toString(localRmsd ? (double)localRmsdArray[s] : 0.0, 'g', 9);
        fields << QLocale::c().toString(localAtomicVolume ? (double)localAtomicVolumeArray[s] : 0.0, 'g', 9);
        fields << QString::number(localContextCount ? localContextCountArray[s] : 0);
        stream << fields.join(QChar(' ')) << "\n";
    }
}

/******************************************************************************
* Converts a user path to an absolute local path, preserving wildcard patterns.
******************************************************************************/
QString VacancyAnalysisSweepService::toLocalPath(const QString& path)
{
    QFileInfo info(path);
    if(info.isAbsolute())
        return QDir::cleanPath(path);
    return QDir::cleanPath(QDir::current().absoluteFilePath(path));
}

/******************************************************************************
* Value formatting and CSV escaping helpers.
******************************************************************************/
QString VacancyAnalysisSweepService::variantToString(const QVariant& value)
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

QString VacancyAnalysisSweepService::csvField(const QString& value)
{
    QString field = value;
    field.replace('"', QStringLiteral("\"\""));
    if(field.contains(',') || field.contains('"') || field.contains('\n') || field.contains('\r'))
        return QStringLiteral("\"%1\"").arg(field);
    return field;
}

void VacancyAnalysisSweepService::writeCsvRow(QTextStream& stream, const QStringList& fields)
{
    for(int i = 0; i < fields.size(); i++) {
        if(i != 0)
            stream << ',';
        stream << csvField(fields[i]);
    }
    stream << '\n';
}

/******************************************************************************
* Runs the native vacancy-analysis parameter sweep.
******************************************************************************/
void VacancyAnalysisSweepService::runSweep(const SweepParameters& params)
{
    qInfo().noquote() << "Vacancy sweep: loading reference" << params.referencePath;
    OORef<Pipeline> referencePipeline = createFilePipeline(params.referencePath, false);

    qInfo().noquote() << "Vacancy sweep: loading input" << params.inputPath;
    OORef<Pipeline> inputPipeline = createFilePipeline(params.inputPath, params.autodetectFileSequences);

    // Promote a plain column to Particle Identifier. AtomicStrain matches atoms to reference sites by
    // identifier, which is the only way it can work when atoms are missing (exactly the vacancy case).
    if(!params.identifierColumn.isEmpty()) {
        qInfo().noquote() << "Vacancy sweep: using column" << params.identifierColumn << "as particle identifier";
        for(Pipeline* pipeline : {inputPipeline.get(), referencePipeline.get()}) {
            OORef<ComputePropertyModifier> idModifier = OORef<ComputePropertyModifier>::create();
            idModifier->setOutputProperty(PropertyReference(&Particles::OOClass(), Particles::IdentifierProperty));
            idModifier->setExpressions(QStringList{params.identifierColumn});
            pipeline->applyModifier(AnimationTime::fromFrame(0), false, idModifier);
        }
    }

    // Upstream deformation signals, inserted below the vacancy analysis so their per-atom properties
    // are visible to it. Computing them here keeps the sweep self-contained: no LAMMPS computes and
    // no re-run of the simulation are needed to get the local-context descriptors.
    if(params.computePtm) {
        qInfo().noquote() << "Vacancy sweep: computing PTM structure types and RMSD";
        OORef<PolyhedralTemplateMatchingModifier> ptm = OORef<PolyhedralTemplateMatchingModifier>::create();
        ptm->setOutputRmsd(true);
        inputPipeline->applyModifier(AnimationTime::fromFrame(0), false, ptm);
    }
    if(params.computeStrain) {
        qInfo().noquote() << "Vacancy sweep: computing atomic strain with cutoff" << params.strainCutoff;
        OORef<AtomicStrainModifier> strain = OORef<AtomicStrainModifier>::create();
        strain->setReferenceConfiguration(referencePipeline->head());
        strain->setCutoff(params.strainCutoff);
        // Leave invalid particles unselected: the vacancy analysis reads no Selection property, but a
        // stray selection would silently change the behaviour of any modifier inserted later.
        strain->setSelectInvalidParticles(false);
        inputPipeline->applyModifier(AnimationTime::fromFrame(0), false, strain);
    }
    if(params.computeVoronoi) {
        qInfo().noquote() << "Vacancy sweep: computing Voronoi atomic volumes";
        OORef<VoronoiAnalysisModifier> voronoi = OORef<VoronoiAnalysisModifier>::create();
        inputPipeline->applyModifier(AnimationTime::fromFrame(0), false, voronoi);
    }

    OORef<VacancyAnalysisModifier> modifier = OORef<VacancyAnalysisModifier>::create();
    modifier->setReferenceConfiguration(referencePipeline->head());
    inputPipeline->applyModifier(AnimationTime::fromFrame(0), false, modifier);

    const int sourceFrameCount = inputPipeline->head()->numberOfSourceFrames();
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

    // Optional per-site dump (for synthetic ground-truth validation).
    QFile dumpFile;
    QTextStream dumpStream;
    const bool dumpSites = !params.dumpSitesPath.isEmpty();
    if(dumpSites) {
        QFileInfo dumpInfo(toLocalPath(params.dumpSitesPath));
        if(!dumpInfo.dir().exists() && !dumpInfo.dir().mkpath(QStringLiteral(".")))
            throw Exception(QStringLiteral("Could not create dump directory: %1").arg(dumpInfo.dir().absolutePath()));
        dumpFile.setFileName(dumpInfo.absoluteFilePath());
        if(!dumpFile.open(QIODevice::WriteOnly | QIODevice::Text))
            throw Exception(QStringLiteral("Could not open site-dump CSV for writing: %1").arg(dumpInfo.absoluteFilePath()));
        dumpStream.setDevice(&dumpFile);
        writeCsvRow(dumpStream, {
            QStringLiteral("source_frame"), QStringLiteral("mask_signal"), QStringLiteral("mask_threshold"),
            QStringLiteral("cavity_radius_ratio"), QStringLiteral("recombination_cutoff"),
            QStringLiteral("site_id"), QStringLiteral("raw_occupancy"), QStringLiteral("corrected_interstitial_surplus"),
            QStringLiteral("defect_type"), QStringLiteral("defect_type_name"), QStringLiteral("vacancy_confidence"),
            QStringLiteral("nearest_atom_distance"), QStringLiteral("disorder_fraction"),
            QStringLiteral("frenkel_partner_site"),
            QStringLiteral("knn_d1"), QStringLiteral("knn_d2"), QStringLiteral("knn_d3"),
            QStringLiteral("knn_d4"), QStringLiteral("knn_d5"), QStringLiteral("knn_d6"),
            QStringLiteral("count_075nn"), QStringLiteral("count_100nn"), QStringLiteral("count_125nn"),
            QStringLiteral("nearest_vacant_distance"), QStringLiteral("vacant_neighbors_150"),
            QStringLiteral("nearest_interstitial_distance"), QStringLiteral("nearest_interstitial_surplus"),
            QStringLiteral("interstitial_neighbors_150"), QStringLiteral("ml_probability"),
            QStringLiteral("cavity_radius")
        });
    }

    QTextStream out(&file);
    writeCsvRow(out, {
        QStringLiteral("source_file"),
        QStringLiteral("source_frame"),
        QStringLiteral("timestep"),
        QStringLiteral("mask_signal"),
        QStringLiteral("mask_threshold"),
        QStringLiteral("cavity_radius_ratio"),
        QStringLiteral("recombination_cutoff"),
        QStringLiteral("frenkel_recombination"),
        QStringLiteral("require_free_volume"),
        QStringLiteral("mask_disordered"),
        QStringLiteral("raw_ws_vacancies"),
        QStringLiteral("raw_ws_interstitials"),
        QStringLiteral("vacancy_count"),
        QStringLiteral("interstitial_count"),
        QStringLiteral("frenkel_recombined"),
        QStringLiteral("freevolume_excluded"),
        QStringLiteral("structural_excluded"),
        QStringLiteral("surface_or_void_excluded"),
        QStringLiteral("estimated_nn_distance"),
        QStringLiteral("estimated_nn_distance_current"),
        QStringLiteral("mask_signal_used"),
        QStringLiteral("mask_signal_cutoff"),
        QStringLiteral("status_type"),
        QStringLiteral("status_text")
    });

    qsizetype rowsWritten = 0;
    qsizetype multiSomIndex = 0, multiSomFilesWritten = 0;
    for(VacancyAnalysisModifier::MaskSignalType maskSignal : params.maskSignals) {
        for(FloatType maskThreshold : params.maskThresholds) {
            for(FloatType cavityRatio : params.cavityRadiusRatios) {
                for(FloatType recombinationCutoff : params.recombinationCutoffs) {
                    modifier->setFrenkelRecombination(params.frenkelRecombination);
                    modifier->setRecombinationCutoff(recombinationCutoff);
                    modifier->setRequireFreeVolume(params.requireFreeVolume);
                    modifier->setCavityRadiusRatio(cavityRatio);
                    modifier->setMaskDisordered(params.maskDisordered);
                    modifier->setMaskSignal(maskSignal);
                    modifier->setMaskThreshold(maskThreshold);
                    modifier->setMlClassification(!params.mlModelPath.isEmpty());
                    if(!params.mlModelPath.isEmpty()) {
                        modifier->setMlModelPath(toLocalPath(params.mlModelPath));
                        modifier->setMlThreshold(params.mlThreshold);
                    }

                    for(int frame = params.firstFrame; frame < endFrame; frame++) {
                        const AnimationTime time = inputPipeline->head()->sourceFrameToAnimationTime(frame);
                        PipelineFlowState state = inputPipeline->evaluatePipeline(PipelineEvaluationRequest(time, true, false)).blockForResult();
                        const PipelineStatus& status = state.status();
                        writeCsvRow(out, {
                            attr(state, QStringLiteral("SourceFile")).toString(),
                            QString::number(attr(state, QStringLiteral("SourceFrame"), frame).toInt()),
                            attr(state, QStringLiteral("Timestep"), QVariant()).toString(),
                            params.maskDisordered ? maskSignalName(maskSignal) : QStringLiteral("disabled"),
                            QLocale::c().toString(maskThreshold, 'g', 9),
                            QLocale::c().toString(cavityRatio, 'g', 9),
                            recombinationCutoff > 0 ? QLocale::c().toString(recombinationCutoff, 'g', 9) : QStringLiteral("auto"),
                            params.frenkelRecombination ? QStringLiteral("true") : QStringLiteral("false"),
                            params.requireFreeVolume ? QStringLiteral("true") : QStringLiteral("false"),
                            params.maskDisordered ? QStringLiteral("true") : QStringLiteral("false"),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.raw_ws_vacancies"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.raw_ws_interstitials"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.vacancy_count"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.interstitial_count"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.frenkel_recombined"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.freevolume_excluded"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.structural_excluded"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.surface_or_void_excluded"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.estimated_nn_distance"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.estimated_nn_distance_current"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.mask_signal_used"))),
                            variantToString(attr(state, QStringLiteral("VacancyAnalysis.mask_signal_cutoff"))),
                            statusTypeName(status.type()),
                            status.text()
                        });
                        rowsWritten++;

                        if(dumpSites) {
                            writeSiteDump(dumpStream, state, {
                                QString::number(frame),
                                params.maskDisordered ? maskSignalName(maskSignal) : QStringLiteral("disabled"),
                                QLocale::c().toString(maskThreshold, 'g', 9),
                                QLocale::c().toString(cavityRatio, 'g', 9),
                                recombinationCutoff > 0 ? QLocale::c().toString(recombinationCutoff, 'g', 9) : QStringLiteral("auto")
                            });
                        }

                        if(!params.multiSomDumpPath.isEmpty()) {
                            // One file per evaluation: MultiSOM classifies one sample file at a time.
                            QString dumpPath = params.multiSomDumpPath;
                            const QString index = QString::number(multiSomIndex++);
                            if(dumpPath.contains(QChar('*')))
                                dumpPath.replace(QChar('*'), index);
                            else {
                                QFileInfo pathInfo(dumpPath);
                                dumpPath = pathInfo.path() + QChar('/') + pathInfo.completeBaseName() + QChar('.') + index
                                        + (pathInfo.suffix().isEmpty() ? QString() : QChar('.') + pathInfo.suffix());
                            }
                            writeMultiSomDump(dumpPath, state,
                                    attr(state, QStringLiteral("Timestep"), frame).toInt(),
                                    params.multiSomCandidatesOnly);
                            multiSomFilesWritten++;
                        }
                    }
                }
            }
        }
    }

    qInfo().noquote() << "Vacancy sweep: wrote" << rowsWritten << "rows to" << outputInfo.absoluteFilePath();
    if(multiSomFilesWritten > 0)
        qInfo().noquote() << "Vacancy sweep: wrote" << multiSomFilesWritten << "MultiSOM dump file(s) from pattern" << params.multiSomDumpPath;
}

}   // End of namespace
