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
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/rendering/SceneRenderer.h>
#include <ovito/core/rendering/CylinderPrimitive.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/particles/objects/ParticleBondMap.h>
#include "BondsVis.h"
#include "ParticlesVis.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondsVis);
OVITO_CLASSINFO(BondsVis, "DisplayName", "Bonds");
IMPLEMENT_ABSTRACT_OVITO_CLASS(BondPickInfo);
DEFINE_PROPERTY_FIELD(BondsVis, bondWidth);
DEFINE_PROPERTY_FIELD(BondsVis, bondColor);
DEFINE_PROPERTY_FIELD(BondsVis, shadingMode);
DEFINE_PROPERTY_FIELD(BondsVis, coloringMode);
DEFINE_PROPERTY_FIELD(BondsVis, visualizeBondOrder);
DEFINE_PROPERTY_FIELD(BondsVis, filledSegments);
DEFINE_PROPERTY_FIELD(BondsVis, filledFraction);
SET_PROPERTY_FIELD_LABEL(BondsVis, bondWidth, "Bond width");
SET_PROPERTY_FIELD_LABEL(BondsVis, bondColor, "Uniform bond color");
SET_PROPERTY_FIELD_LABEL(BondsVis, shadingMode, "Shading mode");
SET_PROPERTY_FIELD_LABEL(BondsVis, coloringMode, "Coloring mode");
SET_PROPERTY_FIELD_LABEL(BondsVis, visualizeBondOrder, "Visualize bond order property");
SET_PROPERTY_FIELD_LABEL(BondsVis, filledSegments, "Number of filled segments");
SET_PROPERTY_FIELD_LABEL(BondsVis, filledFraction, "Filled segment fraction");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(BondsVis, bondWidth, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(BondsVis, filledSegments, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(BondsVis, filledFraction, PercentParameterUnit, 0, 1);

/******************************************************************************
* Computes the bounding box of the visual element.
******************************************************************************/
Box3 BondsVis::boundingBoxImmediate(AnimationTime time, const ConstDataObjectPath& path, const Pipeline* pipeline, const PipelineFlowState& flowState, TimeInterval& validityInterval)
{
    const Bonds* bonds = path.lastAs<Bonds>(0);
    const Particles* particles = path.lastAs<Particles>(1);
    if(!bonds || !particles)
        return {};
    particles->verifyIntegrity();
    bonds->verifyIntegrity();
    const Property* bondTopologyProperty = bonds->getProperty(Bonds::TopologyProperty);
    const Property* bondPeriodicImageProperty = bonds->getProperty(Bonds::PeriodicImageProperty);
    const Property* bondWidthProperty = bonds->getProperty(Bonds::WidthProperty);
    const Property* positionProperty = particles->getProperty(Particles::PositionProperty);
    const SimulationCell* simulationCell = flowState.getObject<SimulationCell>();

    // Compute bounding box from bond data.
    if(bondTopologyProperty && positionProperty) {
        Box3 bbox;

        BufferReadAccess<ParticleIndexPair> bondTopology(bondTopologyProperty);
        BufferReadAccess<Vector3I> bondPeriodicImages(bondPeriodicImageProperty);
        BufferReadAccess<Point3> positions(positionProperty);

        size_t particleCount = positions.size();
        const AffineTransformation cell = simulationCell ? simulationCell->cellMatrix() : AffineTransformation::Zero();

        for(size_t bondIndex = 0; bondIndex < bondTopology.size(); bondIndex++) {
            size_t index1 = bondTopology[bondIndex][0];
            size_t index2 = bondTopology[bondIndex][1];
            if(index1 >= particleCount || index2 >= particleCount)
                continue;

            bbox.addPoint(positions[index1]);
            bbox.addPoint(positions[index2]);
            if(bondPeriodicImages && bondPeriodicImages[bondIndex] != Vector3I::Zero()) {
                Vector3 vec = positions[index2] - positions[index1];
                const Vector3I& pbcShift = bondPeriodicImages[bondIndex];
                for(size_t k = 0; k < 3; k++) {
                    if(pbcShift[k] != 0) vec += cell.column(k) * (FloatType)pbcShift[k];
                }
                bbox.addPoint(positions[index1] + (vec * FloatType(0.5)));
                bbox.addPoint(positions[index2] - (vec * FloatType(0.5)));
            }
        }

        // Extend box to account for width of bonds.
        GraphicsFloatType maxBondWidth = std::max(static_cast<GraphicsFloatType>(bondWidth()), GraphicsFloatType(0));
        if(bondWidthProperty && bondWidthProperty->size() != 0) {
            BufferReadAccess<GraphicsFloatType> widthArray(bondWidthProperty);
            auto minmax = std::minmax_element(widthArray.cbegin(), widthArray.cend());
            if(*minmax.first <= 0)
                maxBondWidth = std::max(maxBondWidth, *minmax.second);
            else
                maxBondWidth = *minmax.second;
        }

        return bbox.padBox(maxBondWidth / FloatType(2) * std::sqrt(FloatType(3)));
    }
    return {};
}

namespace {

// The bond order rendering below (multiple cylinders for double/triple bonds, dashed cylinders for
// fractional bond orders) is ported from BondsVis.cpp of OVITO 3.16.1 and adapted to the 3.14 rendering API.

/// Clamps the bond order to the range [0, 3], as at most 3 cylinders are displayed per bond.
inline GraphicsFloatType clampBondOrder(GraphicsFloatType bondOrder)
{
    return std::clamp(bondOrder, (GraphicsFloatType)0, (GraphicsFloatType)3);
}

/// Checks whether the bond order is fractional.
inline bool isFractionalBond(GraphicsFloatType bondOrder)
{
    bondOrder = clampBondOrder(bondOrder);
    return std::abs(std::round(bondOrder) - bondOrder) > FloatTypeEpsilon<GraphicsFloatType>();
}

/// Estimates the out-of-plane vector (normal) of a bond and its neighbors, used to orient the cylinders of a multiple bond,
/// and the direction towards the center of the local network of fractional bonds, so that dashed bonds are drawn on the inside of rings.
std::pair<Vector3G, Vector3G> getBondNormalVector(const ParticleIndexPair& particleIndices, const ParticleBondMap& bondsMap,
                                                  const BufferReadAccess<Point3>& positions, const std::vector<GraphicsFloatType>& bondOrder,
                                                  const AffineTransformation& cell)
{
    const GraphicsFloatType eps = FloatTypeEpsilon<GraphicsFloatType>();
    Vector3G normal_z = Vector3G::Zero();
    Vector3G normal_x = Vector3G::Zero();
    Vector3G direction = Vector3G::Zero();

    std::array<Vector3G, 2> neighbors;
    for(int64_t index : particleIndices) {
        size_t count = 0;
        for(const BondWithIndex& bond : bondsMap.bondsOfParticle(index)) {
            Vector3 delta = positions[bond.index2] - positions[bond.index1];
            delta += cell * bond.pbcShift.toDataType<FloatType>();
            const Vector3G delta1 = delta.toDataType<GraphicsFloatType>();
            neighbors[count % neighbors.size()] = delta1;

            if(isFractionalBond(bondOrder[bond.bondIndex]))
                direction += delta1;

            if(count >= neighbors.size() - 1) {
                Vector3G localNormal = neighbors[0].cross(neighbors[1]);
                localNormal.normalizeSafely();
                if(localNormal.z() > 0) normal_z += localNormal;
                else normal_z -= localNormal;
                if(localNormal.x() > 0) normal_x += localNormal;
                else normal_x -= localNormal;
            }
            count++;
        }
    }

    if(normal_z.squaredLength() >= eps * eps)
        return {normal_z, direction};
    if(normal_x.squaredLength() >= eps * eps)
        return {normal_x, direction};
    normal_x = direction.cross(Vector3G{0, 0, 1});
    if(normal_x.squaredLength() >= eps * eps)
        return {normal_x, direction};
    return {direction.cross(Vector3G{1, 0, 0}), direction};
}

/// Calculates the (reduced) cylinder width of a bond rendered with the given number of cylinders.
inline GraphicsFloatType getEffectiveBondWidth(const BufferReadAccess<GraphicsFloatType>& bondInputWidths, size_t bondIndex, FloatType bondDiameter, int bondRepCount)
{
    GraphicsFloatType bondWidthValue = (bondInputWidths && bondInputWidths[bondIndex] > GraphicsFloatType(0)) ? bondInputWidths[bondIndex] : static_cast<GraphicsFloatType>(bondDiameter);
    if(bondRepCount == 2)
        bondWidthValue *= (GraphicsFloatType)0.8;
    else if(bondRepCount > 2)
        bondWidthValue *= (GraphicsFloatType)0.63;
    return bondWidthValue;
}

/// Computes the displacement of the i-th cylinder of a multiple bond to avoid overlapping cylinders.
inline Vector3G getBondOffsetVector(int bondRepCount, int bondRepIdx, const Vector3G& gvec, const Vector3G& normal, const Vector3G& shiftDir, GraphicsFloatType bondWidth)
{
    OVITO_ASSERT(bondRepCount >= 1 && bondRepCount <= 3);
    OVITO_ASSERT(bondRepIdx >= 0 && bondRepIdx < bondRepCount);
    constexpr GraphicsFloatType bondShiftFactors[4] = {0, 1, 1.25, 2.5};
    constexpr GraphicsFloatType bondPosOffset[3][3] = {{0, 0, 0}, {0.5, -0.5, 0}, {0, 0.5, -0.5}};
    if(bondRepCount <= 1)
        return Vector3G::Zero();
    // Shift vector points towards the local center of fractional bonds -> dashed bond on the inside of rings.
    Vector3G cross = gvec.cross(normal).safelyNormalized();
    if(shiftDir != Vector3G::Zero() && cross.dot(shiftDir) >= 0)
        cross = -cross;
    return cross * bondPosOffset[bondRepCount - 1][bondRepIdx] * bondShiftFactors[bondRepCount] * bondWidth;
}

/// Number of cylinders of a dashed bond. Always even: with an odd number of filled segments the central segment is split in half.
inline int filledSegmentsToCylinderCount(int filledSegments, FloatType filledFraction)
{
    if(filledSegments <= 0 || filledFraction <= 0) return 0;
    if(filledFraction >= 1) return 2;
    return (filledSegments % 2 == 1) ? filledSegments + 1 : filledSegments;
}

}   // End of anonymous namespace

/******************************************************************************
* Lets the visualization element render the data object.
******************************************************************************/
std::variant<PipelineStatus, Future<PipelineStatus>> BondsVis::render(const ConstDataObjectPath& path, const PipelineFlowState& flowState, FrameGraph& frameGraph, const SceneNode* sceneNode)
{
    const Bonds* bonds = path.lastAs<Bonds>(0);
    const Particles* particles = path.lastAs<Particles>(1);
    if(!bonds || !particles)
        return {};
    particles->verifyIntegrity();
    bonds->verifyIntegrity();
    const Property* bondTopologyProperty = bonds->getProperty(Bonds::TopologyProperty);
    const Property* bondPeriodicImageProperty = bonds->getProperty(Bonds::PeriodicImageProperty);
    const Property* positionProperty = particles->getProperty(Particles::PositionProperty);
    const SimulationCell* simulationCell = flowState.getObject<SimulationCell>();
    const Property* bondTypeProperty = (coloringMode() == ByTypeColoring) ? bonds->getProperty(Bonds::TypeProperty) : nullptr;
    const Property* bondColorProperty = bonds->getProperty(Bonds::ColorProperty);
    const Property* bondWidthProperty = bonds->getProperty(Bonds::WidthProperty);
    const Property* bondSelectionProperty = frameGraph.isInteractive() ? bonds->getProperty(Bonds::SelectionProperty) : nullptr;
    const Property* transparencyProperty = bonds->getProperty(Bonds::TransparencyProperty);
    const Property* bondOrderProperty = visualizeBondOrder() ? bonds->getProperty(QString::fromLatin1(bondOrderPropertyName)) : nullptr;
    if(bondOrderProperty && (bondOrderProperty->componentCount() != 1 || bondOrderProperty->size() != bonds->elementCount()))
        bondOrderProperty = nullptr;

    // Obtain particle-related properties and the vis element.
    const ParticlesVis* particleVis = particles->visElement<ParticlesVis>();
    const Property* particleRadiusProperty = particles->getProperty(Particles::RadiusProperty);
    const Property* particleTransparencyProperty = particles->getProperty(Particles::TransparencyProperty);
    const Property* particleColorProperty = nullptr;
    const Property* particleTypeProperty = nullptr;
    if(coloringMode() == ParticleBasedColoring && particleVis) {
        particleColorProperty = particles->getProperty(Particles::ColorProperty);
        particleTypeProperty = particleVis->getParticleTypeColorProperty(particles);
    }

    // The key type used for caching the rendering primitive:
    using CacheKey = RendererResourceKey<struct BondsVisCache,
        ConstDataObjectRef,     // Bond topology property
        ConstDataObjectRef,     // Bond PBC vector property
        ConstDataObjectRef,     // Particle position property
        ConstDataObjectRef,     // Particle color property
        ConstDataObjectRef,     // Particle type property
        ConstDataObjectRef,     // Particle radius property
        ConstDataObjectRef,     // Bond color property
        ConstDataObjectRef,     // Bond type property
        ConstDataObjectRef,     // Bond selection property
        ConstDataObjectRef,     // Bond transparency
        ConstDataObjectRef,     // Bond width
        ConstDataObjectRef,     // Bond order
        ConstDataObjectRef,     // Simulation cell
        FloatType,              // Bond width
        Color,                  // Bond uniform color
        ColoringMode,           // Bond coloring mode
        ShadingMode,            // Bond shading mode
        int,                    // Filled segments
        FloatType,              // Filled fraction
        bool                    // Render nodal vertices
    >;

    // Make sure the primitive for the nodal vertices gets created if particles display is turned off or if particles are semi-transparent.
    bool renderNodalVertices = !transparencyProperty && !bondWidthProperty && (!particleVis || particleVis->isEnabled() == false || particleTransparencyProperty != nullptr);

    // Lookup the rendering primitive in the vis cache.
    const auto& [cylinders, vertices, pickInfo] = frameGraph.visCache().lookup<std::tuple<CylinderPrimitive, ParticlePrimitive, OORef<BondPickInfo>>>(CacheKey(
            bondTopologyProperty,
            bondPeriodicImageProperty,
            positionProperty,
            particleColorProperty,
            particleTypeProperty,
            particleRadiusProperty,
            bondColorProperty,
            bondTypeProperty,
            bondSelectionProperty,
            transparencyProperty,
            bondWidthProperty,
            bondOrderProperty,
            simulationCell,
            bondWidth(),
            bondColor(),
            coloringMode(),
            shadingMode(),
            filledSegments(),
            filledFraction(),
            renderNodalVertices),
        [&](CylinderPrimitive& cylinders, ParticlePrimitive& vertices, OORef<BondPickInfo>& pickInfo) {
            FloatType bondDiameter = bondWidth();
            const int filledSegments = this->filledSegments();
            const FloatType filledFraction = this->filledFraction();

            // Bond orders, converted to the graphics float type. Only used if some bond is not a single bond.
            std::vector<GraphicsFloatType> bondOrders;
            if(bondOrderProperty) {
                std::vector<FloatType> values(bondOrderProperty->size());
                bondOrderProperty->copyComponentTo(values.begin(), 0);
                if(std::ranges::any_of(values, [](FloatType v) { return v != 1; }))
                    bondOrders.assign(values.begin(), values.end());
            }
            const bool useBondOrder = !bondOrders.empty();
            const int dashedCylinderCount = useBondOrder ? filledSegmentsToCylinderCount(filledSegments, filledFraction) : 0;
            std::vector<int32_t> subobjectToBondMapping;

            if(bondTopologyProperty && positionProperty && bondDiameter > 0) {
                const size_t bondCount = bondTopologyProperty->size();

                // Number of bond representations (parallel cylinder pairs or one dashed bond) of each bond and the total number of cylinders.
                auto bondRepresentation = [&](size_t bondIndex, int& bondRepCount, bool& lastBondRepIsDashed) -> size_t {
                    lastBondRepIsDashed = false;
                    if(!useBondOrder) {
                        bondRepCount = 1;
                        return 2;
                    }
                    GraphicsFloatType intPart;
                    const GraphicsFloatType remainder = std::modf(clampBondOrder(bondOrders[bondIndex]), &intPart);
                    bondRepCount = (int)intPart;
                    size_t cylinderCount = 2 * bondRepCount;
                    if(remainder > FloatTypeEpsilon<GraphicsFloatType>()) {
                        bondRepCount += 1;
                        cylinderCount += dashedCylinderCount;
                        lastBondRepIsDashed = filledFraction < 1;
                    }
                    return cylinderCount;
                };
                size_t totalCylinderCount = 0;
                for(size_t bondIndex = 0; bondIndex < bondCount; bondIndex++) {
                    int bondRepCount; bool dashed;
                    totalCylinderCount += bondRepresentation(bondIndex, bondRepCount, dashed);
                }

                // Make sure we don't exceed our internal limits.
                if(totalCylinderCount > (size_t)std::numeric_limits<int>::max())
                    throw Exception(tr("This version of OVITO cannot render more than %1 bonds.").arg(std::numeric_limits<int>::max() / 2));

                // Allocate buffers for the bonds geometry.
                BufferFactory<Point3G> bondPositions1(totalCylinderCount);
                BufferFactory<Point3G> bondPositions2(totalCylinderCount);
                BufferFactory<ColorG> bondColors(totalCylinderCount);
                BufferFactory<GraphicsFloatType> bondTransparencies = transparencyProperty ? BufferFactory<GraphicsFloatType>(totalCylinderCount) : BufferFactory<GraphicsFloatType>{};
                BufferFactory<GraphicsFloatType> bondWidths = (bondWidthProperty || useBondOrder) ? BufferFactory<GraphicsFloatType>(totalCylinderCount) : BufferFactory<GraphicsFloatType>{};
                if(useBondOrder)
                    subobjectToBondMapping.resize(totalCylinderCount);

                // Allocate buffers for the nodal vertices.
                BufferFactory<ColorG> nodalColors = renderNodalVertices ? BufferFactory<ColorG>(positionProperty->size()) : BufferFactory<ColorG>{};
                BufferFactory<GraphicsFloatType> nodalTransparencies = (renderNodalVertices && transparencyProperty) ? BufferFactory<GraphicsFloatType>(positionProperty->size()) : BufferFactory<GraphicsFloatType>{};
                BufferFactory<int32_t> nodalIndices = renderNodalVertices ? BufferFactory<int32_t>(0) : BufferFactory<int32_t>{};
                boost::dynamic_bitset<> visitedParticles(renderNodalVertices ? positionProperty->size() : 0);
                OVITO_ASSERT(nodalColors || !nodalTransparencies);

                // Cache some values.
                BufferReadAccess<Point3> positions(positionProperty);
                size_t particleCount = positions.size();
                const AffineTransformation cell = simulationCell ? simulationCell->cellMatrix() : AffineTransformation::Zero();

                // Obtain the radii of the particles.
                BufferReadAccessAndRef<GraphicsFloatType> particleRadii;
                if(particleVis)
                    particleRadii = ConstDataBufferPtr(particleVis->particleRadii(particles, false));
                // Make sure the particle radius array has the correct length.
                if(particleRadii && particleRadii.size() != particleCount)
                    particleRadii.reset();

                // Determine half-bond colors.
                std::vector<ColorG> colors = halfBondColors(particles, frameGraph.isInteractive(), coloringMode(), false);
                OVITO_ASSERT(colors.size() == 2 * bondCount);

                // Bond enumerator used to determine the normal vector that orients double/triple bonds.
                std::optional<ParticleBondMap> bondsMap;
                if(useBondOrder)
                    bondsMap.emplace(bondTopologyProperty, bondPeriodicImageProperty);

                // Number of on and off segments of dashed bonds.
                const int numOnSegments = filledSegments;
                const int numOffSegments = filledSegments + 1;

                size_t cylinderIndex = 0;
                BufferReadAccess<ParticleIndexPair> bonds(bondTopologyProperty);
                BufferReadAccess<Vector3I> bondPeriodicImages(bondPeriodicImageProperty);
                BufferReadAccess<GraphicsFloatType> bondInputTransparency(transparencyProperty);
                BufferReadAccess<GraphicsFloatType> bondInputWidths(bondWidthProperty);

                // Appends one cylinder of the given bond.
                auto addCylinder = [&](size_t bondIndex, const Point3G& p1, const Point3G& p2, const ColorG& color, GraphicsFloatType width, GraphicsFloatType transparency) {
                    OVITO_ASSERT(cylinderIndex < totalCylinderCount);
                    bondPositions1[cylinderIndex] = p1;
                    bondPositions2[cylinderIndex] = p2;
                    bondColors[cylinderIndex] = color;
                    if(bondTransparencies) bondTransparencies[cylinderIndex] = transparency;
                    if(bondWidths) bondWidths[cylinderIndex] = width;
                    if(!subobjectToBondMapping.empty()) subobjectToBondMapping[cylinderIndex] = (int32_t)bondIndex;
                    cylinderIndex++;
                };

                for(size_t bondIndex = 0; bondIndex < bondCount; bondIndex++) {
                    int bondRepCount;
                    bool lastBondRepIsDashed;
                    const size_t cylinderCount = bondRepresentation(bondIndex, bondRepCount, lastBondRepIsDashed);
                    if(cylinderCount == 0)
                        continue;   // Bond order 0: nothing is rendered.
                    const size_t firstCylinder = cylinderIndex;

                    size_t particleIndex1 = bonds[bondIndex][0];
                    size_t particleIndex2 = bonds[bondIndex][1];
                    const GraphicsFloatType transparency = bondInputTransparency ? bondInputTransparency[bondIndex] : GraphicsFloatType(0);
                    if(particleIndex1 < particleCount && particleIndex2 < particleCount) {
                        Vector3 vec = positions[particleIndex2] - positions[particleIndex1];
                        bool isSplitBond = false;
                        if(bondPeriodicImageProperty) {
                            for(size_t k = 0; k < 3; k++) {
                                if(int d = bondPeriodicImages[bondIndex][k]) {
                                    vec += cell.column(k) * (FloatType)d;
                                    isSplitBond = true;
                                }
                            }
                        }
                        const Vector3G& gvec = vec.toDataType<GraphicsFloatType>();
                        const GraphicsFloatType length = gvec.length();
                        GraphicsFloatType t = 0.5;
                        GraphicsFloatType blen = length * GraphicsFloatType(2);
                        if(particleRadii && blen != 0) {
                            // This calculation determines the point where to split the bond into the two half-bonds
                            // such that the border appears halfway between the two particles, which may have two different sizes.
                            t = GraphicsFloatType(0.5) + std::min(GraphicsFloatType(0.5), particleRadii[particleIndex1]/blen) - std::min(GraphicsFloatType(0.5), particleRadii[particleIndex2]/blen);
                        }
                        const GraphicsFloatType width = (bondRepCount == 1)
                            ? ((bondInputWidths && bondInputWidths[bondIndex] > GraphicsFloatType(0)) ? bondInputWidths[bondIndex] : (GraphicsFloatType)bondDiameter)
                            : getEffectiveBondWidth(bondInputWidths, bondIndex, bondDiameter, bondRepCount);

                        // Normal vector and local center direction that orient double/triple bonds.
                        Vector3G normal = Vector3G::Zero(), centerVec = Vector3G::Zero();
                        if(bondRepCount > 1) {
                            std::tie(normal, centerVec) = getBondNormalVector(bonds[bondIndex], *bondsMap, positions, bondOrders, cell);
                            if(normal.squaredLength() <= FloatTypeEpsilon<GraphicsFloatType>() * FloatTypeEpsilon<GraphicsFloatType>())
                                normal = Vector3G{0, 0, 1};
                            else
                                normal.normalize();
                        }

                        const Point3G pos1 = positions[particleIndex1].toDataType<GraphicsFloatType>();
                        const Point3G pos2 = positions[particleIndex2].toDataType<GraphicsFloatType>();
                        for(int bondRepIdx = 0; bondRepIdx < bondRepCount; bondRepIdx++) {
                            const Vector3G offsetVec = getBondOffsetVector(bondRepCount, bondRepIdx, gvec, normal, centerVec, width);

                            if(!lastBondRepIsDashed || bondRepIdx < bondRepCount - 1) {
                                // A solid bond made of two cylinders, each representing a half-bond.
                                Point3G a1 = pos1 + offsetVec, a2 = a1 + gvec * t;
                                Point3G b1 = pos2 + offsetVec, b2 = b1 - gvec * (GraphicsFloatType(1) - t);
                                if(isSplitBond) {
                                    std::swap(a1, a2);
                                    std::swap(b1, b2);
                                }
                                addCylinder(bondIndex, a1, a2, colors[2 * bondIndex + 0], width, transparency);
                                addCylinder(bondIndex, b1, b2, colors[2 * bondIndex + 1], width, transparency);
                                if(nodalColors && bondRepCount == 1) {
                                    for(int side = 0; side < 2; side++) {
                                        size_t pindex = side == 0 ? particleIndex1 : particleIndex2;
                                        if(!visitedParticles.test(pindex)) {
                                            nodalColors[pindex] = colors[2 * bondIndex + side];
                                            if(nodalTransparencies)
                                                nodalTransparencies[pindex] = transparency;
                                            visitedParticles.set(pindex);
                                            nodalIndices.push_back(pindex);
                                        }
                                    }
                                }
                            }
                            else if(dashedCylinderCount > 0 && length > FloatTypeEpsilon<GraphicsFloatType>()) {
                                // A dashed bond, generated symmetrically from both ends.
                                const Vector3G bondDir = gvec / length;
                                GraphicsFloatType effectiveBondLength = length;
                                GraphicsFloatType r1 = particleRadii ? particleRadii[particleIndex1] : 0;
                                GraphicsFloatType r2 = particleRadii ? particleRadii[particleIndex2] : 0;
                                effectiveBondLength -= r1 + r2;
                                GraphicsFloatType onSegmentLength = (GraphicsFloatType)filledFraction * effectiveBondLength / numOnSegments;
                                const GraphicsFloatType offSegmentLength = ((GraphicsFloatType)1 - (GraphicsFloatType)filledFraction) * effectiveBondLength / numOffSegments;
                                Point3G currentPos1 = pos1 + offsetVec + r1 * bondDir;
                                Point3G currentPos2 = pos2 + offsetVec - r2 * bondDir;
                                const int numSegmentsPerSide = dashedCylinderCount / 2;
                                for(int segment = 0; segment < numSegmentsPerSide; segment++) {
                                    // With an odd number of filled segments, the central segment is split in half.
                                    if(segment == numSegmentsPerSide - 1 && filledSegments % 2 == 1)
                                        onSegmentLength *= (GraphicsFloatType)0.5;
                                    currentPos1 += bondDir * offSegmentLength;
                                    currentPos2 -= bondDir * offSegmentLength;
                                    const Point3G nextPos1 = currentPos1 + bondDir * onSegmentLength;
                                    const Point3G nextPos2 = currentPos2 - bondDir * onSegmentLength;
                                    addCylinder(bondIndex, currentPos1, nextPos1, colors[2 * bondIndex + 0], width, transparency);
                                    addCylinder(bondIndex, currentPos2, nextPos2, colors[2 * bondIndex + 1], width, transparency);
                                    currentPos1 = nextPos1;
                                    currentPos2 = nextPos2;
                                }
                            }
                        }
                    }

                    // Degenerate or invalid bonds: invisible dummy cylinders keep the cylinder count consistent.
                    while(cylinderIndex < firstCylinder + cylinderCount)
                        addCylinder(bondIndex, Point3G::Origin(), Point3G::Origin(), colors[2 * bondIndex], 0, 0);
                    OVITO_ASSERT(cylinderIndex == firstCylinder + cylinderCount);
                }
                OVITO_ASSERT(cylinderIndex == totalCylinderCount);

                cylinders.setShape(CylinderPrimitive::CylinderShape);
                cylinders.setShadingMode(static_cast<CylinderPrimitive::ShadingMode>(shadingMode()));
                cylinders.setRenderSingleCylinderCap(transparencyProperty != nullptr);
                cylinders.setUniformWidth(bondDiameter);
                cylinders.setWidths(bondWidths.take());
                cylinders.setPositions(bondPositions1.take(), bondPositions2.take());
                cylinders.setColors(bondColors.take());
                cylinders.setTransparencies(bondTransparencies.take());

                if(renderNodalVertices) {
                    OVITO_ASSERT(positionProperty);
                    vertices.setParticleShape(ParticlePrimitive::SphericalShape);
                    vertices.setShadingMode((shadingMode() == NormalShading) ? ParticlePrimitive::NormalShading : ParticlePrimitive::FlatShading);
                    vertices.setRenderingQuality(ParticlePrimitive::HighQuality);
                    vertices.setPositions(positionProperty);
                    vertices.setUniformRadius(0.5 * bondDiameter);
                    vertices.setColors(nodalColors.take());
                    vertices.setIndices(nodalIndices.take());
                    vertices.setTransparencies(nodalTransparencies.take());
                }
            }

            pickInfo = OORef<BondPickInfo>::create(particles, simulationCell, std::move(subobjectToBondMapping));
        });

    if(!cylinders.basePositions())
        return {};

    FrameGraph::RenderingCommandGroup& commandGroup = frameGraph.addCommandGroup(FrameGraph::SceneLayer);
    frameGraph.addPrimitive(commandGroup, std::make_unique<CylinderPrimitive>(cylinders), sceneNode, pickInfo);

    if(renderNodalVertices)
        frameGraph.addPrimitive(commandGroup, std::make_unique<ParticlePrimitive>(vertices), sceneNode);

    return {};
}

/******************************************************************************
* Determines the display colors of half-bonds.
* Returns an array with two colors per full bond, because the two half-bonds
* may have different colors.
******************************************************************************/
std::vector<ColorG> BondsVis::halfBondColors(const Particles* particles, bool highlightSelection, ColoringMode coloringMode, bool ignoreBondColorProperty) const
{
    OVITO_ASSERT(particles != nullptr);
    particles->verifyIntegrity();
    const Bonds* bonds = particles->bonds();
    if(!bonds) return {};
    bonds->verifyIntegrity();

    // Get bond-related properties which determine the bond coloring.
    BufferReadAccess<ParticleIndexPair> topologyProperty = bonds->getProperty(Bonds::TopologyProperty);
    BufferReadAccess<ColorG> bondColorProperty = !ignoreBondColorProperty ? bonds->getProperty(Bonds::ColorProperty) : nullptr;
    const Property* bondTypeProperty = (coloringMode == ByTypeColoring) ? bonds->getProperty(Bonds::TypeProperty) : nullptr;
    BufferReadAccess<SelectionIntType> bondSelectionProperty = highlightSelection ? bonds->getProperty(Bonds::SelectionProperty) : nullptr;

    // Get particle-related properties and the vis element.
    const ParticlesVis* particleVis = particles->visElement<ParticlesVis>();

    std::vector<ColorG> output(bonds->elementCount() * 2);
    const ColorG defaultColor = bondColor().toDataType<GraphicsFloatType>();
    if(bondColorProperty && bondColorProperty.size() * 2 == output.size()) {
        // Take bond colors directly from the color property.
        auto bc = output.begin();
        for(const auto& c : bondColorProperty) {
            *bc++ = c;
            *bc++ = c;
        }
    }
    else if(coloringMode == ParticleBasedColoring && particleVis) {
        // Derive bond colors from particle colors.
        size_t particleCount = particles->elementCount();
        BufferReadAccessAndRef<ColorG> particleColors = particleVis->particleColors(particles, false);
        OVITO_ASSERT(particleColors.size() == particleCount);
        auto bc = output.begin();
        for(const auto& bond : topologyProperty) {
            if((size_t)bond[0] < particleCount && (size_t)bond[1] < particleCount) {
                *bc++ = particleColors[bond[0]];
                *bc++ = particleColors[bond[1]];
            }
            else {
                *bc++ = defaultColor;
                *bc++ = defaultColor;
            }
        }
    }
    else {
        if(bondTypeProperty && bondTypeProperty->size() * 2 == output.size()) {
            // Assign colors based on bond types.
            // Generate a lookup map for bond type colors.
            const std::map<int, ColorG>& colorMap = bondTypeProperty->typeColorMap();
            std::array<ColorG,16> colorArray;
            // Check if all type IDs are within a small, non-negative range.
            // If yes, we can use an array lookup strategy. Otherwise we have to use a dictionary lookup strategy, which is slower.
            if(boost::algorithm::all_of(colorMap, [&colorArray](const auto& i) { return i.first >= 0 && i.first < (int)colorArray.size(); })) {
                colorArray.fill(defaultColor);
                for(const auto& entry : colorMap)
                    colorArray[entry.first] = entry.second;
                // Fill color array.
                BufferReadAccess<int32_t> bondTypeData(bondTypeProperty);
                const int32_t* t = bondTypeData.cbegin();
                for(auto c = output.begin(); c != output.end(); ++t) {
                    if(*t >= 0 && (size_t)*t < colorArray.size()) {
                        *c++ = colorArray[*t];
                        *c++ = colorArray[*t];
                    }
                    else {
                        *c++ = defaultColor;
                        *c++ = defaultColor;
                    }
                }
            }
            else {
                // Fill color array.
                BufferReadAccess<int32_t> bondTypeData(bondTypeProperty);
                const int32_t* t = bondTypeData.cbegin();
                for(auto c = output.begin(); c != output.end(); ++t) {
                    if(auto it = colorMap.find(*t); it != colorMap.end()) {
                        *c++ = it->second;
                        *c++ = it->second;
                    }
                    else {
                        *c++ = defaultColor;
                        *c++ = defaultColor;
                    }
                }
            }
        }
        else {
            // Assign a uniform color to all bonds.
            std::ranges::fill(output, defaultColor);
        }
    }

    // Highlight selected bonds.
    if(bondSelectionProperty && bondSelectionProperty.size() * 2 == output.size()) {
        const ColorG selColor = selectionBondColor();
        const SelectionIntType* t = bondSelectionProperty.cbegin();
        for(auto c = output.begin(); c != output.end(); ++t) {
            if(*t) {
                *c++ = selColor;
                *c++ = selColor;
            }
            else c += 2;
        }
    }

    return output;
}

/******************************************************************************
* Returns a human-readable string describing the picked object,
* which will be displayed in the status bar by OVITO.
******************************************************************************/
QString BondPickInfo::infoString(const Pipeline* pipeline, uint32_t subobjectId)
{
    QString str;
    size_t bondIndex = bondIndexFromSubObjectID(subobjectId);
    if(particles()->bonds()) {
        BufferReadAccess<ParticleIndexPair> topologyProperty = particles()->bonds()->getTopology();
        if(topologyProperty && topologyProperty.size() > bondIndex) {
            size_t index1 = topologyProperty[bondIndex][0];
            size_t index2 = topologyProperty[bondIndex][1];
            str = tr("Bond: ");

            // Bond length
            BufferReadAccess<Point3> posProperty = particles()->getProperty(Particles::PositionProperty);
            if(posProperty && posProperty.size() > index1 && posProperty.size() > index2) {
                const Point3& p1 = posProperty[index1];
                const Point3& p2 = posProperty[index2];
                Vector3 delta = p2 - p1;
                if(BufferReadAccess<Vector3I> periodicImageProperty = particles()->bonds()->getProperty(Bonds::PeriodicImageProperty)) {
                    if(simulationCell()) {
                        delta += simulationCell()->cellMatrix() * periodicImageProperty[bondIndex].toDataType<FloatType>();
                    }
                }
                str += QString("<key>Length:</key> <val>%1</val><sep><key>Delta:</key> <val>%2, %3, %4</val>").arg(delta.length()).arg(delta.x()).arg(delta.y()).arg(delta.z());
            }

            // Bond properties.
            str += QStringLiteral("<sep>");
            str += particles()->bonds()->elementInfoString(bondIndex);

            // Pair type info.
            const Property* typeProperty = particles()->getProperty(Particles::TypeProperty);
            if(typeProperty && typeProperty->size() > index1 && typeProperty->size() > index2) {
                BufferReadAccess<int32_t> typeData(typeProperty);
                const ElementType* type1 = typeProperty->elementType(typeData[index1]);
                const ElementType* type2 = typeProperty->elementType(typeData[index2]);
                if(type1 && type2) {
                    str += QString("<sep><key>Particles:</key> <val>%1 - %2</val>").arg(type1->nameOrNumericId(), type2->nameOrNumericId());
                }
            }
        }
    }
    return str;
}

/******************************************************************************
* Allows the object to parse the serialized contents of a property field in a custom way.
******************************************************************************/
bool BondsVis::loadPropertyFieldFromStream(ObjectLoadStream& stream, const RefMakerClass::SerializedClassInfo::PropertyFieldInfo& serializedField)
{
    // For backward compatibility with OVITO 3.5.4:
    // Parse the "useParticleColors" field, which has been replaced by the "coloringMode" parameter field in later versions.
    if(serializedField.definingClass == &BondsVis::OOClass() && serializedField.identifier == "useParticleColors") {
        bool useParticleColors;
        stream >> useParticleColors;
        setColoringMode(useParticleColors ? ParticleBasedColoring : ByTypeColoring);
        return true;
    }
    return DataVis::loadPropertyFieldFromStream(stream, serializedField);
}

/******************************************************************************
* Determines the display bond widths.
******************************************************************************/
ConstPropertyPtr BondsVis::bondWidths(const Bonds* bonds) const
{
    bonds->verifyIntegrity();

    // Take bond widths directly from the 'Width' property if available.
    ConstPropertyPtr output = bonds->getProperty(Bonds::WidthProperty);
    if(output) {
        // Check if the width array contains any zero entries.
        BufferReadAccess<GraphicsFloatType> widthArray(output);
        if(std::ranges::contains(widthArray, GraphicsFloatType(0))) {
            widthArray.reset();

            // Replace zero entries in the "Width" array with the uniform default width.
            std::ranges::replace(BufferWriteAccess<GraphicsFloatType, access_mode::read_write>(output.makeMutableInplace()), GraphicsFloatType(0), static_cast<GraphicsFloatType>(bondWidth()));
        }
    }
    else {
        // Allocate output array.
        output.reset(Bonds::OOClass().createStandardProperty(DataBuffer::Uninitialized, bonds->elementCount(), Bonds::WidthProperty));

        // Assign the uniform default width to all bonds.
        output.makeMutableInplace()->fill<GraphicsFloatType>(bondWidth());
    }

    return output;
}

}   // End of namespace
