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

#include <ovito/particles/Particles.h>
#include <ovito/particles/util/NearestNeighborFinder.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include "WignerSeitzAnalysisModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(WignerSeitzAnalysisModifier);
OVITO_CLASSINFO(WignerSeitzAnalysisModifier, "DisplayName", "Wigner-Seitz defect analysis");
OVITO_CLASSINFO(WignerSeitzAnalysisModifier, "Description", "Identify point defects (vacancies and interstitials) in crystals.");
OVITO_CLASSINFO(WignerSeitzAnalysisModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(WignerSeitzAnalysisModifier, perTypeOccupancy);
DEFINE_PROPERTY_FIELD(WignerSeitzAnalysisModifier, outputCurrentConfig);
DEFINE_PROPERTY_FIELD(WignerSeitzAnalysisModifier, adaptiveCell);
DEFINE_PROPERTY_FIELD(WignerSeitzAnalysisModifier, strainSensitivity);
SET_PROPERTY_FIELD_LABEL(WignerSeitzAnalysisModifier, perTypeOccupancy, "Compute per-type occupancies");
SET_PROPERTY_FIELD_LABEL(WignerSeitzAnalysisModifier, outputCurrentConfig, "Output current configuration");
SET_PROPERTY_FIELD_LABEL(WignerSeitzAnalysisModifier, adaptiveCell, "Adaptive Wigner-Seitz cell");
SET_PROPERTY_FIELD_LABEL(WignerSeitzAnalysisModifier, strainSensitivity, "Strain sensitivity");

/******************************************************************************
* Adopts existing computation results for an interactive pipeline evaluation.
******************************************************************************/
Future<PipelineFlowState> WignerSeitzAnalysisModifier::reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState)
{
    // Adopt the WS analysis results from the cached state.
    if(const Particles* cachedParticles = cachedState.getObject<Particles>()) {
        if(outputCurrentConfig()) {
            particles->tryToAdoptProperties(cachedParticles, {
                cachedParticles->getProperty(QStringLiteral("Occupancy")),
                cachedParticles->getProperty(QStringLiteral("Site Identifier")),
                cachedParticles->getProperty(QStringLiteral("Site Type")),
                cachedParticles->getProperty(QStringLiteral("Site Index"))
            }, {particles});
        }
        else {
            // Replace complete particles set with the reference configuration.
            output.replaceObject(particles, cachedParticles);
            // Also replace simulation cell with reference cell.
            if(const SimulationCell* cell = output.getObject<SimulationCell>()) {
                if(const SimulationCell* cachedCell = cachedState.getObject<SimulationCell>())
                    output.replaceObject(cell, cachedCell);
            }
        }
    }

    // Adopt all global attributes computed by the modifier from the cached state.
    output.adoptAttributesFrom(cachedState, request.modificationNode());

    return std::move(output);
}

/******************************************************************************
* Creates and initializes a computation engine that will compute the modifier's results.
******************************************************************************/
std::unique_ptr<ReferenceConfigurationModifier::Engine> WignerSeitzAnalysisModifier::createEngine(const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState)
{
    // Get the current particle positions.
    const Particles* particles = input.expectObject<Particles>();
    particles->verifyIntegrity();
    const Property* posProperty = particles->expectProperty(Particles::PositionProperty);

    // Get the reference particle position.
    const Particles* refParticles = referenceState.getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("Reference configuration does not contain any particles."));
    refParticles->verifyIntegrity();
    const Property* refPosProperty = refParticles->expectProperty(Particles::PositionProperty);

    // Get simulation cells.
    const SimulationCell* inputCell = input.getObject<SimulationCell>();
    const SimulationCell* refCell = referenceState.getObject<SimulationCell>();
    if(refCell && !inputCell)
        throw Exception(tr("Input configuration does not have a simulation cell."));
    if(inputCell && !refCell)
        throw Exception(tr("Reference configuration does not have a simulation cell."));

    // Validate simulation cells.
    if(inputCell && inputCell->is2D())
        throw Exception(tr("Wigner-Seitz analysis is not supported for 2D systems."));
    if(inputCell && inputCell->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate in the current configuration."));
    if(refCell && refCell->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate in the reference configuration."));

    // Get the particle types of the current configuration.
    const Property* typeProperty = nullptr;
    int ptypeMinId = std::numeric_limits<int>::max();
    int ptypeMaxId = std::numeric_limits<int>::lowest();
    if(perTypeOccupancy()) {
        typeProperty = particles->expectProperty(Particles::TypeProperty);
        // Determine value range of particle type IDs.
        for(const ElementType* pt : typeProperty->elementTypes()) {
            if(pt->numericId() < ptypeMinId) ptypeMinId = pt->numericId();
            if(pt->numericId() > ptypeMaxId) ptypeMaxId = pt->numericId();
        }
    }

    // If output of the displaced configuration is requested, obtain types of the reference sites.
    const Property* referenceTypeProperty = nullptr;
    const Property* referenceIdentifierProperty = nullptr;
    if(outputCurrentConfig()) {
        referenceTypeProperty = refParticles->getProperty(Particles::TypeProperty);
        referenceIdentifierProperty = refParticles->getProperty(Particles::IdentifierProperty);
    }

    // Create compute engine instance. Pass all relevant modifier parameters and the input data to the engine.
    auto engine = std::make_unique<WignerSeitzAnalysisEngine>(posProperty, inputCell,
            referenceState,
            refPosProperty, refCell, affineMapping(), typeProperty, ptypeMinId, ptypeMaxId,
            referenceTypeProperty, referenceIdentifierProperty,
            adaptiveCell(), strainSensitivity(),
            request.modificationNode());

    // Create output properties:
    if(outputCurrentConfig()) {
        if(referenceIdentifierProperty)
            engine->setSiteIdentifiers(Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, posProperty->size(), Property::IntIdentifier, 1, QStringLiteral("Site Identifier")));
        engine->setSiteTypes(Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, posProperty->size(), Property::Int32, 1, QStringLiteral("Site Type")));
        engine->setSiteIndices(Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, posProperty->size(), Property::Int64, 1, QStringLiteral("Site Index")));
    }

    return engine;
}

/******************************************************************************
* Performs the actual computation. This method is executed in a worker thread.
******************************************************************************/
void WignerSeitzAnalysisModifier::WignerSeitzAnalysisEngine::perform(PipelineFlowState& state)
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Performing Wigner-Seitz cell analysis"));

    if(affineMapping() == TO_CURRENT_CELL)
        throw Exception(tr("Remapping coordinates to the current cell is not supported by the Wigner-Seitz analysis routine. Only remapping to the reference cell or no mapping at all are supported options."));

    if(refPositions()->size() == 0)
        throw Exception(tr("Reference configuration for Wigner-Seitz analysis contains no atomic sites."));

    // Prepare the closest-point query structure.
    NearestNeighborFinder neighborTree(0, refPositions(), refCell(), {});

    // Determine the number of components of the occupancy property.
    int ncomponents = 1;
    int typemin, typemax;
    if(particleTypes()) {
        BufferReadAccess<int32_t> particleTypesArray(particleTypes());
        auto minmax = std::minmax_element(particleTypesArray.cbegin(), particleTypesArray.cend());
        typemin = std::min(_ptypeMinId, *minmax.first);
        typemax = std::max(_ptypeMaxId, *minmax.second);
        if(typemin < 0)
            throw Exception(tr("Negative particle type IDs are not supported by this modifier."));
        if(typemax > 32)
            throw Exception(tr("Number of particle types is too large for this modifier. Cannot compute occupancy numbers for more than 32 particle types."));
        ncomponents = typemax - typemin + 1;
    }

    AffineTransformation tm;
    if(affineMapping() == TO_REFERENCE_CELL)
        tm = refCell().cellMatrix() * cell().reciprocalCellMatrix();

    // Create array for atomic counting.
    size_t arraySize = refPositions()->size() * ncomponents;
    std::vector<std::atomic_int> occupancyArray(arraySize);
    for(auto& o : occupancyArray)
        o.store(0, std::memory_order_relaxed);

    // Allocate atoms -> sites lookup map if needed for output mode or adaptive cell.
    const bool needAtomSiteMapping = (siteTypes() != nullptr) || _adaptiveCell;
    std::vector<size_t> atomsToSites;
    if(needAtomSiteMapping) {
        atomsToSites.resize(positions()->size());
    }

    // For the adaptive cell algorithm: store per-atom displacement-squared from pass 1.
    std::vector<FloatType> atomDispSq;
    if(_adaptiveCell) {
        atomDispSq.resize(positions()->size(), 0.0);
    }

    // Pass 1: Assign particles to reference sites using standard nearest-neighbor (Wigner-Seitz) rule.
    BufferReadAccess<Point3> positionsArray(positions());
    if(ncomponents == 1) {
        // Without per-type occupancies:
        parallelFor(positions()->size(), 1024, progress, [&](size_t index) {
            const Point3& p = positionsArray[index];
            FloatType closestDistanceSq;
            size_t closestIndex = neighborTree.findClosestParticle((affineMapping() == TO_REFERENCE_CELL) ? (tm * p) : p, closestDistanceSq);
            OVITO_ASSERT(closestIndex < occupancyArray.size());
            occupancyArray[closestIndex].fetch_add(1, std::memory_order_relaxed);
            if(!atomsToSites.empty())
                atomsToSites[index] = closestIndex;
            if(!atomDispSq.empty())
                atomDispSq[index] = closestDistanceSq;
        });
    }
    else {
        // With per-type occupancies:
        BufferReadAccess<int32_t> particleTypesArray(particleTypes());
        parallelFor(positions()->size(), 1024, progress, [&](size_t index) {
            const Point3& p = positionsArray[index];
            FloatType closestDistanceSq;
            size_t closestIndex = neighborTree.findClosestParticle((affineMapping() == TO_REFERENCE_CELL) ? (tm * p) : p, closestDistanceSq);
            int offset = particleTypesArray[index] - typemin;
            OVITO_ASSERT(closestIndex * ncomponents + offset < occupancyArray.size());
            occupancyArray[closestIndex * ncomponents + offset].fetch_add(1, std::memory_order_relaxed);
            if(!atomsToSites.empty())
                atomsToSites[index] = closestIndex;
            if(!atomDispSq.empty())
                atomDispSq[index] = closestDistanceSq;
        });
    }

    // Pass 2 (adaptive cell): use a power diagram (weighted Voronoi) to refine assignments
    // based on local strain estimated from pass 1 displacements.
    if(_adaptiveCell && _strainSensitivity > 0) {
        // Aggregate per-atom displacement squared into per-site statistics.
        std::vector<double> siteDispSqSum(refPositions()->size(), 0.0);
        std::vector<int> siteAtomCount(refPositions()->size(), 0);
        for(size_t atomIdx = 0; atomIdx < positions()->size(); atomIdx++) {
            size_t siteIdx = atomsToSites[atomIdx];
            siteDispSqSum[siteIdx] += atomDispSq[atomIdx];
            siteAtomCount[siteIdx]++;
        }

        // Compute per-site power-diagram weight: w_j = strainSensitivity * avg_disp_sq_j.
        // Sites in high-strain regions (larger average displacement) get larger weights,
        // expanding their effective cell so they attract more atoms.
        std::vector<FloatType> siteWeight(refPositions()->size(), 0.0);
        double globalDispSqSum = 0.0;
        int nOccupied = 0;
        for(size_t s = 0; s < refPositions()->size(); s++) {
            if(siteAtomCount[s] > 0) {
                FloatType avgDispSq = (FloatType)(siteDispSqSum[s] / siteAtomCount[s]);
                siteWeight[s] = _strainSensitivity * avgDispSq;
                globalDispSqSum += avgDispSq;
                nOccupied++;
            }
        }
        // Unoccupied (vacant) sites receive the global average weight.
        FloatType globalAvgWeight = (nOccupied > 0) ?
            _strainSensitivity * (FloatType)(globalDispSqSum / nOccupied) : FloatType(0);
        for(size_t s = 0; s < refPositions()->size(); s++) {
            if(siteAtomCount[s] == 0)
                siteWeight[s] = globalAvgWeight;
        }

        // Maximum site weight — used to bound the power-diagram search radius.
        FloatType w_max = *std::max_element(siteWeight.begin(), siteWeight.end());

        // Reset occupancy array for the reassignment.
        for(auto& o : occupancyArray)
            o.store(0, std::memory_order_relaxed);

        // Power-diagram reassignment: each atom is assigned to the reference site j
        // minimizing (|p - r_j|^2 - w_j) instead of just |p - r_j|^2.
        if(ncomponents == 1) {
            parallelFor(positions()->size(), 1024, progress, [&](size_t atomIdx) {
                Point3 p = positionsArray[atomIdx];
                if(affineMapping() == TO_REFERENCE_CELL) p = tm * p;

                FloatType minPowerDist = FLOATTYPE_MAX;
                size_t bestSite = 0;

                // Custom visitor: tracks minimum power distance and prunes the tree search.
                // A branch at geometric distance^2 = d^2 can only improve the current best
                // if d^2 - w_max < minPowerDist, i.e. d^2 < minPowerDist + w_max.
                auto powerVisitor = [&](const NearestNeighborFinder::Neighbor& n, FloatType& mrs) {
                    FloatType powerDist = n.distanceSq - siteWeight[n.index];
                    if(powerDist < minPowerDist) {
                        minPowerDist = powerDist;
                        bestSite = n.index;
                        FloatType new_mrs = minPowerDist + w_max;
                        if(new_mrs < mrs) mrs = new_mrs;
                    }
                };
                neighborTree.visitNeighbors(p, powerVisitor);

                OVITO_ASSERT(bestSite < occupancyArray.size());
                occupancyArray[bestSite].fetch_add(1, std::memory_order_relaxed);
                if(!atomsToSites.empty())
                    atomsToSites[atomIdx] = bestSite;
            });
        }
        else {
            // With per-type occupancies:
            BufferReadAccess<int32_t> particleTypesArray(particleTypes());
            parallelFor(positions()->size(), 1024, progress, [&](size_t atomIdx) {
                Point3 p = positionsArray[atomIdx];
                if(affineMapping() == TO_REFERENCE_CELL) p = tm * p;

                FloatType minPowerDist = FLOATTYPE_MAX;
                size_t bestSite = 0;

                auto powerVisitor = [&](const NearestNeighborFinder::Neighbor& n, FloatType& mrs) {
                    FloatType powerDist = n.distanceSq - siteWeight[n.index];
                    if(powerDist < minPowerDist) {
                        minPowerDist = powerDist;
                        bestSite = n.index;
                        FloatType new_mrs = minPowerDist + w_max;
                        if(new_mrs < mrs) mrs = new_mrs;
                    }
                };
                neighborTree.visitNeighbors(p, powerVisitor);

                int offset = particleTypesArray[atomIdx] - typemin;
                OVITO_ASSERT(bestSite * ncomponents + offset < occupancyArray.size());
                occupancyArray[bestSite * ncomponents + offset].fetch_add(1, std::memory_order_relaxed);
                if(!atomsToSites.empty())
                    atomsToSites[atomIdx] = bestSite;
            });
        }
    }

    // Create output storage.
    setOccupancyNumbers(Particles::OOClass().createUserProperty(DataBuffer::Uninitialized,
        siteTypes() ? positions()->size() : refPositions()->size(),
        Property::Int32, ncomponents, QStringLiteral("Occupancy")));
    if(ncomponents > 1 && typemin != 1) {
        QStringList componentNames;
        for(int i = typemin; i <= typemax; i++)
            componentNames.push_back(QString::number(i));
        occupancyNumbers()->setComponentNames(componentNames);
    }

    // Copy data from atomic array to output buffer.
    BufferWriteAccess<int32_t*, access_mode::discard_write> occupancyNumbersArray(occupancyNumbers());
    if(!siteTypes()) {
        std::ranges::copy(occupancyArray, occupancyNumbersArray.begin());
    }
    else {
        // Map occupancy numbers from sites to atoms.
        BufferWriteAccess<int32_t, access_mode::discard_write> siteTypesArray(siteTypes());
        BufferWriteAccess<int64_t, access_mode::discard_write> siteIndicesArray(siteIndices());
        BufferWriteAccess<IdentifierIntType, access_mode::discard_write> siteIdentifiersArray(siteIdentifiers());
        BufferReadAccess<int32_t> referenceTypeArray(_referenceTypeProperty);
        BufferReadAccess<IdentifierIntType> referenceIdentifierArray(_referenceIdentifierProperty);
        int32_t* occ = occupancyNumbersArray.begin();
        int32_t* st = siteTypesArray.begin();
        auto sidx = siteIndicesArray.begin();
        auto sid = siteIdentifiersArray ? siteIdentifiersArray.begin() : nullptr;
        for(size_t siteIndex : atomsToSites) {
            for(int j = 0; j < ncomponents; j++) {
                *occ++ = occupancyArray[siteIndex * ncomponents + j];
            }
            *st++ = referenceTypeArray ? referenceTypeArray[siteIndex] : 0;
            *sidx++ = siteIndex;
            if(sid)
                *sid++ = referenceIdentifierArray[siteIndex];
        }
    }

    // Count defects.
    if(ncomponents == 1) {
        for(int32_t oc : occupancyArray) {
            if(oc == 0) incrementVacancyCount();
            else if(oc > 1) incrementInterstitialCount(oc - 1);
        }
    }
    else {
        auto o = occupancyArray.cbegin();
        for(size_t i = 0; i < refPositions()->size(); i++) {
            int32_t oc = 0;
            for(int j = 0; j < ncomponents; j++) {
                oc += *o++;
            }
            if(oc == 0) incrementVacancyCount();
            else if(oc > 1) incrementInterstitialCount(oc - 1);
        }
    }

    const Particles* refParticles = referenceState().getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("This modifier cannot be evaluated, because the reference configuration does not contain any particles."));

    if(!siteTypes()) {
        // Replace complete particles set with the reference configuration.
        state.replaceObject(state.expectObject<Particles>(), refParticles);
        // Also replace simulation cell with reference cell.
        if(const SimulationCell* cell = state.getObject<SimulationCell>())
            state.replaceObject(cell, referenceState().getObject<SimulationCell>());
    }

    Particles* particles = state.expectMutableObject<Particles>();
    if(occupancyNumbers()->size() != particles->elementCount())
        throw Exception(tr("Cached modifier results are obsolete, because the number of input particles has changed."));

    particles->createProperty(occupancyNumbers());
    if(siteTypes()) {
        // Transfer particle type list from reference type property to output site type property.
        if(const Property* inProp = refParticles->getProperty(Particles::TypeProperty)) {
            siteTypes()->setElementTypes(inProp->elementTypes());
        }
        particles->createProperty(siteTypes());
    }
    if(siteIndices())
        particles->createProperty(siteIndices());
    if(siteIdentifiers())
        particles->createProperty(siteIdentifiers());

    state.addAttribute(QStringLiteral("WignerSeitz.vacancy_count"), QVariant::fromValue(vacancyCount()), _createdByNode);
    state.addAttribute(QStringLiteral("WignerSeitz.interstitial_count"), QVariant::fromValue(interstitialCount()), _createdByNode);

    state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Found %1 vacancies and %2 interstitials").arg(vacancyCount()).arg(interstitialCount())));
}

}   // End of namespace
