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
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include "VacancyAnalysisModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(VacancyAnalysisModifier);
OVITO_CLASSINFO(VacancyAnalysisModifier, "DisplayName", "Vacancy analysis (consensus)");
OVITO_CLASSINFO(VacancyAnalysisModifier, "Description", "Robust vacancy/interstitial detection that filters out Wigner-Seitz artifacts (Frenkel pairs, deformation, grain boundaries).");
OVITO_CLASSINFO(VacancyAnalysisModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, frenkelRecombination);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, recombinationCutoff);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, requireFreeVolume);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, cavityRadiusRatio);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskDisordered);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskSignal);
DEFINE_PROPERTY_FIELD(VacancyAnalysisModifier, maskThreshold);
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, frenkelRecombination, "Recombine Frenkel pairs");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, recombinationCutoff, "Recombination cutoff");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, requireFreeVolume, "Require free volume");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, cavityRadiusRatio, "Min. cavity radius (x nn dist.)");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskDisordered, "Mask disordered regions");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskSignal, "Disorder signal");
SET_PROPERTY_FIELD_LABEL(VacancyAnalysisModifier, maskThreshold, "Disorder fraction threshold");

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

    return std::make_unique<VacancyAnalysisEngine>(posProperty, inputCell,
            referenceState, refPosProperty, refCell, affineMapping(),
            frenkelRecombination(), recombinationCutoff(),
            requireFreeVolume(), cavityRadiusRatio(),
            maskDisordered(), maskSignal(), maskThreshold(),
            structureProperty, cspProperty, shearProperty,
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

    // Estimate the reference nearest-neighbor distance from a sample of sites.
    FloatType r_nn = 0;
    if(numSites > 1) {
        BufferReadAccess<Point3> refPosArray(refPositions());
        const size_t nSample = std::min(numSites, (size_t)50);
        FloatType minNNSq = FLOATTYPE_MAX;
        for(size_t s = 0; s < nSample; s++) {
            FloatType d;
            siteFinder.findClosestParticle(refPosArray[s], d, /*includeSelf=*/false);
            if(d > 0 && d < minNNSq) minNNSq = d;
        }
        r_nn = (minNNSq < FLOATTYPE_MAX) ? std::sqrt(minNNSq) : FloatType(0);
    }
    if(r_nn <= 0)
        throw Exception(tr("Could not determine the reference nearest-neighbor distance."));

    // Estimate the current-frame nearest-neighbor distance by scaling the reference value with the
    // cubic root of the cell volume ratio. This keeps the free-volume (cavity) test consistent with
    // the possibly compressed/expanded current configuration, where the actual spacing differs from r_nn.
    FloatType r_nn_cur = r_nn;
    {
        const FloatType vRef = refCell().volume3D();
        const FloatType vCur = cell().volume3D();
        if(vRef > 0 && vCur > 0)
            r_nn_cur = r_nn * std::cbrt(vCur / vRef);
    }

    // --- Stage 2: Frenkel-pair recombination. ---
    // A candidate vacancy adjacent to an over-occupied site is the signature of a displaced atom,
    // not a missing one. Match such pairs (greedy nearest, in the reference frame) and annihilate them.
    std::vector<char> matchedVacant(vacantSites.size(), 0);
    size_t frenkelRecombined = 0;
    if(_frenkelRecombination && !vacantSites.empty() && rawInterstitialCount > 0) {
        const FloatType cutoff = (_recombinationCutoff > 0) ? _recombinationCutoff : (FloatType)1.2 * r_nn;
        CutoffNeighborFinder finder(cutoff, refPositions(), refCell(), {});
        BufferReadAccess<Point3> refPosArray(refPositions());

        struct Pair { FloatType dist; size_t vacantListIndex; size_t interstitialSite; };
        std::vector<Pair> candidatePairs;
        for(size_t vi = 0; vi < vacantSites.size(); vi++) {
            const size_t vs = vacantSites[vi];
            for(CutoffNeighborFinder::Query q(finder, refPosArray[vs]); !q.atEnd(); q.next()) {
                if(interstitialSurplus[q.current()] > 0)
                    candidatePairs.push_back({q.distance(), vi, q.current()});
            }
        }
        std::sort(candidatePairs.begin(), candidatePairs.end(),
            [](const Pair& a, const Pair& b) { return a.dist < b.dist; });
        for(const Pair& pr : candidatePairs) {
            if(matchedVacant[pr.vacantListIndex]) continue;
            if(interstitialSurplus[pr.interstitialSite] <= 0) continue;
            matchedVacant[pr.vacantListIndex] = 1;
            interstitialSurplus[pr.interstitialSite]--;
            frenkelRecombined++;
        }
    }

    // --- Precompute the per-atom disorder flag for structural masking (Stage 4). ---
    std::vector<char> disordered;
    bool haveDisorderSignal = false;
    if(_maskDisordered) {
        if(_maskSignal == StructureSignal && _structureProperty) {
            BufferReadAccess<int32_t> structureArray(_structureProperty);
            disordered.resize(numAtoms);
            for(size_t i = 0; i < numAtoms; i++)
                disordered[i] = (structureArray[i] == ParticleType::OTHER) ? 1 : 0;
            haveDisorderSignal = true;
        }
        else if((_maskSignal == CentrosymmetrySignal && _cspProperty) ||
                (_maskSignal == ShearStrainSignal && _shearProperty)) {
            BufferReadAccess<FloatType> signalArray((_maskSignal == CentrosymmetrySignal) ? _cspProperty : _shearProperty);
            // Automatic cutoff = mean + 1 standard deviation of the signal over all atoms.
            double sum = 0, sumSq = 0;
            for(size_t i = 0; i < numAtoms; i++) { double v = signalArray[i]; sum += v; sumSq += v * v; }
            const double mean = (numAtoms > 0) ? sum / numAtoms : 0.0;
            const double var = std::max(0.0, (numAtoms > 0 ? sumSq / numAtoms : 0.0) - mean * mean);
            const FloatType cutoff = (FloatType)(mean + std::sqrt(var));
            disordered.resize(numAtoms);
            for(size_t i = 0; i < numAtoms; i++)
                disordered[i] = (signalArray[i] > cutoff) ? 1 : 0;
            haveDisorderSignal = true;
        }
    }

    // Neighbor finder over the current configuration, used for cavity (free-volume) and masking.
    // Sized with the current-frame nn distance so the first shell is captured under deformation.
    const FloatType searchRadius = (FloatType)1.5 * r_nn_cur;
    CutoffNeighborFinder currentFinder(searchRadius, positions(), cell(), {});

    // --- Output: per-reference-site classification. ---
    PropertyPtr defectTypes = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::Int32, 1, QStringLiteral("Defect Type"));
    PropertyPtr vacancyConfidence = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, numSites, Property::FloatDefault, 1, QStringLiteral("Vacancy Confidence"));
    BufferWriteAccess<int32_t, access_mode::discard_write> defectTypeArray(defectTypes);
    BufferWriteAccess<FloatType, access_mode::discard_write> confidenceArray(vacancyConfidence);
    for(size_t s = 0; s < numSites; s++) {
        int o = occupancy[s].load(std::memory_order_relaxed);
        defectTypeArray[s] = (o >= 2) ? DefectInterstitial : DefectOK;
        confidenceArray[s] = 0;
    }

    // --- Stages 3 + 4: free-volume confirmation and structural masking of the surviving vacancies. ---
    BufferReadAccess<Point3> refPosArray(refPositions());
    size_t freeVolumeExcluded = 0, structuralExcluded = 0, surfaceExcluded = 0, trueVacancyCount = 0;
    for(size_t vi = 0; vi < vacantSites.size(); vi++) {
        const size_t vs = vacantSites[vi];
        if(matchedVacant[vi]) {
            defectTypeArray[vs] = DefectFrenkel;
            confidenceArray[vs] = 0;
            continue;
        }

        // Location of the empty site in the current frame.
        Point3 queryPoint = refPosArray[vs];
        if(affineMapping() == TO_REFERENCE_CELL) queryPoint = tmInv * queryPoint;

        FloatType minNeighborDist = searchRadius;  // also the lower bound used when no neighbor is found
        int numNeighbors = 0, numDisordered = 0;
        for(CutoffNeighborFinder::Query q(currentFinder, queryPoint); !q.atEnd(); q.next()) {
            const FloatType d = q.distance();
            if(d < minNeighborDist) minNeighborDist = d;
            numNeighbors++;
            if(haveDisorderSignal && disordered[q.current()]) numDisordered++;
        }

        if(numNeighbors == 0) {
            // No atom anywhere near the site: it lies outside the material (free surface / large void boundary).
            defectTypeArray[vs] = DefectSurface;
            confidenceArray[vs] = (FloatType)0.1;
            surfaceExcluded++;
            continue;
        }

        const bool cavityPass = !_requireFreeVolume || (minNeighborDist >= _cavityRadiusRatio * r_nn_cur);
        const bool ordered = !(haveDisorderSignal) || ((FloatType)numDisordered / numNeighbors < _maskThreshold);

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
            trueVacancyCount++;
        }
    }

    const size_t trueInterstitialCount = rawInterstitialCount - frenkelRecombined;

    // --- Replace the current configuration with the reference configuration in the output. ---
    const Particles* refParticles = referenceState().getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("This modifier cannot be evaluated, because the reference configuration does not contain any particles."));
    state.replaceObject(state.expectObject<Particles>(), refParticles);
    if(const SimulationCell* cell = state.getObject<SimulationCell>())
        state.replaceObject(cell, referenceState().getObject<SimulationCell>());

    Particles* outParticles = state.expectMutableObject<Particles>();
    if(defectTypes->size() != outParticles->elementCount())
        throw Exception(tr("Cached modifier results are obsolete, because the number of reference sites has changed."));
    outParticles->createProperty(defectTypes);
    outParticles->createProperty(vacancyConfidence);

    // --- Global attributes: corrected counts plus the raw Wigner-Seitz counts for comparison. ---
    state.addAttribute(QStringLiteral("VacancyAnalysis.vacancy_count"), QVariant::fromValue(trueVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.interstitial_count"), QVariant::fromValue(trueInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_vacancies"), QVariant::fromValue(rawVacancyCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.raw_ws_interstitials"), QVariant::fromValue(rawInterstitialCount), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.frenkel_recombined"), QVariant::fromValue(frenkelRecombined), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.freevolume_excluded"), QVariant::fromValue(freeVolumeExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.structural_excluded"), QVariant::fromValue(structuralExcluded), _createdByNode);
    state.addAttribute(QStringLiteral("VacancyAnalysis.surface_excluded"), QVariant::fromValue(surfaceExcluded), _createdByNode);

    state.setStatus(PipelineStatus(PipelineStatus::Success,
        tr("Found %1 true vacancies and %2 interstitials.\nWigner-Seitz raw: %3 vacancies. Removed: %4 Frenkel pairs, %5 without free volume, %6 in disordered regions, %7 at surfaces.")
            .arg(trueVacancyCount).arg(trueInterstitialCount).arg(rawVacancyCount)
            .arg(frenkelRecombined).arg(freeVolumeExcluded).arg(structuralExcluded).arg(surfaceExcluded)));
}

}   // End of namespace
