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
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <voro++.hh>
#include "BaderChargeIntegrationModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BaderChargeIntegrationModifier);
OVITO_CLASSINFO(BaderChargeIntegrationModifier, "DisplayName", "Bader charge integration");
OVITO_CLASSINFO(BaderChargeIntegrationModifier, "Description", "Partition a density field into Bader volumes and integrate quantities over them.");
OVITO_CLASSINFO(BaderChargeIntegrationModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(BaderChargeIntegrationModifier, subject);
DEFINE_PROPERTY_FIELD(BaderChargeIntegrationModifier, densityProperty);
DEFINE_PROPERTY_FIELD(BaderChargeIntegrationModifier, integrandProperty);
SET_PROPERTY_FIELD_LABEL(BaderChargeIntegrationModifier, subject, "Operate on");
SET_PROPERTY_FIELD_LABEL(BaderChargeIntegrationModifier, densityProperty, "Density");
SET_PROPERTY_FIELD_LABEL(BaderChargeIntegrationModifier, integrandProperty, "Integrand");

namespace {

/// A neighbor of a grid point in the Voronoi tessellation of the grid lattice.
struct GridNeighbor {
    Vector3I offset;
    double coupling;    ///< Facet area divided by the distance.
};

/// Facets of the Voronoi cell of a grid lattice point.
std::vector<GridNeighbor> voronoiNeighbors(const Matrix3& spacing)
{
    const double extent = 2 * (spacing.column(0).length() + spacing.column(1).length() + spacing.column(2).length());
    voro::voronoicell_neighbor cell;
    cell.init(-extent, extent, -extent, extent, -extent, extent);
    std::vector<Vector3I> candidates;
    for(int i = -2; i <= 2; i++) for(int j = -2; j <= 2; j++) for(int k = -2; k <= 2; k++) {
        if(i == 0 && j == 0 && k == 0) continue;
        const Vector3 r = spacing * Vector3(i, j, k);
        candidates.emplace_back(i, j, k);
        cell.nplane(r.x(), r.y(), r.z(), r.squaredLength(), (int)candidates.size());
    }
    std::vector<int> ids;
    std::vector<double> areas;
    cell.neighbors(ids);
    cell.face_areas(areas);
    const double minArea = 1e-10 * std::abs(spacing.determinant()) / extent;
    std::vector<GridNeighbor> neighbors;
    for(size_t f = 0; f < ids.size(); f++) {
        if(ids[f] < 1 || areas[f] <= minArea) continue;
        const Vector3I& offset = candidates[ids[f] - 1];
        neighbors.push_back({offset, areas[f] / (spacing * offset.toDataType<FloatType>()).length()});
    }
    return neighbors;
}

}   // End of anonymous namespace

/******************************************************************************
* Partitions the grid into Bader regions and integrates over them.
******************************************************************************/
BaderChargeIntegrationModifier::Partition BaderChargeIntegrationModifier::partition(const std::vector<double>& density,
        const std::vector<double>& integrand, size_t components, const VoxelGrid::GridDimensions& shape, const Matrix3& spacing,
        const std::array<bool, 3>& pbc)
{
    const size_t count = shape[0] * shape[1] * shape[2];
    OVITO_ASSERT(density.size() == count && integrand.size() == count * components);
    const std::vector<GridNeighbor> neighbors = voronoiNeighbors(spacing);
    const double voxelVolume = std::abs(spacing.determinant());

    // Grid index of a neighbor, or -1 if it lies outside a non-periodic grid.
    auto neighborIndex = [&](size_t index, const Vector3I& offset) -> int64_t {
        int64_t coord[3] = {(int64_t)(index % shape[0]), (int64_t)((index / shape[0]) % shape[1]), (int64_t)(index / (shape[0] * shape[1]))};
        for(int d = 0; d < 3; d++) {
            const int64_t n = (int64_t)shape[d];
            coord[d] += offset[d];
            if(coord[d] < 0 || coord[d] >= n) {
                if(!pbc[d]) return -1;
                coord[d] = ((coord[d] % n) + n) % n;
            }
        }
        return coord[0] + (int64_t)shape[0] * (coord[1] + (int64_t)shape[1] * coord[2]);
    };

    // Grid points in order of decreasing density.
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t(0));
    std::ranges::sort(order, [&](size_t a, size_t b) { return density[a] > density[b] || (density[a] == density[b] && a < b); });

    // Interior points carry a single region with weight 1; boundary points a list of (region, weight) pairs.
    Partition result;
    std::vector<int32_t> region(count, -1);
    std::vector<int32_t> boundarySlot(count, -1);
    std::vector<std::vector<std::pair<int32_t, double>>> boundaryWeights;
    std::vector<std::pair<size_t, double>> uphill;
    std::vector<std::pair<int32_t, double>> accumulated;
    for(size_t point : order) {
        const double rho = density[point];
        uphill.clear();
        double total = 0;
        for(const GridNeighbor& neighbor : neighbors) {
            const int64_t other = neighborIndex(point, neighbor.offset);
            if(other < 0 || density[other] <= rho) continue;
            const double flux = neighbor.coupling * (density[other] - rho);
            uphill.emplace_back((size_t)other, flux);
            total += flux;
        }
        if(!uphill.empty() && !(total > 0)) {
            // All fluxes underflowed (subnormal density differences); 0/0 weights would turn every result into NaN.
            uphill.clear();
            for(const GridNeighbor& neighbor : neighbors) {
                const int64_t other = neighborIndex(point, neighbor.offset);
                if(other >= 0 && density[other] > rho) {
                    uphill.emplace_back((size_t)other, neighbor.coupling);
                    total += neighbor.coupling;
                }
            }
        }
        if(uphill.empty()) {
            // Exact ties (a maximum between grid points, or a plateau such as vacuum with zero density): join the
            // neighbors of equal density that were already processed, weighted by the geometric coupling alone.
            // Otherwise every point of the plateau would start its own region. OVITO Pro 3.16.1 also splits such
            // plateaus into spurious regions and may divide a single maximum between two of them.
            for(const GridNeighbor& neighbor : neighbors) {
                const int64_t other = neighborIndex(point, neighbor.offset);
                if(other >= 0 && density[other] == rho && region[other] >= 0) {
                    uphill.emplace_back((size_t)other, neighbor.coupling);
                    total += neighbor.coupling;
                }
            }
        }
        if(uphill.empty()) {
            // A density maximum starts a new region.
            region[point] = (int32_t)result.regionCount++;
            result.maxima.push_back(point);
            continue;
        }
        bool interior = true;
        for(const auto& [other, flux] : uphill) {
            if(boundarySlot[other] >= 0 || region[other] != region[uphill.front().first]) {
                interior = false;
                break;
            }
        }
        if(interior) {
            region[point] = region[uphill.front().first];
            continue;
        }
        accumulated.clear();
        auto add = [&](int32_t r, double w) {
            for(auto& entry : accumulated)
                if(entry.first == r) { entry.second += w; return; }
            accumulated.emplace_back(r, w);
        };
        for(const auto& [other, flux] : uphill) {
            const double j = flux / total;
            if(boundarySlot[other] < 0)
                add(region[other], j);
            else
                for(const auto& [r, w] : boundaryWeights[boundarySlot[other]])
                    add(r, j * w);
        }
        if(accumulated.size() == 1) {
            region[point] = accumulated.front().first;
        }
        else {
            region[point] = std::ranges::max_element(accumulated, {}, &std::pair<int32_t, double>::second)->first;
            boundarySlot[point] = (int32_t)boundaryWeights.size();
            boundaryWeights.push_back(accumulated);
        }
    }

    // Integration: sum_X V_X w_A(X) f(X).
    const size_t numRegions = result.regionCount;
    result.integratedDensity.assign(numRegions, 0.0);
    result.integratedVolume.assign(numRegions, 0.0);
    result.integratedValues.assign(numRegions * components, 0.0);
    result.dominantRegion = std::move(region);
    result.dominantWeight.assign(count, 1.0);
    auto integrate = [&](size_t point, int32_t r, double w) {
        result.integratedDensity[r] += voxelVolume * w * density[point];
        result.integratedVolume[r] += voxelVolume * w;
        for(size_t c = 0; c < components; c++)
            result.integratedValues[r * components + c] += voxelVolume * w * integrand[point * components + c];
    };
    for(size_t point = 0; point < count; point++) {
        if(boundarySlot[point] < 0) {
            integrate(point, result.dominantRegion[point], 1.0);
        }
        else {
            double maxWeight = 0;
            for(const auto& [r, w] : boundaryWeights[boundarySlot[point]]) {
                integrate(point, r, w);
                maxWeight = std::max(maxWeight, w);
            }
            result.dominantWeight[point] = maxWeight;
        }
    }
    return result;
}

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool BaderChargeIntegrationModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<VoxelGrid>();
}

/******************************************************************************
* Is called by the system when the modifier has been inserted into a pipeline.
******************************************************************************/
void BaderChargeIntegrationModifier::initializeModifier(const ModifierInitializationRequest& request)
{
    Modifier::initializeModifier(request);

    // Use the first voxel grid and its first property as inputs when the modifier is newly created.
    if(!densityProperty() && subject().dataPath().isEmpty() && this_task::isInteractive()) {
        const PipelineFlowState& input = request.modificationNode()->evaluateInput(request).blockForResult();
        if(const VoxelGrid* grid = input.getObject<VoxelGrid>()) {
            setSubject(PropertyContainerReference(&grid->getOOMetaClass(), grid->identifier()));
            for(const Property* property : grid->properties()) {
                setDensityProperty(PropertyReference(property, (property->componentCount() > 1) ? 0 : -1));
                break;
            }
        }
    }
}

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void BaderChargeIntegrationModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> BaderChargeIntegrationModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!subject() || subject().dataClass() != &VoxelGrid::OOClass())
        throw Exception(tr("Please select an input voxel grid."));
    if(!densityProperty())
        throw Exception(tr("Please select the density field."));

    // In interactive mode, reuse the results in the pipeline cache if available.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true))
            if(const DataTable* table = cachedState.getObjectBy<DataTable>(request.modificationNode(), QString::fromLatin1(tableIdentifier)))
                state.addObject(table);
        return std::move(state);
    }

    const VoxelGrid* grid = static_object_cast<VoxelGrid>(state.expectLeafObject(subject()));
    grid->verifyIntegrity();
    if(grid->domain()->is2D())
        throw Exception(tr("Bader analysis requires a three-dimensional voxel grid."));

    // Reads one component (or all components if component < 0) of a floating-point grid property.
    auto readField = [&](const PropertyReference& ref, size_t& components, QString& name) {
        QString error;
        auto [property, component] = ref.findInContainerWithComponent(grid, error);
        if(!property)
            throw Exception(error);
        if(property->dataType() != Property::Float32 && property->dataType() != Property::Float64)
            throw Exception(tr("The grid property '%1' must be floating-point valued.").arg(property->name()));
        components = (component >= 0) ? 1 : property->componentCount();
        name = (component >= 0) ? ref.nameWithComponent() : property->name();
        std::vector<double> values(property->size() * components);
        RawBufferReadAccess access(property);
        for(size_t i = 0; i < property->size(); i++)
            for(size_t c = 0; c < components; c++)
                values[i * components + c] = access.get<double>(i, component >= 0 ? (size_t)component : c);
        return values;
    };
    size_t densityComponents;
    QString densityName;
    std::vector<double> density = readField(densityProperty(), densityComponents, densityName);
    if(densityComponents != 1)
        throw Exception(tr("The density must be a scalar field. Please select one component of '%1'.").arg(densityName));
    const bool separateIntegrand = integrandProperty() && integrandProperty() != densityProperty();
    size_t integrandComponents = 0;
    QString integrandName;
    std::vector<double> integrand;
    if(separateIntegrand)
        integrand = readField(integrandProperty(), integrandComponents, integrandName);

    // Grid lattice vectors and point positions.
    const SimulationCell* domain = grid->domain();
    const VoxelGrid::GridDimensions shape = grid->shape();
    const bool cellData = (grid->gridType() == VoxelGrid::GridType::CellData);
    Matrix3 spacing;
    std::array<bool, 3> pbc;
    for(int d = 0; d < 3; d++) {
        pbc[d] = domain->hasPbc(d);
        if(shape[d] < 1 || (!cellData && !pbc[d] && shape[d] < 2))
            throw Exception(tr("The voxel grid is too small."));
        spacing.column(d) = domain->cellMatrix().column(d) / (FloatType)((cellData || pbc[d]) ? shape[d] : shape[d] - 1);
    }
    const Vector3 pointOffset = cellData ? spacing * Vector3(0.5, 0.5, 0.5) : Vector3::Zero();

    const Particles* particles = state.getObject<Particles>();
    ConstPropertyPtr particlePositions = particles ? particles->getProperty(Particles::PositionProperty) : nullptr;

    return asyncLaunch([
            state = std::move(state),
            density = std::move(density),
            integrand = std::move(integrand),
            integrandComponents, integrandName, separateIntegrand,
            shape, spacing, pbc, pointOffset,
            domain = DataOORef<const SimulationCell>(domain),
            particlePositions = std::move(particlePositions),
            subject = subject(),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Bader charge integration"));

        const size_t components = separateIntegrand ? integrandComponents : 0;
        Partition result = partition(density, integrand, components, shape, spacing, pbc);
        const size_t numRegions = result.regionCount;

        // Positions of the density maxima.
        auto pointPosition = [&](size_t index) {
            const Vector3 ijk(index % shape[0], (index / shape[0]) % shape[1], index / (shape[0] * shape[1]));
            return domain->cellOrigin() + spacing * ijk + pointOffset;
        };

        // Map each region to the nearest particle.
        std::vector<int64_t> mappedParticle(numRegions, -1);
        std::vector<double> mappedDistance(numRegions, 0);
        if(particlePositions && particlePositions->size() != 0) {
            BufferReadAccess<Point3> positions(particlePositions);
            for(size_t r = 0; r < numRegions; r++) {
                const Point3 p = pointPosition(result.maxima[r]);
                double best = std::numeric_limits<double>::max();
                for(size_t i = 0; i < positions.size(); i++) {
                    const double d = domain->wrapVector(positions[i] - p).squaredLength();
                    if(d < best) { best = d; mappedParticle[r] = (int64_t)i; }
                }
                mappedDistance[r] = std::sqrt(best);
            }
        }
        const bool mapped = particlePositions && particlePositions->size() != 0;
        const QString integrandColumn = QStringLiteral("Integrated ") + integrandName;

        // Table with one row per region.
        DataTable* table = state.createObject<DataTable>(QString::fromLatin1(tableIdentifier), createdByNode, DataTable::None, tr("Bader Charge Integration Results"));
        table->setElementCount(numRegions);
        std::ranges::copy(result.integratedDensity, BufferWriteAccess<FloatType, access_mode::discard_write>(table->createProperty(DataBuffer::Uninitialized, QStringLiteral("Integrated Density"), Property::FloatDefault)).begin());
        std::ranges::copy(result.integratedVolume, BufferWriteAccess<FloatType, access_mode::discard_write>(table->createProperty(DataBuffer::Uninitialized, QStringLiteral("Integrated Volume"), Property::FloatDefault)).begin());
        {
            BufferWriteAccess<Point3, access_mode::discard_write> maxima(table->createProperty(DataBuffer::Uninitialized, QStringLiteral("Position"), Property::FloatDefault, 3, QStringList{"X", "Y", "Z"}));
            for(size_t r = 0; r < numRegions; r++) maxima[r] = pointPosition(result.maxima[r]);
        }
        if(separateIntegrand) {
            BufferWriteAccess<FloatType*, access_mode::discard_write> values(table->createProperty(DataBuffer::Uninitialized, integrandColumn, Property::FloatDefault, components));
            for(size_t r = 0; r < numRegions; r++)
                for(size_t c = 0; c < components; c++) values.set(r, c, result.integratedValues[r * components + c]);
        }
        if(mapped) {
            std::ranges::copy(mappedDistance, BufferWriteAccess<FloatType, access_mode::discard_write>(table->createProperty(DataBuffer::Uninitialized, QStringLiteral("Mapped Particle Distance"), Property::FloatDefault)).begin());
            std::ranges::copy(mappedParticle, BufferWriteAccess<int64_t, access_mode::discard_write>(table->createProperty(DataBuffer::Uninitialized, QStringLiteral("Mapped Particle Index"), Property::Int64)).begin());
        }

        // Voxel properties.
        VoxelGrid* outputGrid = static_object_cast<VoxelGrid>(state.expectMutableLeafObject(subject));
        std::ranges::copy(result.dominantRegion, BufferWriteAccess<int32_t, access_mode::discard_write>(outputGrid->createProperty(DataBuffer::Uninitialized, QStringLiteral("Region"), Property::Int32)).begin());
        std::ranges::copy(result.dominantWeight, BufferWriteAccess<FloatType, access_mode::discard_write>(outputGrid->createProperty(DataBuffer::Uninitialized, QStringLiteral("Maximum Region Weight"), Property::FloatDefault)).begin());
        if(mapped) {
            BufferWriteAccess<int64_t, access_mode::discard_write> voxelParticle(outputGrid->createProperty(DataBuffer::Uninitialized, QStringLiteral("Mapped Particle Index"), Property::Int64));
            for(size_t i = 0; i < voxelParticle.size(); i++) voxelParticle[i] = mappedParticle[result.dominantRegion[i]];
        }

        // Per-particle sums over the regions mapped to each particle.
        if(mapped) {
            Particles* outputParticles = state.expectMutableObject<Particles>();
            BufferWriteAccess<FloatType, access_mode::read_write> particleDensity(outputParticles->createProperty(DataBuffer::Initialized, QStringLiteral("Integrated Density"), Property::FloatDefault));
            BufferWriteAccess<FloatType, access_mode::read_write> particleVolume(outputParticles->createProperty(DataBuffer::Initialized, QStringLiteral("Integrated Volume"), Property::FloatDefault));
            for(size_t r = 0; r < numRegions; r++) {
                particleDensity[mappedParticle[r]] += result.integratedDensity[r];
                particleVolume[mappedParticle[r]] += result.integratedVolume[r];
            }
            if(separateIntegrand) {
                BufferWriteAccess<FloatType*, access_mode::read_write> particleValues(outputParticles->createProperty(DataBuffer::Initialized, integrandColumn, Property::FloatDefault, components));
                for(size_t r = 0; r < numRegions; r++)
                    for(size_t c = 0; c < components; c++)
                        particleValues.set(mappedParticle[r], c, particleValues.get(mappedParticle[r], c) + result.integratedValues[r * components + c]);
            }
        }

        state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Found %1 Bader regions.").arg(numRegions)));
        return std::move(state);
    });
}

}   // End of namespace
