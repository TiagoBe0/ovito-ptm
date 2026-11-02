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
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/app/PluginManager.h>
#include <cmath>
#include <limits>
#include "SpatialBinningModifier.h"
#include "BinningLineSource.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SpatialBinningModifier);
IMPLEMENT_ABSTRACT_OVITO_CLASS(BinningLineSource);
OVITO_CLASSINFO(SpatialBinningModifier, "DisplayName", "Spatial binning");
OVITO_CLASSINFO(SpatialBinningModifier, "Description", "Project a particle property, or the dislocation density and Nye tensor, onto a 1D, 2D or 3D grid of bins aligned with the simulation cell.");
OVITO_CLASSINFO(SpatialBinningModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, operateOn);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, sourceProperty);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, reductionOperation);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, binDirection);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, numberOfBinsX);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, numberOfBinsY);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, numberOfBinsZ);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, firstDerivative);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, onlySelectedElements);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, fixPropertyAxisRange);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, propertyAxisRangeStart);
DEFINE_PROPERTY_FIELD(SpatialBinningModifier, propertyAxisRangeEnd);
DEFINE_REFERENCE_FIELD(SpatialBinningModifier, voxelGridVis);
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, sourceProperty, "Input property");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, reductionOperation, "Reduction operation");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, binDirection, "Binning direction");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, numberOfBinsX, "Number of bins (1st binned dimension)");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, numberOfBinsY, "Number of bins (2nd binned dimension)");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, numberOfBinsZ, "Number of bins (3rd binned dimension)");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, firstDerivative, "Compute first derivative");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, onlySelectedElements, "Use only selected elements");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, fixPropertyAxisRange, "Fix property axis range");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, propertyAxisRangeStart, "From");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, propertyAxisRangeEnd, "To");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, voxelGridVis, "Voxel grid");
SET_PROPERTY_FIELD_LABEL(SpatialBinningModifier, operateOn, "Operate on");
SET_PROPERTY_FIELD_UNITS_AND_RANGE(SpatialBinningModifier, numberOfBinsX, IntegerParameterUnit, 1, 100000);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(SpatialBinningModifier, numberOfBinsY, IntegerParameterUnit, 1, 100000);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(SpatialBinningModifier, numberOfBinsZ, IntegerParameterUnit, 1, 100000);

/******************************************************************************
* Asks the metaclass whether the modifier can be applied to the given input data.
******************************************************************************/
bool SpatialBinningModifier::SpatialBinningModifierClass::isApplicableTo(const DataCollection& input) const
{
    if(const Particles* p = input.getObject<Particles>())
        if(p->getProperty(Particles::PositionProperty) != nullptr)
            return true;
    return findLineSource(input) != nullptr;
}

/******************************************************************************
* Returns the line source that handles the line data in the given data collection, if any.
******************************************************************************/
OORef<BinningLineSource> SpatialBinningModifier::findLineSource(const DataCollection& input)
{
    for(const BinningLineSource::OOMetaClass* clazz : PluginManager::instance().metaclassMembers<BinningLineSource>()) {
        OORef<BinningLineSource> source = static_object_cast<BinningLineSource>(clazz->createInstance());
        if(source->hasLines(input))
            return source;
    }
    return {};
}

/******************************************************************************
* Constructor.
******************************************************************************/
void SpatialBinningModifier::initializeObject(ObjectInitializationFlags flags)
{
    Modifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        // Create the vis element for rendering the 2D/3D output grid.
        setVoxelGridVis(OORef<VoxelGridVis>::create(flags));
        voxelGridVis()->setObjectTitle(tr("Binning grid"));
    }
}

/******************************************************************************
* This method is called by the system when the modifier has been inserted
* into a pipeline.
******************************************************************************/
void SpatialBinningModifier::initializeModifier(const ModifierInitializationRequest& request)
{
    Modifier::initializeModifier(request);

    // Use the last available particle property from the input state as data source when the modifier is newly created.
    if(!sourceProperty() && this_task::isInteractive()) {
        const PipelineFlowState& input = request.modificationNode()->evaluateInput(request).blockForResult();
        if(!input.getObject<Particles>() && input.data() && findLineSource(*input.data()) != nullptr)
            setOperateOn(OperateOnDislocations);
        if(const Particles* particles = input.getObject<Particles>()) {
            PropertyReference bestProperty;
            for(const Property* property : particles->properties()) {
                if(property->typeId() != Particles::PositionProperty && property->typeId() != Particles::SelectionProperty)
                    bestProperty = PropertyReference(property);
            }
            setSourceProperty(bestProperty);
        }
    }
    updateColorMapping();
}

/******************************************************************************
* Is called when the value of a property of this object has changed.
******************************************************************************/
void SpatialBinningModifier::propertyChanged(const PropertyFieldDescriptor* field)
{
    if(field == PROPERTY_FIELD(sourceProperty) || field == PROPERTY_FIELD(operateOn) || field == PROPERTY_FIELD(fixPropertyAxisRange)
            || field == PROPERTY_FIELD(propertyAxisRangeStart) || field == PROPERTY_FIELD(propertyAxisRangeEnd)) {
        if(!isBeingLoaded() && !isUndoingOrRedoing())
            updateColorMapping();
    }
    Modifier::propertyChanged(field);
}

/******************************************************************************
* Points the color mapping of the voxel grid vis element at the output property.
******************************************************************************/
void SpatialBinningModifier::updateColorMapping()
{
    if(!voxelGridVis() || !voxelGridVis()->colorMapping())
        return;
    PropertyColorMapping* mapping = voxelGridVis()->colorMapping();
    // The output grid property carries the name of the input property (one component if a component was picked).
    // For a whole vector property the user still has to pick a component in the vis element.
    if(operateOn() == OperateOnDislocations)
        mapping->setSourceProperty(PropertyReference(QStringLiteral("Density")));
    else
        mapping->setSourceProperty(sourceProperty() ? sourceProperty() : PropertyReference(QStringLiteral("Count")));
    if(fixPropertyAxisRange()) {
        mapping->setStartValue(propertyAxisRangeStart());
        mapping->setEndValue(propertyAxisRangeEnd());
    }
}


/******************************************************************************
* Determines the grid dimensionality, the cell vector of each grid axis and the grid shape.
******************************************************************************/
SpatialBinningModifier::OutputLayout SpatialBinningModifier::outputLayout(const SimulationCell* cell) const
{
    OutputLayout layout;
    const BinDirection direction = binDirection();
    layout.ndim = dimensionality(direction);
    if(layout.ndim == 0)
        throw Exception(tr("No binning direction selected."));

    // As in OVITO Pro, the binned cell vectors are mapped in order onto the grid axes, and the bin counts
    // apply to the grid axes: a 1D binning along Y uses numberOfBinsX(), an XZ binning yields an
    // (numberOfBinsX() x numberOfBinsY() x 1) grid. binDims[k] is the cell vector of grid axis k.
    int k = 0;
    for(int d = 0; d < 3; d++)
        if(isBinned(direction, d)) layout.binDims[k++] = d;
    for(int d = 0; d < 3; d++)
        if(!isBinned(direction, d)) layout.binDims[k++] = d;
    const int requestedBins[3] = { numberOfBinsX(), numberOfBinsY(), numberOfBinsZ() };
    for(int k = 0; k < 3; k++)
        layout.shape[k] = (k < layout.ndim) ? (size_t)std::max(1, requestedBins[k]) : 1;
    return layout;
}

/******************************************************************************
* Creates the output table (1D) or voxel grid (2D/3D) on the main thread; the worker only fills it.
******************************************************************************/
std::pair<DataTable*, VoxelGrid*> SpatialBinningModifier::createOutput(PipelineFlowState& state, const ModifierEvaluationRequest& request,
        const SimulationCell* cell, const OutputLayout& layout, const QString& label) const
{
    const auto& binDims = layout.binDims;
    if(layout.ndim == 1) {
        // Title, plot mode and axis label as in OVITO Pro.
        DataTable* table = state.createObject<DataTable>(QStringLiteral("binning"), request.modificationNode(), DataTable::Histogram, label);
        table->setAxisLabelX(tr("Position"));
        // Like OVITO Pro, the x-axis runs from 0 to the length of the binned cell vector (the cell origin is ignored).
        table->setIntervalStart(0);
        table->setIntervalEnd(cell->matrix().column(binDims[0]).length());
        return {table, nullptr};
    }
    VoxelGrid* grid = state.createObjectWithVis<VoxelGrid>(QStringLiteral("binning"), request.modificationNode(), voxelGridVis(),
        tr("Binning (%1)").arg(label));
    if(layout.ndim == 3) {
        grid->setDomain(cell);
    }
    else {
        // A 2D grid spans the two binned cell vectors; the third domain vector is a unit placeholder, as in OVITO Pro.
        AffineTransformation domain = AffineTransformation::Identity();
        domain.column(0) = cell->matrix().column(binDims[0]);
        domain.column(1) = cell->matrix().column(binDims[1]);
        domain.column(2) = Vector3(0, 0, 1);
        domain.column(3) = cell->matrix().column(3);
        DataOORef<SimulationCell> domainCell = DataOORef<SimulationCell>::create(ObjectInitializationFlag::DontCreateVisElement, domain,
            cell->hasPbcCorrected(binDims[0]), cell->hasPbcCorrected(binDims[1]), false, true);
        grid->setDomain(std::move(domainCell));
    }
    grid->setShape(layout.shape);
    return {nullptr, grid};
}

/******************************************************************************
* Replaces a 1D profile (component-major, n bins of width h) by its first derivative,
* using central finite differences (one-sided at non-periodic ends).
******************************************************************************/
void SpatialBinningModifier::differentiate(std::vector<double>& values, size_t componentCount, size_t n, double h, bool periodic)
{
    std::vector<double> deriv(values.size(), 0.0);
    for(size_t c = 0; c < componentCount && n > 1; c++) {
        const double* y = values.data() + c * n;
        for(size_t b = 0; b < n; b++) {
            if(periodic)
                deriv[c * n + b] = (y[(b + 1) % n] - y[(b + n - 1) % n]) / (2 * h);
            else if(b == 0)
                deriv[c * n + b] = (y[1] - y[0]) / h;
            else if(b == n - 1)
                deriv[c * n + b] = (y[n - 1] - y[n - 2]) / h;
            else
                deriv[c * n + b] = (y[b + 1] - y[b - 1]) / (2 * h);
        }
    }
    values.swap(deriv);
}

/******************************************************************************
* Dislocations mode: computes the dislocation density and the Nye tensor on the grid.
******************************************************************************/
Future<PipelineFlowState> SpatialBinningModifier::evaluateDislocations(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    const SimulationCell* cell = state.expectObject<SimulationCell>();
    if(cell->volume3D() <= FLOATTYPE_EPSILON)
        throw Exception(tr("Simulation cell is degenerate."));

    OORef<BinningLineSource> source = state.data() ? findLineSource(*state.data()) : OORef<BinningLineSource>{};
    if(!source)
        throw Exception(tr("The input data collection contains no Dislocations data object."));

    // Like OVITO Pro, the dislocation field is only computed for orthogonal, axis-aligned cells.
    const AffineTransformation& m = cell->matrix();
    const FloatType tolerance = FloatType(1e-9) * std::max({m(0, 0), m(1, 1), m(2, 2)});   // Tolerates round-off tilts of LAMMPS boxes.
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            if(i != j && std::abs(m(i, j)) > tolerance)
                throw Exception(tr("Dislocation field can only be calculated for simulation cells that are orthogonal and axis-aligned. "
                                   "The input simulation cell does not fulfill these conditions."));

    std::vector<BinningLineSource::Segment> segments;
    source->collectSegments(*state.data(), segments);

    const OutputLayout layout = outputLayout(cell);
    const bool derivative = firstDerivative() && layout.ndim == 1;
    auto [table, grid] = createOutput(state, request, cell, layout, QStringLiteral("Dislocations"));

    return asyncLaunch([
            state = std::move(state),
            segments = std::move(segments),
            layout, table, grid, derivative,
            inverseMatrix = cell->inverseMatrix(),
            pbc = cell->pbcFlagsCorrected(),
            binLength = cell->matrix().column(layout.binDims[0]).length(),
            cellVolume = cell->volume3D()]() mutable
    {
        const auto& shape = layout.shape;
        const auto& binDims = layout.binDims;
        const int ndim = layout.ndim;
        const size_t numBins = shape[0] * shape[1] * shape[2];
        const double binVolume = cellVolume / numBins;

        // Component-major accumulators: density (1 component) and Nye tensor (9 components, row-major b_i t_j).
        std::vector<double> density(numBins, 0.0);
        std::vector<double> nye(9 * numBins, 0.0);

        // Bin-center coordinate along grid axis k at segment parameter s: u = n_k * reduced - 1/2.
        // Node j has the hat weight max(0, 1 - |u - j|); the weights of a straight segment are thus piecewise products of
        // linear functions of s (degree <= 3) between the parameters where some u crosses an integer, so a 2-point
        // Gauss-Legendre rule per piece integrates them exactly.
        const double gaussOffset = 0.5 / std::sqrt(3.0);
        std::vector<double> breaks;
        for(const BinningLineSource::Segment& segment : segments) {
            this_task::throwIfCanceled();
            const Vector3 delta = segment.b - segment.a;
            const double length = delta.length();
            if(length <= 0)
                continue;
            const Point3 ra = inverseMatrix * segment.a;
            const Point3 rb = inverseMatrix * segment.b;

            // Points must lie inside the cell along non-periodic cell vectors (binned or not).
            for(int d = 0; d < 3; d++) {
                if(pbc[d]) continue;
                const double eps = 1e-9;
                if(ra[d] < -eps || ra[d] > 1 + eps || rb[d] < -eps || rb[d] > 1 + eps)
                    throw Exception(SpatialBinningModifier::tr("A dislocation segment is located outside the simulation domain in a non-periodic direction. "
                        "Please ensure that all dislocation segments are either aligned with a periodic direction or entirely contained within the primary simulation cell."));
            }

            double u0[3], du[3];
            for(int k = 0; k < ndim; k++) {
                const int d = binDims[k];
                u0[k] = ra[d] * shape[k] - 0.5;
                du[k] = (rb[d] - ra[d]) * shape[k];
            }
            breaks.assign({0.0, 1.0});
            for(int k = 0; k < ndim; k++) {
                if(du[k] == 0) continue;
                const double lo = std::min(u0[k], u0[k] + du[k]), hi = std::max(u0[k], u0[k] + du[k]);
                for(double j = std::floor(lo) + 1; j < hi; j += 1)
                    breaks.push_back((j - u0[k]) / du[k]);
            }
            std::sort(breaks.begin(), breaks.end());

            const Vector3& b = segment.burgersVector;
            const Vector3 t = delta / length;
            for(size_t p = 0; p + 1 < breaks.size(); p++) {
                const double s0 = breaks[p], s1 = breaks[p + 1];
                const double ds = s1 - s0;
                if(ds <= 0) continue;
                const double sm = 0.5 * (s0 + s1);
                // Lower node of each axis on this piece (the piece lies between two integers of every u).
                long base[3] = {0, 0, 0};
                for(int k = 0; k < ndim; k++)
                    base[k] = (long)std::floor(u0[k] + sm * du[k]);
                for(double s : {sm - gaussOffset * ds, sm + gaussOffset * ds}) {
                    const double quadratureWeight = 0.5 * ds * length;
                    double frac[3] = {0, 0, 0};
                    for(int k = 0; k < ndim; k++)
                        frac[k] = (u0[k] + s * du[k]) - base[k];
                    for(int corner = 0; corner < (1 << ndim); corner++) {
                        double w = quadratureWeight;
                        std::array<size_t, 3> coords = {0, 0, 0};
                        for(int k = 0; k < ndim; k++) {
                            const int bit = (corner >> k) & 1;
                            w *= bit ? frac[k] : (1.0 - frac[k]);
                            long j = base[k] + bit;
                            const long n = (long)shape[k];
                            if(pbc[binDims[k]])
                                j = ((j % n) + n) % n;
                            else
                                j = std::clamp(j, 0L, n - 1);  // Beyond the outer bin centers, the weight goes to the boundary bin.
                            coords[k] = (size_t)j;
                        }
                        if(w == 0) continue;
                        const size_t bin = VoxelGrid::voxelIndex(coords, shape);
                        density[bin] += w;
                        for(int i = 0; i < 3; i++)
                            for(int j = 0; j < 3; j++)
                                nye[(i * 3 + j) * numBins + bin] += w * b[i] * t[j];
                    }
                }
            }
        }
        for(double& v : density) v /= binVolume;
        for(double& v : nye) v /= binVolume;

        // As in OVITO Pro, the derivative applies to both outputs and keeps their names.
        if(derivative) {
            differentiate(density, 1, numBins, binLength / numBins, pbc[binDims[0]]);
            differentiate(nye, 9, numBins, binLength / numBins, pbc[binDims[0]]);
        }

        PropertyContainer* container = table ? static_cast<PropertyContainer*>(table) : static_cast<PropertyContainer*>(grid);
        container->setElementCount(numBins);
        Property* densityProperty = container->createProperty(DataBuffer::Uninitialized, QStringLiteral("Density"), Property::Float64, 1);
        Property* nyeProperty = container->createProperty(DataBuffer::Uninitialized, QStringLiteral("Nye Tensor"), Property::Float64, 9,
            QStringList{QStringLiteral("XX"), QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YX"), QStringLiteral("YY"),
                        QStringLiteral("YZ"), QStringLiteral("ZX"), QStringLiteral("ZY"), QStringLiteral("ZZ")});
        {
            BufferWriteAccess<double, access_mode::discard_write> densityAcc(densityProperty);
            BufferWriteAccess<double*, access_mode::discard_write> nyeAcc(nyeProperty);
            for(size_t bin = 0; bin < numBins; bin++) {
                densityAcc[bin] = density[bin];
                for(size_t c = 0; c < 9; c++)
                    nyeAcc.set(bin, c, nye[c * numBins + bin]);
            }
        }
        if(table)
            table->setY(densityProperty);

        state.setStatus(PipelineStatus(PipelineStatus::Success,
            SpatialBinningModifier::tr("Binned %1 dislocation segments into %2 bins.").arg(segments.size()).arg(numBins)));
        return std::move(state);
    });
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> SpatialBinningModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(operateOn() == OperateOnDislocations)
        return evaluateDislocations(request, std::move(state));

    const Particles* particles = state.expectObject<Particles>();
    particles->verifyIntegrity();
    const SimulationCell* cell = state.expectObject<SimulationCell>();
    if(cell->volume3D() <= FLOATTYPE_EPSILON)
        throw Exception(tr("Simulation cell is degenerate."));

    ConstPropertyPtr positions = particles->expectProperty(Particles::PositionProperty);

    // Get the input property (none = every particle contributes 1).
    ConstPropertyPtr property;
    int vecComponent = -1;
    if(sourceProperty()) {
        QString errorDescription;
        // Without an explicit component, a vector property is binned component by component (vecComponent = -1).
        std::tie(property, vecComponent) = sourceProperty().findInContainerWithComponent(particles, errorDescription, false);
        if(!property)
            throw Exception(std::move(errorDescription));
    }

    ConstPropertyPtr inputSelection;
    if(onlySelectedElements())
        inputSelection = particles->expectProperty(Particles::SelectionProperty);

    const OutputLayout layout = outputLayout(cell);
    const int ndim = layout.ndim;
    const auto& binDims = layout.binDims;
    const auto& shape = layout.shape;
    const int binnedDim = binDims[0];

    QString outputName = property ? property->nameWithComponent(vecComponent) : QStringLiteral("Count");
    QStringList componentNames = (property && vecComponent < 0) ? property->componentNames() : QStringList();
    size_t componentCount = (property && vecComponent < 0) ? property->componentCount() : 1;

    // The first derivative is only defined for a 1D profile; OVITO Pro ignores the option otherwise.
    const bool derivative = firstDerivative() && ndim == 1;
    if(derivative)
        outputName = QStringLiteral("d(%1)/d(Position)").arg(outputName);
    auto [table, grid] = createOutput(state, request, cell, layout, property ? property->nameWithComponent(vecComponent) : QStringLiteral("Count"));

    return asyncLaunch([
            state = std::move(state),
            positions = std::move(positions),
            property = std::move(property),
            inputSelection = std::move(inputSelection),
            vecComponent, componentCount, componentNames, outputName,
            shape, ndim, binDims, binnedDim, table, grid, derivative,
            inverseMatrix = cell->inverseMatrix(),
            pbc = cell->pbcFlagsCorrected(),
            binLength = cell->matrix().column(binnedDim).length(),
            binVolume = cell->volume3D() / (shape[0] * shape[1] * shape[2]),
            op = reductionOperation()]() mutable
    {
        const size_t numBins = shape[0] * shape[1] * shape[2];
        const size_t numParticles = positions->size();

        // Per-particle input values, component-major.
        std::vector<double> values(componentCount * numParticles, 1.0);
        if(property) {
            for(size_t c = 0; c < componentCount; c++)
                property->copyComponentTo(values.begin() + c * numParticles, vecComponent >= 0 ? vecComponent : (int)c);
        }
        std::vector<SelectionIntType> selection;
        if(inputSelection) {
            selection.resize(inputSelection->size());
            inputSelection->copyTo(selection.begin());
        }

        const double init = (op == Min) ? std::numeric_limits<double>::infinity() : (op == Max) ? -std::numeric_limits<double>::infinity() : 0.0;
        std::vector<double> acc(componentCount * numBins, init);
        std::vector<size_t> counts(numBins, 0);

        BufferReadAccess<Point3> posAcc(positions);
        size_t numBinned = 0;
        for(size_t i = 0; i < numParticles; i++) {
            if(!selection.empty() && !selection[i])
                continue;
            this_task::throwIfCanceled();
            const Point3 reduced = inverseMatrix * posAcc[i];
            std::array<size_t, 3> coords = {0, 0, 0};
            bool inside = true;
            // Axes that are not binned do not restrict which particles are counted.
            for(int k = 0; k < ndim && inside; k++) {
                const int d = binDims[k];
                double f = reduced[d];
                if(pbc[d])
                    f -= std::floor(f);
                else if(f < 0.0 || f > 1.0) {
                    // Outside a non-periodic cell boundary.
                    inside = false;
                    continue;
                }
                // f can be exactly 1.0 (on the upper boundary, or after wrapping a tiny negative value).
                coords[k] = std::min((size_t)(f * (double)shape[k]), shape[k] - 1);
            }
            if(!inside)
                continue;
            const size_t bin = VoxelGrid::voxelIndex(coords, shape);
            counts[bin]++;
            numBinned++;
            for(size_t c = 0; c < componentCount; c++) {
                double v = values[c * numParticles + i];
                double& a = acc[c * numBins + bin];
                switch(op) {
                case Min: a = std::min(a, v); break;
                case Max: a = std::max(a, v); break;
                default: a += v; break;
                }
            }
        }

        // Finalize the reduction; empty bins are reported as 0 for every operation.
        for(size_t c = 0; c < componentCount; c++) {
            for(size_t b = 0; b < numBins; b++) {
                double& a = acc[c * numBins + b];
                if(counts[b] == 0) a = 0;
                else if(op == Mean) a /= counts[b];
                else if(op == SumVol) a /= binVolume;
            }
        }

        if(derivative)
            differentiate(acc, componentCount, numBins, binLength / numBins, pbc[binnedDim]);

        // Write the output property.
        PropertyContainer* container = table ? static_cast<PropertyContainer*>(table) : static_cast<PropertyContainer*>(grid);
        container->setElementCount(numBins);
        Property* output = container->createProperty(DataBuffer::Uninitialized, outputName, Property::FloatDefault, componentCount, componentNames);
        {
            BufferWriteAccess<FloatType*, access_mode::discard_write> outAcc(output);
            for(size_t b = 0; b < numBins; b++)
                for(size_t c = 0; c < componentCount; c++)
                    outAcc.set(b, c, (FloatType)acc[c * numBins + b]);
        }
        if(table)
            table->setY(output);

        state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Binned %1 particles into %2 bins.").arg(numBinned).arg(numBins)));
        return std::move(state);
    });
}

}   // End of namespace
