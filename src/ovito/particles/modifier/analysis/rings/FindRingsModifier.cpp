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
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshBuilder.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include <boost/container/small_vector.hpp>
#include "FindRingsModifier.h"

#include <set>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(FindRingsModifier);
OVITO_CLASSINFO(FindRingsModifier, "DisplayName", "Find rings (SIMAF)");
OVITO_CLASSINFO(FindRingsModifier, "Description", "Identify and visualize the shortest-path rings formed by bonds.");
OVITO_CLASSINFO(FindRingsModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(FindRingsModifier, minRingSize);
DEFINE_PROPERTY_FIELD(FindRingsModifier, maxRingSize);
DEFINE_PROPERTY_FIELD(FindRingsModifier, includeWrappingRings);
DEFINE_REFERENCE_FIELD(FindRingsModifier, ringsVis);
SET_PROPERTY_FIELD_LABEL(FindRingsModifier, minRingSize, "Minimum ring size");
SET_PROPERTY_FIELD_LABEL(FindRingsModifier, maxRingSize, "Maximum ring size");
SET_PROPERTY_FIELD_LABEL(FindRingsModifier, includeWrappingRings, "Include rings wrapping around periodic boundaries");
SET_PROPERTY_FIELD_LABEL(FindRingsModifier, ringsVis, "Rings");
SET_PROPERTY_FIELD_UNITS_AND_RANGE(FindRingsModifier, minRingSize, IntegerParameterUnit, 3, FindRingsModifier::MaxSupportedRingSize);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(FindRingsModifier, maxRingSize, IntegerParameterUnit, 3, FindRingsModifier::MaxSupportedRingSize);

namespace {

/// A node of the periodic bond network: a particle together with the periodic image it lies in.
struct State {
    size_t particle;
    Vector3I image;
};

/// Packs a state into a hash key (image components are limited to [-127, 127]).
inline uint64_t stateKey(size_t particle, const Vector3I& image)
{
    return (uint64_t(particle) << 24) | (uint64_t((image.x() + 128) & 0xFF) << 16) | (uint64_t((image.y() + 128) & 0xFF) << 8) | uint64_t((image.z() + 128) & 0xFF);
}

using Adjacency = std::vector<boost::container::small_vector<std::pair<size_t, Vector3I>, 6>>;

/// Breadth-first search over the periodic network starting at one state, up to a given depth.
struct BoundedBFS {
    struct Visited {
        State state;
        int distance;
        boost::container::small_vector<uint32_t, 4> predecessors;   // Indices into 'visited' at distance - 1.
    };
    std::vector<Visited> visited;
    std::unordered_map<uint64_t, uint32_t> index;

    void run(const Adjacency& adjacency, const State& start, int maxDepth) {
        visited.clear();
        index.clear();
        visited.push_back({start, 0, {}});
        index.emplace(stateKey(start.particle, start.image), 0);
        for(size_t head = 0; head < visited.size(); head++) {
            if(visited[head].distance == maxDepth)
                continue;
            const State current = visited[head].state;
            const int distance = visited[head].distance + 1;
            for(const auto& [neighbor, shift] : adjacency[current.particle]) {
                const Vector3I image = current.image + shift;
                auto [iter, inserted] = index.emplace(stateKey(neighbor, image), (uint32_t)visited.size());
                if(inserted)
                    visited.push_back({{neighbor, image}, distance, {(uint32_t)head}});
                else if(visited[iter->second].distance == distance)
                    visited[iter->second].predecessors.push_back((uint32_t)head);
            }
        }
    }

    /// Distance to a state, or -1 if it was not reached.
    int distanceTo(size_t particle, const Vector3I& image) const {
        auto iter = index.find(stateKey(particle, image));
        return iter == index.end() ? -1 : visited[iter->second].distance;
    }

    /// All shortest paths from the start to the given visited state (as indices into 'visited', start first)
    /// whose intermediate particles all have an index above minParticle.
    void shortestPaths(uint32_t target, size_t minParticle, std::vector<std::vector<uint32_t>>& paths) const {
        paths.clear();
        std::vector<uint32_t> path(visited[target].distance + 1);
        std::function<void(uint32_t)> descend = [&](uint32_t s) {
            path[visited[s].distance] = s;
            if(visited[s].distance == 0) {
                paths.push_back(path);
                return;
            }
            for(uint32_t p : visited[s].predecessors)
                if(visited[p].distance == 0 || visited[p].state.particle > minParticle)
                    descend(p);
        };
        descend(target);
    }
};

}   // End of anonymous namespace

/******************************************************************************
* Finds all shortest-path rings in the bond network.
******************************************************************************/
std::vector<FindRingsModifier::Ring> FindRingsModifier::findRings(size_t particleCount, const std::vector<std::tuple<size_t, size_t, Vector3I>>& bonds, int minSize, int maxSize)
{
    Adjacency adjacency(particleCount);
    for(const auto& [a, b, shift] : bonds) {
        if(a >= particleCount || b >= particleCount || (a == b && shift == Vector3I::Zero()))
            continue;
        adjacency[a].emplace_back(b, shift);
        adjacency[b].emplace_back(a, -shift);
    }

    std::vector<std::vector<Ring>> ringsPerRoot(particleCount);
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Finding rings"));
    parallelFor(particleCount, 256, progress, [&](size_t root) {
        thread_local BoundedBFS search;     // Distances from the root in the whole network.
        thread_local BoundedBFS check;      // Shortcut test.
        thread_local std::vector<std::vector<uint32_t>> pathsA, pathsB;
        std::set<Ring> found;

        // Each ring is found from its lowest particle, the root. The two arcs from the root to the antipode(s) are
        // shortest paths in the whole network that pass only through particles with a higher index.
        search.run(adjacency, {root, Vector3I::Zero()}, maxSize / 2);

        // Accepts a candidate ring (states along the ring, root first) if it has no shortcut.
        auto consider = [&](std::vector<State>& ring) {
            const int n = (int)ring.size();
            if(n < minSize || n > maxSize)
                return;
            // A particle may occur only once (otherwise the cycle wraps around a periodic cell).
            std::vector<size_t> particles(n);
            for(int i = 0; i < n; i++) particles[i] = ring[i].particle;
            std::vector<size_t> sorted = particles;
            std::ranges::sort(sorted);
            if(std::ranges::adjacent_find(sorted) != sorted.end())
                return;
            // No shortcut: the network distance between any two ring members equals their distance along the ring.
            // It suffices to check antipodal members (ring distance floor(n/2)): a shortcut of length l between
            // members i and j that are m < n/2 apart along the ring gives the path i -> j -> antipode of i of
            // length l + floor(n/2) - m < floor(n/2). So the antipodes must not be reachable in fewer steps.
            const int half = n / 2;
            for(int i = 0; i < n; i++) {
                check.run(adjacency, ring[i], half - 1);
                const State& a1 = ring[(i + half) % n];
                const State& a2 = ring[(i + n - half) % n];
                if(check.distanceTo(a1.particle, a1.image) != -1 || check.distanceTo(a2.particle, a2.image) != -1)
                    return;
            }
            // Canonical orientation: the root (lowest index) is first, then the lower of its two ring neighbors.
            if(particles[1] > particles[n - 1])
                std::reverse(particles.begin() + 1, particles.end());
            found.insert(std::move(particles));
        };

        std::vector<State> ring;
        for(uint32_t v = 1; v < search.visited.size(); v++) {
            if(search.visited[v].state.particle <= root)
                continue;
            const int k = search.visited[v].distance;
            // Even rings of size 2k: two disjoint shortest paths from the root to the same state.
            if(2 * k >= minSize && 2 * k <= maxSize && search.visited[v].predecessors.size() >= 2) {
                search.shortestPaths(v, root, pathsA);
                for(size_t a = 0; a < pathsA.size(); a++) {
                    for(size_t b = a + 1; b < pathsA.size(); b++) {
                        bool disjoint = true;
                        for(int i = 1; i < k && disjoint; i++)
                            for(int j = 1; j < k && disjoint; j++)
                                disjoint = search.visited[pathsA[a][i]].state.particle != search.visited[pathsA[b][j]].state.particle;
                        if(!disjoint) continue;
                        ring.clear();
                        for(int i = 0; i <= k; i++) ring.push_back(search.visited[pathsA[a][i]].state);
                        for(int j = k - 1; j >= 1; j--) ring.push_back(search.visited[pathsA[b][j]].state);
                        consider(ring);
                    }
                }
            }
            // Odd rings of size 2k + 1: shortest paths to two bonded states at the same distance.
            if(2 * k + 1 >= minSize && 2 * k + 1 <= maxSize) {
                const State& sv = search.visited[v].state;
                for(const auto& [neighbor, shift] : adjacency[sv.particle]) {
                    auto iter = search.index.find(stateKey(neighbor, sv.image + shift));
                    if(iter == search.index.end() || iter->second <= v || search.visited[iter->second].distance != k || neighbor <= root)
                        continue;
                    const uint32_t w = iter->second;
                    search.shortestPaths(v, root, pathsA);
                    search.shortestPaths(w, root, pathsB);
                    for(const auto& pa : pathsA) {
                        for(const auto& pb : pathsB) {
                            bool disjoint = true;
                            for(int i = 1; i <= k && disjoint; i++)
                                for(int j = 1; j <= k && disjoint; j++)
                                    disjoint = search.visited[pa[i]].state.particle != search.visited[pb[j]].state.particle;
                            if(!disjoint) continue;
                            ring.clear();
                            for(int i = 0; i <= k; i++) ring.push_back(search.visited[pa[i]].state);
                            for(int j = k; j >= 1; j--) ring.push_back(search.visited[pb[j]].state);
                            consider(ring);
                        }
                    }
                }
            }
        }
        ringsPerRoot[root].assign(found.begin(), found.end());
    });

    std::vector<Ring> rings;
    for(auto& r : ringsPerRoot)
        std::ranges::move(r, std::back_inserter(rings));
    std::ranges::stable_sort(rings, {}, &Ring::size);
    return rings;
}

/******************************************************************************
* Constructor.
******************************************************************************/
void FindRingsModifier::initializeObject(ObjectInitializationFlags flags)
{
    Modifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        setRingsVis(OORef<SurfaceMeshVis>::create(flags));
        ringsVis()->setShowCap(false);
        ringsVis()->setSmoothShading(false);
        ringsVis()->setHighlightEdges(true);
        ringsVis()->setObjectTitle(tr("Rings"));
    }
}

/******************************************************************************
* Replaces any references the modifier has to the given visual element with a new compatible object.
******************************************************************************/
void FindRingsModifier::replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement)
{
    if(ringsVis() == visElement)
        setRingsVis(static_object_cast<SurfaceMeshVis>(getReplacement(tr("Rings"))));
}

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool FindRingsModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
 * Is called by the pipeline system before a new modifier evaluation begins.
 ******************************************************************************/
void FindRingsModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> FindRingsModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(minRingSize() < 3)
        throw Exception(tr("The minimum ring size must be at least 3."));
    if(maxRingSize() > MaxSupportedRingSize)
        throw Exception(tr("The maximum ring size must not exceed %1.").arg(MaxSupportedRingSize));
    if(maxRingSize() < minRingSize())
        throw Exception(tr("The maximum ring size must not be smaller than the minimum ring size."));

    const Particles* particles = state.expectObject<Particles>();
    particles->verifyIntegrity();

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            for(int n = minRingSize(); n <= maxRingSize(); n++) {
                if(const DataTable* table = cachedState.getObjectBy<DataTable>(request.modificationNode(), QStringLiteral("%1-rings").arg(n)))
                    state.addObject(table);
            }
            if(const DataTable* table = cachedState.getObjectBy<DataTable>(request.modificationNode(), QString::fromLatin1(histogramTableIdentifier)))
                state.addObject(table);
            if(const SurfaceMesh* mesh = cachedState.getObjectBy<SurfaceMesh>(request.modificationNode(), QStringLiteral("rings")))
                state.addObject(mesh);
            state.adoptAttributesFrom(cachedState, request.modificationNode());
        }
        return std::move(state);
    }

    const Bonds* bonds = particles->expectBonds();
    bonds->verifyIntegrity();

    return asyncLaunch([
            state = std::move(state),
            particles,
            bonds,
            minSize = minRingSize(),
            maxSize = maxRingSize(),
            includeWrapping = includeWrappingRings(),
            vis = OORef<SurfaceMeshVis>(ringsVis()),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        // Collect the bonds with their periodic image shifts.
        std::vector<std::tuple<size_t, size_t, Vector3I>> bondList;
        {
            BufferReadAccess<ParticleIndexPair> topology(bonds->expectProperty(Bonds::TopologyProperty));
            BufferReadAccess<Vector3I> images(bonds->getProperty(Bonds::PeriodicImageProperty));
            bondList.reserve(topology.size());
            for(size_t b = 0; b < topology.size(); b++)
                bondList.emplace_back((size_t)topology[b][0], (size_t)topology[b][1], images ? images[b] : Vector3I::Zero());
        }
        std::vector<Ring> rings;
        if(includeWrapping) {
            // As in OVITO Pro, rings are cycles of the bond graph regardless of periodic images: dropping the image
            // shifts turns cycles that wrap around the cell into ordinary cycles. A pair bonded through several
            // images becomes a single edge.
            std::vector<std::tuple<size_t, size_t, Vector3I>> graph;
            std::set<std::pair<size_t, size_t>> seen;
            for(const auto& [a, b, shift] : bondList)
                if(a != b && seen.emplace(std::min(a, b), std::max(a, b)).second)
                    graph.emplace_back(a, b, Vector3I::Zero());
            rings = findRings(particles->elementCount(), graph, minSize, maxSize);
        }
        else {
            rings = findRings(particles->elementCount(), bondList, minSize, maxSize);
        }

        // One table per ring size, listing the particle indices of each ring.
        std::vector<int64_t> histogram(maxSize - minSize + 1, 0);
        for(const Ring& ring : rings)
            histogram[ring.size() - minSize]++;
        size_t offset = 0;
        for(int n = minSize; n <= maxSize; n++) {
            const size_t count = histogram[n - minSize];
            PropertyPtr indices = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, count, Property::Int64, n, QStringLiteral("Particle Indices"));
            {
                BufferWriteAccess<int64_t*, access_mode::discard_write> access(indices);
                for(size_t r = 0; r < count; r++)
                    for(int i = 0; i < n; i++)
                        access.set(r, i, (int64_t)rings[offset + r][i]);
            }
            offset += count;
            state.createObject<DataTable>(QStringLiteral("%1-rings").arg(n), createdByNode, DataTable::None, tr("%1-Rings").arg(n), std::move(indices));
        }

        // Ring size histogram.
        PropertyPtr sizes = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, histogram.size(), Property::Int32, 1, QStringLiteral("Ring Size"));
        {
            BufferWriteAccess<int32_t, access_mode::discard_write> access(sizes);
            std::iota(access.begin(), access.end(), minSize);
        }
        PropertyPtr counts = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, histogram.size(), Property::Int64, 1, tr("Count"));
        std::ranges::copy(histogram, BufferWriteAccess<int64_t, access_mode::discard_write>(counts).begin());
        DataTable* histogramTable = state.createObject<DataTable>(QString::fromLatin1(histogramTableIdentifier), createdByNode, DataTable::BarChart,
            tr("Ring size histogram"), std::move(counts), std::move(sizes));
        histogramTable->setAxisLabelX(tr("Ring size"));
        histogramTable->setAxisLabelY(tr("Count"));

        // Ring polygons. Vertices are unwrapped along each ring, so the mesh lives in a non-periodic copy of the cell.
        const SimulationCell* cell = state.getObject<SimulationCell>();
        SurfaceMesh* mesh = state.createObjectWithVis<SurfaceMesh>(QStringLiteral("rings"), createdByNode, vis, tr("Rings"));
        if(cell) {
            DataOORef<SimulationCell> domain = DataOORef<SimulationCell>::makeCopy(cell);
            domain->setPbcFlags(false, false, false);
            mesh->setDomain(std::move(domain));
        }
        SurfaceMeshBuilder builder(mesh);
        {
            BufferReadAccess<Point3> positions(particles->expectProperty(Particles::PositionProperty));
            // Image shift of each bond partner, looked up by (particle, neighbor).
            std::unordered_multimap<size_t, std::pair<size_t, Vector3I>> neighbors;
            for(const auto& [a, b, shift] : bondList) {
                neighbors.emplace(a, std::make_pair(b, shift));
                neighbors.emplace(b, std::make_pair(a, -shift));
            }
            SurfaceMeshBuilder::VertexGrower vertexGrower(builder);
            SurfaceMeshBuilder::FaceGrower faceGrower(builder);
            std::vector<SurfaceMesh::vertex_index> vertices;
            for(const Ring& ring : rings) {
                vertices.clear();
                Vector3I image = Vector3I::Zero();
                for(size_t i = 0; i < ring.size(); i++) {
                    if(i > 0) {
                        // Follow the bond from the previous ring member (any bond if there are several images).
                        auto range = neighbors.equal_range(ring[i - 1]);
                        for(auto iter = range.first; iter != range.second; ++iter)
                            if(iter->second.first == ring[i]) { image += iter->second.second; break; }
                    }
                    Point3 p = positions[ring[i]];
                    if(cell) p += cell->cellMatrix() * image.toDataType<FloatType>();
                    vertices.push_back(vertexGrower.createVertex(p));
                }
                SurfaceMesh::face_index front = faceGrower.createFace(vertices.begin(), vertices.end());
                SurfaceMesh::face_index back = faceGrower.createFace(vertices.rbegin(), vertices.rend());
                builder.linkOppositeFaces(front, back);
            }
        }
        BufferWriteAccess<int32_t, access_mode::discard_write> faceSizes(builder.createFaceProperty(DataBuffer::Uninitialized, QStringLiteral("Ring Size"), Property::Int32));
        for(size_t r = 0; r < rings.size(); r++)
            faceSizes[2 * r] = faceSizes[2 * r + 1] = (int32_t)rings[r].size();

        state.addAttribute(QStringLiteral("FindRings.count"), QVariant::fromValue((qlonglong)rings.size()), createdByNode);
        state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Found %1 rings.").arg(rings.size())));
        return std::move(state);
    });
}

}   // End of namespace
