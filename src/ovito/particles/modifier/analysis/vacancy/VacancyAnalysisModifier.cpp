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
#include <ovito/particles/util/NearestNeighborFinder.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/ParticleType.h>
#include <ovito/stdobj/properties/ElementType.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include "VacancyAnalysisModifier.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ovito {

/******************************************************************************
* Loads and validates a GBDT model file written by export_ml_model.py.
******************************************************************************/
std::shared_ptr<const VacancyMLModel> VacancyMLModel::load(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        throw Exception(VacancyAnalysisModifier::tr("Could not open ML model file: %1").arg(path));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw Exception(VacancyAnalysisModifier::tr("ML model file is not valid JSON: %1 (%2)").arg(path).arg(parseError.errorString()));
    const QJsonObject obj = doc.object();

    // The C++ feature assembly is hard-coded; refuse models trained on a different layout.
    const QJsonArray features = obj.value(QStringLiteral("features")).toArray();
    if(features.size() != NUM_FEATURES)
        throw Exception(VacancyAnalysisModifier::tr("ML model expects %1 features, but this build assembles %2. Re-export the model with a matching feature layout.")
            .arg(features.size()).arg(NUM_FEATURES));

    auto model = std::make_shared<VacancyMLModel>();
    model->_baseline = (FloatType)obj.value(QStringLiteral("baseline")).toDouble();
    model->_farSentinel = (FloatType)obj.value(QStringLiteral("far_sentinel")).toDouble(5.0);

    const QJsonArray trees = obj.value(QStringLiteral("trees")).toArray();
    if(trees.isEmpty())
        throw Exception(VacancyAnalysisModifier::tr("ML model file contains no trees: %1").arg(path));
    model->_trees.reserve(trees.size());
    for(const QJsonValue& treeVal : trees) {
        const QJsonArray nodesArr = treeVal.toArray();
        std::vector<Node> nodes;
        nodes.reserve(nodesArr.size());
        for(const QJsonValue& nodeVal : nodesArr) {
            const QJsonObject n = nodeVal.toObject();
            Node node;
            node.leaf = n.value(QStringLiteral("leaf")).toBool();
            node.value = (FloatType)n.value(QStringLiteral("value")).toDouble();
            node.feature = n.value(QStringLiteral("feature")).toInt();
            node.threshold = (FloatType)n.value(QStringLiteral("threshold")).toDouble();
            node.left = n.value(QStringLiteral("left")).toInt();
            node.right = n.value(QStringLiteral("right")).toInt();
            if(!node.leaf && (node.feature < 0 || node.feature >= NUM_FEATURES
                    || node.left < 0 || node.left >= nodesArr.size()
                    || node.right < 0 || node.right >= nodesArr.size()))
                throw Exception(VacancyAnalysisModifier::tr("ML model file contains an out-of-range tree node: %1").arg(path));
            nodes.push_back(node);
        }
        model->_trees.push_back(std::move(nodes));
    }
    return model;
}

/******************************************************************************
* Evaluates the tree ensemble, returning the vacancy probability.
******************************************************************************/
FloatType VacancyMLModel::evaluate(const FloatType* features) const
{
    FloatType raw = _baseline;
    for(const auto& tree : _trees) {
        size_t i = 0;
        while(!tree[i].leaf)
            i = (features[tree[i].feature] <= tree[i].threshold) ? tree[i].left : tree[i].right;
        raw += tree[i].value;
    }
    return 1 / (1 + std::exp(-raw));
}

IMPLEMENT_CREATABLE_OVITO_CLASS(VacancyAnalysisModifier);
OVITO_CLASSINFO(VacancyAnalysisModifier, "DisplayName", "Vacancy analysis (consensus)");
OVITO_CLASSINFO(VacancyAnalysisModifier, "Description", "Consensus vacancy/interstitial classification that reports Wigner-Seitz candidates, local Frenkel recombination, free-volume checks, and structural masking.");
OVITO_CLASSINFO(VacancyAnalysisModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, frenkelRecombination);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, recombinationCutoff);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, requireFreeVolume);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, cavityRadiusRatio);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskDisordered);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskSignal);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskThreshold);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlClassification);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlModelPath);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlThreshold);
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, frenkelRecombination, "Recombine Frenkel pairs");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, recombinationCutoff, "Recombination cutoff");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, requireFreeVolume, "Require free volume");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, cavityRadiusRatio, "Min. cavity radius (x nn dist.)");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskDisordered, "Mask disordered regions");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskSignal, "Disorder signal");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskThreshold, "Disorder fraction threshold");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlClassification, "ML classification");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlModelPath, "ML model file");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlThreshold, "ML probability threshold");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(VacancyAnalysisModifier, recombinationCutoff, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(VacancyAnalysisModifier, cavityRadiusRatio, FloatParameterUnit, 0, 1);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(VacancyAnalysisModifier, maskThreshold, FloatParameterUnit, 0, 1);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(VacancyAnalysisModifier, mlThreshold, FloatParameterUnit, 0, 1);

/******************************************************************************
* Adopts existing computation results for an interactive pipeline evaluation.
******************************************************************************/
Future<PipelineFlowState> VacancyAnalysisModifier::reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState)
{
    // The modifier outputs the reference configuration (with per-site classification).
    if(const Particles* cachedParticles = cachedState.getObject<Particles>()) {
        output.replaceObject(particles, cachedParticles);
        if(const SimulationCell* cell = output.getObject<SimulationCell>()) {
            if(const SimulationCell* cachedCell = cachedState.getObject<SimulationCell>())
                output.replaceObject(cell, cachedCell);
        }
    }

    // Adopt all global attributes computed by the modifier from the cached state.
    output.adoptAttributesFrom(cachedState, request.modificationNode());

    return std::move(output);
}

/******************************************************************************
* Creates and initializes a computation engine that will compute the modifier's results.
******************************************************************************/
std::unique_ptr<ReferenceConfigurationModifier::Engine> VacancyAnalysisModifier::createEngine(const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState)
{
    // Get the current particle positions.
    const Particles* particles = input.expectObject<Particles>();
    particles->verifyIntegrity();
    const Property* posProperty = particles->expectProperty(Particles::PositionProperty);

    // Get the reference particle positions.
    const Particles* refParticles = referenceState.getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("Reference configuration does not contain any particles."));
    refParticles->verifyIntegrity();
    const Property* refPosProperty = refParticles->expectProperty(Particles::PositionProperty);

    // Get and validate simulation cells.
    const SimulationCell* inputCell = input.getObject<SimulationCell>();
    const SimulationCell* refCell = referenceState.getObject<SimulationCell>();
    if(refCell && !inputCell)
        throw Exception(tr("Input configuration does not have a simulation cell."));
    if(inputCell && !refCell)
        throw Exception(tr("Reference configuration does not have a simulation cell."));
    if(inputCell && inputCell->is2D())
        throw Exception(tr("Vacancy analysis is not supported for 2D systems."));
    if(inputCell && inputCell->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate in the current configuration."));
    if(refCell && refCell->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate in the reference configuration."));

    // Pick up optional upstream per-atom disorder signals from the current configuration.
    const Property* structureProperty = particles->getProperty(Particles::StructureTypeProperty);
    const Property* cspProperty = particles->getProperty(Particles::CentroSymmetryProperty);
    const Property* shearProperty = particles->getProperty(QStringLiteral("Shear Strain"));
    const Property* lammpsCentroProperty = particles->getProperty(QStringLiteral("c_centro"));
    const Property* lammpsCoordProperty = particles->getProperty(QStringLiteral("c_coord"));

    // Load the learned classifier if the ML mode is enabled. Loading happens here (main thread)
    // so a bad model path surfaces as a modifier error instead of failing inside the engine.
    std::shared_ptr<const VacancyMLModel> mlModel;
    if(mlClassification()) {
        if(mlModelPath().trimmed().isEmpty())
            throw Exception(tr("ML classification is enabled, but no model file has been specified."));
        mlModel = VacancyMLModel::load(mlModelPath().trimmed());
    }

    return std::make_unique<VacancyAnalysisEngine>(posProperty, inputCell,
            referenceState, refPosProperty, refCell, affineMapping(),
            frenkelRecombination(), recombinationCutoff(),
            requireFreeVolume(), cavityRadiusRatio(),
            maskDisordered(), maskSignal(), maskThreshold(),
            structureProperty, cspProperty, shearProperty, lammpsCentroProperty, lammpsCoordProperty,
            std::move(mlModel), mlThreshold(),
            request.modificationNode());
}

/******************************************************************************
* Performs the actual computation. This method is executed in a worker thread.
******************************************************************************/
void VacancyAnalysisModifier::VacancyAnalysisEngine::perform(PipelineFlowState& state)
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Performing vacancy analysis"));

    if(affineMapping() == TO_CURRENT_CELL)
        throw Exception(tr("Remapping coordinates to the current cell is not supported by the vacancy analysis. Only remapping to the reference cell or no mapping at all are supported options."));
    if(refPositions()->size() == 0)
        throw Exception(tr("Reference configuration for vacancy analysis contains no atomic sites."));

    const size_t numSites = refPositions()->size();
    const size_t numAtoms = positions()->size();

    // Coordinate mappings between the current and the reference frame.
    AffineTransformation tm;     // current -> reference
    AffineTransformation tmInv;  // reference -> current
    if(affineMapping() == TO_REFERENCE_CELL) {
        tm = refCell().cellMatrix() * cell().reciprocalCellMatrix();
        tmInv = cell().cellMatrix() * refCell().reciprocalCellMatrix();
    }

    // --- Stage 1: Wigner-Seitz occupancy of the reference sites. ---
    NearestNeighborFinder siteFinder(0, refPositions(), refCell(), {});
    std::vector<std::atomic_int> occupancy(numSites);
    for(auto& o : occupancy)
        o.store(0, std::memory_order_relaxed);

    BufferReadAccess<Point3> positionsArray(positions());
    parallelFor(numAtoms, 1024, progress, [&](size_t i) {
        Point3 p = positionsArray[i];
        if(affineMapping() == TO_REFERENCE_CELL) p = tm * p;
        FloatType closestDistanceSq;
        size_t site = siteFinder.findClosestParticle(p, closestDistanceSq);
        occupancy[site].fetch_add(1, std::memory_order_relaxed);
    });

    // Raw Wigner-Seitz defect counts (for comparison/reporting).
    size_t rawVacancyCount = 0, rawInterstitialCount = 0;
    std::vector<size_t> vacantSites;
    std::vector<int> interstitialSurplus(numSites, 0);  // available "extra atom" slots per over-occupied site
    for(size_t s = 0; s < numSites; s++) {
        int o = occupancy[s].load(std::memory_order_relaxed);
        if(o == 0) { rawVacancyCount++; vacantSites.push_back(s); }
        else if(o > 1) { rawInterstitialCount += (o - 1); interstitialSurplus[s] = o - 1; }
    }

    // Median nearest-neighbor distance over a uniform sample of query points.
    auto medianNeighborDistance = [](const NearestNeighborFinder& finder, auto&& points, size_t count) -> FloatType {
        if(count < 2)
            return 0;
        const size_t nSample = std::min(count, (size_t)1000);
        std::vector<FloatType> nnDistanceSq;
        nnDistanceSq.reserve(nSample);
        for(size_t i = 0; i < nSample; i++) {
            const size_t s = (nSample == count) ? i : (i * (count - 1)) / (nSample - 1);
            FloatType d;
            finder.findClosestParticle(points[s], d, /*includeSelf=*/false);
            if(d > 0 && d < FLOATTYPE_MAX)
                nnDistanceSq.push_back(d);
        }
        if(nnDistanceSq.empty())
            return 0;
        auto median = nnDistanceSq.begin() + nnDistanceSq.size() / 2;
        std::nth_element(nnDistanceSq.begin(), median, nnDistanceSq.end());
        return std::sqrt(*median);
    };

    // Estimate the reference nearest-neighbor distance from a uniform sample of sites.
    FloatType r_nn = 0;
    {
        BufferReadAccess<Point3> refPosArray(refPositions());
        r_nn = medianNeighborDistance(siteFinder, refPosArray, numSites);
    }
    if(r_nn <= 0)
        throw Exception(tr("Could not determine the reference nearest-neighbor distance."));

    // Estimate the current-frame nearest-neighbor distance by sampling the current configuration
    // directly with the same median procedure. This keeps the free-volume (cavity) test calibrated
    // under anisotropic deformation, where scaling the reference value by the cell volume ratio
    // would miscalibrate the spacing along the compressed/stretched axes.
    // Sized for the k-nearest queries of the ML descriptor block below; findClosestParticle is unaffected.
    NearestNeighborFinder currentNearestFinder(6, positions(), cell(), {});
    FloatType r_nn_cur = medianNeighborDistance(currentNearestFinder, positionsArray, numAtoms);
    if(r_nn_cur <= 0) {
        // Fallback for degenerate samples: scale the reference value with the cell volume ratio.
        r_nn_cur = r_nn;
        const FloatType vRef = refCell().volume3D();
        const FloatType vCur = cell().volume3D();
        if(vRef > 0 && vCur > 0)
            r_nn_cur = r_nn * std::cbrt(vCur / vRef);
    }

    // --- Stage 2: Frenkel-pair recombination. ---
    // A candidate vacancy adjacent to an over-occupied site is the signature of a displaced atom,
    // not a missing one. Match such pairs (greedy nearest, in the reference frame) and annihilate them.
    std::vector<char> matchedVacant(vacantSites.size(), 0);
    std::vector<int64_t> matchedPartnerSite(vacantSites.size(), -1);
    size_t frenkelRecombined = 0;
    if(_frenkelRecombination && !vacantSites.empty() && rawInterstitialCount > 0) {
        const FloatType cutoff = (_recombinationCutoff > 0) ? _recombinationCutoff : (FloatType)1.2 * r_nn;
        CutoffNeighborFinder finder(cutoff, refPositions(), refCell(), {});
        BufferReadAccess<Point3> refPosArray(refPositions());

        progress.setText(tr("Vacancy analysis: recombining Frenkel pairs"));
        progress.setMaximum(vacantSites.size());
        struct Pair { FloatType dist; size_t vacantListIndex; size_t interstitialSite; };
        std::vector<Pair> candidatePairs;
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            progress.setValueIntermittent(vi);
            const size_t vs = vacantSites[vi];
            for(CutoffNeighborFinder::Query q(finder, refPosArray[vs]); !q.atEnd(); q.next()) {
                if(interstitialSurplus[q.current()] > 0)
                    candidatePairs.push_back({q.distance(), vi, q.current()});
            }
        }
        // Total order with explicit tie-breaks so the greedy matching is reproducible: at equal
        // pair distance (common on a perfect lattice) std::sort alone leaves the order unspecified.
        std::sort(candidatePairs.begin(), candidatePairs.end(),
            [](const Pair& a, const Pair& b) {
                if(a.dist != b.dist) return a.dist < b.dist;
                if(a.vacantListIndex != b.vacantListIndex) return a.vacantListIndex < b.vacantListIndex;
                return a.interstitialSite < b.interstitialSite;
            });
        for(const Pair& pr : candidatePairs) {
            if(matchedVacant[pr.vacantListIndex]) continue;
            if(interstitialSurplus[pr.interstitialSite] <= 0) continue;
            matchedVacant[pr.vacantListIndex] = 1;
            matchedPartnerSite[pr.vacantListIndex] = (int64_t)pr.interstitialSite;
            interstitialSurplus[pr.interstitialSite]--;
            frenkelRecombined++;
        }
    }

    // --- Precompute the per-atom disorder flag for structural masking (Stage 4). ---
    std::vector<char> disordered;
    bool haveDisorderSignal = false;
    bool haveMaskSignalCutoff = false;
    FloatType maskSignalCutoff = (FloatType)-1;
    auto selectedMaskSignalName = [&]() {
        switch(_maskSignal) {
        case StructureSignal: return tr("Structure Type");
        case CentrosymmetrySignal: return tr("Centrosymmetry");
        case ShearStrainSignal: return tr("Shear Strain");
        case LammpsCentroSignal: return tr("LAMMPS c_centro");
        case LammpsCoordSignal: return tr("LAMMPS c_coord");
        }
        return QString();
    }();
    auto maskFromScalarSignal = [&](const Property* signalProperty, bool highValuesAreDisordered) {
        if(!signalProperty || signalProperty->componentCount() != 1 || signalProperty->size() != numAtoms)
            return false;

        // Accept float as well as integer per-atom signals (e.g. a coordination count that was
        // imported or remapped as an integer column).
        std::vector<FloatType> signalValues(numAtoms);
        if(signalProperty->dataType() == Property::FloatDefault) {
            BufferReadAccess<FloatType> signalArray(signalProperty);
            for(size_t i = 0; i < numAtoms; i++) signalValues[i] = signalArray[i];
        }
        else if(signalProperty->dataType() == Property::Int32) {
            BufferReadAccess<int32_t> signalArray(signalProperty);
            for(size_t i = 0; i < numAtoms; i++) signalValues[i] = (FloatType)signalArray[i];
        }
        else if(signalProperty->dataType() == Property::Int64) {
            BufferReadAccess<int64_t> signalArray(signalProperty);
            for(size_t i = 0; i < numAtoms; i++) signalValues[i] = (FloatType)signalArray[i];
        }
        else
            return false;

        double sum = 0, sumSq = 0;
        for(FloatType v : signalValues) {
            sum += v;
            sumSq += (double)v * v;
        }
        const double mean = (numAtoms > 0) ? sum / numAtoms : 0.0;
        const double var = std::max(0.0, (numAtoms > 0 ? sumSq / numAtoms : 0.0) - mean * mean);
        const FloatType cutoff = (FloatType)(highValuesAreDisordered ? mean + std::sqrt(var) : mean - std::sqrt(var));

        disordered.resize(numAtoms);
        for(size_t i = 0; i < numAtoms; i++)
            disordered[i] = highValuesAreDisordered ? (signalValues[i] > cutoff) : (signalValues[i] < cutoff);

        maskSignalCutoff = cutoff;
        haveMaskSignalCutoff = true;
        return true;
    };
    if(_maskDisordered) {
        if(_maskSignal == StructureSignal && _structureProperty) {
            BufferReadAccess<int32_t> structureArray(_structureProperty);
            disordered.resize(numAtoms);
            for(size_t i = 0; i < numAtoms; i++)
                disordered[i] = (structureArray[i] == ParticleType::OTHER) ? 1 : 0;
            haveDisorderSignal = true;
        }
        else if(_maskSignal == CentrosymmetrySignal)
            haveDisorderSignal = maskFromScalarSignal(_cspProperty, true);
        else if(_maskSignal == ShearStrainSignal)
            haveDisorderSignal = maskFromScalarSignal(_shearProperty, true);
        else if(_maskSignal == LammpsCentroSignal)
            haveDisorderSignal = maskFromScalarSignal(_lammpsCentroProperty, true);
        else if(_maskSignal == LammpsCoordSignal)
            haveDisorderSignal = maskFromScalarSignal(_lammpsCoordProperty, false);
    }

    // Neighbor finder over the current configuration, used for cavity (free-volume) and masking.
    // Sized with the current-frame nn distance so the first shell is captured under deformation.
    const FloatType searchRadius = (FloatType)1.5 * r_nn_cur;
    CutoffNeighborFinder currentFinder(searchRadius, positions(), cell(), {});

    // --- Output: per-reference-site classification. ---
    PropertyPtr rawOccupancy = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Raw Occupancy"));
    PropertyPtr correctedInterstitialSurplus = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Corrected Interstitial Surplus"));
    PropertyPtr defectTypes = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Defect Type"));
    PropertyPtr vacancyConfidence = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Vacancy Confidence"));
    PropertyPtr nearestAtomDistance = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Nearest Atom Distance"));
    PropertyPtr disorderFraction = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Disorder Fraction"));
    PropertyPtr frenkelPartner = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int64, 1, QStringLiteral("Frenkel Partner Site"));

    // ML descriptor block: raw, continuous versions of the consensus signals, computed for every
    // WS candidate site (occupancy == 0) including sites the rule stages reclassify below. They
    // feed the site dump used for offline training and, eventually, the learned classification mode.
    PropertyPtr knnDistances = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 6, QStringLiteral("Candidate KNN Distances"));
    PropertyPtr shellCounts = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 3, QStringLiteral("Candidate Shell Counts"));
    PropertyPtr nearestVacantDistance = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Nearest Vacant Site Distance"));
    PropertyPtr vacantSitesInShell = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Vacant Sites In Shell"));
    PropertyPtr nearestInterstitialDistance = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Nearest Interstitial Site Distance"));
    PropertyPtr nearestInterstitialSurplus = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Nearest Interstitial Surplus"));
    PropertyPtr interstitialSitesInShell = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Interstitial Sites In Shell"));
    knnDistances->setComponentNames(QStringList() << QStringLiteral("D1") << QStringLiteral("D2") << QStringLiteral("D3") << QStringLiteral("D4") << QStringLiteral("D5") << QStringLiteral("D6"));
    shellCounts->setComponentNames(QStringList() << QStringLiteral("R0.75") << QStringLiteral("R1.00") << QStringLiteral("R1.25"));
    PropertyPtr mlProbability = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("ML Vacancy Probability"));

    // Attach named, colored categories to the Defect Type property so the data inspector shows them
    // as labelled types (and "Color by type" works). The numeric ids are unchanged, so Expression
    // Selection still filters with e.g. "DefectType == 1".
    {
        auto addDefectType = [&](int id, const QString& name, const Color& color) {
            DataOORef<ElementType> type = DataOORef<ElementType>::create();
            type->setNumericId(id);
            type->setName(name);
            type->setColor(color);
            defectTypes->addElementType(std::move(type));
        };
        addDefectType(DefectOK,           tr("OK"),           Color(0.70, 0.70, 0.70));
        addDefectType(DefectVacancy,      tr("Vacancy"),      Color(0.90, 0.15, 0.15));
        addDefectType(DefectInterstitial, tr("Interstitial"), Color(0.15, 0.30, 0.90));
        addDefectType(DefectFrenkel,      tr("Frenkel pair"), Color(0.95, 0.60, 0.10));
        addDefectType(DefectNoCavity,     tr("No cavity"),    Color(0.60, 0.40, 0.20));
        addDefectType(DefectStructural,   tr("Structural"),   Color(0.20, 0.70, 0.30));
        addDefectType(DefectSurfaceOrVoid, tr("Surface/void"), Color(0.50, 0.50, 0.50));
        addDefectType(DefectMLRejected,   tr("ML rejected"),  Color(0.60, 0.20, 0.60));
    }
    BufferWriteAccess<int32_t, access_mode::discard_write> rawOccupancyArray(rawOccupancy);
    BufferWriteAccess<int32_t, access_mode::discard_write> correctedSurplusArray(correctedInterstitialSurplus);
    BufferWriteAccess<int32_t, access_mode::discard_write> defectTypeArray(defectTypes);
    BufferWriteAccess<FloatType, access_mode::discard_write> confidenceArray(vacancyConfidence);
    BufferWriteAccess<FloatType, access_mode::discard_write> nearestDistanceArray(nearestAtomDistance);
    BufferWriteAccess<FloatType, access_mode::discard_write> disorderFractionArray(disorderFraction);
    BufferWriteAccess<int64_t, access_mode::discard_write> partnerArray(frenkelPartner);
    // discard_read_write: the ML pass below reads these descriptors back after writing them.
    BufferWriteAccess<FloatType*, access_mode::discard_read_write> knnDistancesArray(knnDistances);
    BufferWriteAccess<int32_t*, access_mode::discard_read_write> shellCountsArray(shellCounts);
    BufferWriteAccess<FloatType, access_mode::discard_write> nearestVacantArray(nearestVacantDistance);
    BufferWriteAccess<int32_t, access_mode::discard_write> vacantShellArray(vacantSitesInShell);
    BufferWriteAccess<FloatType, access_mode::discard_write> nearestInterstitialArray(nearestInterstitialDistance);
    BufferWriteAccess<int32_t, access_mode::discard_write> interstitialSurplusNNArray(nearestInterstitialSurplus);
    BufferWriteAccess<int32_t, access_mode::discard_write> interstitialShellArray(interstitialSitesInShell);
    BufferWriteAccess<FloatType, access_mode::discard_write> mlProbabilityArray(mlProbability);
    for(size_t s = 0; s < numSites; s++) {
        int o = occupancy[s].load(std::memory_order_relaxed);
        rawOccupancyArray[s] = o;
        correctedSurplusArray[s] = interstitialSurplus[s];
        defectTypeArray[s] = (interstitialSurplus[s] > 0) ? DefectInterstitial : DefectOK;
        confidenceArray[s] = 0;
        nearestDistanceArray[s] = (FloatType)-1;
        disorderFractionArray[s] = (FloatType)-1;
        partnerArray[s] = -1;
        for(size_t c = 0; c < 6; c++) knnDistancesArray.set(s, c, (FloatType)-1);
        for(size_t c = 0; c < 3; c++) shellCountsArray.set(s, c, -1);
        nearestVacantArray[s] = (FloatType)-1;
        vacantShellArray[s] = -1;
        nearestInterstitialArray[s] = (FloatType)-1;
        interstitialSurplusNNArray[s] = -1;
        interstitialShellArray[s] = -1;
        mlProbabilityArray[s] = (FloatType)-1;
    }

    // --- ML descriptor pass over the WS candidate sites. ---
    // Distances to current atoms are measured in the current frame; the candidate-context and
    // interstitial-context descriptors live in the reference frame, like the Frenkel matching.
    if(!vacantSites.empty()) {
        progress.setText(tr("Vacancy analysis: computing candidate descriptors"));
        BufferReadAccess<Point3> refPosDescArray(refPositions());

        std::vector<size_t> overOccupiedSites;
        for(size_t s = 0; s < numSites; s++)
            if(rawOccupancyArray[s] >= 2) overOccupiedSites.push_back(s);

        auto makeSiteSubsetFinder = [&](const std::vector<size_t>& subset) {
            PropertyPtr pos = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, subset.size(), Property::FloatDefault, 3, QStringLiteral("Position"));
            BufferWriteAccess<Point3, access_mode::discard_write> acc(pos);
            for(size_t i = 0; i < subset.size(); i++)
                acc[i] = refPosDescArray[subset[i]];
            acc.reset();
            return std::make_unique<NearestNeighborFinder>(0, pos, refCell(), BufferReadAccess<SelectionIntType>{});
        };
        std::unique_ptr<NearestNeighborFinder> vacantFinder = (vacantSites.size() >= 2) ? makeSiteSubsetFinder(vacantSites) : nullptr;
        std::unique_ptr<NearestNeighborFinder> overFinder = !overOccupiedSites.empty() ? makeSiteSubsetFinder(overOccupiedSites) : nullptr;

        const FloatType contextRadiusSq = (FloatType)(1.5 * 1.5) * r_nn * r_nn;
        auto countWithin = [contextRadiusSq](const NearestNeighborFinder& finder, const Point3& p) {
            int count = 0;
            auto visitor = [&count, contextRadiusSq](const NearestNeighborFinder::Neighbor& n, FloatType& mrs) {
                mrs = contextRadiusSq;
                if(n.distanceSq <= contextRadiusSq) count++;
            };
            finder.visitNeighbors(p, visitor, /*includeSelf=*/false);
            return count;
        };

        const FloatType shellR075Sq = (FloatType)(0.75 * 0.75) * r_nn * r_nn;
        const FloatType shellR100Sq = r_nn * r_nn;
        const FloatType shellR125Sq = (FloatType)(1.25 * 1.25) * r_nn * r_nn;

        parallelFor(vacantSites.size(), 64, progress, [&](size_t vi) {
            const size_t vs = vacantSites[vi];
            const Point3 refPoint = refPosDescArray[vs];
            Point3 queryPoint = refPoint;
            if(affineMapping() == TO_REFERENCE_CELL) queryPoint = tmInv * queryPoint;

            if(numAtoms > 0) {
                // Distances to the 6 nearest current atoms (the cavity signal, continuous).
                NearestNeighborFinder::Query<6> knnQuery(currentNearestFinder);
                knnQuery.findNeighbors(queryPoint, /*includeSelf=*/true);
                size_t c = 0;
                for(const auto& n : knnQuery.results())
                    knnDistancesArray.set(vs, c++, std::sqrt(n.distanceSq));

                // Current atoms within 0.75 / 1.00 / 1.25 reference nn distances.
                int c075 = 0, c100 = 0, c125 = 0;
                auto binVisitor = [&](const NearestNeighborFinder::Neighbor& n, FloatType& mrs) {
                    mrs = shellR125Sq;
                    if(n.distanceSq <= shellR125Sq) {
                        c125++;
                        if(n.distanceSq <= shellR100Sq) {
                            c100++;
                            if(n.distanceSq <= shellR075Sq) c075++;
                        }
                    }
                };
                currentNearestFinder.visitNeighbors(queryPoint, binVisitor, /*includeSelf=*/true);
                shellCountsArray.set(vs, 0, c075);
                shellCountsArray.set(vs, 1, c100);
                shellCountsArray.set(vs, 2, c125);
            }

            // Clustering of the candidates themselves (cascade cores vs isolated vacancies).
            if(vacantFinder) {
                FloatType dsq;
                vacantFinder->findClosestParticle(refPoint, dsq, /*includeSelf=*/false);
                if(dsq < FLOATTYPE_MAX) nearestVacantArray[vs] = std::sqrt(dsq);
                vacantShellArray[vs] = countWithin(*vacantFinder, refPoint);
            }

            // Continuous Frenkel signal: proximity of over-occupied sites and their remaining surplus.
            if(overFinder) {
                FloatType dsq;
                size_t idx = overFinder->findClosestParticle(refPoint, dsq, /*includeSelf=*/false);
                if(dsq < FLOATTYPE_MAX && idx < overOccupiedSites.size()) {
                    nearestInterstitialArray[vs] = std::sqrt(dsq);
                    interstitialSurplusNNArray[vs] = interstitialSurplus[overOccupiedSites[idx]];
                }
                interstitialShellArray[vs] = countWithin(*overFinder, refPoint);
            }
        });
    }

    // --- Stages 3 + 4: free-volume confirmation and structural masking of the surviving vacancies. ---
    BufferReadAccess<Point3> refPosArray(refPositions());
    size_t freeVolumeExcluded = 0, structuralExcluded = 0, surfaceExcluded = 0, classifiedVacancyCount = 0;
    progress.setText(tr("Vacancy analysis: classifying vacancy candidates"));
    progress.setMaximum(vacantSites.size());
    for(size_t vi = 0; vi < vacantSites.size(); vi++) {
        progress.setValueIntermittent(vi);
        const size_t vs = vacantSites[vi];
        if(matchedVacant[vi]) {
            defectTypeArray[vs] = DefectFrenkel;
            partnerArray[vs] = matchedPartnerSite[vi];
            confidenceArray[vs] = 0;
            continue;
        }

        // Location of the empty site in the current frame.
        Point3 queryPoint = refPosArray[vs];
        if(affineMapping() == TO_REFERENCE_CELL) queryPoint = tmInv * queryPoint;

        FloatType nearestDistanceSq = FLOATTYPE_MAX;
        if(numAtoms > 0) {
            currentNearestFinder.findClosestParticle(queryPoint, nearestDistanceSq);
            if(nearestDistanceSq < FLOATTYPE_MAX)
                nearestDistanceArray[vs] = std::sqrt(nearestDistanceSq);
        }

        int numNeighbors = 0, numDisordered = 0;
        for(CutoffNeighborFinder::Query q(currentFinder, queryPoint); !q.atEnd(); q.next()) {
            numNeighbors++;
            if(haveDisorderSignal && disordered[q.current()]) numDisordered++;
        }
        if(numNeighbors > 0 && haveDisorderSignal)
            disorderFractionArray[vs] = (FloatType)numDisordered / numNeighbors;

        if(numNeighbors == 0) {
            // No atom in the local shell: compatible with a free surface, void boundary, or large low-density region.
            defectTypeArray[vs] = DefectSurfaceOrVoid;
            confidenceArray[vs] = (FloatType)0.1;
            surfaceExcluded++;
            continue;
        }

        const bool cavityPass = !_requireFreeVolume || (nearestDistanceArray[vs] >= _cavityRadiusRatio * r_nn_cur);
        // A candidate is masked as structural only when the disordered-neighbor fraction is
        // strictly above the threshold, so maskThreshold = 0 keeps candidates with a fully
        // ordered environment and maskThreshold = 1 disables the mask.
        const bool ordered = !(haveDisorderSignal) || (disorderFractionArray[vs] <= _maskThreshold);

        // Confidence = fraction of the consensus tests passed (survived Frenkel + cavity + ordered).
        const int passed = 1 + (cavityPass ? 1 : 0) + (ordered ? 1 : 0);
        confidenceArray[vs] = (FloatType)passed / 3;

        if(!cavityPass) {
            defectTypeArray[vs] = DefectNoCavity;
            freeVolumeExcluded++;
        }
        else if(!ordered) {
            defectTypeArray[vs] = DefectStructural;
            structuralExcluded++;
        }
        else {
            defectTypeArray[vs] = DefectVacancy;
            confidenceArray[vs] = 1;
            classifiedVacancyCount++;
        }
    }

    // --- ML mode: the learned classifier overrides the final vacancy decision. ---
    // The rule stages above still run, so their per-site types and stage counters remain available
    // as diagnostics; only the Vacancy verdict (and the reported vacancy_count) is replaced.
    size_t mlRejectedCount = 0, mlResurrectedCount = 0;
    if(_mlModel && !vacantSites.empty()) {
        progress.setText(tr("Vacancy analysis: evaluating ML classifier"));
        const FloatType farDistance = _mlModel->farSentinel();  // in nn units
        size_t mlVacancyCount = 0;
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            const size_t vs = vacantSites[vi];

            // Feature layout must match FEATURES in ml_prototype_vacancy_classifier.py.
            FloatType features[VacancyMLModel::NUM_FEATURES];
            for(size_t c = 0; c < 6; c++) {
                const FloatType d = knnDistancesArray.get(vs, c);
                features[c] = (d >= 0) ? (d / r_nn) : farDistance;
            }
            for(size_t c = 0; c < 3; c++)
                features[6 + c] = std::max(shellCountsArray.get(vs, c), 0);
            features[9] = correctedSurplusArray[vs];
            features[10] = disorderFractionArray[vs];
            features[11] = (partnerArray[vs] >= 0) ? 1 : 0;
            features[12] = (nearestVacantArray[vs] >= 0) ? (nearestVacantArray[vs] / r_nn) : farDistance;
            features[13] = std::max(vacantShellArray[vs], 0);
            features[14] = (nearestInterstitialArray[vs] >= 0) ? (nearestInterstitialArray[vs] / r_nn) : farDistance;
            features[15] = std::max(interstitialSurplusNNArray[vs], 0);
            features[16] = std::max(interstitialShellArray[vs], 0);

            const FloatType prob = _mlModel->evaluate(features);
            mlProbabilityArray[vs] = prob;
            confidenceArray[vs] = prob;
            if(prob >= _mlThreshold) {
                if(defectTypeArray[vs] != DefectVacancy) mlResurrectedCount++;
                defectTypeArray[vs] = DefectVacancy;
                mlVacancyCount++;
            }
            else if(defectTypeArray[vs] == DefectVacancy) {
                defectTypeArray[vs] = DefectMLRejected;
                mlRejectedCount++;
            }
        }
        classifiedVacancyCount = mlVacancyCount;
    }

    const size_t correctedInterstitialCount = rawInterstitialCount - frenkelRecombined;

    // --- Replace the current configuration with the reference configuration in the output. ---
    const Particles* refParticles = referenceState().getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("This modifier cannot be evaluated, because the reference configuration does not contain any particles."));
    state.replaceObject(state.expectObject<Particles>(), refParticles);
    if(const SimulationCell* cell = state.getObject<SimulationCell>())
        state.replaceObject(cell, referenceState().getObject<SimulationCell>());

    Particles* outParticles = state.expectMutableObject<Particles>();
    OVITO_ASSERT(defectTypes->size() == outParticles->elementCount());
    outParticles->createProperty(rawOccupancy);
    outParticles->createProperty(correctedInterstitialSurplus);
    outParticles->createProperty(defectTypes);
    outParticles->createProperty(vacancyConfidence);
    outParticles->createProperty(nearestAtomDistance);
    outParticles->createProperty(disorderFraction);
    outParticles->createProperty(frenkelPartner);
    outParticles->createProperty(knnDistances);
    outParticles->createProperty(shellCounts);
    outParticles->createProperty(nearestVacantDistance);
    outParticles->createProperty(vacantSitesInShell);
    outParticles->createProperty(nearestInterstitialDistance);
    outParticles->createProperty(nearestInterstitialSurplus);
    outParticles->createProperty(interstitialSitesInShell);
    outParticles->createProperty(mlProbability);

    // --- Global attributes: corrected counts plus the raw Wigner-Seitz counts for comparison. ---
    state.addAttribute(QStringLiteral("VacancyAnalysis.vacancy_count"), QVariant::fromValue(classifiedVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.interstitial_count"), QVariant::fromValue(correctedInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_vacancies"), QVariant::fromValue(rawVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_interstitials"), QVariant::fromValue(rawInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.frenkel_recombined"), QVariant::fromValue(frenkelRecombined), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.freevolume_excluded"), QVariant::fromValue(freeVolumeExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.structural_excluded"), QVariant::fromValue(structuralExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.surface_or_void_excluded"), QVariant::fromValue(surfaceExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.estimated_nn_distance"), QVariant::fromValue(r_nn), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.estimated_nn_distance_current"), QVariant::fromValue(r_nn_cur), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_used"), QVariant::fromValue(haveDisorderSignal), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_name"), QVariant::fromValue(selectedMaskSignalName), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_cutoff"), QVariant::fromValue(haveMaskSignalCutoff ? maskSignalCutoff : (FloatType)-1), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.ml_used"), QVariant::fromValue(_mlModel != nullptr), _createdByNode);
    if(_mlModel) {
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_threshold"), QVariant::fromValue(_mlThreshold), _createdByNode);
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_rejected"), QVariant::fromValue(mlRejectedCount), _createdByNode);
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_resurrected"), QVariant::fromValue(mlResurrectedCount), _createdByNode);
    }

    PipelineStatus status(PipelineStatus::Success,
        tr("Classified %1 vacancy candidates and %2 corrected interstitials.\nWigner-Seitz raw: %3 vacancies. Removed: %4 Frenkel pairs, %5 without free volume, %6 in disordered regions, %7 near surfaces/voids.")
            .arg(classifiedVacancyCount).arg(correctedInterstitialCount).arg(rawVacancyCount)
            .arg(frenkelRecombined).arg(freeVolumeExcluded).arg(structuralExcluded).arg(surfaceExcluded));
    if(_mlModel)
        status.setText(status.text() + tr("\nML classifier verdict (threshold %1): rejected %2 rule-passed candidates, restored %3 rule-rejected ones.")
            .arg(_mlThreshold).arg(mlRejectedCount).arg(mlResurrectedCount));
    if(_maskDisordered && !haveDisorderSignal) {
        status.setType(PipelineStatus::Warning);
        status.setText(status.text() + tr("\nStructural masking was requested, but the selected upstream signal '%1' is not available. No sites were excluded by the structural mask.").arg(selectedMaskSignalName));
    }
    state.setStatus(std::move(status));
}

}   // End of namespace
