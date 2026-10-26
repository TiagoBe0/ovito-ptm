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
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <ovito/delaunay/DelaunayTessellation.h>
#include <ovito/delaunay/ManifoldConstructionHelper.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshBuilder.h>
#include <ovito/grid/modifier/MarchingCubes.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <boost/container/small_vector.hpp>
#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include "ConstructSurfaceModifier.h"

#include <boost/range/numeric.hpp>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ConstructSurfaceModifier);
OVITO_CLASSINFO(ConstructSurfaceModifier, "DisplayName", "Construct surface mesh");
OVITO_CLASSINFO(ConstructSurfaceModifier, "Description", "Build triangle mesh represention and compute volume and surface area of voids.");
OVITO_CLASSINFO(ConstructSurfaceModifier, "ModifierCategory", "Visualization");
DEFINE_REFERENCE_FIELD(ConstructSurfaceModifier, surfaceMeshVis);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, smoothingLevel);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, probeSphereRadius);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, onlySelectedParticles);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, selectSurfaceParticles);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, transferParticleProperties);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, identifyRegions);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, method);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, gridResolution);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, radiusFactor);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, isoValue);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, computeSurfaceDistance);
DEFINE_PROPERTY_FIELD(ConstructSurfaceModifier, mapParticlesToRegions);
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, smoothingLevel, "Smoothing level");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, probeSphereRadius, "Probe sphere radius");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, onlySelectedParticles, "Use only selected input particles");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, selectSurfaceParticles, "Select particles on the surface");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, transferParticleProperties, "Transfer particle properties to surface");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, identifyRegions, "Identify volumetric regions");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, method, "Construction method");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, gridResolution, "Resolution");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, radiusFactor, "Radius scaling");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, isoValue, "Iso value");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, computeSurfaceDistance, "Compute particle distances from surface");
SET_PROPERTY_FIELD_LABEL(ConstructSurfaceModifier, mapParticlesToRegions, "Map particles to regions");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(ConstructSurfaceModifier, probeSphereRadius, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(ConstructSurfaceModifier, smoothingLevel, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(ConstructSurfaceModifier, gridResolution, IntegerParameterUnit, 2, 600);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(ConstructSurfaceModifier, radiusFactor, PercentParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(ConstructSurfaceModifier, isoValue, FloatParameterUnit, 0);

namespace {

/******************************************************************************
 * Computes the part of the filled regions' volume that lies outside the simulation cell
 * along its non-periodic directions.
 *
 * Exact for the mesh, using the divergence theorem in reduced cell coordinates: the volume of the
 * filled set F beyond a cell face r_d = b is the flux of the field (r_d - b) e_d through the part of
 * the boundary of F on that side. The field vanishes on the plane r_d = b, so the cut does not need
 * to be capped, and it is invariant under periodic shifts along the other cell vectors. Parts of F
 * beyond several faces at once (edges, corners) are handled by inclusion-exclusion, clipping the
 * boundary against all faces involved. The boundary of F consists of the mesh faces belonging to an
 * empty region (filled-filled interfaces are excluded).
 ******************************************************************************/
FloatType computeFilledVolumeOutsideCell(const SurfaceMeshBuilder& mesh)
{
    const SimulationCellData& cell = mesh.domain();
    std::vector<int> openDims;
    for(int d = 0; d < 3; d++)
        if(!cell.hasPbc(d)) openDims.push_back(d);
    if(openDims.empty())
        return 0;

    BufferReadAccess<SelectionIntType> isFilled{mesh.expectRegionProperty(SurfaceMeshRegions::IsFilledProperty)};
    BufferReadAccess<Point3> vertexPositions{mesh.expectVertexProperty(SurfaceMeshVertices::PositionProperty)};
    BufferReadAccess<SurfaceMesh::region_index> faceRegions{mesh.expectFaceProperty(SurfaceMeshFaces::RegionProperty)};
    const AffineTransformation& inverse = cell.reciprocalCellMatrix();

    // A half-space beyond one cell face: r_dim < 0 (low side) or r_dim > 1 (high side).
    struct HalfSpace { int dim; bool high; };
    using Polygon = boost::container::small_vector<Point3, 8>;

    // Clips a polygon (reduced coordinates) to a half-space.
    auto clip = [](const Polygon& in, const HalfSpace& h) {
        Polygon out;
        auto inside = [&](const Point3& p) { return h.high ? p[h.dim] >= 1 : p[h.dim] <= 0; };
        const FloatType b = h.high ? 1 : 0;
        for(size_t i = 0; i < in.size(); i++) {
            const Point3& p = in[i];
            const Point3& q = in[(i + 1) % in.size()];
            if(inside(p)) out.push_back(p);
            if(inside(p) != inside(q)) {
                FloatType t = (b - p[h.dim]) / (q[h.dim] - p[h.dim]);
                out.push_back(p + t * (q - p));
            }
        }
        return out;
    };

    // Flux of (r_d - b) e_d through a planar polygon, as a fan of triangles.
    auto flux = [](const Polygon& poly, int d, FloatType b) {
        FloatType sum = 0;
        for(size_t i = 1; i + 1 < poly.size(); i++) {
            Vector3 area = (poly[i] - poly[0]).cross(poly[i + 1] - poly[0]) / 2;
            sum += area[d] * ((poly[0][d] + poly[i][d] + poly[i + 1][d]) / 3 - b);
        }
        return sum;
    };

    // The boundary of the filled set, as triangles in reduced coordinates (unwrapped along periodic directions).
    std::vector<std::array<Point3, 3>> boundary;
    for(SurfaceMesh::edge_index edge : mesh.firstFaceEdges()) {
        SurfaceMesh::region_index region = faceRegions[mesh.adjacentFace(edge)];
        if(region < 0 || isFilled[region])
            continue;
        SurfaceMesh::edge_index edge2 = mesh.nextFaceEdge(edge);
        Point3 p0 = vertexPositions[mesh.vertex1(edge)];
        Point3 p1 = p0 + mesh.edgeVector(edge, vertexPositions);
        Point3 p2 = p1 + mesh.edgeVector(edge2, vertexPositions);
        boundary.push_back({inverse * p0, inverse * p1, inverse * p2});
    }

    // Sign of the face orientation: the unclipped flux equals +/- the enclosed volume.
    FloatType orientedVolume = 0;
    for(const auto& tri : boundary)
        orientedVolume += flux(Polygon(tri.begin(), tri.end()), openDims[0], 0);
    const FloatType sign = (orientedVolume < 0) ? -1 : 1;

    // Only triangles reaching beyond an open cell face can contribute to the clipped fluxes.
    std::erase_if(boundary, [&](const std::array<Point3, 3>& tri) {
        for(int d : openDims)
            for(const Point3& p : tri)
                if(p[d] < 0 || p[d] > 1) return false;
        return true;
    });

    // Inclusion-exclusion over all combinations of at most one outside half-space per open direction.
    FloatType outside = 0;
    std::vector<HalfSpace> subset;
    std::function<void(size_t)> visit = [&](size_t k) {
        if(k == openDims.size()) {
            if(subset.empty()) return;
            FloatType volume = 0;
            for(const auto& tri : boundary) {
                Polygon poly(tri.begin(), tri.end());
                for(const HalfSpace& h : subset) {
                    poly = clip(poly, h);
                    if(poly.size() < 3) break;
                }
                if(poly.size() >= 3)
                    volume += flux(poly, subset.front().dim, subset.front().high ? 1 : 0);
            }
            outside += (subset.size() % 2 ? 1 : -1) * sign * volume;
            return;
        }
        visit(k + 1);
        for(bool high : {false, true}) {
            subset.push_back({openDims[k], high});
            visit(k + 1);
            subset.pop_back();
        }
    };
    visit(0);

    return std::max(outside, FloatType(0)) * std::abs(cell.cellMatrix().determinant());
}

}   // End of anonymous namespace

/******************************************************************************
* Constructor.
******************************************************************************/
void ConstructSurfaceModifier::initializeObject(ObjectInitializationFlags flags)
{
    Modifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        // Create the vis element for rendering the surface generated by the modifier.
        setSurfaceMeshVis(OORef<SurfaceMeshVis>::create(flags));
    }
}

/******************************************************************************
* Replaces any references the modifier has to the given visual element with a new compatible object.
******************************************************************************/
void ConstructSurfaceModifier::replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement)
{
    if(surfaceMeshVis() == visElement)
        setSurfaceMeshVis(static_object_cast<SurfaceMeshVis>(getReplacement({})));
}

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool ConstructSurfaceModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
 * Is called by the pipeline system before a new modifier evaluation begins.
 ******************************************************************************/
void ConstructSurfaceModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // Indicate that we will do different computations depending on whether the pipeline is evaluated in interactive mode or not.
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> ConstructSurfaceModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    // In interactive mode, fetch and return outdated results from the pipeline cache if available.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(const SurfaceMesh* cachedSurface = cachedState.getObjectBy<SurfaceMesh>(request.modificationNode(), QStringLiteral("surface"))) {
                state.addObject(cachedSurface);
            }
            // Adopt all global attributes computed by the modifier from the cached state.
            state.adoptAttributesFrom(cachedState, request.modificationNode());
        }
        return std::move(state);
    }

    // Get input particle positions.
    const Particles* particles = state.expectObject<Particles>();
    particles->verifyIntegrity();
    const Property* posProperty = particles->expectProperty(Particles::PositionProperty);

    // Get particle selection flags if requested.
    const Property* selProperty = onlySelectedParticles() ? particles->expectProperty(Particles::SelectionProperty) : nullptr;

    // Get particle "Grain" property.
    ConstPropertyPtr grainProperty = particles->getProperty(QStringLiteral("Grain"));
    if(grainProperty && grainProperty->componentCount() != 1)
        grainProperty.reset();

    // Get simulation cell.
    const SimulationCell* simCell = state.getObject<SimulationCell>();
    if(simCell && simCell->is2D())
        throw Exception(tr("The construct surface mesh modifier does not support 2d simulation cells."));

    // Collect the set of particle properties that should be transferred over to the surface mesh vertices.
    std::vector<ConstPropertyPtr> particleProperties;

    // In alpha-shape mode, always transfer an implicitly created 'Particle Index' particle property to the mesh - to
    // provide a mapping from the generated mesh vertices back the original particles they were created from.
    if(method() == AlphaShape) {
        // Generate adhoc particle property array 'Particle Index' and fill it with numbers 0 through N-1.
        PropertyPtr particleIndexProp = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, particles->elementCount(), Property::Int64, 1, QStringLiteral("Particle Index"));
        boost::algorithm::iota_n(BufferWriteAccess<int64_t, access_mode::discard_write>(particleIndexProp).begin(), (int64_t)0, particleIndexProp->size());
        particleProperties.push_back(std::move(particleIndexProp));
    }

    // Collect explicit particle properties to be transferred.
    if(transferParticleProperties()) {
        for(const Property* property : particles->properties()) {
            // Certain properties should not be transferred to the mesh vertices.
            if(property->typeId() == Particles::SelectionProperty) continue;
            if(property->typeId() == Particles::PositionProperty) continue;
            if(property->typeId() == Particles::IdentifierProperty) continue;
            particleProperties.push_back(property);
        }
    }

    // Create an empty surface mesh.
    SurfaceMesh* mesh = state.createObjectWithVis<SurfaceMesh>(QStringLiteral("surface"), request.modificationNode(), surfaceMeshVis(), tr("Surface"));
    mesh->setDomain(simCell);

    std::unique_ptr<ConstructSurfaceEngineBase> engine;
    if(method() == AlphaShape) {
        // Create engine object. Pass all relevant modifier parameters to the engine as well as the input data.
        engine = std::make_unique<AlphaShapeEngine>(
                posProperty,
                selProperty,
                std::move(grainProperty),
                std::move(mesh),
                probeSphereRadius(),
                smoothingLevel(),
                selectSurfaceParticles(),
                identifyRegions(),
                mapParticlesToRegions() && identifyRegions(),
                computeSurfaceDistance(),
                std::move(particleProperties),
                request.modificationNode());
    }
    else {
        // Create engine object. Pass all relevant modifier parameters to the engine as well as the input data.
        engine = std::make_unique<GaussianDensityEngine>(posProperty, selProperty, std::move(mesh), radiusFactor(),
                                                       isoValue(), gridResolution(), identifyRegions(),
                                                       computeSurfaceDistance(), particles->inputParticleRadii(),
                                                       std::move(particleProperties), request.modificationNode());
    }

    // Perform the calculation in a separate thread.
    return asyncLaunch([
            state = std::move(state),
            engine = std::move(engine)]() mutable
    {
        // Create an ad-hoc simulation cell if the input particles do not have one.
        if(!engine->mesh()->domain()) {
            // Compute bounding box that is large enough to contain the input particles.
            SimulationCellData cellData(engine->positions());
            engine->mesh()->setDomain(DataOORef<SimulationCell>::create(ObjectInitializationFlag::DontCreateVisElement, cellData.cellMatrix(), false, false, false));
        }

        engine->perform();
        this_task::throwIfCanceled();
        engine->applyResults(state);
        return std::move(state);
    });
}

/******************************************************************************
* Performs the actual analysis. This method is executed in a worker thread.
******************************************************************************/
void ConstructSurfaceModifier::AlphaShapeEngine::perform()
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Constructing surface mesh"));

    if(probeSphereRadius() <= 0)
        throw Exception(tr("Radius parameter must be positive."));

    OVITO_ASSERT(mesh()->domain());
    if(!mesh()->domain() || mesh()->domain()->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate (volume of parallelepiped is zero)."));

    double alpha = probeSphereRadius() * probeSphereRadius();
    FloatType ghostLayerSize = probeSphereRadius() * FloatType(3.5);

    // Check if combination of radius parameter and simulation cell size is valid.
    for(size_t dim = 0; dim < 3; dim++) {
        if(mesh()->domain()->hasPbc(dim)) {
            int stencilCount = (int)ceil(ghostLayerSize / mesh()->domain()->matrix().column(dim).dot(mesh()->domain()->cellNormalVector(dim)));
            if(stencilCount > 1)
                throw Exception(tr("Cannot generate Delaunay tessellation. Simulation cell is too small, or radius parameter is too large."));
        }
    }

    // Algorithm is divided into several sub-steps.
    // Assign weights to sub-steps according to estimated runtime.
    progress.beginSubSteps({ 10, 30, 2, 2, 2, surfaceDistances() ? 1000 : 1 });

    // Generate Delaunay tessellation.
    DelaunayTessellation tessellation;

    // When identifying regions (including empty ones), we need to make sure that the entire simulation cell is covered by
    // finite Delaunay tetrahedra. In other words, we have to prevent infinite tetrahedra from penetrating into the simulation cekk.
    // This is accomplished in the DelaunayTessellation class by adding 8 extra input points to the tessellation,
    // far away from the simulation cell and any real input particles. These 8 points form a convex hull, whose interior gets tessellated.
    bool coverDomainWithFiniteTets = _identifyRegions;

    tessellation.generateTessellation(
            mesh()->domain(),
            BufferReadAccess<Point3>(positions()).cbegin(),
            positions()->size(),
            ghostLayerSize,
            coverDomainWithFiniteTets,
            selection() ? BufferReadAccess<SelectionIntType>(selection()).cbegin() : nullptr,
            progress);

    progress.nextSubStep();

    SurfaceMeshBuilder meshBuilder(this->mesh());

    // Access the per-particle grain IDs.
    BufferAccessConvertedTo<const int64_t> particleGrains(_identifyRegions ? this->particleGrains().get() : nullptr);

    // Predefine the filled spatial regions of the output SurfaceMesh if the input particles are divided into separate grains by e.g. a GrainSegmentationModifier.
    if(particleGrains) {

        // Determine the maximum grain ID.
        int64_t maxGrainId = 0;
        if(particleGrains.size() != 0) {
            maxGrainId = qBound(int64_t{0},
                *std::ranges::max_element(particleGrains),
                static_cast<int64_t>(std::numeric_limits<SurfaceMesh::region_index>::max() - 1));
        }

        // Create one region in the output mesh for each grain.
        meshBuilder.mutableRegions()->setElementCount(maxGrainId + 1);
    }

    // Helper function that determines which spatial region a filled Delaunay cell belongs to.
    // This is only used if the input particles have previously been divided into grains by a GrainSegmentationModifier.
    // Otherwise, all tetrahedra are attributed to the null grain initially. Subsequently, they will be
    // grouped into disconnected sets, which form the regions of the output SurfaceMesh.
    auto tetrahedronRegion = [&](DelaunayTessellation::CellHandle cell) -> SurfaceMesh::region_index {
        if(particleGrains) {
            // Decide which particle cluster the Delaunay cell belongs to.
            // We need a tie-breaker in case the four vertex atoms belong to different grains.
            int64_t result = 0;
            for(int v = 0; v < 4; v++) {
                size_t particleIndex = tessellation.inputPointIndex(tessellation.cellVertex(cell, v));
                int64_t clusterId = particleGrains[particleIndex];
                if(clusterId > result)
                    result = clusterId;
            }
            return result;
        }
        return 0;
    };

    // This callback function is called for every surface facet created by the manifold construction helper.
    // It marks the particles corresponding to the mesh vertices as belonging to the surface.
    BufferWriteAccess<SelectionIntType, access_mode::write> surfaceParticleSelectionArray(surfaceParticleSelection());
    auto prepareMeshFace = [&](SurfaceMesh::face_index face, const std::array<size_t,3>& vertexIndices, const std::array<DelaunayTessellation::VertexHandle,3>& vertexHandles, DelaunayTessellation::CellHandle cell) {
        // Mark the face's corner particles as belonging to the surface.
        if(surfaceParticleSelectionArray) {
            for(size_t vi : vertexIndices) {
                OVITO_ASSERT(vi < surfaceParticleSelectionArray.size());
                surfaceParticleSelectionArray[vi] = 1;
            }
        }
    };

    // This callback function is called for every surface vertex created by the manifold construction helper.
    // It registers the vertex in the map that associates each mesh vertex with the corresponding input particle index.
    std::vector<size_t> vertexToParticleMap;
    auto prepareMeshVertex = [&](SurfaceMesh::vertex_index vertex, size_t particleIndex) {
        OVITO_ASSERT(vertex == vertexToParticleMap.size());
        vertexToParticleMap.push_back(particleIndex);
    };

    if(!_identifyRegions) {
        // Predefine the filled spatial region.
        // An empty region is not defined, because we are creating only a one-sided surface mesh.
        meshBuilder.mutableRegions()->setElementCount(1);

        // Just construct a one-sided surface mesh without caring about spatial regions.
        ManifoldConstructionHelper manifoldConstructor(tessellation, meshBuilder, alpha, false, positions(), progress);
        manifoldConstructor.construct(tetrahedronRegion, std::move(prepareMeshFace), std::move(prepareMeshVertex));
    }
    else {
        if(!particleRegionIds())
            progress.beginSubSteps({ 2, 1 });
        else
            progress.beginSubSteps({ 2, 1, 1 });

        // Construct a two-sided surface mesh with mesh faces associated with spatial regions (filled or solid).
        ManifoldConstructionHelper manifoldConstructor(tessellation, meshBuilder, alpha, true, positions(), progress);
        manifoldConstructor.construct(tetrahedronRegion, std::move(prepareMeshFace), std::move(prepareMeshVertex));

        progress.nextSubStep();

        // After construct() has identified the filled regions, identify the empty regions.
        manifoldConstructor.formEmptyRegions();

        // Reference to filledRegionCount for convenience.
        SurfaceMesh::size_type& filledRegionCount = _aggregateVolumes.filledRegionCount;
        filledRegionCount = manifoldConstructor.filledRegionCount();
        // Reference to emptyRegionCount for convenience.
        SurfaceMesh::size_type& emptyRegionCount = _aggregateVolumes.emptyRegionCount;
        emptyRegionCount = manifoldConstructor.emptyRegionCount();

        // Transfer the region ID information to the output particles.
        if(_mapParticlesToRegions) {
            progress.nextSubStep();
            progress.setMaximum(positions()->size());
            size_t numVisitedParticles = 0;

            // Initially, mark all particles as not assigned to any region (special region ID -1).
            _particleRegionIds = Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, positions()->size(), Property::Int32, 1, QStringLiteral("Region"));
            BufferWriteAccess<int32_t, access_mode::discard_read_write> regionIds{_particleRegionIds};
            std::ranges::fill(regionIds, -1);

            // Create one list (growable Property) per region, which stores the particle indices belonging to the region.
            std::vector<PropertyFactory<int64_t>> regionParticleLists(meshBuilder.regionCount());
            int regionIndex = 0;
            for(PropertyFactory<int64_t>& bufferFactory : regionParticleLists)
                bufferFactory = PropertyFactory<int64_t>{PropertyContainer::OOClass(), 0, QStringLiteral("Particles Region %1").arg(regionIndex++)};

            // Visit each tetrahedral cell and assign its four vertex particles to the region of the cell.
            DelaunayTessellation::CellHandle queryHint = DelaunayTessellation::CellHandle(-1);
            for(DelaunayTessellation::CellHandle cell : tessellation.cells()) {
                if(tessellation.isGhostCell(cell) || !tessellation.isFiniteCell(cell))
                    continue;
                queryHint = cell;
                if(int regionId = tessellation.getUserField(cell); regionId >= 0) {
                    OVITO_ASSERT(regionId >= 0 && regionId < filledRegionCount + emptyRegionCount);
                    OVITO_ASSERT(regionId < regionParticleLists.size());
                    for(int v = 0; v < 4; v++) {
                        size_t particleIndex = tessellation.inputPointIndex(tessellation.cellVertex(cell, v));
                        OVITO_ASSERT(particleIndex < regionIds.size() || particleIndex == std::numeric_limits<size_t>::max());
                        if(particleIndex != std::numeric_limits<size_t>::max()) {
                            // Keep track of the number of particles visited so far.
                            if(regionIds[particleIndex] == -1)
                                progress.setValueIntermittent(++numVisitedParticles);

                            // Give precedence to filled regions. Particles on the boundary are always assigned to the filled region, not the empty region.
                            if(regionId < filledRegionCount || regionIds[particleIndex] == -1)
                                regionIds[particleIndex] = regionId;

                            // Add to the region's particle list.
                            regionParticleLists[regionId].push_back(particleIndex);
                        }
                    }
                }
            }

            // If only selected particles were used as input points for the Delaunay tessellation, the unselected particles
            // are not attributed to any region yet. We do the attribution next by performing point queries on the Delaunay tessellation.
            // For each unassigned particle we determine the Delaunay cell it is located in and then use its region.
            size_t particleIndex = 0;
            for(const Point3& pos : BufferReadAccess<Point3>(positions())) {
                auto& particleRegionId = regionIds[particleIndex];
                if(particleRegionId == -1) {
                    progress.setValueIntermittent(++numVisitedParticles);

                    DelaunayTessellation::CellHandle cell = tessellation.locate(tessellation.simCell().wrapPoint(pos), queryHint);
                    OVITO_ASSERT(cell >= 0 && cell < tessellation.numberOfTetrahedra());

                    if(int regionId = tessellation.getUserField(cell); regionId >= 0) {
                        OVITO_ASSERT(regionId >= 0 && regionId < filledRegionCount + emptyRegionCount);
                        particleRegionId = regionId;
                        regionParticleLists[regionId].push_back(particleIndex);
                    }
                    queryHint = cell;
                }
                ++particleIndex;
            }

            // Finalize regionParticleLists structure. Convert it to a QVariant such that the nested list structure can be output as a global attribute.
            QVariantList regionParticleListsVariant(regionParticleLists.size());
            std::transform(regionParticleLists.begin(), regionParticleLists.end(), regionParticleListsVariant.begin(), [](PropertyFactory<int64_t>& bufferFactory) {
                // Note: QVariant can only hold a OORef<OvitoObject>, not a DataOORef<Property>, because only the former is a registered Qt meta type.
                return QVariant::fromValue(static_cast<OORef<OvitoObject>>(bufferFactory.take().get()));
            });
            _regionParticleLists.setValue(std::move(regionParticleListsVariant));
        }

        // Output "Filled" region property.
        BufferWriteAccess<SelectionIntType, access_mode::discard_write> filledProperty(meshBuilder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::IsFilledProperty));
        std::fill(filledProperty.begin(), filledProperty.begin() + filledRegionCount, 1);
        std::fill(filledProperty.begin() + filledRegionCount, filledProperty.end(), 0);

        progress.endSubSteps();
    }

    // Create mesh vertex properties.
    for(const ConstPropertyPtr& particleProperty : particleProperties()) {
        PropertyPtr vertexProperty;
        if(particleProperty->typeId() < Property::FirstSpecificProperty && SurfaceMeshVertices::OOClass().isValidStandardPropertyId(particleProperty->typeId())) {
            // Input property is also a standard property for mesh vertices.
            vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Uninitialized, static_cast<SurfaceMeshVertices::Type>(particleProperty->typeId()));
            OVITO_ASSERT(vertexProperty->dataType() == particleProperty->dataType());
            OVITO_ASSERT(vertexProperty->stride() == particleProperty->stride());
        }
        else if(SurfaceMeshVertices::OOClass().standardPropertyTypeId(particleProperty->name()) != 0) {
            // Input property name is that of a standard property for mesh vertices.
            // Must rename the property to avoid conflict, because user properties may not have a standard property name.
            QString newPropertyName = particleProperty->name() + tr("_particles");
            vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Uninitialized, newPropertyName, particleProperty->dataType(), particleProperty->componentCount(), particleProperty->componentNames());
        }
        else {
            // Input property is a user property for mesh vertices.
            vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Uninitialized, particleProperty->name(), particleProperty->dataType(), particleProperty->componentCount(), particleProperty->componentNames());
        }
        // Copy particle property values to mesh vertices using precomputed index mapping.
        particleProperty->mappedCopyTo(*vertexProperty, vertexToParticleMap);
        this_task::throwIfCanceled();
    }

    progress.nextSubStep();

    // Make sure every mesh vertex is only part of one surface manifold.
    meshBuilder.makeManifold();
    progress.nextSubStep();

    // The region volumes stem from the Delaunay tetrahedra, i.e. the unsmoothed mesh. Measure the filled volume
    // outside the cell on that same mesh, before smoothing moves the vertices.
    if(identifyRegions())
        _filledVolumeOutsideCell = computeFilledVolumeOutsideCell(meshBuilder);

    meshBuilder.smoothMesh(_smoothingLevel, progress);
    progress.nextSubStep();

    if(identifyRegions()) {
        meshBuilder.setExternalRegionVolumeInfinityIfNonPeriodic();
        _totalSurfaceArea = meshBuilder.computeSurfaceAreaWithRegions();
        _aggregateVolumes = meshBuilder.computeAggregateVolumes();
    }
    else {
        _totalSurfaceArea = meshBuilder.computeTotalSurfaceArea();
    }
    progress.nextSubStep();

    // Compute the distance of each input particle from the constructed surface.
    computeSurfaceDistances(meshBuilder, progress);
    progress.endSubSteps();
}

/******************************************************************************
* Performs the actual analysis. This method is executed in a worker thread.
******************************************************************************/
void ConstructSurfaceModifier::GaussianDensityEngine::perform()
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Constructing surface mesh"));

    // Check input data.
    OVITO_ASSERT(mesh()->domain());
    if(!mesh()->domain() || mesh()->domain()->isDegenerate())
        throw Exception(tr("Simulation cell is degenerate."));

    if(positions()->size() == 0) {
        if(_identifyRegions) {
            SurfaceMeshBuilder meshBuilder(this->mesh());
            meshBuilder.mutableRegions()->setElementCount(1);
            BufferWriteAccess<FloatType, access_mode::discard_write> volumeProperty{
                meshBuilder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::VolumeProperty)};
            BufferWriteAccess<SelectionIntType, access_mode::discard_write> isExteriorProperty{
                meshBuilder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::IsExteriorProperty)};
            BufferWriteAccess<SelectionIntType, access_mode::discard_write> isFilledProperty{
                meshBuilder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::IsFilledProperty)};
            BufferWriteAccess<FloatType, access_mode::discard_write> surfaceArea{
                meshBuilder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::SurfaceAreaProperty)};
            volumeProperty[0] = meshBuilder.domain().volume3D();
            isFilledProperty[0] = 0;
            isExteriorProperty[0] = !(meshBuilder.hasPbc(0) && meshBuilder.hasPbc(1) && meshBuilder.hasPbc(2));
            surfaceArea[0] = 0;
            meshBuilder.setSpaceFillingRegion(0);
        }
        return;
    }

    // Algorithm is divided into several sub-steps.
    // Assign weights to sub-steps according to estimated runtime.
    progress.beginSubSteps({ 1, 30, 1600, 1500, 30, 500, 100, 300, surfaceDistances() ? 10000 : 1 });

    // Access the atomic radii.
    BufferReadAccess<GraphicsFloatType> particleRadii(_particleRadii);

    // Determine the cutoff range of atomic Gaussians.
    FloatType cutoffSize = FloatType(3) * *std::ranges::max_element(particleRadii) * _radiusFactor;

    // Determine the extents of the density grid.
    AffineTransformation gridBoundaries = mesh()->domain()->matrix();
    BufferReadAccess<Point3> positionsArray(positions());
    for(size_t dim = 0; dim < 3; dim++) {
        // Use bounding box of particles in directions that are non-periodic.
        if(!mesh()->domain()->hasPbc(dim)) {
            // Compute range of relative atomic coordinates in the current direction.
            FloatType xmin =  FLOATTYPE_MAX;
            FloatType xmax = -FLOATTYPE_MAX;
            const AffineTransformation inverseCellMatrix = mesh()->domain()->inverseMatrix();
            for(const Point3& p : positionsArray) {
                FloatType rp = inverseCellMatrix.prodrow(p, dim);
                if(rp < xmin) xmin = rp;
                if(rp > xmax) xmax = rp;
            }

            // Need to add extra margin along non-periodic dimensions, because Gaussian functions reach beyond atomic radii.
            FloatType rcutoff = cutoffSize / gridBoundaries.column(dim).length();
            xmin -= rcutoff;
            xmax += rcutoff;

            gridBoundaries.column(3) += xmin * gridBoundaries.column(dim);
            gridBoundaries.column(dim) *= (xmax - xmin);
        }
    }

    // Determine the number of voxels in each direction of the density grid.
    size_t gridDims[3];
    FloatType voxelSizeX = gridBoundaries.column(0).length() / _gridResolution;
    FloatType voxelSizeY = gridBoundaries.column(1).length() / _gridResolution;
    FloatType voxelSizeZ = gridBoundaries.column(2).length() / _gridResolution;
    FloatType voxelSize = std::max(voxelSizeX, std::max(voxelSizeY, voxelSizeZ));
    gridDims[0] = std::max((size_t)2, (size_t)(gridBoundaries.column(0).length() / voxelSize));
    gridDims[1] = std::max((size_t)2, (size_t)(gridBoundaries.column(1).length() / voxelSize));
    gridDims[2] = std::max((size_t)2, (size_t)(gridBoundaries.column(2).length() / voxelSize));

    progress.nextSubStep();

    // Allocate storage for the density grid values.
    std::vector<FloatType> densityData(gridDims[0] * gridDims[1] * gridDims[2], FloatType(0));

    // Set up a particle neighbor finder to speed up density field computation.
    CutoffNeighborFinder neighFinder(cutoffSize, positions(), mesh()->domain(), selection());
    progress.nextSubStep();

    // Set up a matrix that converts grid coordinates to spatial coordinates.
    AffineTransformation gridToCartesian = gridBoundaries;
    gridToCartesian.column(0) /= gridDims[0] - (mesh()->domain()->hasPbc(0)?0:1);
    gridToCartesian.column(1) /= gridDims[1] - (mesh()->domain()->hasPbc(1)?0:1);
    gridToCartesian.column(2) /= gridDims[2] - (mesh()->domain()->hasPbc(2)?0:1);

    // Compute the accumulated density at each grid point.
    parallelFor(densityData.size(), 4096, progress, [&](size_t voxelIndex) {

        // Determine the center coordinates of the current grid cell.
        size_t ix = voxelIndex % gridDims[0];
        size_t iy = (voxelIndex / gridDims[0]) % gridDims[1];
        size_t iz = voxelIndex / (gridDims[0] * gridDims[1]);
        Point3 voxelCenter = gridToCartesian * Point3(ix, iy, iz);
        FloatType& density = densityData[voxelIndex];

        // Visit all particles in the vicinity of the center point.
        for(CutoffNeighborFinder::Query neighQuery(neighFinder, voxelCenter); !neighQuery.atEnd(); neighQuery.next()) {
            FloatType alpha = _radiusFactor * particleRadii[neighQuery.current()];
            density += std::exp(-neighQuery.distanceSquared() / (FloatType(2) * alpha * alpha));
        }
    });
    progress.nextSubStep();

    // Set up callback function returning the field value, which will be passed to the marching cubes algorithm.
    auto getFieldValue = [
            _data = densityData.data(),
            _pbcFlags = mesh()->domain()->pbcFlags(),
            _gridShape = gridDims
            ](int i, int j, int k) -> FloatType {
        if(_pbcFlags[0]) {
            if(i == _gridShape[0]) i = 0;
        }
        else {
            if(i == 0 || i == _gridShape[0] + 1) return std::numeric_limits<FloatType>::lowest();
            i--;
        }
        if(_pbcFlags[1]) {
            if(j == _gridShape[1]) j = 0;
        }
        else {
            if(j == 0 || j == _gridShape[1] + 1) return std::numeric_limits<FloatType>::lowest();
            j--;
        }
        if(_pbcFlags[2]) {
            if(k == _gridShape[2]) k = 0;
        }
        else {
            if(k == 0 || k == _gridShape[2] + 1) return std::numeric_limits<FloatType>::lowest();
            k--;
        }
        OVITO_ASSERT(i >= 0 && i < _gridShape[0]);
        OVITO_ASSERT(j >= 0 && j < _gridShape[1]);
        OVITO_ASSERT(k >= 0 && k < _gridShape[2]);
        return _data[(i + j*_gridShape[0] + k*_gridShape[0]*_gridShape[1])];
    };

    // Temporarily set the domain of the output mesh to the grid domain.
    DataOORef<const SimulationCell> originalDomain = mesh()->domain();
    if(mesh()->domain()->cellMatrix() != gridBoundaries) {
        auto newCell = DataOORef<SimulationCell>::makeCopy(mesh()->domain());
        newCell->setCellMatrix(gridBoundaries);
        mesh()->setDomain(std::move(newCell));
    }

    // Construct isosurface of the density field.
    SurfaceMeshBuilder meshBuilder(this->mesh());

    // Request identification of regions in Marching Cubes algorithm.
    if(identifyRegions())
        meshBuilder.createFaceProperty(DataBuffer::Uninitialized, SurfaceMeshFaces::RegionProperty);

    {  // limit lifetime of mc to free up resources
        MarchingCubes mc(meshBuilder, gridDims[0], gridDims[1], gridDims[2], false, std::move(getFieldValue));
        mc.generateIsosurface(_isoLevel, progress);
    }
    progress.nextSubStep();

    // Transform mesh vertices from orthogonal grid space to world space.
    meshBuilder.transformVertices(gridToCartesian);
    this_task::throwIfCanceled();

    // Map mesh region volumes from orthogonal grid space to world space.
    FloatType gridToCartesianDeterminant = gridToCartesian.determinant();
    BufferWriteAccess<FloatType, access_mode::read_write> regionVolumes = meshBuilder.mutableRegionProperty(SurfaceMeshRegions::VolumeProperty);
    for(SurfaceMesh::region_index region : meshBuilder.regionsRange()) {
        regionVolumes[region] *= gridToCartesianDeterminant;
    }
    regionVolumes.reset();

    progress.nextSubStep();

    // Create mesh vertex properties for transferring particle property values to the surface.
    std::vector<std::pair<BufferReadAccess<float*>, BufferWriteAccess<float*, access_mode::read_write>>> propertyMapping32;
    std::vector<std::pair<BufferReadAccess<double*>, BufferWriteAccess<double*, access_mode::read_write>>> propertyMapping64;
    for(const ConstPropertyPtr& particleProperty : particleProperties()) {
        // Can only transfer floating-point properties, because we'll need to blend values of several particles.
        if(particleProperty->dataType() == Property::Float32 || particleProperty->dataType() == Property::Float64) {
            PropertyPtr vertexProperty;
            if(particleProperty->typeId() < Property::FirstSpecificProperty && SurfaceMeshVertices::OOClass().isValidStandardPropertyId(particleProperty->typeId())) {
                // Input property is also a standard property for mesh vertices.
                vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Initialized, static_cast<SurfaceMeshVertices::Type>(particleProperty->typeId()));
                OVITO_ASSERT(vertexProperty->dataType() == particleProperty->dataType());
                OVITO_ASSERT(vertexProperty->stride() == particleProperty->stride());
            }
            else if(SurfaceMeshVertices::OOClass().standardPropertyTypeId(particleProperty->name()) != 0) {
                // Input property name is that of a standard property for mesh vertices.
                // Must rename the property to avoid conflict, because user properties may not have a standard property name.
                QString newPropertyName = particleProperty->name() + QStringLiteral("_particles");
                vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Initialized, newPropertyName, particleProperty->dataType(), particleProperty->componentCount(), particleProperty->componentNames());
            }
            else {
                // Input property is a user property for mesh vertices.
                vertexProperty = meshBuilder.createVertexProperty(DataBuffer::Initialized, particleProperty->name(), particleProperty->dataType(), particleProperty->componentCount(), particleProperty->componentNames());
            }
            if(particleProperty->dataType() == Property::Float32)
                propertyMapping32.emplace_back(particleProperty, std::move(vertexProperty));
            else
                propertyMapping64.emplace_back(particleProperty, std::move(vertexProperty));
            this_task::throwIfCanceled();
        }
    }

    // Transfer property values from particles to the mesh vertices.
    if(!propertyMapping32.empty() || !propertyMapping64.empty()) {
        // Compute the accumulated density at each grid point.
        BufferReadAccess<Point3> vertexPositions = meshBuilder.expectVertexProperty(SurfaceMeshVertices::PositionProperty);
        parallelFor(meshBuilder.vertexCount(), 4096, progress, [&](size_t vertexIndex) {
            // Visit all particles in the vicinity of the vertex.
            FloatType weightSum = 0;
            for(CutoffNeighborFinder::Query neighQuery(neighFinder, vertexPositions[vertexIndex]); !neighQuery.atEnd(); neighQuery.next()) {
                FloatType alpha = _radiusFactor * particleRadii[neighQuery.current()];
                FloatType weight = std::exp(-neighQuery.distanceSquared() / (FloatType(2) * alpha * alpha));
                // Perform summation of particle contributions to the property values at the current mesh vertex.
                for(auto& p : propertyMapping32) {
                    for(size_t component = 0; component < p.first.componentCount(); component++) {
                        p.second.value(vertexIndex, component) += weight * p.first.get(neighQuery.current(), component);
                    }
                }
                for(auto& p : propertyMapping64) {
                    for(size_t component = 0; component < p.first.componentCount(); component++) {
                        p.second.value(vertexIndex, component) += weight * p.first.get(neighQuery.current(), component);
                    }
                }
                weightSum += weight;
            }
            if(weightSum != 0) {
                // Normalize property values.
                for(auto& p : propertyMapping32) {
                    for(size_t component = 0; component < p.second.componentCount(); component++) {
                        p.second.value(vertexIndex, component) /= weightSum;
                    }
                }
                for(auto& p : propertyMapping64) {
                    for(size_t component = 0; component < p.second.componentCount(); component++) {
                        p.second.value(vertexIndex, component) /= weightSum;
                    }
                }
            }
        });
    }

    // Flip surface orientation if cell is mirrored.
    if(gridToCartesianDeterminant < 0)
        meshBuilder.flipFaces();

    // Restore original mesh domain.
    meshBuilder.setDomain(std::move(originalDomain));

    progress.nextSubStep();

    if(!meshBuilder.connectOppositeHalfedges())
        throw Exception(tr("Something went wrong. Isosurface mesh is not closed."));

    progress.nextSubStep();

    if(_identifyRegions) {
        meshBuilder.setExternalRegionVolumeInfinityIfNonPeriodic();
        _totalSurfaceArea = meshBuilder.computeSurfaceAreaWithRegions();
        _aggregateVolumes = meshBuilder.computeAggregateVolumes();
        _filledVolumeOutsideCell = computeFilledVolumeOutsideCell(meshBuilder);
    }
    else {
        _totalSurfaceArea = meshBuilder.computeTotalSurfaceArea();
    }

    progress.nextSubStep();

    // Compute the distance of each input particle from the constructed surface.
    computeSurfaceDistances(meshBuilder, progress);

    progress.endSubSteps();
}

/******************************************************************************
* Compute the distance of each input particle from the constructed surface.
******************************************************************************/
void ConstructSurfaceModifier::ConstructSurfaceEngineBase::computeSurfaceDistances(const SurfaceMeshBuilder& mesh, TaskProgress& progress)
{
    if(!surfaceDistances())
        return;
    progress.setText(tr("Computing surface distances"));

    // Access output array.
    BufferWriteAccess<FloatType, access_mode::discard_write> distanceArray(surfaceDistances());
    // Access input positions.
    BufferReadAccess<Point3> positionArray(positions());

    // Perform computation for each particle.
    parallelFor(positions()->size(), 256, progress, [&](size_t index) {
        auto result = mesh.locatePoint(positionArray[index], 0.0);
        distanceArray[index] = result ? result->second : 0.0;
    });
}

/******************************************************************************
 * Injects the computed results into the data pipeline.
 ******************************************************************************/
void ConstructSurfaceModifier::ConstructSurfaceEngineBase::applyResults(PipelineFlowState& state)
{
    // Output computed particle distances from surface.
    if(surfaceDistances()) {
        Particles* particles = state.expectMutableObject<Particles>();
        particles->verifyIntegrity();
        particles->createProperty(surfaceDistances());
    }

    // Output total surface area.
    state.addAttribute(QStringLiteral("ConstructSurfaceMesh.surface_area"), QVariant::fromValue(surfaceArea()), _createdByNode);

    if(identifyRegions()) {
        const SimulationCell* simCell = state.getObject<SimulationCell>();
        bool periodic = simCell && (simCell->pbcX() && simCell->pbcY() && simCell->pbcZ());
        FloatType totalCellVolume = periodic ? simCell->volume3D() : std::numeric_limits<FloatType>::quiet_NaN();

        // Additional attributes (not in OVITO Pro, whose attributes above are infinite or NaN for a non-periodic cell):
        // the volumes inside the simulation cell. The empty volume in the cell counts the interior voids plus the part
        // of the infinite exterior region inside the cell, i.e. everything in the cell that is not filled. Filled volume
        // sticking out of the cell (e.g. a Gaussian density surface around atoms close to an open cell face) is not
        // subtracted from the cell volume. For a fully periodic cell these equal the standard attributes.
        if(simCell) {
            const FloatType boxVolume = simCell->volume3D();
            const FloatType filledInCell = periodic ? _aggregateVolumes.totalFilledVolume : _aggregateVolumes.totalFilledVolume - _filledVolumeOutsideCell;
            const FloatType emptyInCell = periodic ? _aggregateVolumes.totalEmptyVolume : std::max(boxVolume - filledInCell, FloatType(0));
            const FloatType nan = std::numeric_limits<FloatType>::quiet_NaN();
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.box_volume"), QVariant::fromValue(boxVolume), _createdByNode);
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.filled_volume_in_cell"), QVariant::fromValue(filledInCell), _createdByNode);
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.empty_volume_in_cell"), QVariant::fromValue(emptyInCell), _createdByNode);
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.filled_fraction_in_cell"), QVariant::fromValue(boxVolume ? filledInCell / boxVolume : nan), _createdByNode);
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.empty_fraction_in_cell"), QVariant::fromValue(boxVolume ? emptyInCell / boxVolume : nan), _createdByNode);
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.specific_surface_area_in_cell"), QVariant::fromValue(boxVolume ? surfaceArea() / boxVolume : nan), _createdByNode);
        }

        // Output more global attributes.
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.cell_volume"), QVariant::fromValue(totalCellVolume), _createdByNode);
        state.addAttribute(
            QStringLiteral("ConstructSurfaceMesh.specific_surface_area"),
            QVariant::fromValue(totalCellVolume ? (surfaceArea() / totalCellVolume) : std::numeric_limits<FloatType>::quiet_NaN()),
            _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.filled_volume"), QVariant::fromValue(_aggregateVolumes.totalFilledVolume), _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.filled_fraction"),
                           QVariant::fromValue(totalCellVolume ? (_aggregateVolumes.totalFilledVolume / totalCellVolume)
                                                               : std::numeric_limits<FloatType>::quiet_NaN()), _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.filled_region_count"),
                           QVariant::fromValue(_aggregateVolumes.filledRegionCount), _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.empty_volume"), QVariant::fromValue(_aggregateVolumes.totalEmptyVolume), _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.empty_fraction"),
                           QVariant::fromValue(totalCellVolume ? (_aggregateVolumes.totalEmptyVolume / totalCellVolume)
                                                               : std::numeric_limits<FloatType>::quiet_NaN()),
                           _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.empty_region_count"),
                           QVariant::fromValue(_aggregateVolumes.emptyRegionCount), _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.void_volume"), QVariant::fromValue(_aggregateVolumes.totalVoidVolume),
                           _createdByNode);
        state.addAttribute(QStringLiteral("ConstructSurfaceMesh.void_region_count"), QVariant::fromValue(_aggregateVolumes.voidRegionCount),
                           _createdByNode);

        QString statusString =
            tr("Surface area: %1\n# filled regions (volume): %2 (%3)\n# empty regions (volume): %4 (%5)\n# void regions (volume): %6 (%7)")
                .arg(surfaceArea())
                .arg(_aggregateVolumes.filledRegionCount)
                .arg(_aggregateVolumes.totalFilledVolume)
                .arg(_aggregateVolumes.emptyRegionCount)
                .arg(_aggregateVolumes.totalEmptyVolume)
                .arg(_aggregateVolumes.voidRegionCount)
                .arg(_aggregateVolumes.totalVoidVolume);

        state.setStatus(PipelineStatus(PipelineStatus::Success, std::move(statusString)));
    }
    else {
        state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Surface area: %1").arg(surfaceArea())));
    }
}

/******************************************************************************
 * Injects the computed results of the engine into the data pipeline.
 ******************************************************************************/
void ConstructSurfaceModifier::AlphaShapeEngine::applyResults(PipelineFlowState& state)
{
    ConstructSurfaceEngineBase::applyResults(state);

    if(surfaceParticleSelection() || particleRegionIds()) {
        Particles* particles = state.expectMutableObject<Particles>();
        particles->verifyIntegrity();

        // Output selection of surface particles.
        if(surfaceParticleSelection()) particles->createProperty(surfaceParticleSelection());

        // Output particle region IDs.
        if(particleRegionIds()) particles->createProperty(particleRegionIds());

        // Output per-region particle lists.
        if(_regionParticleLists.isValid()) {
            state.addAttribute(QStringLiteral("ConstructSurfaceMesh.region_memberships"), _regionParticleLists, _createdByNode);
        }
    }
}

}   // End of namespace
