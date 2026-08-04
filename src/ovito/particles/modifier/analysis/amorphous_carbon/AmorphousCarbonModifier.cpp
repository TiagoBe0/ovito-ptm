////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include "AmorphousCarbonModifier.h"

#include <deque>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <unordered_set>

namespace Ovito {

namespace {

/// Converts an OKLCH color to sRGB, backing off chroma until the result fits the
/// sRGB gamut. Used for the generated cluster-identifier and cluster-size colors:
/// OKLCH keeps perceived lightness constant across hues, which HSV does not, so a
/// generated palette cannot drift into pale near-white yellows the way an
/// HSV-based one does.
ColorG oklchToColor(GraphicsFloatType lightness, GraphicsFloatType chroma, GraphicsFloatType hueDegrees)
{
    const GraphicsFloatType h = hueDegrees * GraphicsFloatType(0.017453292519943295);
    const GraphicsFloatType cosH = std::cos(h), sinH = std::sin(h);
    auto encode = [](GraphicsFloatType c) {
        c = std::clamp(c, GraphicsFloatType(0), GraphicsFloatType(1));
        return c <= GraphicsFloatType(0.0031308)
            ? GraphicsFloatType(12.92) * c
            : GraphicsFloatType(1.055) * std::pow(c, GraphicsFloatType(1.0 / 2.4)) - GraphicsFloatType(0.055);
    };
    for(int attempt = 0; attempt < 24; ++attempt, chroma *= GraphicsFloatType(0.92)) {
        const GraphicsFloatType a = chroma * cosH, b = chroma * sinH;
        const GraphicsFloatType l_ = lightness + GraphicsFloatType(0.3963377774) * a + GraphicsFloatType(0.2158037573) * b;
        const GraphicsFloatType m_ = lightness - GraphicsFloatType(0.1055613458) * a - GraphicsFloatType(0.0638541728) * b;
        const GraphicsFloatType s_ = lightness - GraphicsFloatType(0.0894841775) * a - GraphicsFloatType(1.2914855480) * b;
        const GraphicsFloatType l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
        const GraphicsFloatType r =  GraphicsFloatType(4.0767416621) * l - GraphicsFloatType(3.3077115913) * m + GraphicsFloatType(0.2309699292) * s;
        const GraphicsFloatType g = -GraphicsFloatType(1.2684380046) * l + GraphicsFloatType(2.6097574011) * m - GraphicsFloatType(0.3413193965) * s;
        const GraphicsFloatType bl =  GraphicsFloatType(0.0041960863) * l - GraphicsFloatType(0.7034186147) * m + GraphicsFloatType(1.7076147010) * s;
        constexpr GraphicsFloatType tol = GraphicsFloatType(0.001);
        if(r >= -tol && r <= 1 + tol && g >= -tol && g <= 1 + tol && bl >= -tol && bl <= 1 + tol)
            return ColorG(encode(r), encode(g), encode(bl));
    }
    return ColorG(GraphicsFloatType(0.5), GraphicsFloatType(0.5), GraphicsFloatType(0.5));
}

// Categorical palette, validated against the data-visualization checks (OKLCH
// lightness band, chroma floor, all-pairs protan/deutan separation, normal-vision
// separation, and >= 3:1 WCAG contrast on a white viewport background). Worst
// all-pairs separation: dE 18.1 under deuteranopia, 23.4 with normal vision.
// Deliberately no light values: every entry stays well clear of white so the
// particles read against the default light background.
constexpr ColorG kSkyBlue{GraphicsFloatType(0.1176), GraphicsFloatType(0.6157), GraphicsFloatType(0.8510)};  // #1E9DD9
constexpr ColorG kAmber  {GraphicsFloatType(0.7725), GraphicsFloatType(0.5294), GraphicsFloatType(0.0078)};  // #C58702
constexpr ColorG kIndigo {GraphicsFloatType(0.2902), GraphicsFloatType(0.2275), GraphicsFloatType(0.6549)};  // #4A3AA7
constexpr ColorG kGraphite{GraphicsFloatType(0.3020), GraphicsFloatType(0.3020), GraphicsFloatType(0.3020)}; // #4D4D4D, null category

/// One entry of the same-phase bond adjacency list used by the ring search: the neighbor
/// atom index and the integer periodic-image shift of the bond, in the same convention as
/// CutoffNeighborFinder::Query::pbcShift() (the shift that carries the neighbor's *unshifted*
/// position into the image that is actually within cutoff of the atom being queried).
struct RingAdjEntry {
    int64_t neighbor;
    Vector3I shift;
};

/// Output of the primitive-ring search: one histogram/per-atom summary of the primitive
/// (shortest-path, Franzblau 1991) rings found in the same-phase bond graph.
struct RingAnalysisResult {
    std::vector<int32_t> ringSize;         // Per atom: size of the smallest ring it belongs to, 0 if none.
    std::vector<int64_t> smallestRingId;   // Per atom: ID of that smallest ring (ties broken by lowest ID), 0 if none.
    std::vector<int32_t> ringMembership;   // Per atom: number of distinct primitive rings it belongs to.
    std::vector<int64_t> countBySize;      // Indexed by ring size (0..maxRingSize); counts of distinct rings found.
    int64_t totalRings = 0;
    double meanRingSize = 0.0;
};

/// Canonicalizes a cyclic sequence of atom indices so that the same physical ring, discovered
/// starting from any of its bonds and walked in either direction, produces an identical key.
/// Rotates to start at the smallest atom index, then picks whichever walking direction gives
/// the lexicographically smaller remainder.
std::vector<int64_t> canonicalizeRing(const std::vector<int64_t>& ring)
{
    const size_t n = ring.size();
    size_t minPos = 0;
    for(size_t k = 1; k < n; ++k)
        if(ring[k] < ring[minPos]) minPos = k;
    std::vector<int64_t> forward(n), backward(n);
    for(size_t k = 0; k < n; ++k)
        forward[k] = ring[(minPos + k) % n];
    backward[0] = forward[0];
    for(size_t k = 1; k < n; ++k)
        backward[k] = forward[n - k];
    return (backward < forward) ? backward : forward;
}

/// Hashes a canonicalized ring for use as an unordered_set key. Collisions only cost an extra
/// (exact) vector comparison -- unordered_set always falls back to operator== within a bucket --
/// so this cannot merge two different rings.
struct RingKeyHash {
    size_t operator()(const std::vector<int64_t>& v) const noexcept {
        size_t h = v.size();
        for(int64_t x : v)
            h ^= std::hash<int64_t>{}(x) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

/// Finds all primitive (shortest-path) rings in the bond graph induced by atoms of
/// `targetHybridization`, using the criterion of D. S. Franzblau, Phys. Rev. B 44, 4925 (1991):
/// for each bond (i,j), find the shortest alternative path connecting i and j that does not use
/// that bond directly; the resulting cycle is accepted only if no pair of atoms on it is
/// connected by a shorter path through the rest of the graph (i.e. the cycle cannot be written
/// as the sum of two smaller ones sharing an edge). This is the standard ring-statistics
/// definition used for amorphous carbon and silica networks and avoids the combinatorial
/// explosion of enumerating every closed walk in a dense, percolating network.
///
/// Rings are required to close in real (unwrapped) space -- the sum of the integer periodic
/// image shifts around the loop must be exactly zero -- so a path that merely wraps around the
/// periodic cell (a percolation loop, not a molecule) is never reported as a ring. Ties among
/// several equally-short paths through the same bond are all enumerated (capped, to stay
/// tractable in locally symmetric diamond-like regions) and each is primitivity-checked and
/// deduplicated independently, matching Franzblau's original prescription.
///
/// `minRingSize` filters the *reported* rings only, after detection and primitivity-checking:
/// a bond whose shortest primitive ring is smaller than the floor is simply not counted, rather
/// than searched for a second, larger ring (there isn't a well-defined "next" ring through the
/// same bond in this criterion).
RingAnalysisResult findPrimitiveRings(
    size_t particleCount,
    const std::vector<int32_t>& hybridization,
    CutoffNeighborFinder& clusterFinder,
    int targetHybridization,
    int maxRingSize,
    int minRingSize,
    TaskProgress& progress)
{
    RingAnalysisResult result;
    result.ringSize.assign(particleCount, 0);
    result.smallestRingId.assign(particleCount, 0);
    result.ringMembership.assign(particleCount, 0);
    result.countBySize.assign(static_cast<size_t>(maxRingSize) + 1, 0);

    // Build a compact (CSR) adjacency list restricted to bonds between two atoms of the
    // target phase. Connected-component membership guarantees a ring can never cross between
    // clusters, so no separate per-cluster bookkeeping is needed here.
    std::vector<int64_t> adjStart(particleCount + 1, 0);
    for(size_t i = 0; i < particleCount; ++i) {
        if(hybridization[i] != targetHybridization) continue;
        int64_t degree = 0;
        for(CutoffNeighborFinder::Query q(clusterFinder, i); !q.atEnd(); q.next()) {
            if(hybridization[q.current()] == targetHybridization) ++degree;
        }
        adjStart[i + 1] = degree;
    }
    for(size_t i = 0; i < particleCount; ++i)
        adjStart[i + 1] += adjStart[i];
    std::vector<RingAdjEntry> adjFlat(adjStart.back());
    {
        std::vector<int64_t> cursor(adjStart.begin(), adjStart.end() - 1);
        for(size_t i = 0; i < particleCount; ++i) {
            if(hybridization[i] != targetHybridization) continue;
            for(CutoffNeighborFinder::Query q(clusterFinder, i); !q.atEnd(); q.next()) {
                const int64_t j = q.current();
                if(hybridization[j] != targetHybridization) continue;
                adjFlat[cursor[i]++] = RingAdjEntry{ j, q.pbcShift() };
            }
        }
    }
    this_task::throwIfCanceled();

    // Scratch state for the bounded BFS searches below, reused across every bond instead of
    // being reallocated per search. Entries are lazily invalidated via a monotonically
    // increasing generation counter rather than being cleared between searches, which would
    // cost O(particleCount) per bond on a graph with up to ~10^6 bonds.
    int32_t generation = 0;
    std::vector<int32_t> visitGen(particleCount, -1);
    std::vector<int32_t> depthOf(particleCount, 0);
    std::vector<Vector3I> shiftFromRoot(particleCount, Vector3I::Zero());
    std::vector<std::vector<std::pair<int32_t, Vector3I>>> preds(particleCount);

    // Checks whether `ring` (a simple cycle, listed in order) is a primitive/SP ring: for
    // every pair of ring atoms, no path through the *whole* graph is shorter than their
    // distance measured by walking around the ring. Only needs plain hop counts, so it shares
    // the same lazily-reset scratch arrays (a fresh generation, ignoring periodic shift).
    auto isPrimitiveRing = [&](const std::vector<int64_t>& ring) -> bool {
        const int L = static_cast<int>(ring.size());
        const int halfL = L / 2;
        for(int a = 0; a < L; ++a) {
            ++generation;
            std::vector<int64_t> frontier{ ring[a] };
            visitGen[ring[a]] = generation;
            depthOf[ring[a]] = 0;
            for(int depth = 1; depth <= halfL && !frontier.empty(); ++depth) {
                std::vector<int64_t> next;
                for(int64_t u : frontier) {
                    for(int64_t k = adjStart[u]; k < adjStart[u + 1]; ++k) {
                        const int64_t v = adjFlat[k].neighbor;
                        if(visitGen[v] != generation) {
                            visitGen[v] = generation;
                            depthOf[v] = depth;
                            next.push_back(v);
                        }
                    }
                }
                frontier.swap(next);
            }
            for(int b = 0; b < L; ++b) {
                if(b == a) continue;
                const int arcDist = std::min(std::abs(b - a), L - std::abs(b - a));
                if(arcDist > halfL) continue;
                const int64_t vb = ring[b];
                if(visitGen[vb] == generation && depthOf[vb] < arcDist)
                    return false; // A shortcut exists: this ring is the sum of two smaller ones.
            }
        }
        return true;
    };

    std::unordered_set<std::vector<int64_t>, RingKeyHash> seenRings;
    int64_t nextRingId = 1;
    int64_t edgesProcessed = 0;
    progress.setText(QStringLiteral("Detecting closed carbon rings"));
    progress.setMaximum(static_cast<qlonglong>(adjFlat.size()));

    for(size_t i = 0; i < particleCount; ++i) {
        if(hybridization[i] != targetHybridization) continue;
        for(int64_t k = adjStart[i]; k < adjStart[i + 1]; ++k) {
            const RingAdjEntry& edge = adjFlat[k];
            const int64_t j = edge.neighbor;

            // Process each physical bond exactly once. Ordinarily that just means i < j; a
            // bond from an atom to its own periodic image (i == j) is vanishingly rare but
            // handled too, by keeping only the instance with the lexicographically positive
            // shift.
            bool canonical = static_cast<int64_t>(i) < j;
            if(!canonical && static_cast<int64_t>(i) == j) {
                canonical = edge.shift.x() > 0
                    || (edge.shift.x() == 0 && (edge.shift.y() > 0 || (edge.shift.y() == 0 && edge.shift.z() > 0)));
            }
            if(!canonical) continue;

            ++edgesProcessed;
            progress.setValueIntermittent(edgesProcessed);

            // Bounded multi-predecessor BFS from i, forbidding the direct i-j bond itself, up
            // to the depth where the resulting cycle would exceed maxRingSize. All layers tied
            // for the first depth at which j is reached are kept, since Franzblau's criterion
            // requires checking every shortest alternative path, not an arbitrary one.
            ++generation;
            std::vector<int64_t> frontier{ static_cast<int64_t>(i) };
            visitGen[i] = generation;
            depthOf[i] = 0;
            shiftFromRoot[i] = Vector3I::Zero();
            const int maxDepth = maxRingSize - 1; // Ring size = path depth + 1 (the closing bond).
            int foundDepth = -1;
            for(int depth = 1; depth <= maxDepth && foundDepth < 0 && !frontier.empty(); ++depth) {
                std::vector<int64_t> next;
                for(int64_t u : frontier) {
                    for(int64_t k2 = adjStart[u]; k2 < adjStart[u + 1]; ++k2) {
                        const RingAdjEntry& ue = adjFlat[k2];
                        const int64_t v = ue.neighbor;
                        if(u == static_cast<int64_t>(i) && v == j && ue.shift == edge.shift)
                            continue; // Forbid using the direct bond itself as the very first step.
                        const Vector3I candidateShift = shiftFromRoot[u] + ue.shift;
                        if(visitGen[v] != generation) {
                            visitGen[v] = generation;
                            depthOf[v] = depth;
                            shiftFromRoot[v] = candidateShift;
                            preds[v].clear();
                            preds[v].emplace_back(static_cast<int32_t>(u), ue.shift);
                            next.push_back(v);
                        }
                        else if(depthOf[v] == depth && preds[v].size() < 64) {
                            preds[v].emplace_back(static_cast<int32_t>(u), ue.shift);
                        }
                    }
                }
                if(visitGen[j] == generation && depthOf[j] == depth)
                    foundDepth = depth;
                frontier.swap(next);
            }
            if(foundDepth < 0)
                continue; // No ring closes through this bond within maxRingSize.

            const int ringSize = foundDepth + 1;

            // Reconstruct every tied shortest path from i to j by walking the predecessor DAG
            // backwards from j, capped to keep highly symmetric local regions (e.g. diamond-
            // like sp3 cores) tractable. Each reconstructed path is only accepted as a genuine
            // ring if it closes in real, unwrapped space together with the direct bond.
            std::vector<std::vector<int64_t>> candidatePaths;
            std::vector<int64_t> stack;
            std::function<void(int64_t, Vector3I)> backtrack = [&](int64_t v, Vector3I accumulatedShift) {
                if(candidatePaths.size() >= 32) return;
                stack.push_back(v);
                if(v == static_cast<int64_t>(i)) {
                    if((accumulatedShift - edge.shift) == Vector3I::Zero()) {
                        candidatePaths.emplace_back(stack.rbegin(), stack.rend());
                    }
                }
                else {
                    for(const auto& [pred, shiftUsed] : preds[v]) {
                        if(candidatePaths.size() >= 32) break;
                        backtrack(pred, accumulatedShift + shiftUsed);
                    }
                }
                stack.pop_back();
            };
            backtrack(j, Vector3I::Zero());

            for(const std::vector<int64_t>& path : candidatePaths) {
                if(!isPrimitiveRing(path))
                    continue;
                std::vector<int64_t> canonical_ = canonicalizeRing(path);
                if(!seenRings.insert(canonical_).second)
                    continue; // Already counted, discovered from a different bond.
                if(ringSize < minRingSize)
                    continue; // Below the configured floor -- e.g. a triangle when only 4+ is wanted.

                const int64_t ringId = nextRingId++;
                result.countBySize[static_cast<size_t>(ringSize)] += 1;
                result.totalRings += 1;
                for(int64_t atom : path) {
                    result.ringMembership[atom] += 1;
                    if(result.ringSize[atom] == 0 || ringSize < result.ringSize[atom]) {
                        result.ringSize[atom] = ringSize;
                        result.smallestRingId[atom] = ringId;
                    }
                }
            }
        }
    }

    if(result.totalRings > 0) {
        int64_t weightedSum = 0;
        for(size_t size = 0; size < result.countBySize.size(); ++size)
            weightedSum += static_cast<int64_t>(size) * result.countBySize[size];
        result.meanRingSize = static_cast<double>(weightedSum) / static_cast<double>(result.totalRings);
    }

    return result;
}

}   // End of anonymous namespace

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonModifier);
OVITO_CLASSINFO(AmorphousCarbonModifier, "DisplayName", "Amorphous carbon cluster analysis");
OVITO_CLASSINFO(AmorphousCarbonModifier, "Description", "Determine carbon hybridization and identify connected sp2 or sp3 clusters.");
OVITO_CLASSINFO(AmorphousCarbonModifier, "ModifierCategory", "Analysis");

DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, coordinationCutoff);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, bondCutoff);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, clusterSpecies);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, minimumClusterSize);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, minSamePhaseNeighbors);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, colorMode);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, ringAnalysisEnabled);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, maxRingSize);
DEFINE_PROPERTY_FIELD(AmorphousCarbonModifier, minRingSize);

SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, coordinationCutoff, "Coordination cutoff");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, bondCutoff, "Cluster bond cutoff");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, clusterSpecies, "Cluster species");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, minimumClusterSize, "Minimum relevant cluster size");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, minSamePhaseNeighbors, "Minimum same-phase neighbors");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, colorMode, "Particle coloring");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, ringAnalysisEnabled, "Detect closed rings (SP/primitive)");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, maxRingSize, "Maximum ring size to search");
SET_PROPERTY_FIELD_LABEL(AmorphousCarbonModifier, minRingSize, "Minimum ring size to search");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, coordinationCutoff, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, bondCutoff, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, minimumClusterSize, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, minSamePhaseNeighbors, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, maxRingSize, IntegerParameterUnit, 3);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(AmorphousCarbonModifier, minRingSize, IntegerParameterUnit, 3);

bool AmorphousCarbonModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

void AmorphousCarbonModifier::preevaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineEvaluationResult::EvaluationTypes& evaluationTypes,
    TimeInterval& validityInterval) const
{
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

Future<PipelineFlowState> AmorphousCarbonModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& state)
{
    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();

    if(coordinationCutoff() <= 0)
        throw Exception(tr("The coordination cutoff must be positive."));
    if(bondCutoff() <= 0)
        throw Exception(tr("The cluster bond cutoff must be positive."));
    if(minimumClusterSize() < 1)
        throw Exception(tr("The minimum cluster size must be at least one."));
    if(minSamePhaseNeighbors() < 0)
        throw Exception(tr("The minimum number of same-phase neighbors cannot be negative."));
    if(maxRingSize() < 3)
        throw Exception(tr("The maximum ring size must be at least three."));
    if(minRingSize() < 3)
        throw Exception(tr("The minimum ring size must be at least three."));
    if(minRingSize() > maxRingSize())
        throw Exception(tr("The minimum ring size cannot exceed the maximum ring size."));

    // Selecting the ring-size coloring mode implies the (otherwise opt-in) ring search must
    // run, even if the checkbox itself is off -- there would be nothing to color by otherwise.
    const bool computeRings = ringAnalysisEnabled() || colorMode() == ColorByRingSize;

    // Reuse complete results for cheap interactive viewport refreshes.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(DataOORef<const Particles> cachedParticles = cachedState.getObject<Particles>()) {
                const Property* cachedHybridization = cachedParticles->getProperty(QStringLiteral("Hybridization"));
                const Property* cachedCoordination = cachedParticles->getProperty(QStringLiteral("Carbon Coordination"));
                const Property* cachedCluster = cachedParticles->getProperty(QStringLiteral("Carbon Cluster Identifier"));
                const Property* cachedSize = cachedParticles->getProperty(QStringLiteral("Carbon Cluster Size"));
                const Property* cachedRank = cachedParticles->getProperty(QStringLiteral("Carbon Cluster Rank"));
                const Property* cachedRelevant = cachedParticles->getProperty(QStringLiteral("Relevant Carbon Cluster"));
                const Property* cachedColor = colorMode() != NoColoring ? cachedParticles->getProperty(Particles::ColorProperty) : nullptr;
                const Property* cachedRingSize = computeRings ? cachedParticles->getProperty(QStringLiteral("Ring Size")) : nullptr;
                const Property* cachedRingId = computeRings ? cachedParticles->getProperty(QStringLiteral("Smallest Ring Identifier")) : nullptr;
                const Property* cachedRingMembership = computeRings ? cachedParticles->getProperty(QStringLiteral("Ring Membership Count")) : nullptr;
                if(cachedHybridization && cachedCoordination && cachedCluster && cachedSize && cachedRank && cachedRelevant
                   && (!computeRings || (cachedRingSize && cachedRingId && cachedRingMembership))) {
                    state.adoptAttributesFrom(cachedState, request.modificationNode());
                    return asyncLaunch([state = std::move(state), particles,
                        cachedHybridization, cachedCoordination, cachedCluster, cachedSize,
                        cachedRank, cachedRelevant, cachedColor,
                        cachedRingSize, cachedRingId, cachedRingMembership,
                        cachedParticles = std::move(cachedParticles)]() mutable {
                        particles->tryToAdoptProperties(cachedParticles,
                            {cachedHybridization, cachedCoordination, cachedCluster,
                             cachedSize, cachedRank, cachedRelevant, cachedColor,
                             cachedRingSize, cachedRingId, cachedRingMembership},
                            {particles});
                        return std::move(state);
                    });
                }
            }
        }
        return std::move(state);
    }

    const Property* positions = particles->expectProperty(Particles::PositionProperty);
    const SimulationCell* simulationCell = state.getObject<SimulationCell>();
    if(!simulationCell)
        throw Exception(tr("Amorphous carbon cluster analysis requires a simulation cell."));
    if(simulationCell->isDegenerate())
        throw Exception(tr("The simulation cell is degenerate."));

    return asyncLaunch([
        state = std::move(state),
        particles,
        positions = ConstPropertyPtr(positions),
        simulationCell = SimulationCellData(*simulationCell),
        coordinationCutoff = coordinationCutoff(),
        clusterBondCutoff = bondCutoff(),
        targetHybridization = static_cast<int>(clusterSpecies()),
        minimumSize = minimumClusterSize(),
        minSamePhase = minSamePhaseNeighbors(),
        coloring = colorMode(),
        computeRings,
        maxRingSize = maxRingSize(),
        minRingSize = minRingSize(),
        createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Analyzing amorphous carbon clusters"));

        const size_t particleCount = positions->size();
        // The two cutoffs describe different operations: the first-minimum cutoff
        // determines coordination/hybridization, the bond cutoff defines same-phase
        // cluster connectivity. They default to the same value, because a bond that
        // is good enough to make an atom sp3 is also a bond along which its cluster
        // should propagate. A shorter bond cutoff discards such bonds; the count of
        // discarded ones is tracked below and surfaced as a warning.
        CutoffNeighborFinder coordinationFinder(coordinationCutoff, positions, simulationCell, {});
        CutoffNeighborFinder clusterFinder(clusterBondCutoff, positions, simulationCell, {});
        const FloatType clusterBondCutoffSquared = clusterBondCutoff * clusterBondCutoff;
        const FloatType coordinationCutoffSquared = coordinationCutoff * coordinationCutoff;
        const bool connectivityIsStricter = clusterBondCutoff < coordinationCutoff;
        const bool connectivityIsLooser = clusterBondCutoff > coordinationCutoff;

        PropertyPtr coordination = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
            QStringLiteral("Carbon Coordination"));
        PropertyPtr hybridization = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
            QStringLiteral("Hybridization"));
        PropertyPtr clusterIds = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int64, 1,
            QStringLiteral("Carbon Cluster Identifier"));
        PropertyPtr clusterSizes = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int64, 1,
            QStringLiteral("Carbon Cluster Size"));
        PropertyPtr clusterRanks = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
            QStringLiteral("Carbon Cluster Rank"));
        PropertyPtr relevantClusters = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
            QStringLiteral("Relevant Carbon Cluster"));
        PropertyPtr samePhaseCoordination = Particles::OOClass().createUserProperty(
            DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
            QStringLiteral("Same-Phase Coordination"));

        BufferWriteAccess<int32_t, access_mode::discard_write> coordinationAcc(coordination);
        BufferWriteAccess<int32_t, access_mode::discard_write> hybridizationAcc(hybridization);
        BufferWriteAccess<int64_t, access_mode::discard_write> clusterAcc(clusterIds);
        BufferWriteAccess<int64_t, access_mode::discard_write> sizeAcc(clusterSizes);
        BufferWriteAccess<int32_t, access_mode::discard_write> rankAcc(clusterRanks);
        BufferWriteAccess<int32_t, access_mode::discard_write> relevantAcc(relevantClusters);

        size_t nSp = 0, nSp2 = 0, nSp3 = 0, nOther = 0;
        // Number of atoms of the tracked phase that have at least one bond counted
        // towards their hybridization but ignored when building clusters. Zero
        // whenever the two cutoffs agree, which is the default.
        size_t atomsWithDiscardedBonds = 0;
        // The mirror-image mistake: a bond cutoff LONGER than the coordination cutoff
        // propagates clusters along bonds that never counted towards any atom's
        // hybridization, so the graph being clustered is not the graph that defined the
        // phases. It also lets an atom have more same-phase neighbors than its own
        // coordination number, which makes the core filter incoherent.
        size_t atomsWithUncountedBonds = 0;
        progress.setMaximum(particleCount * 2);
        for(size_t i = 0; i < particleCount; ++i) {
            progress.setValueIntermittent(i);
            int cn = 0;
            bool hasDiscardedBond = false;
            for(CutoffNeighborFinder::Query q(coordinationFinder, i); !q.atEnd(); q.next()) {
                ++cn;
                if(connectivityIsStricter && q.distanceSquared() > clusterBondCutoffSquared)
                    hasDiscardedBond = true;
            }
            coordinationAcc[i] = cn;

            // 0 = other/undercoordinated, 1 = sp, 2 = sp2, 3 = sp3.
            int hyb = 0;
            if(cn == 2) { hyb = 1; ++nSp; }
            else if(cn == 3) { hyb = 2; ++nSp2; }
            else if(cn == 4) { hyb = 3; ++nSp3; }
            else ++nOther;
            hybridizationAcc[i] = hyb;
            if(hasDiscardedBond && hyb == targetHybridization)
                ++atomsWithDiscardedBonds;
            clusterAcc[i] = (hyb == targetHybridization) ? -1 : 0;
            sizeAcc[i] = 0;
            rankAcc[i] = 0;
            relevantAcc[i] = 0;
        }
        this_task::throwIfCanceled();

        // Optionally restrict clustering to the well-embedded core of the phase.
        // In a network far above the percolation threshold, clustering every atom of
        // the phase is degenerate: the largest component stays at ~99% of the phase no
        // matter how much damage accumulates. Dropping atoms with fewer than
        // minSamePhase same-phase neighbors clusters the core instead, which does
        // fragment. Counted with the connectivity cutoff, so it describes the same
        // graph the clustering walks.
        // How deeply each atom sits inside its own phase: of its neighbors, how many
        // share its hybridization. This is the most damage-sensitive per-atom quantity
        // available here -- on camorfo_sp3_60_mCRA the population with all four
        // neighbors sp3 halves between cycle 0 and 100, against 4% for the connected
        // component -- so it is always computed and exported, not just when the core
        // filter needs it. Counted with the connectivity cutoff, so it describes the
        // same graph the clustering walks.
        BufferWriteAccess<int32_t, access_mode::discard_write> samePhaseAcc(samePhaseCoordination);
        for(size_t i = 0; i < particleCount; ++i) {
            const bool inPhase = hybridizationAcc[i] == targetHybridization;
            if(!inPhase && !connectivityIsLooser) {
                samePhaseAcc[i] = 0;
                continue;
            }
            int count = 0;
            bool hasUncountedBond = false;
            for(CutoffNeighborFinder::Query q(clusterFinder, i); !q.atEnd(); q.next()) {
                if(inPhase && hybridizationAcc[q.current()] == targetHybridization)
                    ++count;
                if(connectivityIsLooser && q.distanceSquared() > coordinationCutoffSquared)
                    hasUncountedBond = true;
            }
            samePhaseAcc[i] = inPhase ? count : 0;
            // Counted over every atom, not just the phase: an atom holding a bond in the
            // gap between the two cutoffs is in a locally stretched environment, and the
            // shorter coordination cutoff is exactly what pushes it out of the phase. On
            // camorfo_sp3_60_mCRA at coord 1.9 / bond 2.1, zero sp3 atoms carry such a
            // bond while 20336 atoms overall do -- restricting the count to the phase
            // would report "no problem" precisely when the problem is worst.
            if(hasUncountedBond)
                ++atomsWithUncountedBonds;
        }
        this_task::throwIfCanceled();

        // Optionally restrict clustering to the well-embedded core of the phase.
        // In a network far above the percolation threshold, clustering every atom of
        // the phase is degenerate: the largest component stays at ~99% of the phase no
        // matter how much damage accumulates. Dropping atoms with fewer than
        // minSamePhase same-phase neighbors clusters the core instead, which does
        // fragment.
        size_t coreAtomCount = 0;
        for(size_t i = 0; i < particleCount; ++i) {
            if(clusterAcc[i] != -1)
                continue;
            if(minSamePhase > 0 && samePhaseAcc[i] < minSamePhase)
                clusterAcc[i] = 0;
            else
                ++coreAtomCount;
        }

        std::vector<int64_t> sizes(1, 0); // Cluster ID 0 means not in target set.
        // A cluster percolates when the walk reaches an already-visited atom through a
        // different periodic image: that means the cluster connects to its own image
        // across the cell, which is the real test for a spanning network. Tracking the
        // integer image offset during the walk is exact and costs one vector per atom.
        std::vector<Vector3I> imageOffset(particleCount, Vector3I::Zero());
        // Position of each atom relative to the seed of its own cluster, accumulated along the
        // BFS tree so that a cluster crossing a periodic boundary stays contiguous. Same idiom
        // as OVITO's own ClusterAnalysisModifier ("unwrap particle coordinates"), which avoids
        // having to reconstruct anything from the cell matrix. Needed for the radius of
        // gyration below; the integer offsets above stay the percolation test, because an
        // exact integer comparison cannot be fooled by rounding.
        std::vector<Vector3> unwrappedDelta(particleCount, Vector3::Zero());
        std::vector<char> clusterPercolates(1, 0);
        std::deque<size_t> queue;
        size_t visitedSinceCancelCheck = 0;
        constexpr size_t cancelCheckInterval = 8192;
        for(size_t seed = 0; seed < particleCount; ++seed) {
            if(clusterAcc[seed] != -1)
                continue;
            const int64_t id = static_cast<int64_t>(sizes.size());
            sizes.push_back(0);
            clusterPercolates.push_back(0);
            clusterAcc[seed] = id;
            imageOffset[seed] = Vector3I::Zero();
            unwrappedDelta[seed] = Vector3::Zero();
            queue.push_back(seed);
            while(!queue.empty()) {
                const size_t current = queue.front();
                queue.pop_front();
                ++sizes[id];
                for(CutoffNeighborFinder::Query q(clusterFinder, current); !q.atEnd(); q.next()) {
                    const size_t neighbor = q.current();
                    const Vector3I neighborOffset = imageOffset[current] + q.pbcShift();
                    if(clusterAcc[neighbor] == -1) {
                        clusterAcc[neighbor] = id;
                        imageOffset[neighbor] = neighborOffset;
                        unwrappedDelta[neighbor] = unwrappedDelta[current] + q.delta();
                        queue.push_back(neighbor);
                    }
                    else if(clusterAcc[neighbor] == id && neighborOffset != imageOffset[neighbor]) {
                        clusterPercolates[id] = 1;
                    }
                }
                // A single percolating cluster (e.g. the sp3 matrix) can hold hundreds
                // of thousands of atoms; without a check inside this inner loop the task
                // stays uncancelable for the whole BFS, not just between clusters.
                if(++visitedSinceCancelCheck >= cancelCheckInterval) {
                    visitedSinceCancelCheck = 0;
                    this_task::throwIfCanceled();
                }
            }
            progress.setValueIntermittent(particleCount + seed);
        }
        this_task::throwIfCanceled();

        const size_t numClusters = sizes.size() - 1;
        std::vector<int64_t> order(numClusters);
        std::iota(order.begin(), order.end(), int64_t(1));
        std::ranges::sort(order, [&](int64_t a, int64_t b) {
            if(sizes[a] != sizes[b]) return sizes[a] > sizes[b];
            return a < b;
        });

        std::vector<int32_t> rankById(sizes.size(), 0);
        size_t relevantCount = 0;
        size_t clusteredAtoms = 0;
        size_t relevantClusteredAtoms = 0;
        for(size_t rank = 0; rank < order.size(); ++rank) {
            rankById[order[rank]] = static_cast<int32_t>(rank + 1);
            // "Clustered" has a stable scientific meaning independent of the
            // user-selected relevance threshold: a singleton is not clustered.
            if(sizes[order[rank]] >= 2)
                clusteredAtoms += sizes[order[rank]];
            if(sizes[order[rank]] >= minimumSize) {
                ++relevantCount;
                relevantClusteredAtoms += sizes[order[rank]];
            }
        }

        for(size_t i = 0; i < particleCount; ++i) {
            const int64_t id = clusterAcc[i];
            if(id > 0) {
                sizeAcc[i] = sizes[id];
                rankAcc[i] = rankById[id];
                relevantAcc[i] = sizes[id] >= minimumSize ? 1 : 0;
            }
        }
        this_task::throwIfCanceled();

        // Radius of gyration of the largest cluster, from the BFS-unwrapped coordinates.
        // Two passes (centroid, then spread) rather than the sum-of-squares shortcut, which
        // loses precision when the centroid is far from the origin -- it is here, since the
        // deltas are relative to an arbitrary seed atom.
        //
        // IMPORTANT: while the largest cluster percolates, this number is not a cluster size.
        // A spanning cluster connects to its own periodic image, so no unwrapping of it is
        // well-defined (that is exactly what the percolation test detects), and the value
        // saturates at the scale of the cell: ~74 A of a ~160 A cell on camorfo_sp3_60_mCRA,
        // i.e. "fills the box". It only measures the cluster once
        // AmorphousCarbon.largest_cluster_percolates is 0. The value stays reproducible in
        // both regimes, because the BFS is deterministic.
        double radiusOfGyration = 0.0;
        if(!order.empty()) {
            const int64_t largestId = order.front();
            Vector_3<double> centroid = Vector_3<double>::Zero();
            size_t n = 0;
            for(size_t i = 0; i < particleCount; ++i) {
                if(clusterAcc[i] != largestId) continue;
                centroid += Vector_3<double>(unwrappedDelta[i].x(), unwrappedDelta[i].y(), unwrappedDelta[i].z());
                ++n;
            }
            if(n > 0) {
                centroid /= static_cast<double>(n);
                double sumSquares = 0.0;
                for(size_t i = 0; i < particleCount; ++i) {
                    if(clusterAcc[i] != largestId) continue;
                    const Vector_3<double> d = Vector_3<double>(
                        unwrappedDelta[i].x(), unwrappedDelta[i].y(), unwrappedDelta[i].z()) - centroid;
                    sumSquares += d.squaredLength();
                }
                radiusOfGyration = std::sqrt(sumSquares / static_cast<double>(n));
            }
        }
        // Size of the second-largest cluster. In percolation this is the susceptibility-like
        // observable: it peaks at the transition, where the spanning cluster is breaking up and
        // the runner-up is at its largest. Unlike the boolean spanning flag it is continuous and
        // does not flicker, which makes it the better locator of the transition band.
        const int64_t secondLargestSize = order.size() >= 2 ? sizes[order[1]] : 0;
        this_task::throwIfCanceled();

        // Optional closed-ring detection over the same-phase bond graph -- see
        // findPrimitiveRings() above and "Ring statistics" in AMORPHOUS_CARBON_MODIFIER.md.
        // Deliberately independent of the min-same-phase-neighbors core filter: an atom the
        // core filter drops for percolation purposes can still legitimately close a ring.
        std::optional<RingAnalysisResult> ringResult;
        if(computeRings) {
            std::vector<int32_t> hybridizationSnapshot(particleCount);
            for(size_t i = 0; i < particleCount; ++i)
                hybridizationSnapshot[i] = hybridizationAcc[i];
            ringResult = findPrimitiveRings(particleCount, hybridizationSnapshot, clusterFinder,
                targetHybridization, maxRingSize, minRingSize, progress);
        }
        this_task::throwIfCanceled();

        coordinationAcc.reset();
        hybridizationAcc.reset();
        clusterAcc.reset();
        sizeAcc.reset();
        rankAcc.reset();
        relevantAcc.reset();
        samePhaseAcc.reset();

        particles->createProperty(coordination);
        particles->createProperty(hybridization);
        particles->createProperty(clusterIds);
        particles->createProperty(clusterSizes);
        particles->createProperty(clusterRanks);
        particles->createProperty(relevantClusters);
        particles->createProperty(samePhaseCoordination);

        PropertyPtr ringSizes, ringIds, ringMembership;
        if(computeRings) {
            ringSizes = Particles::OOClass().createUserProperty(
                DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
                QStringLiteral("Ring Size"));
            ringIds = Particles::OOClass().createUserProperty(
                DataBuffer::Uninitialized, particleCount, Property::Int64, 1,
                QStringLiteral("Smallest Ring Identifier"));
            ringMembership = Particles::OOClass().createUserProperty(
                DataBuffer::Uninitialized, particleCount, Property::Int32, 1,
                QStringLiteral("Ring Membership Count"));
            {
                BufferWriteAccess<int32_t, access_mode::discard_write> ringSizeAcc(ringSizes);
                BufferWriteAccess<int64_t, access_mode::discard_write> ringIdAcc(ringIds);
                BufferWriteAccess<int32_t, access_mode::discard_write> ringMembershipAcc(ringMembership);
                for(size_t i = 0; i < particleCount; ++i) {
                    ringSizeAcc[i] = ringResult->ringSize[i];
                    ringIdAcc[i] = static_cast<int64_t>(ringResult->smallestRingId[i]);
                    ringMembershipAcc[i] = ringResult->ringMembership[i];
                }
            }
            particles->createProperty(ringSizes);
            particles->createProperty(ringIds);
            particles->createProperty(ringMembership);
        }

        if(coloring != NoColoring) {
            BufferReadAccess<int32_t> hybridizationRead(hybridization);
            BufferReadAccess<int64_t> clusterRead(clusterIds);
            BufferReadAccess<int64_t> sizeRead(clusterSizes);
            BufferReadAccess<int32_t> samePhaseRead(samePhaseCoordination);
            BufferWriteAccess<ColorG, access_mode::discard_write> colors =
                particles->createProperty(Particles::ColorProperty);
            // Only constructed when actually needed: the ring properties above only exist
            // when computeRings is true, which selecting this color mode already guarantees.
            std::unique_ptr<BufferReadAccess<int32_t>> ringSizeReadPtr;
            if(coloring == ColorByRingSize)
                ringSizeReadPtr = std::make_unique<BufferReadAccess<int32_t>>(ringSizes);

            const double largest = order.empty() ? 1.0 : static_cast<double>(sizes[order.front()]);
            for(size_t i = 0; i < particleCount; ++i) {
                if(coloring == ColorByHybridization) {
                    switch(hybridizationRead[i]) {
                        case 1: colors[i] = kIndigo; break;    // sp
                        case 2: colors[i] = kAmber; break;     // sp2, warm: graphitic
                        case 3: colors[i] = kSkyBlue; break;   // sp3, cool: diamond-like matrix
                        default: colors[i] = kGraphite; break; // undercoordinated / other
                    }
                }
                else if(coloring == ColorBySamePhaseCoordination) {
                    // How embedded each atom is in its own phase, 0 to 4. Sequential
                    // ramp, dark = deep inside the network, light = at its edge; atoms
                    // outside the phase stay neutral. This is the spatial map of where
                    // the network erodes, which the cluster picture cannot show while
                    // the phase still percolates.
                    if(hybridizationRead[i] != targetHybridization)
                        colors[i] = kGraphite;
                    else {
                        const GraphicsFloatType f = std::min(GraphicsFloatType(1),
                            static_cast<GraphicsFloatType>(samePhaseRead[i]) / GraphicsFloatType(4));
                        colors[i] = oklchToColor(GraphicsFloatType(0.74) - GraphicsFloatType(0.30) * f,
                                                 GraphicsFloatType(0.13), GraphicsFloatType(250));
                    }
                }
                else if(coloring == ColorByRingSize) {
                    // Ring membership is deliberately independent of the min-same-phase-
                    // neighbors core filter (a ring can pass through an atom the filter
                    // drops for percolation purposes), so this reads Ring Size directly
                    // rather than falling back through the clusterRead[i] == 0 check below.
                    const int32_t ringSize = (*ringSizeReadPtr)[i];
                    if(ringSize <= 0) {
                        colors[i] = kGraphite;
                    }
                    else {
                        // Sequential ramp across the searched size range, light (small
                        // rings) to dark (large ones), as a magnitude encoding requires.
                        // Warm hue keeps it visually distinct from the cool cluster-size ramp.
                        const GraphicsFloatType span = static_cast<GraphicsFloatType>(std::max(1, maxRingSize - 3));
                        const GraphicsFloatType fraction = std::clamp(
                            static_cast<GraphicsFloatType>(ringSize - 3) / span, GraphicsFloatType(0), GraphicsFloatType(1));
                        colors[i] = oklchToColor(GraphicsFloatType(0.74) - GraphicsFloatType(0.34) * fraction,
                                                 GraphicsFloatType(0.14), GraphicsFloatType(30));
                    }
                }
                else if(clusterRead[i] == 0) {
                    // Atoms outside the tracked phase. Dark and neutral so they recede
                    // without washing out against a light viewport background.
                    colors[i] = kGraphite;
                }
                else if(coloring == ColorByCluster) {
                    // Golden-ratio hue stepping gives deterministic, well-separated colors.
                    // Working in OKLCH rather than HSV keeps every cluster equally legible:
                    // no hue lands on a pale near-white yellow the way constant-value HSV does.
                    //
                    // With thousands of clusters no palette can make every pair identifiable
                    // by color -- this encoding only has to separate neighbouring clusters.
                    // Hue alone cannot do that for a colorblind reader, because dichromatic
                    // vision collapses the hue circle onto one axis, so consecutive IDs also
                    // alternate in lightness. That secondary encoding lifts the worst
                    // consecutive-pair separation from dE 1.7 to 8.8 under deuteranopia while
                    // every step stays above 3:1 contrast on a white background.
                    const GraphicsFloatType hue = std::fmod(
                        static_cast<GraphicsFloatType>(clusterRead[i]) * GraphicsFloatType(0.61803398875), GraphicsFloatType(1))
                        * GraphicsFloatType(360);
                    const GraphicsFloatType lightness = (clusterRead[i] % 2 == 0)
                        ? GraphicsFloatType(0.64) : GraphicsFloatType(0.48);
                    colors[i] = oklchToColor(lightness, GraphicsFloatType(0.105), hue);
                }
                else {
                    // Single-hue sequential ramp, light to dark, as magnitude ramps require.
                    // The light end stops well short of white (2.5:1 against it).
                    const GraphicsFloatType fraction = static_cast<GraphicsFloatType>(
                        std::sqrt(static_cast<double>(sizeRead[i]) / largest));
                    colors[i] = oklchToColor(GraphicsFloatType(0.72) - GraphicsFloatType(0.34) * fraction,
                                             GraphicsFloatType(0.13),
                                             GraphicsFloatType(250));
                }
            }
        }

        const size_t largestSize = order.empty() ? 0 : sizes[order.front()];
        const double atomCount = static_cast<double>(particleCount);
        const double spPercent = atomCount > 0 ? 100.0 * nSp / atomCount : 0.0;
        const double sp2Percent = atomCount > 0 ? 100.0 * nSp2 / atomCount : 0.0;
        const double sp3Percent = atomCount > 0 ? 100.0 * nSp3 / atomCount : 0.0;
        const double otherPercent = atomCount > 0 ? 100.0 * nOther / atomCount : 0.0;
        const double targetCount = targetHybridization == 2 ? static_cast<double>(nSp2) : static_cast<double>(nSp3);
        const double largestFraction = targetCount > 0 ? largestSize / targetCount : 0.0;
        const double clusteredFraction = targetCount > 0 ? clusteredAtoms / targetCount : 0.0;
        const double relevantClusteredFraction = targetCount > 0 ? relevantClusteredAtoms / targetCount : 0.0;

        // Distribution-of-sizes statistics and the Robertson gap estimate, the remaining metrics
        // ported from the reference Python pipeline (ac_analyzer.analysis.sp2_clusters.
        // sp2_cluster_metrics; see "Remaining metrics from the Python pipeline" in
        // AMORPHOUS_CARBON_MODIFIER.md). Computed over the same "sizes" array as `order` above --
        // every connected component of the target phase, not filtered by the relevance threshold
        // -- matching the Python reference's scope exactly.
        double meanClusterSize = 0.0, medianClusterSize = 0.0, clusterSizeP90 = 0.0;
        if(numClusters > 0) {
            std::vector<int64_t> sortedSizes(sizes.begin() + 1, sizes.end());
            std::ranges::sort(sortedSizes);
            double sum = 0.0;
            for(int64_t s : sortedSizes)
                sum += static_cast<double>(s);
            meanClusterSize = sum / static_cast<double>(numClusters);
            // np.median: average of the two middle elements for an even-length array.
            if(numClusters % 2 == 1)
                medianClusterSize = static_cast<double>(sortedSizes[numClusters / 2]);
            else
                medianClusterSize = 0.5 * static_cast<double>(sortedSizes[numClusters / 2 - 1] + sortedSizes[numClusters / 2]);
            // np.percentile default: linear interpolation between the two closest ranks.
            const double h = static_cast<double>(numClusters - 1) * 0.90;
            const size_t lo = static_cast<size_t>(std::floor(h));
            const size_t hi = std::min(lo + 1, numClusters - 1);
            const double frac = h - static_cast<double>(lo);
            clusterSizeP90 = static_cast<double>(sortedSizes[lo]) + frac * static_cast<double>(sortedSizes[hi] - sortedSizes[lo]);
        }
        const double cellVolume = simulationCell.volume3D();
        const double clusterNumberDensity = cellVolume > 0 ? static_cast<double>(numClusters) / cellVolume : 0.0;
        // Robertson & O'Reilly, Phys. Rev. B 35, 2946 (1987): E_g ~ 2|beta|/sqrt(M) for compact
        // graphitic clusters of fused six-membered rings, M = number of rings. Converts the
        // largest cluster's size in atoms to rings assuming ~2 sp2 atoms per ring (a compact
        // graphene-like sheet), matching the reference Python's estimate_gap_eV exactly. A
        // heuristic upper bound on the gap, not a DFT-level estimate -- it assumes compact
        // sheets, not chains or isolated rings. Zero below the smallest cluster that can define
        // a ring at all.
        constexpr double robertsonBetaEV = 2.9;
        constexpr double robertsonAtomsPerRing = 2.0;
        const double nRings = static_cast<double>(largestSize) / robertsonAtomsPerRing;
        const double gapEstimateEV = nRings >= 1.0 ? (2.0 * robertsonBetaEV / std::sqrt(nRings)) : 0.0;

        state.addAttribute(QStringLiteral("AmorphousCarbon.sp_count"), QVariant::fromValue(nSp), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.sp2_count"), QVariant::fromValue(nSp2), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.sp3_count"), QVariant::fromValue(nSp3), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.other_count"), QVariant::fromValue(nOther), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.sp_percent"), QVariant::fromValue(spPercent), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.sp2_percent"), QVariant::fromValue(sp2Percent), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.sp3_percent"), QVariant::fromValue(sp3Percent), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.other_percent"), QVariant::fromValue(otherPercent), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.cluster_count"), QVariant::fromValue(numClusters), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.relevant_cluster_count"), QVariant::fromValue(relevantCount), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.largest_cluster_size"), QVariant::fromValue(largestSize), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.largest_cluster_fraction"), QVariant::fromValue(largestFraction), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.clustered_fraction"), QVariant::fromValue(clusteredFraction), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.relevant_clustered_fraction"), QVariant::fromValue(relevantClusteredFraction), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.coordination_cutoff"), QVariant::fromValue(coordinationCutoff), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.cluster_bond_cutoff"), QVariant::fromValue(clusterBondCutoff), createdByNode);
        // Compatibility alias used by output pipelines created with the first version.
        state.addAttribute(QStringLiteral("AmorphousCarbon.bond_cutoff"), QVariant::fromValue(clusterBondCutoff), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.atoms_with_discarded_bonds"), QVariant::fromValue(atomsWithDiscardedBonds), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.atoms_with_uncounted_bonds"), QVariant::fromValue(atomsWithUncountedBonds), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.min_same_phase_neighbors"), QVariant::fromValue(minSamePhase), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.clustered_atom_count"), QVariant::fromValue(coreAtomCount), createdByNode);
        const bool largestPercolates = !order.empty() && clusterPercolates[order.front()] != 0;
        state.addAttribute(QStringLiteral("AmorphousCarbon.largest_cluster_percolates"), QVariant::fromValue(largestPercolates ? 1 : 0), createdByNode);
        // Fraction of the largest cluster over the atoms actually clustered, which is the
        // percolation order parameter when a core filter is in use.
        const double coreLargestFraction = coreAtomCount > 0 ? largestSize / static_cast<double>(coreAtomCount) : 0.0;
        state.addAttribute(QStringLiteral("AmorphousCarbon.largest_cluster_core_fraction"), QVariant::fromValue(coreLargestFraction), createdByNode);
        // Counterpart of clustered_fraction over the atoms the core filter actually admitted.
        // clustered_fraction divides by the whole phase, so with a filter on it drops for a
        // reason that is not fragmentation: atoms that were never eligible are indistinguishable
        // there from atoms that ended up isolated. Both are kept, under names that say which is
        // which, rather than silently redefining the older one when a parameter changes.
        const double coreClusteredFraction = coreAtomCount > 0 ? clusteredAtoms / static_cast<double>(coreAtomCount) : 0.0;
        state.addAttribute(QStringLiteral("AmorphousCarbon.clustered_core_fraction"), QVariant::fromValue(coreClusteredFraction), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.second_largest_cluster_size"), QVariant::fromValue(secondLargestSize), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.largest_cluster_radius_of_gyration"), QVariant::fromValue(radiusOfGyration), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.mean_cluster_size"), QVariant::fromValue(meanClusterSize), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.median_cluster_size"), QVariant::fromValue(medianClusterSize), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.cluster_size_p90"), QVariant::fromValue(clusterSizeP90), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.cluster_number_density"), QVariant::fromValue(clusterNumberDensity), createdByNode);
        state.addAttribute(QStringLiteral("AmorphousCarbon.gap_estimate_eV"), QVariant::fromValue(gapEstimateEV), createdByNode);

        state.addAttribute(QStringLiteral("AmorphousCarbon.ring_analysis_enabled"), QVariant::fromValue(computeRings ? 1 : 0), createdByNode);
        if(computeRings) {
            state.addAttribute(QStringLiteral("AmorphousCarbon.ring_max_size_searched"), QVariant::fromValue(maxRingSize), createdByNode);
            state.addAttribute(QStringLiteral("AmorphousCarbon.ring_min_size_searched"), QVariant::fromValue(minRingSize), createdByNode);
            state.addAttribute(QStringLiteral("AmorphousCarbon.ring_count_total"), QVariant::fromValue(ringResult->totalRings), createdByNode);
            state.addAttribute(QStringLiteral("AmorphousCarbon.ring_mean_size"), QVariant::fromValue(ringResult->meanRingSize), createdByNode);
            for(int size = 3; size <= maxRingSize; ++size) {
                state.addAttribute(QStringLiteral("AmorphousCarbon.ring_count_%1").arg(size),
                    QVariant::fromValue(ringResult->countBySize[static_cast<size_t>(size)]), createdByNode);
            }
        }

        const QString phaseName = targetHybridization == 2 ? QStringLiteral("sp2") : QStringLiteral("sp3");
        QString longText = tr("Composition: sp %1% (~%2 atoms), sp2 %3% (~%4 atoms), "
               "sp3 %5% (~%6 atoms), other %7% (~%8 atoms).\n"
               "Found %9 relevant %10 cluster(s); largest contains ~%11 atoms.")
                .arg(spPercent, 0, 'f', 2).arg(nSp)
                .arg(sp2Percent, 0, 'f', 2).arg(nSp2)
                .arg(sp3Percent, 0, 'f', 2).arg(nSp3)
                .arg(otherPercent, 0, 'f', 2).arg(nOther)
                .arg(relevantCount)
                .arg(phaseName)
                .arg(largestSize);
        QString shortText = tr("sp2 %1% (~%2), sp3 %3% (~%4); %5 relevant %6 clusters.")
                .arg(sp2Percent, 0, 'f', 2).arg(nSp2)
                .arg(sp3Percent, 0, 'f', 2).arg(nSp3)
                .arg(relevantCount)
                .arg(phaseName);

        if(computeRings) {
            longText += tr("\nFound %1 primitive ring(s) of size %2-%3 (mean size %4).")
                    .arg(ringResult->totalRings)
                    .arg(minRingSize)
                    .arg(maxRingSize)
                    .arg(ringResult->meanRingSize, 0, 'f', 2);
            shortText += tr(" %1 rings (%2-%3).").arg(ringResult->totalRings).arg(minRingSize).arg(maxRingSize);
        }

        // With a core filter on, the whole-phase fractions fall for two reasons at once and
        // stop being comparable with a run that had no filter. Say so where a GUI user reads
        // the numbers, not only in the documentation.
        if(minSamePhase > 0) {
            longText += tr("\nCore filter: only atoms with at least %1 same-phase neighbors are "
                           "clustered, %2 of %3 %4 atoms (%5%). Note that largest_cluster_fraction "
                           "and clustered_fraction still divide by the whole phase; the "
                           "corresponding core quantities are largest_cluster_core_fraction "
                           "(%6) and clustered_core_fraction (%7).")
                    .arg(minSamePhase)
                    .arg(coreAtomCount)
                    .arg(static_cast<qlonglong>(targetCount))
                    .arg(phaseName)
                    .arg(targetCount > 0 ? 100.0 * coreAtomCount / targetCount : 0.0, 0, 'f', 1)
                    .arg(coreLargestFraction, 0, 'f', 4)
                    .arg(coreClusteredFraction, 0, 'f', 4);
        }

        // A bond cutoff below the coordination cutoff breaks the network along bonds
        // that were good enough to define the hybridization in the first place, which
        // inflates cluster counts and makes them very sensitive to the exact value.
        // Say so rather than reporting the inflated numbers as if they were robust.
        PipelineStatus::StatusType statusType = PipelineStatus::Success;
        if(connectivityIsLooser && atomsWithUncountedBonds > 0) {
            statusType = PipelineStatus::Warning;
            const double affectedPercent = atomCount > 0 ? 100.0 * atomsWithUncountedBonds / atomCount : 0.0;
            const QString warning = tr("\nCluster bond cutoff (%1) is longer than the coordination cutoff (%2): "
                                       "%3 atoms (%4% of all) hold a bond in the gap between them. Those bonds "
                                       "connect clusters without ever counting towards any atom's hybridization, "
                                       "and the shorter coordination cutoff under-counts exactly those atoms, "
                                       "demoting them out of the phase. Set the two cutoffs equal.")
                    .arg(clusterBondCutoff, 0, 'f', 3)
                    .arg(coordinationCutoff, 0, 'f', 3)
                    .arg(atomsWithUncountedBonds)
                    .arg(affectedPercent, 0, 'f', 1);
            longText += warning;
            shortText += tr(" Bond cutoff longer than coordination cutoff.");
        }
        if(connectivityIsStricter && atomsWithDiscardedBonds > 0) {
            statusType = PipelineStatus::Warning;
            const double affectedPercent = targetCount > 0 ? 100.0 * atomsWithDiscardedBonds / targetCount : 0.0;
            const QString warning = tr("\nCluster bond cutoff (%1) is shorter than the coordination cutoff (%2): "
                                       "%3 %4 atoms (%5% of the phase) are connected through fewer bonds than "
                                       "determined their hybridization, which inflates the cluster count. "
                                       "Set the bond cutoff to the coordination cutoff unless a stricter "
                                       "connectivity criterion is intended.")
                    .arg(clusterBondCutoff, 0, 'f', 3)
                    .arg(coordinationCutoff, 0, 'f', 3)
                    .arg(atomsWithDiscardedBonds)
                    .arg(phaseName)
                    .arg(affectedPercent, 0, 'f', 1);
            longText += warning;
            shortText += tr(" Bond cutoff shorter than coordination cutoff.");
        }

        PipelineStatus status(longText, shortText);
        status.setType(statusType);
        state.setStatus(std::move(status));
        return std::move(state);
    });
}

} // End of namespace
