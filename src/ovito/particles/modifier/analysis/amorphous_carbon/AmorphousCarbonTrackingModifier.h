////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/particles/modifier/analysis/ReferenceConfigurationModifier.h>

namespace Ovito {

/**
 * \brief Tracks the connected sp2/sp3 clusters produced by AmorphousCarbonModifier across
 * a reference and a current configuration (e.g. mCRA cycle 0 vs. cycle N).
 *
 * This modifier does not detect hybridization or connectivity itself: it must sit downstream
 * of an "Amorphous carbon cluster analysis" modifier and consumes the "Carbon Cluster
 * Identifier" / "Carbon Cluster Size" / "Carbon Cluster Rank" particle properties that
 * modifier produces, in both the current pipeline output and the reference configuration
 * (which is evaluated through the same upstream pipeline, so it must contain the same
 * modifier too). Correspondence between the two frames is established through the
 * "Particle Identifier" property, reusing the reference/current infrastructure that OVITO's
 * Displacement Vectors and Wigner-Seitz modifiers are built on.
 *
 * For every current-frame atom that belonged to a tracked-phase cluster in the reference
 * configuration, it reports (via "Cluster Fate") whether the atom left the tracked
 * hybridization state, is still in that state but no longer clustered by the upstream core
 * filter, ended up in the current-frame cluster that inherited most of its reference
 * cluster's atoms (the "main fragment"), ended up in a smaller offshoot cluster, or
 * became an isolated (dispersed) atom. "Reference Cluster Identifier" stores which reference
 * cluster (by rank; 1 = largest) the atom came from.
 *
 * Aggregate fragmentation statistics (fragment count, retained fraction, main-fragment
 * fraction) are reported as global attributes twice over: once for the single largest reference
 * cluster, whose breakup is the signal of interest whenever the phase has a dominant cluster (the
 * percolating sp3 matrix), and once pooled over every reference cluster at or above the upstream
 * "Minimum relevant cluster size" ("relevant_*" attributes). The pooled form exists because the
 * rank-1 form is meaningless for a finely divided phase: in sp2 mode on camorfo_sp3_60_mCRA the
 * largest cluster holds 14 atoms out of 174096, so the rank-1 attributes describe 0.008% of it.
 * Both share the same denominators-sum-to-one convention (retained + left_phase + left_core +
 * vanished = 1).
 *
 * Independently of that rank-1-only summary, the modifier also classifies *every* relevant
 * cluster (reference and current alike, i.e. those at or above the upstream "Minimum relevant
 * cluster size") with a bidirectional Stable/Fragmented/Merged/Created/Disappeared genealogy --
 * see "Reference Cluster Genealogy" and "Cluster Genealogy" below and "genealogyOverlapFraction".
 * This is what surfaces current-frame clusters built mostly from atoms that were not part of any
 * tracked-phase cluster in the reference ("Created"), which the rank-1 fate/fraction attributes
 * cannot show.
 */
class OVITO_PARTICLES_EXPORT AmorphousCarbonTrackingModifier : public ReferenceConfigurationModifier
{
    OVITO_CLASS(AmorphousCarbonTrackingModifier)

public:

    /// Per-atom classification stored in the "Cluster Fate" output property.
    enum ClusterFate {
        FateNotTracked = 0,     ///< Atom was not part of a tracked-phase cluster in the reference configuration.
        FateLeftPhase = 1,      ///< Atom is no longer in the tracked hybridization state.
        FateMainFragment = 2,   ///< Atom is in the current-frame cluster that inherited most of its reference cluster.
        FateMinorFragment = 3,  ///< Atom is in a smaller offshoot cluster.
        FateDispersed = 4,      ///< Atom ended up as an isolated (unclustered) atom.
        /// Atom is still in the tracked hybridization state, but the upstream modifier's core
        /// filter ("minimum same-phase neighbors") no longer clusters it, so it carries no
        /// cluster identifier. Distinguishing this from FateLeftPhase matters: an unclustered
        /// atom has cluster id 0 for both reasons, and lumping them together reports atoms that
        /// are still sp3 as having left sp3. On camorfo_sp3_60_mCRA at cycle 100 with a core
        /// filter of 3, that inflated "left the phase" from 0.423 to 0.748 of the reference
        /// cluster. Never produced when the core filter is off, so older results are unaffected.
        FateLeftCore = 5
    };
    Q_ENUM(ClusterFate)

    enum ColorMode {
        NoColoring = 0,
        ColorByFate = 1,
        ColorByGenealogy = 2
    };
    Q_ENUM(ColorMode)

    /// Per-cluster classification stored in the "Reference Cluster Genealogy" output property.
    /// Describes what happened to a *reference* cluster, from its own outgoing edges in the
    /// atom-overlap graph towards current-frame clusters (see "genealogyOverlapFraction").
    enum ReferenceClusterGenealogy {
        RefGenealogyNotTracked = 0,   ///< Atom's reference cluster does not exist or is below the upstream relevance threshold.
        RefGenealogyDisappeared = 1,  ///< No current-frame cluster reaches the overlap threshold: the cluster left no significant successor.
        RefGenealogyStable = 2,       ///< Exactly one current-frame cluster reaches the threshold: the cluster continues as a single successor.
        RefGenealogyFragmented = 3    ///< Two or more current-frame clusters reach the threshold: the cluster split into significant pieces.
    };
    Q_ENUM(ReferenceClusterGenealogy)

    /// Per-cluster classification stored in the "Cluster Genealogy" output property. Describes
    /// where a *current* cluster came from, from its own incoming edges in the same graph.
    enum ClusterGenealogy {
        GenealogyNotClustered = 0,  ///< Atom is not part of a relevant current-frame cluster.
        GenealogyCreated = 1,       ///< No reference cluster reaches the overlap threshold: the cluster has no significant ancestor.
        GenealogyStable = 2,        ///< Exactly one reference cluster reaches the threshold: the cluster is a single continuation.
        GenealogyMerged = 3         ///< Two or more reference clusters reach the threshold: the cluster absorbed multiple ancestors.
    };
    Q_ENUM(ClusterGenealogy)

protected:

    /// Creates a computation engine that will compute the modifier's results.
    virtual std::unique_ptr<Engine> createEngine(const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState) override;

    /// Adopts existing computation results for an interactive pipeline evaluation.
    virtual Future<PipelineFlowState> reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState) override;

private:

    /// Computes the modifier's results.
    class TrackingEngine : public Engine
    {
    public:

        /// Constructor.
        TrackingEngine(ConstPropertyPtr positions, const SimulationCell* simCell,
                ConstPropertyPtr refPositions, const SimulationCell* simCellRef,
                ConstPropertyPtr identifiers, ConstPropertyPtr refIdentifiers,
                ConstPropertyPtr currentClusterIds, ConstPropertyPtr currentClusterSizes,
                ConstPropertyPtr refClusterIds, ConstPropertyPtr refClusterSizes, ConstPropertyPtr refClusterRanks,
                ConstPropertyPtr currentHybridization, ConstPropertyPtr refHybridization,
                ConstPropertyPtr currentRelevant, ConstPropertyPtr refRelevant, FloatType genealogyOverlapFraction,
                ColorMode coloring,
                OOWeakRef<const PipelineNode> createdByNode) :
            Engine(std::move(positions), simCell, std::move(refPositions), simCellRef,
                std::move(identifiers), std::move(refIdentifiers), NO_MAPPING, false),
            _currentClusterIds(std::move(currentClusterIds)), _currentClusterSizes(std::move(currentClusterSizes)),
            _refClusterIds(std::move(refClusterIds)), _refClusterSizes(std::move(refClusterSizes)), _refClusterRanks(std::move(refClusterRanks)),
            _currentHybridization(std::move(currentHybridization)), _refHybridization(std::move(refHybridization)),
            _currentRelevant(std::move(currentRelevant)), _refRelevant(std::move(refRelevant)),
            _genealogyOverlapFraction(genealogyOverlapFraction),
            _coloring(coloring), _createdByNode(std::move(createdByNode)) {}

        /// Performs the actual computation of the modifier's results.
        virtual void perform(PipelineFlowState& state) override;

    private:

        ConstPropertyPtr _currentClusterIds;
        ConstPropertyPtr _currentClusterSizes;
        ConstPropertyPtr _refClusterIds;
        ConstPropertyPtr _refClusterSizes;
        ConstPropertyPtr _refClusterRanks;
        ConstPropertyPtr _currentHybridization;
        ConstPropertyPtr _refHybridization;
        ConstPropertyPtr _currentRelevant;
        ConstPropertyPtr _refRelevant;
        FloatType _genealogyOverlapFraction;
        ColorMode _coloring;
        OOWeakRef<const PipelineNode> _createdByNode;
    };

    /// Optional automatic particle coloring by cluster fate.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(ColorMode{ColorByFate}, colorMode, setColorMode, PROPERTY_FIELD_MEMORIZE)

    /// Minimum fraction of either cluster's size that an atom overlap must reach before it counts
    /// as a genealogical edge between a reference cluster and a current cluster.
    ///
    /// Drives the bidirectional cluster classification (Stable/Fragmented/Disappeared on the
    /// reference side, Stable/Merged/Created on the current side): an edge (R, C) exists when
    /// overlap(R,C) >= threshold * size(R) OR overlap(R,C) >= threshold * size(C), and each
    /// cluster is labeled purely from its own edge count in that graph -- the same overlap-based
    /// scheme used for storm-cell/grain tracking (e.g. Dixon & Wiener 1993, "TITAN"). Validated on
    /// camorfo_sp3_60_mCRA to be robust across 0.05-0.30 (results essentially unchanged); default
    /// 0.10 sits in the middle of that plateau. See AMORPHOUS_CARBON_MODIFIER.md, "Bidirectional
    /// cluster genealogy".
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.10}, genealogyOverlapFraction, setGenealogyOverlapFraction, PROPERTY_FIELD_MEMORIZE)
};

} // End of namespace
