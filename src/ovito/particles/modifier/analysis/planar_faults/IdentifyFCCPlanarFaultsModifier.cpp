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
#include <ovito/particles/util/NearestNeighborFinder.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/properties/ElementType.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include "IdentifyFCCPlanarFaultsModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(IdentifyFCCPlanarFaultsModifier);
OVITO_CLASSINFO(IdentifyFCCPlanarFaultsModifier, "DisplayName", "Identify fcc planar faults");
OVITO_CLASSINFO(IdentifyFCCPlanarFaultsModifier, "Description", "Classify hcp-like atoms in fcc crystals as stacking faults or twin boundaries.");
OVITO_CLASSINFO(IdentifyFCCPlanarFaultsModifier, "ModifierCategory", "Structure identification");
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorParticles);
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorParticles, "Color atoms by fault type");
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorNonHCP);
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorOther);
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorISF);
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorTwin);
DEFINE_PROPERTY_FIELD(IdentifyFCCPlanarFaultsModifier, colorMulti);
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorNonHCP, "Non-hcp");
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorOther, "Other hcp-like");
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorISF, "Intrinsic stacking fault");
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorTwin, "Coherent twin boundary");
SET_PROPERTY_FIELD_LABEL(IdentifyFCCPlanarFaultsModifier, colorMulti, "Multi-layer stacking fault");

namespace {

/// Numeric id of the hcp structure type, identical in the PTM and CNA modifiers.
constexpr int32_t HCP = 2;

/// Bits stored per hcp-like atom after the first pass.
enum : uint8_t {
    EnvValid = 1 << 0,      ///< 6 basal neighbors and 3 + 3 out-of-plane neighbors were found
    UpperHCP = 1 << 1,      ///< The neighboring layer on one side is hcp-like
    LowerHCP = 1 << 2,      ///< The neighboring layer on the other side is hcp-like
    BasalHCP = 1 << 3,      ///< The atom's own layer is hcp-like around it
};

/// At least this many of the 3 out-of-plane neighbors on one side must be hcp-like to call the adjacent
/// layer hcp-like. Majority vote, so a single misclassified neighbor near a partial dislocation does not count.
constexpr int MIN_HCP_OUT_OF_PLANE = 2;

/// At least this many of the 6 basal neighbors must be hcp-like for a single layer to count as a twin boundary.
constexpr int MIN_HCP_IN_PLANE = 2;

/// The local geometry of a single hcp-like atom.
struct LocalEnvironment
{
    bool valid = false;
    int basalHCP = 0;
    int sideHCP[2] = {0, 0};
    size_t outOfPlane[6];
    int side[6];
    FloatType basalArea = 0;
};

/// Splits the 12 nearest neighbors of an hcp-like atom into basal-plane and out-of-plane neighbors.
LocalEnvironment analyzeEnvironment(const NearestNeighborFinder& finder, size_t index, const BufferReadAccess<int32_t>& structures)
{
    LocalEnvironment env;
    NearestNeighborFinder::Query<12> query(finder);
    query.findNeighbors(index);
    const auto& nbs = query.results();
    if(nbs.size() != 12)
        return env;

    // In hcp the 6 basal neighbors come in antipodal pairs, the 6 out-of-plane neighbors do not:
    // the best partner of an out-of-plane vector leaves a residual of at least a/sqrt(3).
    std::array<FloatType, 12> residual;
    for(int i = 0; i < 12; i++) {
        residual[i] = std::numeric_limits<FloatType>::max();
        for(int j = 0; j < 12; j++)
            if(j != i)
                residual[i] = std::min(residual[i], (nbs[i].delta + nbs[j].delta).squaredLength());
    }
    std::array<int, 12> order;
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return residual[a] < residual[b]; });

    // Basal-plane normal from the most perpendicular pair of basal neighbors.
    Vector3 normal = Vector3::Zero();
    FloatType meanSquaredLength = 0;
    for(int a = 0; a < 6; a++) {
        const Vector3& va = nbs[order[a]].delta;
        meanSquaredLength += va.squaredLength() / 6;
        if(structures[nbs[order[a]].index] == HCP)
            env.basalHCP++;
        for(int b = a + 1; b < 6; b++) {
            Vector3 c = va.cross(nbs[order[b]].delta);
            if(c.squaredLength() > normal.squaredLength())
                normal = c;
        }
    }
    if(normal == Vector3::Zero())
        return env;

    int sideCount[2] = {0, 0};
    for(int k = 0; k < 6; k++) {
        const auto& nb = nbs[order[6 + k]];
        env.outOfPlane[k] = nb.index;
        env.side[k] = (normal.dot(nb.delta) > 0) ? 0 : 1;
        sideCount[env.side[k]]++;
        if(structures[nb.index] == HCP)
            env.sideHCP[env.side[k]]++;
    }
    env.valid = (sideCount[0] == 3 && sideCount[1] == 3);

    // Basal-plane area per atom of a triangular layer with nearest-neighbor distance a: sqrt(3)/2 * a^2.
    env.basalArea = FloatType(0.5) * std::sqrt(FloatType(3)) * meanSquaredLength;
    return env;
}

}   // End of anonymous namespace

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool IdentifyFCCPlanarFaultsModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
 * Is called by the pipeline system before a new modifier evaluation begins.
 ******************************************************************************/
void IdentifyFCCPlanarFaultsModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
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
Future<PipelineFlowState> IdentifyFCCPlanarFaultsModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    // Get input data.
    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(const Particles* cachedParticles = cachedState.getObject<Particles>()) {
                std::vector<const Property*> adopted = { cachedParticles->getProperty(QStringLiteral("Planar Fault Type")) };
                if(colorParticles())
                    adopted.push_back(cachedParticles->getProperty(Particles::ColorProperty));
                particles->tryToAdoptProperties(cachedParticles, adopted, {particles});
            }
            if(const DataTable* cachedTable = cachedState.getObjectBy<DataTable>(request.modificationNode(), QStringLiteral("planar-faults")))
                state.addObject(cachedTable);
            state.adoptAttributesFrom(cachedState, request.modificationNode());
        }
        return std::move(state);
    }

    const Property* structureProperty = particles->getProperty(Particles::StructureTypeProperty);
    if(!structureProperty)
        throw Exception(tr("No 'Structure Type' particle property found. Please insert the Polyhedral template matching or Common neighbor analysis modifier into the pipeline first."));

    // Perform the calculation in a separate thread.
    return asyncLaunch([
            state = std::move(state),
            particles,
            structureProperty,
            colorParticles = colorParticles(),
            typeColors = std::array<Color, NUM_FAULT_TYPES>{colorNonHCP(), colorOther(), colorISF(), colorTwin(), colorMulti()},
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Identifying fcc planar faults"));

        const size_t count = particles->elementCount();
        NearestNeighborFinder neighFinder(12, particles->expectProperty(Particles::PositionProperty), state.getObject<SimulationCell>(), nullptr);
        BufferReadAccess<int32_t> structures(structureProperty);

        // First pass: which neighboring layers of every hcp-like atom are hcp-like too.
        std::vector<uint8_t> flags(count, 0);
        std::vector<FloatType> basalAreas(count, 0);
        parallelFor(count, 4096, progress, [&](size_t i) {
            if(structures[i] != HCP)
                return;
            LocalEnvironment env = analyzeEnvironment(neighFinder, i, structures);
            if(!env.valid)
                return;
            uint8_t f = EnvValid;
            if(env.sideHCP[0] >= MIN_HCP_OUT_OF_PLANE) f |= UpperHCP;
            if(env.sideHCP[1] >= MIN_HCP_OUT_OF_PLANE) f |= LowerHCP;
            if(env.basalHCP >= MIN_HCP_IN_PLANE) f |= BasalHCP;
            flags[i] = f;
            basalAreas[i] = env.basalArea;
        });

        // Second pass: count the adjacent hcp-like layers. An atom with hcp-like layers on both sides is
        // in the middle of a stack of 3+; an atom with one such side is either half of an ISF or the
        // outer layer of a 3+ stack, depending on whether the adjacent layer has hcp-like layers on both sides.
        Property* faultProperty = particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Planar Fault Type"), Property::Int32);
        BufferWriteAccess<int32_t, access_mode::discard_write> faultTypes(faultProperty);
        constexpr uint8_t BothSides = UpperHCP | LowerHCP;
        parallelFor(count, 4096, progress, [&](size_t i) {
            const uint8_t f = flags[i];
            if(structures[i] != HCP)
                faultTypes[i] = NonHCP;
            else if(!(f & EnvValid))
                faultTypes[i] = Indeterminate;
            else if((f & BothSides) == BothSides)
                faultTypes[i] = MultiLayerStackingFault;
            else if(f & BothSides) {
                LocalEnvironment env = analyzeEnvironment(neighFinder, i, structures);
                const int hcpSide = (f & UpperHCP) ? 0 : 1;
                bool thickStack = false;
                for(int k = 0; k < 6 && !thickStack; k++)
                    thickStack = env.side[k] == hcpSide && (flags[env.outOfPlane[k]] & BothSides) == BothSides;
                faultTypes[i] = thickStack ? MultiLayerStackingFault : IntrinsicStackingFault;
            }
            else
                faultTypes[i] = (f & BasalHCP) ? CoherentTwinBoundary : Indeterminate;
        });

        // Aggregate counts and areas. The area of a defect is the basal area of its atoms divided by the number of layers.
        std::array<int64_t, NUM_FAULT_TYPES> typeCounts{};
        FloatType isfArea = 0, tbArea = 0;
        for(size_t i = 0; i < count; i++) {
            typeCounts[faultTypes[i]]++;
            if(faultTypes[i] == IntrinsicStackingFault) isfArea += basalAreas[i] / 2;
            else if(faultTypes[i] == CoherentTwinBoundary) tbArea += basalAreas[i];
        }

        auto addFaultType = [&](Property* property, int id, const QString& name, const Color& color) {
            DataOORef<ElementType> type = DataOORef<ElementType>::create();
            type->setNumericId(id);
            type->setName(name);
            type->setColor(color);
            property->addElementType(std::move(type));
        };
        const std::array<QString, NUM_FAULT_TYPES> typeNames = {
            tr("Non-hcp"), tr("Indeterminate hcp-like"), tr("Intrinsic stacking fault"), tr("Coherent twin boundary"), tr("Multi-layer stacking fault")};
        for(int t = 0; t < NUM_FAULT_TYPES; t++)
            addFaultType(faultProperty, t, typeNames[t], typeColors[t]);

        if(colorParticles) {
            // Non-hcp atoms keep their input colors, as in OVITO Pro (colorNonHCP is only the color of type 0).
            ConstPropertyPtr inputColors = particles->inputParticleColors();
            Property* colorProperty = particles->createProperty(DataBuffer::Uninitialized, Particles::ColorProperty);
            BufferReadAccess<ColorG> inputColorArray(inputColors);
            BufferWriteAccess<ColorG, access_mode::discard_write> colorArray(colorProperty);
            for(size_t i = 0; i < count; i++)
                colorArray[i] = (faultTypes[i] == NonHCP) ? inputColorArray[i] : typeColors[faultTypes[i]].toDataType<GraphicsFloatType>();
        }

        // Bar chart with the atom count of each fault type plus the estimated ISF and TB areas.
        PropertyPtr countColumn = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, NUM_FAULT_TYPES, Property::Int64, 1, tr("Count"));
        std::ranges::copy(typeCounts, BufferWriteAccess<int64_t, access_mode::discard_write>(countColumn).begin());
        PropertyPtr typeColumn = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, NUM_FAULT_TYPES, Property::Int32, 1, tr("Planar fault type"));
        boost::algorithm::iota_n(BufferWriteAccess<int32_t, access_mode::discard_write>(typeColumn).begin(), 0, NUM_FAULT_TYPES);
        for(int t = 0; t < NUM_FAULT_TYPES; t++)
            addFaultType(typeColumn.get(), t, typeNames[t], typeColors[t]);
        DataTable* table = state.createObject<DataTable>(QStringLiteral("planar-faults"), createdByNode, DataTable::BarChart, tr("Planar fault counts"), std::move(countColumn), std::move(typeColumn));
        Property* areaColumn = table->createProperty(DataBuffer::Initialized, tr("Estimated area"), Property::FloatDefault);
        BufferWriteAccess<FloatType, access_mode::read_write> areas(areaColumn);
        areas[IntrinsicStackingFault] = isfArea;
        areas[CoherentTwinBoundary] = tbArea;

        state.addAttribute(QStringLiteral("PlanarFaults.indeterminate_count"), QVariant::fromValue(typeCounts[Indeterminate]), createdByNode);
        state.addAttribute(QStringLiteral("PlanarFaults.isf_count"), QVariant::fromValue(typeCounts[IntrinsicStackingFault]), createdByNode);
        state.addAttribute(QStringLiteral("PlanarFaults.tb_count"), QVariant::fromValue(typeCounts[CoherentTwinBoundary]), createdByNode);
        state.addAttribute(QStringLiteral("PlanarFaults.multilayer_count"), QVariant::fromValue(typeCounts[MultiLayerStackingFault]), createdByNode);
        state.addAttribute(QStringLiteral("PlanarFaults.isf_area"), QVariant::fromValue(isfArea), createdByNode);
        state.addAttribute(QStringLiteral("PlanarFaults.tb_area"), QVariant::fromValue(tbArea), createdByNode);

        return std::move(state);
    });
}

}   // End of namespace
