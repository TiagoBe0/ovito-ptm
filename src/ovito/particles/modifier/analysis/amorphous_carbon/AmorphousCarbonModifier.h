////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

/**
 * \brief Identifies carbon hybridization states and connected sp2/sp3 domains.
 *
 * This modifier is intended for large amorphous-carbon configurations. It uses
 * independent C-C cutoffs to determine coordination and to build connected
 * components from atoms of the selected hybridization state.
 */
class OVITO_PARTICLES_EXPORT AmorphousCarbonModifier : public Modifier
{
    class AmorphousCarbonModifierClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(AmorphousCarbonModifier, AmorphousCarbonModifierClass)

public:

    enum ClusterSpecies {
        ClusterSp2 = 2,
        ClusterSp3 = 3
    };
    Q_ENUM(ClusterSpecies)

    enum ColorMode {
        NoColoring = 0,
        ColorByHybridization = 1,
        ColorByCluster = 2,
        ColorByClusterSize = 3,
        ColorBySamePhaseCoordination = 4,
        ColorByRingSize = 5
    };
    Q_ENUM(ColorMode)

    virtual void preevaluateModifier(const ModifierEvaluationRequest& request,
        PipelineEvaluationResult::EvaluationTypes& evaluationTypes,
        TimeInterval& validityInterval) const override;

    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& state) override;

    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

private:

    /// C-C neighbor cutoff used to determine coordination and hybridization.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{2.064}, coordinationCutoff, setCoordinationCutoff, PROPERTY_FIELD_MEMORIZE)

    /// C-C neighbor cutoff used to connect atoms of the selected phase.
    ///
    /// Defaults to the coordination cutoff so that connectivity uses the same
    /// bonds that determined hybridization. The earlier 1.85 default sat in the
    /// middle of a populated part of the C-C bond-length distribution, which made
    /// cluster counts extremely cutoff-sensitive and silently discarded ~2% of
    /// real bonds; see AMORPHOUS_CARBON_MODIFIER.md. Shorter values remain
    /// available but are reported as a warning.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{2.064}, bondCutoff, setBondCutoff, PROPERTY_FIELD_MEMORIZE)

    /// Hybridization state whose connected domains are constructed.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(ClusterSpecies{ClusterSp2}, clusterSpecies, setClusterSpecies, PROPERTY_FIELD_MEMORIZE)

    /// Minimum size used to mark a cluster as scientifically relevant.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{3}, minimumClusterSize, setMinimumClusterSize, PROPERTY_FIELD_MEMORIZE)

    /// Minimum number of same-phase neighbors an atom needs before it is clustered at all.
    ///
    /// 0 clusters every atom of the phase. That is degenerate for a dense network:
    /// well above the percolation threshold (sp3-rich amorphous carbon sits there),
    /// the largest connected component is always almost the whole phase and barely
    /// moves under damage. Requiring 3 same-phase neighbors clusters only the
    /// well-embedded core of the network instead, which does lose percolation.
    /// See AMORPHOUS_CARBON_MODIFIER.md, "Clustering a percolating network".
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, minSamePhaseNeighbors, setMinSamePhaseNeighbors, PROPERTY_FIELD_MEMORIZE)

    /// Optional automatic particle coloring.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(ColorMode{ColorByHybridization}, colorMode, setColorMode, PROPERTY_FIELD_MEMORIZE)

    /// Enables detection of closed primitive (shortest-path) rings in the same-phase bond graph.
    ///
    /// Off by default: ring perception does a bounded breadth-first search from every bond in
    /// the selected phase, which costs meaningfully more than clustering on a large percolating
    /// network. See "Ring statistics" in AMORPHOUS_CARBON_MODIFIER.md for the algorithm
    /// (Franzblau 1991 shortest-path/primitive-ring criterion) and its cost characteristics.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, ringAnalysisEnabled, setRingAnalysisEnabled, PROPERTY_FIELD_MEMORIZE)

    /// Largest ring size (in atoms) the search looks for.
    ///
    /// Bounds the search depth of the shortest-path BFS run from every bond, so this is the
    /// main cost control knob for ring analysis. Real rings in amorphous carbon are almost
    /// always well under 10 atoms; going much higher mainly costs time on bonds that never
    /// close into a small ring.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{8}, maxRingSize, setMaxRingSize, PROPERTY_FIELD_MEMORIZE)

    /// Smallest ring size (in atoms) the search reports.
    ///
    /// The smallest ring a simple graph can contain at all is a 3-membered one (a triangle);
    /// sizes below that are not topologically possible and never occur. Raising this excludes
    /// small rings (e.g. triangles) from the counts and from the per-atom Ring Size /
    /// Smallest Ring Identifier properties without changing how larger rings are found or
    /// primitivity-checked, since each bond independently contributes at most its own
    /// shortest primitive ring(s).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{3}, minRingSize, setMinRingSize, PROPERTY_FIELD_MEMORIZE)
};

} // End of namespace
