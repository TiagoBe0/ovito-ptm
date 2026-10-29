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
#include <ovito/stdobj/table/DataTable.h>
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
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, nearestAtomRatio);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, identityPairing);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskDisordered);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskSignal);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskThreshold);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlClassification);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlModelPath);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, mlThreshold);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, outputSites);
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, frenkelRecombination, "Recombine Frenkel pairs");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, recombinationCutoff, "Recombination cutoff");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, requireFreeVolume, "Require free volume");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, cavityRadiusRatio, "Min. cavity radius (x nn dist.)");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, nearestAtomRatio, "Min. nearest-atom distance (x closest-pair dist.)");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, identityPairing, "Pair defects by atom identity");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskDisordered, "Mask disordered regions");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskSignal, "Disorder signal");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskThreshold, "Disorder fraction threshold");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlClassification, "ML classification");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlModelPath, "ML model file");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, mlThreshold, "ML probability threshold");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, outputSites, "Output sites");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(VacancyAnalysisModifier, recombinationCutoff, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(VacancyAnalysisModifier, cavityRadiusRatio, FloatParameterUnit, 0, 1);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(VacancyAnalysisModifier, nearestAtomRatio, FloatParameterUnit, 0, 1);
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

    // Adopt the bar charts and all global attributes computed by the modifier from the cached state.
    for(const QString& tableId : {QStringLiteral("vacancy-ws-comparison"), QStringLiteral("vacancy-ws-outcome")}) {
        if(const DataTable* cachedTable = cachedState.getObjectBy<DataTable>(request.modificationNode(), tableId))
            output.addObject(cachedTable);
    }
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

    // Particle identifiers enable the replacement-chain ledger; without them in both configurations the
    // interstitials are corrected by the geometric Frenkel recombination alone.
    const Property* identifierProperty = identityPairing() ? particles->getProperty(Particles::IdentifierProperty) : nullptr;
    const Property* refIdentifierProperty = identityPairing() ? refParticles->getProperty(Particles::IdentifierProperty) : nullptr;
    if(!identifierProperty || !refIdentifierProperty)
        identifierProperty = refIdentifierProperty = nullptr;

    // Pick up optional upstream per-atom disorder signals from the current configuration.
    const Property* structureProperty = particles->getProperty(Particles::StructureTypeProperty);
    const Property* cspProperty = particles->getProperty(Particles::CentroSymmetryProperty);
    const Property* shearProperty = particles->getProperty(QStringLiteral("Shear Strain"));
    const Property* lammpsCentroProperty = particles->getProperty(QStringLiteral("c_centro"));
    const Property* lammpsCoordProperty = particles->getProperty(QStringLiteral("c_coord"));

    // Optional upstream per-atom deformation signals (AtomicStrain / PTM / Voronoi), used to build
    // the continuous local-context descriptors of each candidate site.
    const Property* volumetricStrainProperty = particles->getProperty(QStringLiteral("Volumetric Strain"));
    const Property* rmsdProperty = particles->getProperty(QStringLiteral("RMSD"));
    const Property* atomicVolumeProperty = particles->getProperty(QStringLiteral("Atomic Volume"));

    // Load the learned classifier if the ML mode is enabled. Loading happens here (main thread)
    // so a bad model path surfaces as a modifier error instead of failing inside the engine.
    std::shared_ptr<const VacancyMLModel> mlModel;
    if(mlClassification()) {
        if(mlModelPath().trimmed().isEmpty())
            throw Exception(tr("ML classification is enabled, but no model file has been specified."));
        mlModel = VacancyMLModel::load(mlModelPath().trimmed());
    }

    return std::make_unique<VacancyAnalysisEngine>(posProperty, inputCell,
            referenceState, refPosProperty, refCell, identifierProperty, refIdentifierProperty, affineMapping(),
            frenkelRecombination(), recombinationCutoff(),
            requireFreeVolume(), cavityRadiusRatio(), nearestAtomRatio(),
            maskDisordered(), maskSignal(), maskThreshold(),
            structureProperty, cspProperty, shearProperty, lammpsCentroProperty, lammpsCoordProperty,
            volumetricStrainProperty, rmsdProperty, atomicVolumeProperty,
            std::move(mlModel), mlThreshold(),
            outputSites(),
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
    // Sized for the 6-nearest queries of the nn-distance estimate; findClosestParticle is unaffected.
    NearestNeighborFinder siteFinder(6, refPositions(), refCell(), {});
    std::vector<std::atomic_int> occupancy(numSites);
    for(auto& o : occupancy)
        o.store(0, std::memory_order_relaxed);

    BufferReadAccess<Point3> positionsArray(positions());
    std::vector<size_t> siteOfAtom(numAtoms);
    parallelFor(numAtoms, 1024, progress, [&](size_t i) {
        Point3 p = positionsArray[i];
        if(affineMapping() == TO_REFERENCE_CELL) p = tm * p;
        FloatType closestDistanceSq;
        size_t site = siteFinder.findClosestParticle(p, closestDistanceSq);
        siteOfAtom[i] = site;
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

    // Nearest-neighbor distance estimate: median, over a uniform sample of points, of the mean
    // distance to their 6 nearest neighbors. On FCC/BCC/HCP lattices these 6 all belong to the first
    // shell, so the estimate equals the nn distance. The distance to the single closest neighbor is
    // NOT used: as the minimum over the whole first shell it is biased low by thermal noise (at the
    // synthetic noise sigma = 0.20 it read 1.65 instead of 2.55, i.e. 35% too small). It is also the
    // same statistic as the cavity radius of an empty site, so the cavity test compares like with like.
    // With closestOnly = true the median of the closest-neighbor distance is returned instead; that is
    // the comparator of the nearest-atom test, which wants the noise bias on both sides.
    auto medianNeighborDistance = [](const NearestNeighborFinder& finder, auto&& points, size_t count, bool closestOnly = false) -> FloatType {
        if(count < 2)
            return 0;
        const size_t nSample = std::min(count, (size_t)1000);
        std::vector<FloatType> meanDistances;
        meanDistances.reserve(nSample);
        NearestNeighborFinder::Query<6> query(finder);
        for(size_t i = 0; i < nSample; i++) {
            const size_t s = (nSample == count) ? i : (i * (count - 1)) / (nSample - 1);
            query.findNeighbors(points[s], /*includeSelf=*/false);
            if(query.results().empty())
                continue;
            FloatType sum = 0, closestSq = std::numeric_limits<FloatType>::max();
            for(const auto& n : query.results()) {
                sum += std::sqrt(n.distanceSq);
                closestSq = std::min(closestSq, n.distanceSq);
            }
            const FloatType mean = closestOnly ? std::sqrt(closestSq) : sum / query.results().size();
            if(mean > 0)
                meanDistances.push_back(mean);
        }
        if(meanDistances.empty())
            return 0;
        auto median = meanDistances.begin() + meanDistances.size() / 2;
        std::nth_element(meanDistances.begin(), median, meanDistances.end());
        return *median;
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
    // Sized for the k-nearest queries here and in the ML descriptor block below.
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
    // Median closest-pair distance between current atoms, the yardstick of the nearest-atom test.
    FloatType r_closest_cur = medianNeighborDistance(currentNearestFinder, positionsArray, numAtoms, true);
    if(r_closest_cur <= 0)
        r_closest_cur = r_nn_cur;

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

    // --- Replacement chains through the particle identifiers. ---
    // Every site emits its own atom and receives the atoms assigned to it, so a vacant site is the start
    // of a path: its own atom sits in site t, the own atom of t sits in site u, ... until the path reaches
    // a site with surplus capacity left (the displaced atom is counted there as an interstitial) or a
    // site whose own atom no longer exists (the vacancy is a missing atom, not a displaced one). Under
    // the BCC/HCP transformation these paths run 2-8 nn, far beyond the geometric recombination cutoff,
    // which is why rejecting a vacancy without cancelling its chain end left a spurious interstitial.
    constexpr int64_t ChainMissingAtom = -1;
    constexpr int64_t ChainUnresolved = -2;
    const bool identityLedger = identifiers() && refIdentifiers();
    std::vector<int64_t> chainEnd(vacantSites.size(), ChainUnresolved);
    std::vector<int> chainSurplusLeft;   // surplus units no chain reached (atoms absent from the reference)
    size_t missingAtomVacancies = 0;
    if(identityLedger) {
        progress.setText(tr("Vacancy analysis: following replacement chains"));
        buildParticleMapping(false, false);
        const std::vector<size_t>& ownAtom = refToCurrentIndexMap();
        chainSurplusLeft.resize(numSites);
        for(size_t s = 0; s < numSites; s++)
            chainSurplusLeft[s] = std::max(occupancy[s].load(std::memory_order_relaxed) - 1, 0);
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            size_t t = vacantSites[vi];
            // A path never revisits a site, so it is at most numSites steps long.
            for(size_t step = 0; step < numSites; step++) {
                const size_t a = ownAtom[t];
                if(a == std::numeric_limits<size_t>::max()) {
                    chainEnd[vi] = ChainMissingAtom;
                    missingAtomVacancies++;
                    break;
                }
                t = siteOfAtom[a];
                if(chainSurplusLeft[t] > 0) {
                    chainSurplusLeft[t]--;
                    chainEnd[vi] = (int64_t)t;
                    break;
                }
            }
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
    // Reads a single-component per-atom property into a plain float array. Accepts float as well as
    // integer signals (e.g. a coordination count that was imported or remapped as an integer column).
    auto toScalarArray = [&](const Property* signalProperty, std::vector<FloatType>& signalValues) {
        if(!signalProperty || signalProperty->componentCount() != 1 || signalProperty->size() != numAtoms)
            return false;

        signalValues.resize(numAtoms);
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
        return true;
    };
    auto maskFromScalarSignal = [&](const Property* signalProperty, bool highValuesAreDisordered) {
        std::vector<FloatType> signalValues;
        if(!toScalarArray(signalProperty, signalValues))
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
    // Sample-wide fraction of disordered atoms. When it exceeds the mask threshold, even a site
    // picked at random would be masked, so the mask no longer discriminates anything (typically
    // PTM/CNA under thermal noise beyond their RMSD tolerance).
    FloatType globalDisorderedFraction = (FloatType)-1;
    if(haveDisorderSignal && numAtoms > 0)
        globalDisorderedFraction = (FloatType)std::count(disordered.begin(), disordered.end(), 1) / numAtoms;

    // --- Continuous per-atom deformation context (used for the local-context descriptors). ---
    // These are never thresholded: they are averaged over the neighborhood of each candidate site so
    // that "this empty site sits in a heavily sheared region" becomes a number instead of a binary
    // mask decision. The atomic volume is normalized by its own mean, making it a dimensionless
    // free-volume excess that is comparable across materials and cell sizes.
    std::vector<FloatType> localShearValues, localVolStrainValues, localRmsdValues, localAtomicVolumeValues;
    const bool haveLocalShear = toScalarArray(_shearProperty, localShearValues);
    const bool haveLocalVolStrain = toScalarArray(_volumetricStrainProperty, localVolStrainValues);
    const bool haveLocalRmsd = toScalarArray(_rmsdProperty, localRmsdValues);
    bool haveLocalAtomicVolume = toScalarArray(_atomicVolumeProperty, localAtomicVolumeValues);
    if(haveLocalAtomicVolume) {
        double volumeSum = 0;
        for(FloatType v : localAtomicVolumeValues) volumeSum += v;
        const double meanVolume = (numAtoms > 0) ? volumeSum / numAtoms : 0.0;
        if(meanVolume > 0) {
            for(FloatType& v : localAtomicVolumeValues) v = (FloatType)(v / meanVolume);
        }
        else haveLocalAtomicVolume = false;
    }
    QStringList availableLocalSignals;
    if(haveLocalShear) availableLocalSignals << QStringLiteral("shear_strain");
    if(haveLocalVolStrain) availableLocalSignals << QStringLiteral("volumetric_strain");
    if(haveLocalRmsd) availableLocalSignals << QStringLiteral("rmsd");
    if(haveLocalAtomicVolume) availableLocalSignals << QStringLiteral("atomic_volume");

    // Neighbor finder over the current configuration, used for cavity (free-volume) and masking.
    // Sized with the current-frame nn distance so the first shell is captured under deformation.
    const FloatType searchRadius = (FloatType)1.5 * r_nn_cur;
    CutoffNeighborFinder currentFinder(searchRadius, positions(), cell(), {});

    // The structural mask is evaluated over a shell that EXCLUDES the atoms adjacent to the empty
    // site. Those atoms lost a neighbor to the vacancy itself, so PTM/CNA label them OTHER, their
    // centrosymmetry jumps and their coordination drops: measured over the 1.5 nn shell, an isolated
    // vacancy in a perfect FCC crystal has 12/18 "disordered" neighbors and was always masked.
    // The inner radius also skips the BCC second shell (1.155 nn), which CNA and PTM use as well.
    const FloatType maskInnerRadiusSq = (FloatType)(1.3 * 1.3) * r_nn_cur * r_nn_cur;
    std::optional<CutoffNeighborFinder> maskFinder;
    if(haveDisorderSignal)
        maskFinder.emplace((FloatType)2.1 * r_nn_cur, positions(), cell(), BufferReadAccess<SelectionIntType>{});

    // --- Output: per-reference-site classification. ---
    PropertyPtr rawOccupancy = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Raw Occupancy"));
    PropertyPtr correctedInterstitialSurplus = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Corrected Interstitial Surplus"));
    PropertyPtr defectTypes = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Defect Type"));
    PropertyPtr vacancyConfidence = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Vacancy Confidence"));
    PropertyPtr nearestAtomDistance = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Nearest Atom Distance"));
    PropertyPtr cavityRadius = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Cavity Radius"));
    PropertyPtr disorderFraction = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Disorder Fraction"));
    PropertyPtr frenkelPartner = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int64, 1, QStringLiteral("Frenkel Partner Site"));
    // Identity ledger: the over-occupied site at the end of each vacancy's replacement chain (-1 = the
    // site's own atom is missing, -2 = no chain) and its distance in the reference frame.
    PropertyPtr chainPartner = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int64, 1, QStringLiteral("Replacement Chain Partner"));
    PropertyPtr chainDistance = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Replacement Chain Distance"));

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

    // Local deformation context of each candidate site, aggregated over the current atoms inside the
    // 1.5 nn shell around it (first shell included, unlike "Disorder Fraction"). This is the
    // continuous counterpart of the binary structural mask: it is what lets a classifier tell a
    // genuinely empty site apart from a site that only looks empty because its atom was pushed away
    // by the surrounding deformation.
    PropertyPtr localShearStrain = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 2, QStringLiteral("Local Shear Strain"));
    PropertyPtr localVolumetricStrain = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Local Volumetric Strain"));
    PropertyPtr localRmsd = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Local RMSD"));
    PropertyPtr localAtomicVolume = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Local Atomic Volume"));
    // Number of current atoms the aggregates above average over. -1 marks a non-candidate site and 0
    // a candidate with an empty shell, so consumers never have to guess from a sentinel value
    // (volumetric strain in particular is legitimately negative under compression).
    PropertyPtr localContextCount = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Local Context Count"));
    localShearStrain->setComponentNames(QStringList() << QStringLiteral("Mean") << QStringLiteral("Max"));

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
    BufferWriteAccess<FloatType, access_mode::discard_write> cavityRadiusArray(cavityRadius);
    BufferWriteAccess<FloatType, access_mode::discard_write> disorderFractionArray(disorderFraction);
    BufferWriteAccess<int64_t, access_mode::discard_write> partnerArray(frenkelPartner);
    BufferWriteAccess<int64_t, access_mode::discard_write> chainPartnerArray(chainPartner);
    BufferWriteAccess<FloatType, access_mode::discard_write> chainDistanceArray(chainDistance);
    // discard_read_write: the ML pass below reads these descriptors back after writing them.
    BufferWriteAccess<FloatType*, access_mode::discard_read_write> knnDistancesArray(knnDistances);
    BufferWriteAccess<int32_t*, access_mode::discard_read_write> shellCountsArray(shellCounts);
    BufferWriteAccess<FloatType, access_mode::discard_write> nearestVacantArray(nearestVacantDistance);
    BufferWriteAccess<int32_t, access_mode::discard_write> vacantShellArray(vacantSitesInShell);
    BufferWriteAccess<FloatType, access_mode::discard_write> nearestInterstitialArray(nearestInterstitialDistance);
    BufferWriteAccess<int32_t, access_mode::discard_write> interstitialSurplusNNArray(nearestInterstitialSurplus);
    BufferWriteAccess<int32_t, access_mode::discard_write> interstitialShellArray(interstitialSitesInShell);
    BufferWriteAccess<FloatType, access_mode::discard_write> mlProbabilityArray(mlProbability);
    BufferWriteAccess<FloatType*, access_mode::discard_write> localShearArray(localShearStrain);
    BufferWriteAccess<FloatType, access_mode::discard_write> localVolStrainArray(localVolumetricStrain);
    BufferWriteAccess<FloatType, access_mode::discard_write> localRmsdArray(localRmsd);
    BufferWriteAccess<FloatType, access_mode::discard_write> localAtomicVolumeArray(localAtomicVolume);
    BufferWriteAccess<int32_t, access_mode::discard_write> localContextCountArray(localContextCount);
    for(size_t s = 0; s < numSites; s++) {
        int o = occupancy[s].load(std::memory_order_relaxed);
        rawOccupancyArray[s] = o;
        correctedSurplusArray[s] = interstitialSurplus[s];
        defectTypeArray[s] = (interstitialSurplus[s] > 0) ? DefectInterstitial : DefectOK;
        confidenceArray[s] = 0;
        nearestDistanceArray[s] = (FloatType)-1;
        cavityRadiusArray[s] = (FloatType)-1;
        disorderFractionArray[s] = (FloatType)-1;
        partnerArray[s] = -1;
        chainPartnerArray[s] = ChainUnresolved;
        chainDistanceArray[s] = (FloatType)-1;
        for(size_t c = 0; c < 6; c++) knnDistancesArray.set(s, c, (FloatType)-1);
        for(size_t c = 0; c < 3; c++) shellCountsArray.set(s, c, -1);
        nearestVacantArray[s] = (FloatType)-1;
        vacantShellArray[s] = -1;
        nearestInterstitialArray[s] = (FloatType)-1;
        interstitialSurplusNNArray[s] = -1;
        interstitialShellArray[s] = -1;
        mlProbabilityArray[s] = (FloatType)-1;
        localShearArray.set(s, 0, (FloatType)0);
        localShearArray.set(s, 1, (FloatType)0);
        localVolStrainArray[s] = 0;
        localRmsdArray[s] = 0;
        localAtomicVolumeArray[s] = 0;
        localContextCountArray[s] = -1;
    }

    if(identityLedger) {
        BufferReadAccess<Point3> refPosChainArray(refPositions());
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            const size_t vs = vacantSites[vi];
            chainPartnerArray[vs] = chainEnd[vi];
            if(chainEnd[vi] >= 0)
                chainDistanceArray[vs] = refCell().wrapVector(refPosChainArray[chainEnd[vi]] - refPosChainArray[vs]).length();
        }
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

            // Local deformation context: aggregate the continuous per-atom signals over the 1.5 nn
            // shell (first shell included), so every candidate carries a measure of how deformed
            // its surroundings are, independent of whether the mask happened to reject it.
            {
                int contextCount = 0;
                double shearSum = 0, volStrainSum = 0, rmsdSum = 0, atomicVolumeSum = 0;
                FloatType shearMax = 0;
                for(CutoffNeighborFinder::Query q(currentFinder, queryPoint); !q.atEnd(); q.next()) {
                    const size_t a = q.current();
                    contextCount++;
                    if(haveLocalShear) {
                        shearSum += localShearValues[a];
                        shearMax = std::max(shearMax, localShearValues[a]);
                    }
                    if(haveLocalVolStrain) volStrainSum += localVolStrainValues[a];
                    if(haveLocalRmsd) rmsdSum += localRmsdValues[a];
                    if(haveLocalAtomicVolume) atomicVolumeSum += localAtomicVolumeValues[a];
                }
                localContextCountArray[vs] = contextCount;
                if(contextCount > 0) {
                    if(haveLocalShear) {
                        localShearArray.set(vs, 0, (FloatType)(shearSum / contextCount));
                        localShearArray.set(vs, 1, shearMax);
                    }
                    if(haveLocalVolStrain) localVolStrainArray[vs] = (FloatType)(volStrainSum / contextCount);
                    if(haveLocalRmsd) localRmsdArray[vs] = (FloatType)(rmsdSum / contextCount);
                    if(haveLocalAtomicVolume) localAtomicVolumeArray[vs] = (FloatType)(atomicVolumeSum / contextCount);
                }
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

        int numNeighbors = 0;
        for(CutoffNeighborFinder::Query q(currentFinder, queryPoint); !q.atEnd(); q.next())
            numNeighbors++;

        if(maskFinder) {
            int numMaskShell = 0, numDisordered = 0;
            for(CutoffNeighborFinder::Query q(*maskFinder, queryPoint); !q.atEnd(); q.next()) {
                if(q.distanceSquared() <= maskInnerRadiusSq) continue;
                numMaskShell++;
                if(disordered[q.current()]) numDisordered++;
            }
            if(numMaskShell > 0)
                disorderFractionArray[vs] = (FloatType)numDisordered / numMaskShell;
        }

        if(numNeighbors == 0) {
            // No atom in the local shell: compatible with a free surface, void boundary, or large low-density region.
            defectTypeArray[vs] = DefectSurfaceOrVoid;
            confidenceArray[vs] = (FloatType)0.1;
            surfaceExcluded++;
            continue;
        }

        // Cavity radius = mean distance to the 6 nearest current atoms. The nearest-atom distance
        // alone cannot reject anything: an atom assigned to another WS cell is never closer to the
        // empty site than half the site spacing, so that test passes trivially at ratios <= 0.5.
        // The 6-atom mean exposes the displaced atom AND the inward collapse of the first shell
        // around a deformation artifact. On the synthetic noise/strain/cluster/cascade sets (held-out
        // seed) a ratio of 0.85 removed 55% of the false candidates surviving Frenkel recombination
        // and lost 2 of 916 vacancies; 0.80 removed 28% and lost none.
        double knnSum = 0;
        int knnCount = 0;
        for(size_t c = 0; c < 6; c++) {
            const FloatType d = knnDistancesArray.get(vs, c);
            if(d >= 0) { knnSum += d; knnCount++; }
        }
        if(knnCount > 0)
            cavityRadiusArray[vs] = (FloatType)(knnSum / knnCount);

        // Nearest-atom test: a mean over 6 atoms can still pass when one atom sits halfway into the
        // site, the signature of a reference site that does not match the current lattice (phase
        // boundaries, transformed regions). The closest atom is compared with the closest-pair distance
        // of the crystal itself, so both carry the same thermal-noise bias. On the HCP-BCC calibration
        // (300 K, 240 inserted vacancies) 0.75 kept 218 inserted vacancies and cut the background of
        // ~830 unconfirmed candidates to 33; those candidates have d1 ~ 0.65 and 90% of them vanish
        // within 1 ps. On the synthetic sets it lost 1 of 1065 strain/noise/cluster vacancies.
        FloatType closestAtom = std::numeric_limits<FloatType>::max();
        for(size_t c = 0; c < 6; c++) {
            const FloatType d = knnDistancesArray.get(vs, c);
            if(d >= 0) closestAtom = std::min(closestAtom, d);
        }
        const bool cavityPass = !_requireFreeVolume || (cavityRadiusArray[vs] >= _cavityRadiusRatio * r_nn_cur
                && closestAtom >= _nearestAtomRatio * r_closest_cur);
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
                // A restored Frenkel vacancy keeps its partner's surplus consumed: a vacancy-interstitial
                // pair within the recombination cutoff is unstable, so if the vacancy is genuine the
                // adjacent over-occupation is a displacement artifact, not an interstitial. Counting it
                // again was tested and raised the interstitial error on every synthetic case.
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

    // --- Identity ledger: an interstitial survives only if its chain starts at a confirmed vacancy. ---
    // This replaces the geometric surplus bookkeeping, which cancelled only the interstitials adjacent to
    // a recombined vacancy and kept those of every vacancy rejected by the cavity or the mask. Surplus no
    // chain reached belongs to atoms absent from the reference (inserted) and is always kept.
    size_t correctedInterstitialCount = rawInterstitialCount - frenkelRecombined;
    if(identityLedger) {
        // Where chains cross, which vacancy inherits a missing atom is decided by the traversal order,
        // not by physics: only the number of missing atoms is fixed. A missing atom is a genuine vacancy,
        // so it is handed to a confirmed vacancy, and the rejected one takes over that vacancy's surplus
        // end (the nearest one), which it then cancels. Without this swap a rejected true vacancy left an
        // extra interstitial behind (synthetic cascade/cluster cases: +4 interstitials on average).
        std::vector<size_t> acceptedWithSurplus;
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            if(chainEnd[vi] >= 0 && defectTypeArray[vacantSites[vi]] == DefectVacancy)
                acceptedWithSurplus.push_back(vi);
        }
        for(size_t vi = 0; vi < vacantSites.size() && !acceptedWithSurplus.empty(); vi++) {
            if(chainEnd[vi] != ChainMissingAtom || defectTypeArray[vacantSites[vi]] == DefectVacancy)
                continue;
            const Point3& rejectedPos = refPosArray[vacantSites[vi]];
            auto nearest = std::min_element(acceptedWithSurplus.begin(), acceptedWithSurplus.end(), [&](size_t a, size_t b) {
                return refCell().wrapVector(refPosArray[chainEnd[a]] - rejectedPos).squaredLength()
                     < refCell().wrapVector(refPosArray[chainEnd[b]] - rejectedPos).squaredLength();
            });
            const size_t ai = *nearest;
            std::swap(chainEnd[vi], chainEnd[ai]);
            *nearest = acceptedWithSurplus.back();
            acceptedWithSurplus.pop_back();
            for(size_t k : {vi, ai}) {
                const size_t vs = vacantSites[k];
                chainPartnerArray[vs] = chainEnd[k];
                chainDistanceArray[vs] = (chainEnd[k] >= 0) ? refCell().wrapVector(refPosArray[chainEnd[k]] - refPosArray[vs]).length() : (FloatType)-1;
            }
        }

        std::vector<int> ledgerSurplus = std::move(chainSurplusLeft);
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            if(chainEnd[vi] >= 0 && defectTypeArray[vacantSites[vi]] == DefectVacancy)
                ledgerSurplus[chainEnd[vi]]++;
        }
        correctedInterstitialCount = 0;
        for(size_t s = 0; s < numSites; s++) {
            if(rawOccupancyArray[s] < 2) continue;
            correctedSurplusArray[s] = ledgerSurplus[s];
            defectTypeArray[s] = (ledgerSurplus[s] > 0) ? DefectInterstitial : DefectOK;
            correctedInterstitialCount += ledgerSurplus[s];
        }
    }

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
    outParticles->createProperty(cavityRadius);
    outParticles->createProperty(disorderFraction);
    outParticles->createProperty(frenkelPartner);
    outParticles->createProperty(chainPartner);
    outParticles->createProperty(chainDistance);
    outParticles->createProperty(knnDistances);
    outParticles->createProperty(shellCounts);
    outParticles->createProperty(nearestVacantDistance);
    outParticles->createProperty(vacantSitesInShell);
    outParticles->createProperty(nearestInterstitialDistance);
    outParticles->createProperty(nearestInterstitialSurplus);
    outParticles->createProperty(interstitialSitesInShell);
    outParticles->createProperty(mlProbability);
    outParticles->createProperty(localShearStrain);
    outParticles->createProperty(localVolumetricStrain);
    outParticles->createProperty(localRmsd);
    outParticles->createProperty(localAtomicVolume);
    outParticles->createProperty(localContextCount);

    // --- Site-by-site agreement with plain Wigner-Seitz. ---
    // Every vacancy/interstitial of this modifier is also a Wigner-Seitz defect, so each WS defect
    // site is either confirmed ("both") or rejected by the filters ("WS only").
    PropertyPtr comparison = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Wigner-Seitz Comparison"));
    static const Color comparisonColors[] = {
        Color(0.70, 0.70, 0.70),    // no defect
        Color(0.90, 0.10, 0.10),    // vacancy, both
        Color(1.00, 0.75, 0.55),    // vacancy, WS only
        Color(0.10, 0.30, 0.95),    // interstitial, both
        Color(0.60, 0.85, 1.00)     // interstitial, WS only
    };
    {
        const QString names[] = { tr("No defect"), tr("Vacancy (both)"), tr("Vacancy (WS only)"),
                                  tr("Interstitial (both)"), tr("Interstitial (WS only)") };
        for(int id = ComparisonNoDefect; id <= ComparisonInterstitialWSOnly; id++) {
            DataOORef<ElementType> type = DataOORef<ElementType>::create();
            type->setNumericId(id);
            type->setName(names[id]);
            type->setColor(comparisonColors[id]);
            comparison->addElementType(std::move(type));
        }
    }
    size_t wsOnlyVacancies = 0;
    {
        BufferWriteAccess<int32_t, access_mode::discard_write> comparisonArray(comparison);
        for(size_t s = 0; s < numSites; s++) {
            const int occ = rawOccupancyArray[s];
            int c = ComparisonNoDefect;
            if(occ == 0) {
                c = (defectTypeArray[s] == DefectVacancy) ? ComparisonVacancyBoth : ComparisonVacancyWSOnly;
                if(c == ComparisonVacancyWSOnly) wsOnlyVacancies++;
            }
            else if(occ >= 2) {
                c = (correctedSurplusArray[s] > 0) ? ComparisonInterstitialBoth : ComparisonInterstitialWSOnly;
            }
            comparisonArray[s] = c;
        }
    }
    outParticles->createProperty(comparison);

    // --- Bar charts for the modifier panel and the data inspector. ---
    // A bar chart is one bar per row, colored by the element type of its x column.
    auto createBarChart = [&](const QString& id, const QString& title, const QString& xName,
                              const std::vector<std::tuple<QString, Color, int64_t>>& bars) {
        const size_t n = bars.size();
        PropertyPtr countColumn = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, n, Property::Int64, 1, QStringLiteral("Count"));
        PropertyPtr categoryColumn = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, n, Property::Int32, 1, xName);
        {
            BufferWriteAccess<int64_t, access_mode::discard_write> countArray(countColumn);
            BufferWriteAccess<int32_t, access_mode::discard_write> categoryArray(categoryColumn);
            for(size_t i = 0; i < n; i++) {
                categoryArray[i] = (int32_t)i;
                countArray[i] = std::get<2>(bars[i]);
                DataOORef<ElementType> type = DataOORef<ElementType>::create();
                type->setNumericId((int)i);
                type->setName(std::get<0>(bars[i]));
                type->setColor(std::get<1>(bars[i]));
                categoryColumn->addElementType(std::move(type));
            }
        }
        state.createObject<DataTable>(id, _createdByNode, DataTable::BarChart, title, std::move(countColumn), std::move(categoryColumn));
    };

    createBarChart(QStringLiteral("vacancy-ws-comparison"), tr("Wigner-Seitz vs. consensus"), QStringLiteral("Defect"), {
        {tr("WS vacancies"),        comparisonColors[ComparisonVacancyWSOnly],      (int64_t)rawVacancyCount},
        {tr("Vacancies"),           comparisonColors[ComparisonVacancyBoth],        (int64_t)classifiedVacancyCount},
        {tr("WS interstitials"),    comparisonColors[ComparisonInterstitialWSOnly], (int64_t)rawInterstitialCount},
        {tr("Interstitials"),       comparisonColors[ComparisonInterstitialBoth],   (int64_t)correctedInterstitialCount}
    });

    // What happened to each Wigner-Seitz vacancy: kept, or the stage that rejected it.
    {
        std::array<int64_t, DefectMLRejected + 1> outcomeCounts{};
        for(size_t s = 0; s < numSites; s++) {
            if(rawOccupancyArray[s] == 0 && defectTypeArray[s] >= 0 && defectTypeArray[s] <= DefectMLRejected)
                outcomeCounts[defectTypeArray[s]]++;
        }
        std::vector<std::tuple<QString, Color, int64_t>> bars;
        for(int t : {(int)DefectVacancy, (int)DefectFrenkel, (int)DefectNoCavity, (int)DefectStructural, (int)DefectSurfaceOrVoid, (int)DefectMLRejected}) {
            if(t == DefectMLRejected && !_mlModel)
                continue;
            const ElementType* type = defectTypes->elementType(t);
            bars.emplace_back(type->name(), type->color(), outcomeCounts[t]);
        }
        createBarChart(QStringLiteral("vacancy-ws-outcome"), tr("Fate of the Wigner-Seitz vacancies"), QStringLiteral("Outcome"), bars);
    }

    // Optionally keep only the defect sites and color them by comparison class, so the two
    // results can be told apart in the viewport without further modifiers.
    if(_outputSites != AllSites) {
        PropertyPtr deletionMask = Particles::OOClass().createStandardProperty(DataBuffer::Uninitialized, numSites, Particles::SelectionProperty);
        size_t numDeleted = 0;
        {
            BufferReadAccess<int32_t> comparisonArray(comparison);
            BufferWriteAccess<SelectionIntType, access_mode::discard_write> maskArray(deletionMask);
            for(size_t s = 0; s < numSites; s++) {
                const int c = comparisonArray[s];
                const bool keep = (_outputSites == WignerSeitzDefectSites)
                    ? (c != ComparisonNoDefect)
                    : (c == ComparisonVacancyBoth || c == ComparisonInterstitialBoth);
                maskArray[s] = keep ? 0 : 1;
                if(!keep) numDeleted++;
            }
        }
        outParticles->deleteElements(std::move(deletionMask), numDeleted);
        BufferReadAccess<int32_t> comparisonArray(outParticles->getProperty(QStringLiteral("Wigner-Seitz Comparison")));
        BufferWriteAccess<ColorG, access_mode::discard_write> colorArray = outParticles->createProperty(Particles::ColorProperty);
        for(size_t i = 0; i < comparisonArray.size(); i++)
            colorArray[i] = comparisonColors[comparisonArray[i]].toDataType<GraphicsFloatType>();
    }

    // --- Global attributes: corrected counts plus the raw Wigner-Seitz counts for comparison. ---
    state.addAttribute(QStringLiteral("VacancyAnalysis.vacancy_count"), QVariant::fromValue(classifiedVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.interstitial_count"), QVariant::fromValue(correctedInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_vacancies"), QVariant::fromValue(rawVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_interstitials"), QVariant::fromValue(rawInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.frenkel_recombined"), QVariant::fromValue(frenkelRecombined), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.freevolume_excluded"), QVariant::fromValue(freeVolumeExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.structural_excluded"), QVariant::fromValue(structuralExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.surface_or_void_excluded"), QVariant::fromValue(surfaceExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.ws_only_vacancies"), QVariant::fromValue(wsOnlyVacancies), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.ws_only_interstitials"), QVariant::fromValue(rawInterstitialCount > correctedInterstitialCount ? rawInterstitialCount - correctedInterstitialCount : (size_t)0), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.interstitial_pairing"), QVariant::fromValue(identityLedger ? QStringLiteral("identity") : QStringLiteral("geometric")), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.missing_atom_vacancies"), QVariant::fromValue(missingAtomVacancies), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.estimated_nn_distance"), QVariant::fromValue(r_nn), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.estimated_nn_distance_current"), QVariant::fromValue(r_nn_cur), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.closest_pair_distance_current"), QVariant::fromValue(r_closest_cur), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_used"), QVariant::fromValue(haveDisorderSignal), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_name"), QVariant::fromValue(selectedMaskSignalName), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_signal_cutoff"), QVariant::fromValue(haveMaskSignalCutoff ? maskSignalCutoff : (FloatType)-1), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.mask_global_disordered_fraction"), QVariant::fromValue(globalDisorderedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.local_signals"), QVariant::fromValue(availableLocalSignals.join(QChar(','))), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.ml_used"), QVariant::fromValue(_mlModel != nullptr), _createdByNode);
    if(_mlModel) {
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_threshold"), QVariant::fromValue(_mlThreshold), _createdByNode);
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_rejected"), QVariant::fromValue(mlRejectedCount), _createdByNode);
        state.addAttribute(QStringLiteral("VacancyAnalysis.ml_resurrected"), QVariant::fromValue(mlResurrectedCount), _createdByNode);
    }

    PipelineStatus status(PipelineStatus::Success,
        tr("Vacancies: %1 (Wigner-Seitz: %3)\nInterstitials: %2 (Wigner-Seitz: %8)\nRemoved vacancies: %4 Frenkel pairs, %5 without free volume, %6 in disordered regions, %7 near surfaces/voids.")
            .arg(classifiedVacancyCount).arg(correctedInterstitialCount).arg(rawVacancyCount)
            .arg(frenkelRecombined).arg(freeVolumeExcluded).arg(structuralExcluded).arg(surfaceExcluded)
            .arg(rawInterstitialCount));
    if(identityLedger)
        status.setText(status.text() + tr("\nInterstitials balanced by atom identity: %1 vacant sites lost their atom, the rest were displaced along replacement chains.").arg(missingAtomVacancies));
    if(_mlModel)
        status.setText(status.text() + tr("\nML classifier verdict (threshold %1): rejected %2 rule-passed candidates, restored %3 rule-rejected ones.")
            .arg(_mlThreshold).arg(mlRejectedCount).arg(mlResurrectedCount));
    if(_maskDisordered && !haveDisorderSignal) {
        status.setType(PipelineStatus::Warning);
        status.setText(status.text() + tr("\nStructural masking was requested, but the selected upstream signal '%1' is not available. No sites were excluded by the structural mask.").arg(selectedMaskSignalName));
    }
    else if(haveDisorderSignal && globalDisorderedFraction > _maskThreshold) {
        status.setType(PipelineStatus::Warning);
        status.setText(status.text() + tr("\n%1% of all atoms are flagged as disordered by '%2', more than the mask threshold. The structural mask cannot discriminate in this sample (for PTM, consider raising its RMSD cutoff).")
            .arg(globalDisorderedFraction * 100, 0, 'f', 1).arg(selectedMaskSignalName));
    }
    state.setStatus(std::move(status));
}

}   // End of namespace
