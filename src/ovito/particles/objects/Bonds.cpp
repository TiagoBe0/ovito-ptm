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
#include <ovito/particles/objects/BondsVis.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include "Bonds.h"
#include "BondType.h"
#include "Particles.h"
#include "ParticleBondMap.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(Bonds);
OVITO_CLASSINFO(Bonds, "DisplayName", "Bonds");
OVITO_CLASSINFO(Bonds, "ClassNameAlias", "BondsObject");  // For backward compatibility with OVITO 3.9.2

/******************************************************************************
* Constructor.
******************************************************************************/
void Bonds::initializeObject(ObjectInitializationFlags flags)
{
    PropertyContainer::initializeObject(flags);

    // Assign the default data object identifier.
    setIdentifier(OOClass().pythonName());

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        if(!flags.testFlag(ObjectInitializationFlag::DontCreateVisElement)) {
            // Create and attach a default visualization element for rendering the bonds.
            setVisElement(OORef<BondsVis>::create(flags));
        }
    }
}

/******************************************************************************
* Determines the PBC shift vectors for bonds using the minimum image convention.
******************************************************************************/
void Bonds::generatePeriodicImageProperty(const Particles* particles, const SimulationCell* simulationCellObject)
{
    BufferReadAccess<Point3> posProperty = particles->getProperty(Particles::PositionProperty);
    if(!posProperty) return;

    BufferReadAccess<ParticleIndexPair> bondTopologyProperty = getProperty(Bonds::TopologyProperty);
    if(!bondTopologyProperty) return;

    if(!simulationCellObject)
        return;
    std::array<bool,3> pbcFlags = simulationCellObject->pbcFlags();
    if(!pbcFlags[0] && !pbcFlags[1] && !pbcFlags[2])
        return;
    const AffineTransformation inverseCellMatrix = simulationCellObject->reciprocalCellMatrix();

    auto topoIter = bondTopologyProperty.begin();
    BufferWriteAccess<Vector3I, access_mode::discard_write> bondPeriodicImageProperty = createProperty(Bonds::PeriodicImageProperty);
    for(Vector3I& pbcVec : bondPeriodicImageProperty) {
        size_t particleIndex1 = (*topoIter)[0];
        size_t particleIndex2 = (*topoIter)[1];
        pbcVec.setZero();
        if(particleIndex1 < posProperty.size() && particleIndex2 < posProperty.size()) {
            const Point3& p1 = posProperty[particleIndex1];
            const Point3& p2 = posProperty[particleIndex2];
            Vector3 delta = p1 - p2;
            for(size_t dim = 0; dim < 3; dim++) {
                if(pbcFlags[dim])
                    pbcVec[dim] = std::lround(inverseCellMatrix.prodrow(delta, dim));
            }
        }
        ++topoIter;
    }
}

namespace {

/// Returns whether the property is the bond order property used by BondsVis.
bool isBondOrderProperty(const Property* property)
{
    return property->typeId() == 0 && property->name() == QLatin1String(BondsVis::bondOrderPropertyName) && property->componentCount() == 1;
}

/// Makes the bonds from the given index on single bonds (as in OVITO 3.16). A bond order of 0, which the array
/// would be filled with otherwise, means "no bond" and is not rendered by BondsVis.
void initializeBondOrders(Property* property, size_t firstIndex)
{
    if(property->dataType() == Property::Float64) {
        BufferWriteAccess<double, access_mode::read_write> orders(property);
        std::fill(std::next(orders.begin(), firstIndex), orders.end(), 1.0);
    }
    else if(property->dataType() == Property::Float32) {
        BufferWriteAccess<float, access_mode::read_write> orders(property);
        std::fill(std::next(orders.begin(), firstIndex), orders.end(), 1.0f);
    }
}

}   // End of anonymous namespace

/******************************************************************************
* Creates new bonds making sure bonds are not created twice.
******************************************************************************/
size_t Bonds::addBonds(const std::vector<Bond>& newBonds, BondsVis* bondsVis, const Particles* particles, const std::vector<PropertyPtr>& bondProperties, DataOORef<const BondType> bondType)
{
    OVITO_ASSERT(isSafeToModify());

    if(bondsVis)
        setVisElement(bondsVis);

    // Are there existing bonds?
    if(elementCount() == 0) {
        setElementCount(newBonds.size());

        // Create essential bond properties.
        BufferWriteAccess<ParticleIndexPair, access_mode::discard_write> topologyProperty = createProperty(Bonds::TopologyProperty);
        BufferWriteAccess<Vector3I, access_mode::discard_write> periodicImageProperty = createProperty(Bonds::PeriodicImageProperty);
        Property* bondTypeProperty = bondType ? createProperty(Bonds::TypeProperty) : nullptr;

        // Transfer per-bond data into the standard property arrays.
        auto t = topologyProperty.begin();
        auto pbc = periodicImageProperty.begin();
        for(const Bond& bond : newBonds) {
            OVITO_ASSERT(!particles || bond.index1 < particles->elementCount());
            OVITO_ASSERT(!particles || bond.index2 < particles->elementCount());
            (*t)[0] = bond.index1;
            (*t)[1] = bond.index2;
            ++t;
            *pbc++ = bond.pbcShift;
        }
        topologyProperty.reset();
        periodicImageProperty.reset();

        // Existing "Bond Order" property (e.g. kept by Create bonds after discarding all bonds): new bonds are single bonds.
        for(Property* bondProperty : makePropertiesMutable()) {
            if(isBondOrderProperty(bondProperty))
                initializeBondOrders(bondProperty, 0);
        }

        // Insert bond type.
        if(bondTypeProperty) {
            bondTypeProperty->fill<int32_t>(bondType->numericId());
            if(!bondTypeProperty->elementType(bondType->numericId()))
                bondTypeProperty->addElementType(std::move(bondType));
        }

        // Insert other bond properties.
        for(const auto& bprop : bondProperties) {
            OVITO_ASSERT(bprop);
            OVITO_ASSERT(bprop->size() == newBonds.size());
            OVITO_ASSERT(bprop->typeId() != Bonds::TopologyProperty);
            OVITO_ASSERT(bprop->typeId() != Bonds::PeriodicImageProperty);
            OVITO_ASSERT(!bondTypeProperty || bprop->typeId() != Bonds::TypeProperty);
            createProperty(bprop);
        }

        return newBonds.size();
    }
    else {
        // This is needed to determine which bonds already exist.
        ParticleBondMap bondMap(*this);

        // Check which bonds are new and need to be merged.
        size_t originalBondCount = elementCount();
        size_t outputBondCount = originalBondCount;
        size_t addedBondCount = newBonds.size();
        std::vector<size_t> mapping(addedBondCount);
        for(size_t bondIndex = 0; bondIndex < addedBondCount; bondIndex++) {
            // Check if there is already a bond like this.
            const Bond& bond = newBonds[bondIndex];
            auto existingBondIndex = bondMap.findBond(bond);
            if(existingBondIndex == originalBondCount) {
                // It's a new bond.
                mapping[bondIndex] = outputBondCount;
                outputBondCount++;
            }
            else {
                // It's an already existing bond.
                mapping[bondIndex] = existingBondIndex;
            }
        }
        if(outputBondCount == originalBondCount)
            return 0;

        // Resize the existing property arrays.
        setElementCount(outputBondCount);

        BufferWriteAccess<ParticleIndexPair, access_mode::write> newBondsTopology = expectMutableProperty(Bonds::TopologyProperty);
        BufferWriteAccess<Vector3I, access_mode::write> newBondsPeriodicImages = createProperty(DataBuffer::Initialized, Bonds::PeriodicImageProperty);
        Property* newBondTypeProperty = bondType ? createProperty(DataBuffer::Initialized, Bonds::TypeProperty) : nullptr;

        if(newBondTypeProperty && !newBondTypeProperty->elementType(bondType->numericId()))
            newBondTypeProperty->addElementType(bondType);

        // Copy bonds information into the extended arrays.
        BufferWriteAccess<int32_t, access_mode::write> newBondTypePropertyAccess(newBondTypeProperty);
        for(size_t bondIndex = 0; bondIndex < addedBondCount; bondIndex++) {
            size_t mappedIndex = mapping[bondIndex];
            if(mappedIndex >= originalBondCount) {
                const Bond& bond = newBonds[bondIndex];
                OVITO_ASSERT(!particles || bond.index1 < particles->elementCount());
                OVITO_ASSERT(!particles || bond.index2 < particles->elementCount());
                newBondsTopology[mappedIndex][0] = bond.index1;
                newBondsTopology[mappedIndex][1] = bond.index2;
                newBondsPeriodicImages[mappedIndex] = bond.pbcShift;
                if(newBondTypePropertyAccess)
                    newBondTypePropertyAccess[mappedIndex] = bondType->numericId();
            }
        }
        newBondsTopology.reset();
        newBondsPeriodicImages.reset();
        newBondTypePropertyAccess.reset();

        // Initialize property values of existing properties for new bonds.
        for(Property* bondProperty : makePropertiesMutable()) {
            if(isBondOrderProperty(bondProperty)) {
                initializeBondOrders(bondProperty, originalBondCount);
            }
            else if(bondProperty->typeId() == Bonds::ColorProperty) {
                if(particles) {
                    ConstPropertyPtr bondColors;
                    if(particles->bonds() != this) {
                        // Create a temporary copy of the Particles, which is assigned this Bonds.
                        DataOORef<Particles> particlesCopy = DataOORef<Particles>::makeCopy(particles);
                        particlesCopy->setBonds(this);
                        bondColors = particlesCopy->inputBondColors(true);
                    }
                    else {
                        bondColors = particles->inputBondColors(true);
                    }
                    bondProperty->copyRangeFrom(*bondColors, originalBondCount, originalBondCount, outputBondCount - originalBondCount);
                }
            }
        }

        // Merge new bond properties.
        for(const auto& bprop : bondProperties) {
            OVITO_ASSERT(bprop);
            OVITO_ASSERT(bprop->size() == newBonds.size());
            OVITO_ASSERT(bprop->typeId() != Bonds::TopologyProperty);
            OVITO_ASSERT(bprop->typeId() != Bonds::PeriodicImageProperty);
            OVITO_ASSERT(!bondType || bprop->typeId() != Bonds::TypeProperty);

            Property* property;
            if(bprop->isStandardProperty()) {
                property = createProperty(DataBuffer::Initialized, bprop->typeId());
            }
            else {
                property = createProperty(DataBuffer::Initialized, bprop->name(), bprop->dataType(), bprop->componentCount());
            }

            // Copy bond property data.
            property->mappedCopyFrom(*bprop, mapping, originalBondCount == 0);
        }

        return outputBondCount - originalBondCount;
    }
}

/******************************************************************************
* Returns a property array with the input bond widths.
******************************************************************************/
ConstPropertyPtr Bonds::inputBondWidths() const
{
    // Access the bonds vis element.
    if(BondsVis* bondsVis = visElement<BondsVis>()) {

        // Query bond widths from vis element.
        return bondsVis->bondWidths(this);
    }

    // Return uniform default width for all bonds.
    PropertyPtr buffer = OOClass().createStandardProperty(DataBuffer::Uninitialized, elementCount(), Bonds::WidthProperty);
    buffer->fill<GraphicsFloatType>(1);
    return buffer;
}

/******************************************************************************
* Creates a storage object for standard bond properties.
******************************************************************************/
PropertyPtr Bonds::OOMetaClass::createStandardPropertyInternal(DataBuffer::BufferInitialization init, size_t elementCount, int type, const ConstDataObjectPath& containerPath) const
{
    // Initialize memory if requested.
    if(init == DataBuffer::Initialized && containerPath.size() >= 2) {
        // Certain standard properties need to be initialized with default values determined by the attached visual elements.
        if(type == ColorProperty) {
            if(const Particles* particles = dynamic_object_cast<Particles>(containerPath[containerPath.size()-2])) {
                ConstPropertyPtr property = particles->inputBondColors();
                OVITO_ASSERT(property && property->size() == elementCount && property->typeId() == ColorProperty);
                return std::move(property).makeMutable();
            }
        }
        else if(type == WidthProperty) {
            if(const Bonds* bonds = dynamic_object_cast<Bonds>(containerPath.back())) {
                OVITO_ASSERT(bonds->elementCount() == elementCount);
                ConstPropertyPtr property = bonds->inputBondWidths();
                OVITO_ASSERT(property);
                OVITO_ASSERT(property->size() == elementCount);
                OVITO_ASSERT(property->typeId() == WidthProperty);
                return std::move(property).makeMutable();
            }
        }
    }

    int dataType;
    size_t componentCount;

    switch(type) {
    case SelectionProperty:
        dataType = Property::IntSelection;
        componentCount = 1;
        break;
    case TypeProperty:
        dataType = Property::Int32;
        componentCount = 1;
        break;
    case TransparencyProperty:
    case WidthProperty:
        dataType = Property::FloatGraphics;
        componentCount = 1;
        break;
    case LengthProperty:
        dataType = Property::FloatDefault;
        componentCount = 1;
        break;
    case ColorProperty:
        dataType = Property::FloatGraphics;
        componentCount = 3;
        break;
    case TopologyProperty:
    case ParticleIdentifiersProperty:
        dataType = Property::Int64;
        componentCount = 2;
        break;
    case PeriodicImageProperty:
        dataType = Property::Int32;
        componentCount = 3;
        break;
    default:
        OVITO_ASSERT_MSG(false, "Bonds::createStandardPropertyInternal", "Invalid standard property type");
        throw Exception(tr("This is not a valid standard bond property type: %1").arg(type));
    }

    const QStringList& componentNames = standardPropertyComponentNames(type);
    const QString& propertyName = standardPropertyName(type);

    OVITO_ASSERT(componentCount == standardPropertyComponentCount(type));

    PropertyPtr property = PropertyPtr::create(DataBuffer::Uninitialized, elementCount, dataType, componentCount, propertyName, type, componentNames);

    if(init == DataBuffer::Initialized) {
        // Default-initialize property values with zeros.
        property->fillZero();
    }

    return property;
}

/******************************************************************************
* Registers all standard properties with the property traits class.
******************************************************************************/
void Bonds::OOMetaClass::initialize()
{
    PropertyContainerClass::initialize();

    setPropertyClassDisplayName(tr("Bonds"));
    setElementDescriptionName(QStringLiteral("bonds"));
    setPythonName(QStringLiteral("bonds"));

    const QStringList emptyList;
    const QStringList abList = QStringList() << "A" << "B";
    const QStringList xyzList = QStringList() << "X" << "Y" << "Z";
    const QStringList rgbList = QStringList() << "R" << "G" << "B";
    const QStringList onetwoList = QStringList() << "1" << "2";

    registerStandardProperty(TypeProperty, tr("Bond Type"), Property::Int32, emptyList, &BondType::OOClass(), tr("Bond types"));
    registerStandardProperty(SelectionProperty, tr("Selection"), Property::IntSelection, emptyList);
    registerStandardProperty(ColorProperty, tr("Color"), Property::FloatGraphics, rgbList, nullptr, tr("Bond colors"));
    registerStandardProperty(LengthProperty, tr("Length"), Property::FloatDefault, emptyList, nullptr, tr("Lengths"));
    registerStandardProperty(TopologyProperty, tr("Topology"), Property::Int64, abList);
    registerStandardProperty(PeriodicImageProperty, tr("Periodic Image"), Property::Int32, xyzList);
    registerStandardProperty(TransparencyProperty, tr("Transparency"), Property::FloatGraphics, emptyList);
    registerStandardProperty(ParticleIdentifiersProperty, tr("Particle Identifiers"), Property::Int64, onetwoList);
    registerStandardProperty(WidthProperty, tr("Width"), Property::FloatGraphics, emptyList, nullptr, tr("Widths"));
}

/******************************************************************************
* Returns the default color for a numeric type ID.
******************************************************************************/
Color Bonds::OOMetaClass::getElementTypeDefaultColor(const OwnerPropertyRef& property, const QString& typeName, int numericTypeId, bool loadUserDefaults) const
{
    if(property.typeId() == Bonds::TypeProperty) {

        // Initial standard colors assigned to new bond types:
        static const Color defaultTypeColors[] = {
            Color(1.0,  1.0,  0.0), // 0
            Color(0.7,  0.0,  1.0), // 1
            Color(0.2,  1.0,  1.0), // 2
            Color(1.0,  0.4,  1.0), // 3
            Color(0.4,  1.0,  0.4), // 4
            Color(1.0,  0.4,  0.4), // 5
            Color(0.4,  0.4,  1.0), // 6
            Color(1.0,  1.0,  0.7), // 7
            Color(0.97, 0.97, 0.97) // 8
        };
        return defaultTypeColors[std::abs(numericTypeId) % std::size(defaultTypeColors)];
    }

    return PropertyContainerClass::getElementTypeDefaultColor(property, typeName, numericTypeId, loadUserDefaults);
}

/******************************************************************************
* Returns the index of the element that was picked in a viewport.
******************************************************************************/
std::pair<size_t, ConstDataObjectPath> Bonds::OOMetaClass::elementFromPickResult(const ViewportWindow::PickResult& pickResult) const
{
    // Check if a bond was picked.
    if(BondPickInfo* pickInfo = dynamic_object_cast<BondPickInfo>(pickResult.pickInfo().get())) {
        size_t bondIndex = pickInfo->bondIndexFromSubObjectID(pickResult.subobjectId());
        if(pickInfo->particles()->bonds() && bondIndex < pickInfo->particles()->bonds()->elementCount()) {
            return std::make_pair(bondIndex, ConstDataObjectPath{{pickInfo->particles(), pickInfo->particles()->bonds()}});
        }
    }

    return std::pair<size_t, DataObjectPath>(std::numeric_limits<size_t>::max(), {});
}

/******************************************************************************
* Tries to remap an index from one property container to another, considering the
* possibility that elements may have been added or removed.
******************************************************************************/
size_t Bonds::OOMetaClass::remapElementIndex(const ConstDataObjectPath& source, size_t elementIndex, const ConstDataObjectPath& dest) const
{
    const Bonds* sourceBonds = static_object_cast<Bonds>(source.back());
    const Bonds* destBonds = static_object_cast<Bonds>(dest.back());
    const Particles* sourceParticles = dynamic_object_cast<Particles>(source.size() >= 2 ? source[source.size()-2] : nullptr);
    const Particles* destParticles = dynamic_object_cast<Particles>(dest.size() >= 2 ? dest[dest.size()-2] : nullptr);
    if(sourceParticles && destParticles) {

        // Make sure the topology information is present.
        if(BufferReadAccess<ParticleIndexPair> sourceTopology = sourceBonds->getProperty(TopologyProperty)) {
            if(BufferReadAccess<ParticleIndexPair> destTopology = destBonds->getProperty(TopologyProperty)) {

                // If unique IDs are available try to use them to look up the bond in the other data collection.
                if(BufferReadAccess<int64_t> sourceIdentifiers = sourceParticles->getProperty(Particles::IdentifierProperty)) {
                    if(BufferReadAccess<int64_t> destIdentifiers = destParticles->getProperty(Particles::IdentifierProperty)) {
                        size_t index_a = sourceTopology[elementIndex][0];
                        size_t index_b = sourceTopology[elementIndex][1];
                        if(index_a < sourceIdentifiers.size() && index_b < sourceIdentifiers.size()) {
                            int64_t id_a = sourceIdentifiers[index_a];
                            int64_t id_b = sourceIdentifiers[index_b];

                            // Quick test if the bond storage order is the same.
                            if(elementIndex < destTopology.size()) {
                                size_t index2_a = destTopology[elementIndex][0];
                                size_t index2_b = destTopology[elementIndex][1];
                                if(index2_a < destIdentifiers.size() && index2_b < destIdentifiers.size()) {
                                    if(destIdentifiers[index2_a] == id_a && destIdentifiers[index2_b] == id_b) {
                                        return elementIndex;
                                    }
                                }
                            }

                            // Determine the indices of the two particles connected by the bond.
                            auto it_a = std::ranges::find(destIdentifiers, id_a);
                            auto it_b = std::ranges::find(destIdentifiers, id_b);
                            if(it_a != destIdentifiers.cend() && it_b != destIdentifiers.cend()) {
                                size_t index2_a = std::distance(destIdentifiers.cbegin(), it_a);
                                size_t index2_b = std::distance(destIdentifiers.cbegin(), it_b);
                                // Go through the whole bonds list to see if there is a bond connecting the particles with
                                // the same IDs.
                                for(const auto& bond : destTopology) {
                                    if((bond[0] == index2_a && bond[1] == index2_b) || (bond[0] == index2_b && bond[1] == index2_a)) {
                                        return (&bond - destTopology.cbegin());
                                    }
                                }
                            }
                        }

                        // Give up.
                        return PropertyContainerClass::remapElementIndex(source, elementIndex, dest);
                    }
                }

                // Try to find matching bond based on particle indices alone.
                if(BufferReadAccess<Point3> sourcePos = sourceParticles->getProperty(Particles::PositionProperty)) {
                    if(BufferReadAccess<Point3> destPos = destParticles->getProperty(Particles::PositionProperty)) {
                        size_t index_a = sourceTopology[elementIndex][0];
                        size_t index_b = sourceTopology[elementIndex][1];
                        if(index_a < sourcePos.size() && index_b < sourcePos.size()) {

                            // Quick check if number of particles and bonds didn't change.
                            if(sourcePos.size() == destPos.size() && sourceTopology.size() == destTopology.size()) {
                                size_t index2_a = destTopology[elementIndex][0];
                                size_t index2_b = destTopology[elementIndex][1];
                                if(index_a == index2_a && index_b == index2_b) {
                                    return elementIndex;
                                }
                            }

                            // Find matching bond by means of particle positions.
                            const Point3& pos_a = sourcePos[index_a];
                            const Point3& pos_b = sourcePos[index_b];
                            auto it_a = std::ranges::find(destPos, pos_a);
                            auto it_b = std::ranges::find(destPos, pos_b);
                            if(it_a != destPos.cend() && it_b != destPos.cend()) {
                                size_t index2_a = std::distance(destPos.cbegin(), it_a);
                                size_t index2_b = std::distance(destPos.cbegin(), it_b);
                                // Go through the whole bonds list to see if there is a bond connecting the particles with
                                // the same positions.
                                for(const auto& bond : destTopology) {
                                    if((bond[0] == index2_a && bond[1] == index2_b) || (bond[0] == index2_b && bond[1] == index2_a)) {
                                        return (&bond - destTopology.cbegin());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Give up.
    return PropertyContainerClass::remapElementIndex(source, elementIndex, dest);
}

/******************************************************************************
* Determines which elements are located within the given
* viewport fence region (=2D polygon).
******************************************************************************/
ConstPropertyPtr Bonds::OOMetaClass::viewportFenceSelection(const QVector<Point2>& fence, const ConstDataObjectPath& objectPath, Pipeline* pipeline, const Matrix4& projectionTM) const
{
    const Bonds* bonds = static_object_cast<Bonds>(objectPath.back());
    const Particles* particles = dynamic_object_cast<Particles>(objectPath.size() >= 2 ? objectPath[objectPath.size()-2] : nullptr);

    if(particles) {
        if(BufferReadAccess<ParticleIndexPair> topologyProperty = bonds->getProperty(Bonds::TopologyProperty)) {
            if(BufferReadAccess<Point3> posProperty = particles->getProperty(Particles::PositionProperty)) {

                if(!bonds->visElement() || bonds->visElement()->isEnabled() == false)
                    throw Exception(tr("Cannot select bonds while the corresponding visual element is disabled. Please enable the display of bonds first."));

                // Create the output property storage.
                PropertyPtr selection = Particles::OOClass().createStandardProperty(DataBuffer::Uninitialized, topologyProperty.size(), Bonds::SelectionProperty, objectPath);

                BufferWriteAccess<SelectionIntType, access_mode::discard_write> selectionAcc{selection};
                parallelFor(topologyProperty.size(), 16000, TaskProgress::Ignore, [&](size_t index) {
                    selectionAcc[index] = 0;
                    const ParticleIndexPair& t = topologyProperty[index];
                    int insideCount = 0;
                    for(size_t i = 0; i < 2; i++) {
                        if(t[i] >= (qlonglong)posProperty.size())
                            continue;
                        const Point3& p = posProperty[t[i]];

                        // Project particle center to screen coordinates.
                        Point3 projPos = projectionTM * p;

                        // Perform z-clipping.
                        if(std::abs(projPos.z()) >= FloatType(1))
                            break;

                        // Perform point-in-polygon test.
                        int intersectionsRight = 0;
                        for(auto p2 = fence.constBegin(), p1 = p2 + (fence.size()-1); p2 != fence.constEnd(); p1 = p2++) {
                            if(p1->y() == p2->y())
                                continue;
                            if(projPos.y() >= p1->y() && projPos.y() >= p2->y())
                                continue;
                            if(projPos.y() < p1->y() && projPos.y() < p2->y())
                                continue;
                            FloatType xint = (projPos.y() - p2->y()) / (p1->y() - p2->y()) * (p1->x() - p2->x()) + p2->x();
                            if(xint >= projPos.x())
                                intersectionsRight++;
                        }
                        if(intersectionsRight & 1)
                            insideCount++;
                    }
                    if(insideCount == 2)
                        selectionAcc[index] = 1;
                });

                return selection;
            }
        }
    }

    // Give up.
    return PropertyContainerClass::viewportFenceSelection(fence, objectPath, pipeline, projectionTM);
}

/******************************************************************************
 * Returns the data for visualizing a vector property from this container using a VectorVis element.
 ******************************************************************************/
VectorVis::VectorData Bonds::getVectorVisData(const ConstDataObjectPath& path, const PipelineFlowState& state,
                                              const RendererResourceCache::ResourceFrame& visCache) const
{
    OVITO_ASSERT(path.lastAs<Bonds>(1) == this);
    verifyIntegrity();

    if(const Particles* particles = path.lastAs<Particles>(2)) {
        const Property* positionProperty = particles->getProperty(Particles::PositionProperty);
        const Property* bondTopologyProperty = getProperty(Bonds::TopologyProperty);
        const Property* bondPeriodicImageProperty = getProperty(Bonds::PeriodicImageProperty);
        if(positionProperty && bondTopologyProperty) {
            const SimulationCell* simulationCell = state.getObject<SimulationCell>();

            // Look up the bond centers in the cache.
            const auto& basePositions = visCache.lookup<ConstDataBufferPtr>(
                RendererResourceKey<struct BondCentersCache, ConstDataObjectRef, ConstDataObjectRef>{ particles, simulationCell },
                [&](ConstDataBufferPtr& basePositions) {
                    // Compute bond centers.
                    BufferFactory<Point3> centers(elementCount());
                    BufferReadAccess<ParticleIndexPair> bondTopology(bondTopologyProperty);
                    BufferReadAccess<Vector3I> bondPeriodicImages(bondPeriodicImageProperty);
                    BufferReadAccess<Point3> positions(positionProperty);

                    size_t particleCount = positions.size();
                    const AffineTransformation cell = simulationCell ? simulationCell->cellMatrix() : AffineTransformation::Zero();

                    for(size_t bondIndex = 0; bondIndex < bondTopology.size(); bondIndex++) {
                        size_t index1 = bondTopology[bondIndex][0];
                        size_t index2 = bondTopology[bondIndex][1];
                        if(index1 >= particleCount || index2 >= particleCount) {
                            centers[bondIndex] = Point3::Origin();
                            continue;
                        }

                        Vector3 vec = positions[index2] - positions[index1];
                        if(bondPeriodicImageProperty) {
                            for(size_t k = 0; k < 3; k++) {
                                if(int d = bondPeriodicImages[bondIndex][k]) {
                                    vec += cell.column(k) * (FloatType)d;
                                }
                            }
                        }
                        centers[bondIndex] = positions[index1] + FloatType(0.5) * vec;
                    }
                    basePositions = centers.take();
                });
            return {basePositions, path.lastAs<DataBuffer>(), nullptr, nullptr, nullptr};
        }
    }
    return {};
}

}   // End of namespace
