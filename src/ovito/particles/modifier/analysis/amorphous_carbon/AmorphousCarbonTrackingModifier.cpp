////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "AmorphousCarbonTrackingModifier.h"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>
#include <vector>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonTrackingModifier);
OVITO_CLASSINFO(AmorphousCarbonTrackingModifier, "DisplayName", "Amorphous carbon cluster tracking");
OVITO_CLASSINFO(AmorphousCarbonTrackingModifier, "Description", "Track sp2/sp3 cluster fragmentation between a reference and the current configuration.");
OVITO_CLASSINFO(AmorphousCarbonTrackingModifier, "ModifierCategory", "Analysis");

DEFINE_PROPERTY_FIELD(AmorphousCarbonTrackingModifier, colorMode);
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonTrackingModifier, colorMode, "Particle coloring");

DEFINE_PROPERTY_FIELD(AmorphousCarbonTrackingModifier, genealogyOverlapFraction);
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonTrackingModifier, genealogyOverlapFraction, "Cluster genealogy overlap threshold");
SET_PROPERTY_FIELD_UNITS_AND_RANGE(AmorphousCarbonTrackingModifier, genealogyOverlapFraction, PercentParameterUnit, 0.0, 1.0);

/******************************************************************************
* Creates and initializes a computation engine that will compute the modifier's results.
******************************************************************************/
std::unique_ptr<ReferenceConfigurationModifier::Engine> AmorphousCarbonTrackingModifier::createEngine(
        const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState)
{
    const Particles* particles = input.expectObject<Particles>();
    particles->verifyIntegrity();
    const Property* posProperty = particles->expectProperty(Particles::PositionProperty);
    const Property* identifierProperty = particles->getProperty(Particles::IdentifierProperty);
    if(!identifierProperty)
        throw Exception(tr("Amorphous carbon cluster tracking requires the 'Particle Identifier' property in the "
                            "current configuration to match atoms against the reference configuration."));

    const Property* currentClusterIds = particles->getProperty(QStringLiteral("Carbon Cluster Identifier"));
    const Property* currentClusterSizes = particles->getProperty(QStringLiteral("Carbon Cluster Size"));
    // "Hybridization" is what separates an atom that left the tracked phase from one that is
    // still in it but was dropped by the upstream core filter. Both end up with cluster id 0.
    const Property* currentHybridization = particles->getProperty(QStringLiteral("Hybridization"));
    // Scopes the bidirectional genealogy classification to scientifically relevant clusters on
    // both sides, reusing the exact threshold the upstream modifier already applies.
    const Property* currentRelevant = particles->getProperty(QStringLiteral("Relevant Carbon Cluster"));
    if(!currentClusterIds || !currentClusterSizes || !currentHybridization || !currentRelevant)
        throw Exception(tr("Amorphous carbon cluster tracking requires an upstream 'Amorphous carbon cluster analysis' "
                            "modifier. Add it to the pipeline before this modifier."));

    const Particles* refParticles = referenceState.getObject<Particles>();
    if(!refParticles)
        throw Exception(tr("Reference configuration does not contain any particles."));
    refParticles->verifyIntegrity();
    const Property* refPosProperty = refParticles->expectProperty(Particles::PositionProperty);
    const Property* refIdentifierProperty = refParticles->getProperty(Particles::IdentifierProperty);
    if(!refIdentifierProperty)
        throw Exception(tr("Amorphous carbon cluster tracking requires the 'Particle Identifier' property in the "
                            "reference configuration."));

    const Property* refClusterIds = refParticles->getProperty(QStringLiteral("Carbon Cluster Identifier"));
    const Property* refClusterSizes = refParticles->getProperty(QStringLiteral("Carbon Cluster Size"));
    const Property* refClusterRanks = refParticles->getProperty(QStringLiteral("Carbon Cluster Rank"));
    const Property* refHybridization = refParticles->getProperty(QStringLiteral("Hybridization"));
    const Property* refRelevant = refParticles->getProperty(QStringLiteral("Relevant Carbon Cluster"));
    if(!refClusterIds || !refClusterSizes || !refClusterRanks || !refHybridization || !refRelevant)
        throw Exception(tr("Amorphous carbon cluster tracking requires the reference configuration to also pass "
                            "through an upstream 'Amorphous carbon cluster analysis' modifier (same pipeline, "
                            "evaluated at the reference frame)."));

    return std::make_unique<TrackingEngine>(
        posProperty, input.getObject<SimulationCell>(),
        refPosProperty, referenceState.getObject<SimulationCell>(),
        identifierProperty, refIdentifierProperty,
        currentClusterIds, currentClusterSizes,
        refClusterIds, refClusterSizes, refClusterRanks,
        currentHybridization, refHybridization,
        currentRelevant, refRelevant, genealogyOverlapFraction(),
        colorMode(),
        request.modificationNode());
}

/******************************************************************************
* Adopts existing computation results for an interactive pipeline evaluation.
******************************************************************************/
Future<PipelineFlowState> AmorphousCarbonTrackingModifier::reuseCachedState(
        const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState)
{
    if(DataOORef<const Particles> cachedParticles = cachedState.getObject<Particles>()) {
        const Property* cachedFate = cachedParticles->getProperty(QStringLiteral("Cluster Fate"));
        const Property* cachedRefId = cachedParticles->getProperty(QStringLiteral("Reference Cluster Identifier"));
        const Property* cachedRefGenealogy = cachedParticles->getProperty(QStringLiteral("Reference Cluster Genealogy"));
        const Property* cachedGenealogy = cachedParticles->getProperty(QStringLiteral("Cluster Genealogy"));
        const Property* cachedColor = colorMode() != NoColoring ? cachedParticles->getProperty(Particles::ColorProperty) : nullptr;
        if(cachedFate && cachedRefId && cachedRefGenealogy && cachedGenealogy) {
            return asyncLaunch([output = std::move(output), particles, cachedFate, cachedRefId, cachedRefGenealogy, cachedGenealogy, cachedColor,
                    cachedParticles = std::move(cachedParticles)]() mutable {
                particles->tryToAdoptProperties(cachedParticles, {cachedFate, cachedRefId, cachedRefGenealogy, cachedGenealogy, cachedColor}, {particles});
                return std::move(output);
            });
        }
    }
    return std::move(output);
}

/******************************************************************************
* Performs the actual computation. This method is executed in a worker thread.
******************************************************************************/
void AmorphousCarbonTrackingModifier::TrackingEngine::perform(PipelineFlowState& state)
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Tracking amorphous carbon clusters"));

    // Match atoms between the reference and the current configuration by Particle Identifier.
    buildParticleMapping(false, false);
    constexpr size_t unmappedIndex = std::numeric_limits<size_t>::max();

    const size_t numAtoms = positions()->size();
    const size_t numRefAtoms = refPositions()->size();

    BufferReadAccess<int64_t> currentClusterIdRead(_currentClusterIds);
    BufferReadAccess<int64_t> currentClusterSizeRead(_currentClusterSizes);
    BufferReadAccess<int64_t> refClusterIdRead(_refClusterIds);
    BufferReadAccess<int64_t> refClusterSizeRead(_refClusterSizes);
    BufferReadAccess<int32_t> refClusterRankRead(_refClusterRanks);
    BufferReadAccess<int32_t> currentHybridizationRead(_currentHybridization);
    BufferReadAccess<int32_t> refHybridizationRead(_refHybridization);
    BufferReadAccess<int32_t> currentRelevantRead(_currentRelevant);
    BufferReadAccess<int32_t> refRelevantRead(_refRelevant);

    // Identify the largest reference cluster. Its fragmentation history is the scientifically
    // relevant signal for radiation-damage studies (the percolating sp3 matrix / dominant sp2
    // domain), so the aggregate attributes below are reported for this single cluster.
    //
    // The same atom also identifies which hybridization state is being tracked: an atom only
    // carries a cluster id if it is in the tracked phase, so its "Hybridization" value is the
    // tracked one by construction. Reading it here rather than taking it as a parameter keeps
    // the two modifiers from being able to disagree. Stays 0 when the reference has no
    // clusters at all, in which case every atom is skipped below and it is never used.
    int64_t targetRefId = 0;
    int64_t targetRefSize = 0;
    int32_t trackedHybridization = 0;
    for(size_t r = 0; r < numRefAtoms; ++r) {
        if(refClusterRankRead[r] == 1) {
            targetRefId = refClusterIdRead[r];
            targetRefSize = refClusterSizeRead[r];
            trackedHybridization = refHybridizationRead[r];
            break;
        }
    }
    this_task::throwIfCanceled();

    // Every relevant reference cluster (at or above the upstream "Minimum relevant cluster size"),
    // whether or not any of its atoms survive into the current configuration. Serves two purposes:
    // the population the pooled fate fractions below are computed over, and the reference side of
    // the genealogy graph further down -- a cluster absent from the overlap pass there must still
    // be visited so it can be labeled Disappeared rather than skipped.
    std::unordered_map<int64_t,int64_t> refSizeByCid;
    for(size_t r = 0; r < numRefAtoms; ++r) {
        if(refRelevantRead[r] && refClusterIdRead[r] != 0)
            refSizeByCid.emplace(refClusterIdRead[r], refClusterSizeRead[r]);
    }
    this_task::throwIfCanceled();

    PropertyPtr fate = Particles::OOClass().createUserProperty(
        DataBuffer::Uninitialized, numAtoms, Property::Int32, 1, QStringLiteral("Cluster Fate"));
    PropertyPtr refClusterIdentifier = Particles::OOClass().createUserProperty(
        DataBuffer::Uninitialized, numAtoms, Property::Int64, 1, QStringLiteral("Reference Cluster Identifier"));

    BufferWriteAccess<int32_t, access_mode::discard_write> fateAcc(fate);
    BufferWriteAccess<int64_t, access_mode::discard_write> refIdAcc(refClusterIdentifier);

    // Pairs (reference cluster id, current cluster id), restricted to atoms that are still
    // clustered (size >= 2) in both frames: candidates for the "main fragment" majority vote.
    std::vector<std::pair<int64_t,int64_t>> pairs;
    std::vector<size_t> pendingAtoms;

    size_t unmappedCount = 0;
    size_t targetRetained = 0, targetLeftPhase = 0, targetLeftCore = 0, targetDispersed = 0;
    // Same tallies pooled over *every* relevant reference cluster, not just rank 1. The rank-1-only
    // summary describes the whole phase for a percolating sp3 matrix, but is meaningless for a
    // fragmented phase: on camorfo_sp3_60_mCRA in sp2 mode the largest cluster holds 14 atoms out of
    // 174096, so those attributes report on 0.008% of the phase.
    size_t relevantRetained = 0, relevantLeftPhase = 0, relevantLeftCore = 0, relevantDispersed = 0;
    // The mapping is only ever walked from the current configuration towards the reference, so
    // a reference atom with no current counterpart is classified nowhere and silently drops out
    // of every fraction below -- they would then no longer sum to 1. Marking which reference
    // atoms were reached lets those atoms be counted instead of lost.
    std::vector<char> refAtomSeen(numRefAtoms, 0);

    progress.setMaximum(numAtoms * 2);
    for(size_t i = 0; i < numAtoms; ++i) {
        progress.setValueIntermittent(i);
        fateAcc[i] = FateNotTracked;
        refIdAcc[i] = 0;

        const size_t refIdx = currentToRefIndexMap()[i];
        if(refIdx == unmappedIndex) {
            ++unmappedCount;
            continue;
        }
        refAtomSeen[refIdx] = 1;
        const int64_t refCid = refClusterIdRead[refIdx];
        if(refCid == 0)
            continue; // Atom was not part of a tracked-phase cluster in the reference configuration.

        refIdAcc[i] = refClusterRankRead[refIdx];
        const bool isTargetCluster = (refCid == targetRefId);
        const bool isRelevantRef = (refRelevantRead[refIdx] != 0);

        const int64_t curCid = currentClusterIdRead[i];
        if(curCid == 0) {
            // Cluster id 0 has two distinct causes, and conflating them is a factual error:
            // the atom either left the tracked hybridization state, or is still in it and was
            // excluded from clustering by the upstream "minimum same-phase neighbors" core
            // filter. The second case cannot occur when that filter is off, so this branch
            // reduces to the previous behaviour for every run without it.
            if(currentHybridizationRead[i] == trackedHybridization) {
                fateAcc[i] = FateLeftCore;
                if(isTargetCluster) ++targetLeftCore;
                if(isRelevantRef) ++relevantLeftCore;
            }
            else {
                fateAcc[i] = FateLeftPhase;
                if(isTargetCluster) ++targetLeftPhase;
                if(isRelevantRef) ++relevantLeftPhase;
            }
            continue;
        }
        if(isTargetCluster) ++targetRetained;
        if(isRelevantRef) ++relevantRetained;
        if(currentClusterSizeRead[i] == 1) {
            fateAcc[i] = FateDispersed;
            if(isTargetCluster) ++targetDispersed;
            if(isRelevantRef) ++relevantDispersed;
            continue;
        }
        pairs.emplace_back(refCid, curCid);
        pendingAtoms.push_back(i);
    }
    this_task::throwIfCanceled();

    // Reference atoms that have no counterpart in the current configuration. Zero whenever the
    // two frames contain the same atom set, which is the normal case for an mCRA series, but
    // any trajectory that deletes atoms would otherwise under-report every fraction without
    // saying so.
    size_t vanishedRefAtoms = 0, targetVanished = 0;
    size_t relevantRefAtoms = 0, relevantVanished = 0;
    for(size_t r = 0; r < numRefAtoms; ++r) {
        const bool isRelevantRef = (refRelevantRead[r] && refClusterIdRead[r] != 0);
        if(isRelevantRef)
            ++relevantRefAtoms;
        if(refAtomSeen[r])
            continue;
        ++vanishedRefAtoms;
        if(targetRefId != 0 && refClusterIdRead[r] == targetRefId)
            ++targetVanished;
        if(isRelevantRef)
            ++relevantVanished;
    }
    this_task::throwIfCanceled();

    // Majority vote: for each reference cluster, the current cluster receiving the largest
    // share of its atoms is the "main fragment"; every other landing cluster is a minor offshoot.
    std::unordered_map<int64_t,int64_t> mainFragmentId;
    // How many distinct current clusters each reference cluster scattered into, which is what
    // "fragment_count" reports for rank 1. Reference clusters with no surviving clustered atom at
    // all never appear here and correctly contribute 0 to the pooled sum below.
    std::unordered_map<int64_t,int64_t> fragmentCountByRefCid;
    {
        std::vector<std::pair<int64_t,int64_t>> sortedPairs = pairs;
        std::sort(sortedPairs.begin(), sortedPairs.end());
        size_t idx = 0;
        while(idx < sortedPairs.size()) {
            const int64_t refCid = sortedPairs[idx].first;
            size_t groupEnd = idx;
            int64_t bestCurCid = 0, bestCount = 0, distinctCurCids = 0;
            while(groupEnd < sortedPairs.size() && sortedPairs[groupEnd].first == refCid) {
                const int64_t curCid = sortedPairs[groupEnd].second;
                size_t runEnd = groupEnd;
                while(runEnd < sortedPairs.size() && sortedPairs[runEnd].first == refCid && sortedPairs[runEnd].second == curCid)
                    ++runEnd;
                const int64_t count = static_cast<int64_t>(runEnd - groupEnd);
                ++distinctCurCids;
                if(count > bestCount) {
                    bestCount = count;
                    bestCurCid = curCid;
                }
                groupEnd = runEnd;
            }
            mainFragmentId[refCid] = bestCurCid;
            fragmentCountByRefCid[refCid] = distinctCurCids;
            idx = groupEnd;
        }
    }
    this_task::throwIfCanceled();

    size_t relevantFragmentCount = 0;
    for(const auto& kv : fragmentCountByRefCid) {
        if(refSizeByCid.count(kv.first))
            relevantFragmentCount += static_cast<size_t>(kv.second);
    }

    size_t targetMainFragmentAtoms = 0, relevantMainFragmentAtoms = 0;
    std::set<int64_t> targetFragmentIds;
    for(size_t k = 0; k < pendingAtoms.size(); ++k) {
        progress.setValueIntermittent(numAtoms + k);
        const size_t i = pendingAtoms[k];
        const int64_t refCid = pairs[k].first;
        const int64_t curCid = pairs[k].second;
        const bool isMain = (curCid == mainFragmentId[refCid]);
        fateAcc[i] = isMain ? FateMainFragment : FateMinorFragment;
        if(refCid == targetRefId) {
            targetFragmentIds.insert(curCid);
            if(isMain) ++targetMainFragmentAtoms;
        }
        if(isMain && refRelevantRead[currentToRefIndexMap()[i]])
            ++relevantMainFragmentAtoms;
    }
    this_task::throwIfCanceled();

    fateAcc.reset();
    refIdAcc.reset();

    // --- Bidirectional cluster genealogy (Stable/Fragmented/Merged/Created/Disappeared) ---
    //
    // Independent of the rank-1-only fate/fraction attributes above: classifies *every* relevant
    // cluster (both reference-side and current-side, i.e. at or above the upstream "Minimum
    // relevant cluster size") from its own edges in the atom-overlap graph between the two
    // configurations. An edge (R, C) exists when the atoms R and C share reach
    // genealogyOverlapFraction() of either cluster's own size; each cluster is then labeled
    // purely from its own edge count -- the standard overlap-tracking scheme (e.g. Dixon & Wiener
    // 1993, "TITAN"). See AmorphousCarbonTrackingModifier.h and AMORPHOUS_CARBON_MODIFIER.md,
    // "Bidirectional cluster genealogy", for the full rationale and validation against the
    // camorfo_sp3_60_mCRA series.
    // Current-side counterpart of refSizeByCid (built above): every relevant current cluster,
    // including those with no qualifying edge, so they can be labeled Created rather than skipped.
    std::unordered_map<int64_t,int64_t> curSizeByCid;
    for(size_t i = 0; i < numAtoms; ++i) {
        if(currentRelevantRead[i] && currentClusterIdRead[i] != 0)
            curSizeByCid.emplace(currentClusterIdRead[i], currentClusterSizeRead[i]);
    }
    this_task::throwIfCanceled();

    std::vector<std::pair<int64_t,int64_t>> genealogyPairs; // (refCid, curCid), one entry per atom shared by a relevant pair
    for(size_t i = 0; i < numAtoms; ++i) {
        const size_t refIdx = currentToRefIndexMap()[i];
        if(refIdx == unmappedIndex || !refRelevantRead[refIdx] || !currentRelevantRead[i])
            continue;
        const int64_t refCid = refClusterIdRead[refIdx];
        const int64_t curCid = currentClusterIdRead[i];
        if(refCid == 0 || curCid == 0)
            continue;
        genealogyPairs.emplace_back(refCid, curCid);
    }
    this_task::throwIfCanceled();
    std::sort(genealogyPairs.begin(), genealogyPairs.end());

    std::unordered_map<int64_t,int32_t> refOutDegree;
    std::unordered_map<int64_t,int32_t> curInDegree;
    {
        size_t idx = 0;
        while(idx < genealogyPairs.size()) {
            const int64_t refCid = genealogyPairs[idx].first;
            size_t groupEnd = idx;
            while(groupEnd < genealogyPairs.size() && genealogyPairs[groupEnd].first == refCid) {
                const int64_t curCid = genealogyPairs[groupEnd].second;
                size_t runEnd = groupEnd;
                while(runEnd < genealogyPairs.size() && genealogyPairs[runEnd].first == refCid && genealogyPairs[runEnd].second == curCid)
                    ++runEnd;
                const int64_t sharedCount = static_cast<int64_t>(runEnd - groupEnd);
                const int64_t refSize = refSizeByCid[refCid];
                const int64_t curSize = curSizeByCid[curCid];
                if(sharedCount >= _genealogyOverlapFraction * refSize || sharedCount >= _genealogyOverlapFraction * curSize) {
                    ++refOutDegree[refCid];
                    ++curInDegree[curCid];
                }
                groupEnd = runEnd;
            }
            idx = groupEnd;
        }
    }
    this_task::throwIfCanceled();

    auto refGenealogyLabel = [&refOutDegree](int64_t refCid) -> int32_t {
        const auto it = refOutDegree.find(refCid);
        const int32_t degree = (it != refOutDegree.end()) ? it->second : 0;
        return degree == 0 ? AmorphousCarbonTrackingModifier::RefGenealogyDisappeared
             : degree == 1 ? AmorphousCarbonTrackingModifier::RefGenealogyStable
             :                AmorphousCarbonTrackingModifier::RefGenealogyFragmented;
    };
    auto curGenealogyLabel = [&curInDegree](int64_t curCid) -> int32_t {
        const auto it = curInDegree.find(curCid);
        const int32_t degree = (it != curInDegree.end()) ? it->second : 0;
        return degree == 0 ? AmorphousCarbonTrackingModifier::GenealogyCreated
             : degree == 1 ? AmorphousCarbonTrackingModifier::GenealogyStable
             :                AmorphousCarbonTrackingModifier::GenealogyMerged;
    };

    PropertyPtr refGenealogy = Particles::OOClass().createUserProperty(
        DataBuffer::Uninitialized, numAtoms, Property::Int32, 1, QStringLiteral("Reference Cluster Genealogy"));
    PropertyPtr curGenealogy = Particles::OOClass().createUserProperty(
        DataBuffer::Uninitialized, numAtoms, Property::Int32, 1, QStringLiteral("Cluster Genealogy"));
    {
        BufferWriteAccess<int32_t, access_mode::discard_write> refGenealogyAcc(refGenealogy);
        BufferWriteAccess<int32_t, access_mode::discard_write> curGenealogyAcc(curGenealogy);
        for(size_t i = 0; i < numAtoms; ++i) {
            // Reference-side label: a property of the atom's *reference* cluster, broadcast to
            // every current atom that traces back to it (same idiom as "Reference Cluster Identifier").
            int32_t refLabel = RefGenealogyNotTracked;
            const size_t refIdx = currentToRefIndexMap()[i];
            if(refIdx != unmappedIndex && refRelevantRead[refIdx]) {
                const int64_t refCid = refClusterIdRead[refIdx];
                if(refCid != 0)
                    refLabel = refGenealogyLabel(refCid);
            }
            refGenealogyAcc[i] = refLabel;

            // Current-side label: a property of the atom's own current cluster.
            int32_t curLabel = GenealogyNotClustered;
            if(currentRelevantRead[i]) {
                const int64_t curCid = currentClusterIdRead[i];
                if(curCid != 0)
                    curLabel = curGenealogyLabel(curCid);
            }
            curGenealogyAcc[i] = curLabel;
        }
    }
    this_task::throwIfCanceled();

    size_t refClustersStable = 0, refClustersFragmented = 0, refClustersDisappeared = 0;
    for(const auto& kv : refSizeByCid) {
        switch(refGenealogyLabel(kv.first)) {
            case RefGenealogyStable:     ++refClustersStable; break;
            case RefGenealogyFragmented: ++refClustersFragmented; break;
            default:                     ++refClustersDisappeared; break;
        }
    }
    size_t curClustersStable = 0, curClustersMerged = 0, curClustersCreated = 0;
    for(const auto& kv : curSizeByCid) {
        switch(curGenealogyLabel(kv.first)) {
            case GenealogyStable:  ++curClustersStable; break;
            case GenealogyMerged:  ++curClustersMerged; break;
            default:               ++curClustersCreated; break;
        }
    }

    Particles* particles = state.expectMutableObject<Particles>();
    particles->createProperty(fate);
    particles->createProperty(refClusterIdentifier);
    particles->createProperty(refGenealogy);
    particles->createProperty(curGenealogy);

    if(_coloring == ColorByFate) {
        BufferReadAccess<int32_t> fateRead(fate);
        BufferWriteAccess<ColorG, access_mode::discard_write> colors = particles->createProperty(Particles::ColorProperty);
        for(size_t i = 0; i < numAtoms; ++i) {
            // Cool hues mean "still in the tracked phase", warm hues "left it"; the null
            // category is a dark neutral. The previous green/orange/red set put the two most
            // important outcomes (main fragment vs. dispersed) on the green-red axis, which
            // red-green colorblind readers cannot separate, and its light gray read as white
            // against a light background. Validated over the chromatic entries: worst pair
            // dE 13.1 under protanopia, 11.9 under deuteranopia, 13.8 with normal vision, each
            // entry >= 3:1 contrast on white. Teal for FateLeftCore is deliberately cool: those
            // atoms are still sp3, and the whole point of separating them from FateLeftPhase is
            // that a red atom must mean "left the phase" and nothing else.
            switch(fateRead[i]) {
                case FateMainFragment:  colors[i] = ColorG(0.1176f, 0.6157f, 0.8510f); break; // #1E9DD9 sky blue
                case FateMinorFragment: colors[i] = ColorG(0.2902f, 0.2275f, 0.6549f); break; // #4A3AA7 indigo
                case FateLeftCore:      colors[i] = ColorG(0.0706f, 0.5569f, 0.4941f); break; // #128E7E teal
                case FateDispersed:     colors[i] = ColorG(0.7725f, 0.5294f, 0.0078f); break; // #C58702 amber
                case FateLeftPhase:     colors[i] = ColorG(0.6980f, 0.0941f, 0.1686f); break; // #B2182B deep red
                default:                colors[i] = ColorG(0.3020f, 0.3020f, 0.3020f); break; // #4D4D4D graphite
            }
        }
    }
    else if(_coloring == ColorByGenealogy) {
        // Colors by the CURRENT cluster's genealogy -- the perspective that actually shows the
        // headline "Created" clusters an atom-fate-only view cannot. Reuses hues already
        // validated for the Cluster Fate palette rather than deriving a new one: graphite keeps
        // its "not applicable" meaning, sky blue keeps its "intact / main line" meaning, and
        // deep red/amber (previously "left phase"/"dispersed", both already >= 3:1 contrast and
        // >= 11.9 dE apart under deuteranopia) mark the two events with no counterpart in that
        // palette. Only a subset of the six validated entries is used, so the documented
        // worst-pair separation still holds.
        BufferReadAccess<int32_t> curGenealogyRead(curGenealogy);
        BufferWriteAccess<ColorG, access_mode::discard_write> colors = particles->createProperty(Particles::ColorProperty);
        for(size_t i = 0; i < numAtoms; ++i) {
            switch(curGenealogyRead[i]) {
                case GenealogyStable:  colors[i] = ColorG(0.1176f, 0.6157f, 0.8510f); break; // #1E9DD9 sky blue
                case GenealogyCreated: colors[i] = ColorG(0.6980f, 0.0941f, 0.1686f); break; // #B2182B deep red
                case GenealogyMerged:  colors[i] = ColorG(0.7725f, 0.5294f, 0.0078f); break; // #C58702 amber
                default:               colors[i] = ColorG(0.3020f, 0.3020f, 0.3020f); break; // #4D4D4D graphite
            }
        }
    }

    const double retainedFraction = targetRefSize > 0 ? static_cast<double>(targetRetained) / targetRefSize : 0.0;
    const double mainFragmentFraction = targetRefSize > 0 ? static_cast<double>(targetMainFragmentAtoms) / targetRefSize : 0.0;
    const double leftPhaseFraction = targetRefSize > 0 ? static_cast<double>(targetLeftPhase) / targetRefSize : 0.0;
    const double leftCoreFraction = targetRefSize > 0 ? static_cast<double>(targetLeftCore) / targetRefSize : 0.0;
    const double dispersedFraction = targetRefSize > 0 ? static_cast<double>(targetDispersed) / targetRefSize : 0.0;
    const double vanishedFraction = targetRefSize > 0 ? static_cast<double>(targetVanished) / targetRefSize : 0.0;

    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_largest_cluster_size"), QVariant::fromValue(targetRefSize), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.main_fragment_size"), QVariant::fromValue(static_cast<qulonglong>(targetMainFragmentAtoms)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.main_fragment_fraction"), QVariant::fromValue(mainFragmentFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.retained_fraction"), QVariant::fromValue(retainedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.left_phase_fraction"), QVariant::fromValue(leftPhaseFraction), _createdByNode);
    // Atoms of the reference cluster that are still in the tracked phase but fell below the
    // upstream core-filter threshold. Always 0 when that filter is off; when it is on, this is
    // the part that must not be reported as phase loss.
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.left_core_fraction"), QVariant::fromValue(leftCoreFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.dispersed_fraction"), QVariant::fromValue(dispersedFraction), _createdByNode);
    // Completes the balance: retained + left_phase + left_core + vanished = 1 exactly.
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.vanished_fraction"), QVariant::fromValue(vanishedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.fragment_count"), QVariant::fromValue(static_cast<qulonglong>(targetFragmentIds.size())), _createdByNode);
    // Two different failures, previously only one of them reported: an atom present now but not
    // in the reference, versus an atom present in the reference but gone now.
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.unmapped_atom_count"), QVariant::fromValue(static_cast<qulonglong>(unmappedCount)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.vanished_reference_atom_count"), QVariant::fromValue(static_cast<qulonglong>(vanishedRefAtoms)), _createdByNode);

    // Same fate breakdown pooled over every relevant reference cluster. Denominator is the number
    // of reference atoms in those clusters, so retained + left_phase + left_core + vanished = 1
    // exactly, just as for rank 1. Use these -- not the rank-1 attributes -- whenever the phase has
    // no dominant cluster (the sp2 case), and note that the population they describe is set by the
    // upstream "Minimum relevant cluster size".
    const double relevantRetainedFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantRetained) / relevantRefAtoms : 0.0;
    const double relevantMainFragmentFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantMainFragmentAtoms) / relevantRefAtoms : 0.0;
    const double relevantLeftPhaseFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantLeftPhase) / relevantRefAtoms : 0.0;
    const double relevantLeftCoreFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantLeftCore) / relevantRefAtoms : 0.0;
    const double relevantDispersedFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantDispersed) / relevantRefAtoms : 0.0;
    const double relevantVanishedFraction = relevantRefAtoms > 0 ? static_cast<double>(relevantVanished) / relevantRefAtoms : 0.0;
    const double meanFragmentsPerCluster = !refSizeByCid.empty() ? static_cast<double>(relevantFragmentCount) / refSizeByCid.size() : 0.0;

    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_reference_atom_count"), QVariant::fromValue(static_cast<qulonglong>(relevantRefAtoms)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_size"), QVariant::fromValue(static_cast<qulonglong>(relevantMainFragmentAtoms)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_fraction"), QVariant::fromValue(relevantMainFragmentFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_retained_fraction"), QVariant::fromValue(relevantRetainedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_left_phase_fraction"), QVariant::fromValue(relevantLeftPhaseFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_left_core_fraction"), QVariant::fromValue(relevantLeftCoreFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_dispersed_fraction"), QVariant::fromValue(relevantDispersedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_vanished_fraction"), QVariant::fromValue(relevantVanishedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_fragment_count"), QVariant::fromValue(static_cast<qulonglong>(relevantFragmentCount)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.relevant_mean_fragments_per_cluster"), QVariant::fromValue(meanFragmentsPerCluster), _createdByNode);

    // Bidirectional genealogy, over *every* relevant cluster on each side -- not just rank 1.
    const size_t refClustersRelevant = refSizeByCid.size();
    const size_t curClustersRelevant = curSizeByCid.size();
    const double refStableFraction = refClustersRelevant > 0 ? static_cast<double>(refClustersStable) / refClustersRelevant : 0.0;
    const double refFragmentedFraction = refClustersRelevant > 0 ? static_cast<double>(refClustersFragmented) / refClustersRelevant : 0.0;
    const double refDisappearedFraction = refClustersRelevant > 0 ? static_cast<double>(refClustersDisappeared) / refClustersRelevant : 0.0;
    const double curStableFraction = curClustersRelevant > 0 ? static_cast<double>(curClustersStable) / curClustersRelevant : 0.0;
    const double curMergedFraction = curClustersRelevant > 0 ? static_cast<double>(curClustersMerged) / curClustersRelevant : 0.0;
    const double curCreatedFraction = curClustersRelevant > 0 ? static_cast<double>(curClustersCreated) / curClustersRelevant : 0.0;

    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.genealogy_overlap_fraction"), QVariant::fromValue(static_cast<double>(_genealogyOverlapFraction)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_relevant"), QVariant::fromValue(static_cast<qulonglong>(refClustersRelevant)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_stable"), QVariant::fromValue(static_cast<qulonglong>(refClustersStable)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_fragmented"), QVariant::fromValue(static_cast<qulonglong>(refClustersFragmented)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_disappeared"), QVariant::fromValue(static_cast<qulonglong>(refClustersDisappeared)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_stable_fraction"), QVariant::fromValue(refStableFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_fragmented_fraction"), QVariant::fromValue(refFragmentedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.reference_clusters_disappeared_fraction"), QVariant::fromValue(refDisappearedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_relevant"), QVariant::fromValue(static_cast<qulonglong>(curClustersRelevant)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_stable"), QVariant::fromValue(static_cast<qulonglong>(curClustersStable)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_merged"), QVariant::fromValue(static_cast<qulonglong>(curClustersMerged)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_created"), QVariant::fromValue(static_cast<qulonglong>(curClustersCreated)), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_stable_fraction"), QVariant::fromValue(curStableFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_merged_fraction"), QVariant::fromValue(curMergedFraction), _createdByNode);
    state.addAttribute(QStringLiteral("AmorphousCarbonTracking.current_clusters_created_fraction"), QVariant::fromValue(curCreatedFraction), _createdByNode);

    QString statusText = tr("Reference cluster (rank 1, %1 atoms): %2% retained, main fragment %3%, %4 fragment(s).")
            .arg(targetRefSize)
            .arg(retainedFraction * 100.0, 0, 'f', 1)
            .arg(mainFragmentFraction * 100.0, 0, 'f', 1)
            .arg(targetFragmentIds.size());
    // With a core filter upstream, "not retained" is mostly not phase loss. Spelling out the
    // split here is the only place a GUI user sees it before quoting the number.
    if(targetLeftCore > 0) {
        statusText += tr("\nOf the %1% not retained, %2% left the tracked phase and %3% are still "
                         "in it but below the upstream core-filter threshold.")
            .arg((1.0 - retainedFraction) * 100.0, 0, 'f', 1)
            .arg(leftPhaseFraction * 100.0, 0, 'f', 1)
            .arg(leftCoreFraction * 100.0, 0, 'f', 1);
    }
    if(targetVanished > 0) {
        statusText += tr("\n%1 atoms of the reference cluster (%2%) are absent from the current "
                         "configuration and are counted in none of the fractions above.")
            .arg(targetVanished)
            .arg(vanishedFraction * 100.0, 0, 'f', 1);
    }
    statusText += tr("\nPooled over all %1 relevant reference clusters (%2 atoms): %3% retained, "
                     "main fragments %4%, %5 fragment(s), %6 per cluster.")
        .arg(refSizeByCid.size())
        .arg(relevantRefAtoms)
        .arg(relevantRetainedFraction * 100.0, 0, 'f', 1)
        .arg(relevantMainFragmentFraction * 100.0, 0, 'f', 1)
        .arg(relevantFragmentCount)
        .arg(meanFragmentsPerCluster, 0, 'f', 2);
    // Where rank 1 is a negligible share of the phase -- the sp2 case, 14 atoms out of 174096 on
    // camorfo_sp3_60_mCRA -- the rank-1 numbers above are real but describe almost nothing. Say so
    // where a GUI user reads them, rather than leaving it to the documentation.
    if(relevantRefAtoms > 0 && targetRefSize * 20 < static_cast<int64_t>(relevantRefAtoms)) {
        statusText += tr("\nThe rank-1 cluster holds only %1% of the relevant reference atoms: quote "
                         "the pooled figures, not the rank-1 ones, for this phase.")
            .arg(100.0 * targetRefSize / relevantRefAtoms, 0, 'f', 2);
    }
    statusText += tr("\nGenealogy (%1 relevant reference clusters): %2 stable, %3 fragmented, %4 disappeared. "
                     "(%5 relevant current clusters): %6 stable, %7 merged, %8 created.")
        .arg(refClustersRelevant).arg(refClustersStable).arg(refClustersFragmented).arg(refClustersDisappeared)
        .arg(curClustersRelevant).arg(curClustersStable).arg(curClustersMerged).arg(curClustersCreated);
    state.setStatus(PipelineStatus(statusText));
}

} // End of namespace
