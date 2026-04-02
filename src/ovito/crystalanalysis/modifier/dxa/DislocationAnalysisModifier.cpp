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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/MicrostructurePhase.h>
#include <ovito/crystalanalysis/objects/DislocationVis.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "DislocationAnalysisModifier.h"
#include "DislocationAnalysisEngine.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationAnalysisModifier);
OVITO_CLASSINFO(DislocationAnalysisModifier, "DisplayName", "Dislocation analysis (DXA)");
OVITO_CLASSINFO(DislocationAnalysisModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, inputCrystalStructure);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, maxTrialCircuitSize);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, circuitStretchability);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, outputInterfaceMesh);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, onlyPerfectDislocations);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, usePTM);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, markCoreAtoms);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, defectMeshSmoothingLevel);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, lineSmoothingEnabled);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, lineSmoothingLevel);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, lineCoarseningEnabled);
DEFINE_PROPERTY_FIELD(DislocationAnalysisModifier, linePointInterval);
DEFINE_REFERENCE_FIELD(DislocationAnalysisModifier, dislocationVis);
DEFINE_REFERENCE_FIELD(DislocationAnalysisModifier, defectMeshVis);
DEFINE_REFERENCE_FIELD(DislocationAnalysisModifier, interfaceMeshVis);
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, inputCrystalStructure, "Input crystal structure");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, maxTrialCircuitSize, "Trial circuit length");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, circuitStretchability, "Circuit stretchability");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, outputInterfaceMesh, "Output interface mesh");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, onlyPerfectDislocations, "Generate perfect dislocations");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, usePTM, "Use PTM for structure identification");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, markCoreAtoms, "Mark dislocation core atoms");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, defectMeshSmoothingLevel, "Defect mesh smoothing");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, lineSmoothingEnabled, "Line smoothing enabled");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, lineSmoothingLevel, "Line smoothing level");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, lineCoarseningEnabled, "Line coarsening enabled");
SET_PROPERTY_FIELD_LABEL(DislocationAnalysisModifier, linePointInterval, "Line point spacing");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationAnalysisModifier, maxTrialCircuitSize, IntegerParameterUnit, 3);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationAnalysisModifier, circuitStretchability, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationAnalysisModifier, defectMeshSmoothingLevel, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationAnalysisModifier, lineSmoothingLevel, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationAnalysisModifier, linePointInterval, FloatParameterUnit, 0);

/******************************************************************************
 * Constructor.
 ******************************************************************************/
void DislocationAnalysisModifier::initializeObject(ObjectInitializationFlags flags)
{
    StructureIdentificationModifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        // Create the vis elements.
        setDislocationVis(OORef<DislocationVis>::create(flags));

        setDefectMeshVis(OORef<SurfaceMeshVis>::create(flags));
        defectMeshVis()->setShowCap(true);
        defectMeshVis()->setSmoothShading(true);
        defectMeshVis()->setReverseOrientation(true);
        defectMeshVis()->setCapTransparency(0.5);
        defectMeshVis()->setObjectTitle(tr("Defect mesh"));

        setInterfaceMeshVis(OORef<SurfaceMeshVis>::create(flags));
        interfaceMeshVis()->setShowCap(false);
        interfaceMeshVis()->setSmoothShading(false);
        interfaceMeshVis()->setReverseOrientation(true);
        interfaceMeshVis()->setCapTransparency(0.5);
        interfaceMeshVis()->setObjectTitle(tr("Interface mesh"));

        // Create the structure types.
        ParticleType::PredefinedStructureType predefTypes[] = {
            ParticleType::PredefinedStructureType::OTHER,         ParticleType::PredefinedStructureType::FCC,
            ParticleType::PredefinedStructureType::HCP,           ParticleType::PredefinedStructureType::BCC,
            ParticleType::PredefinedStructureType::CUBIC_DIAMOND, ParticleType::PredefinedStructureType::HEX_DIAMOND};
        OVITO_STATIC_ASSERT(std::size(predefTypes) == StructureAnalysis::NUM_LATTICE_TYPES);
        for(int id = 0; id < StructureAnalysis::NUM_LATTICE_TYPES; id++) {
            OORef<MicrostructurePhase> stype = OORef<MicrostructurePhase>::create(flags);
            stype->setNumericId(id);
            stype->setDimensionality(MicrostructurePhase::Dimensionality::Volumetric);
            stype->setName(ParticleType::getPredefinedStructureTypeName(predefTypes[id]));
            stype->setColor(
                ElementType::getDefaultColor(OwnerPropertyRef(&Particles::OOClass(), Particles::StructureTypeProperty), stype->name(), id));
            addStructureType(std::move(stype));
        }

        // Create Burgers vector families.

        MicrostructurePhase* fccPattern = structureTypeById(StructureAnalysis::LATTICE_FCC);
        fccPattern->setCrystalSymmetryClass(MicrostructurePhase::CrystalSymmetryClass::CubicSymmetry);
        fccPattern->setShortName(QStringLiteral("fcc"));
        fccPattern->createBurgersVectorFamily();
        fccPattern->createBurgersVectorFamily(1, tr("1/2<110> (Perfect)"), Vector3(1.0 / 2.0, 1.0 / 2.0, 0.0), Color(0.2, 0.2, 1));
        fccPattern->createBurgersVectorFamily(2, tr("1/6<112> (Shockley)"), Vector3(1.0 / 6.0, 1.0 / 6.0, 2.0f / 6.0f), Color(0, 1, 0));
        fccPattern->createBurgersVectorFamily(3, tr("1/6<110> (Stair-rod)"), Vector3(1.0 / 6.0, 1.0 / 6.0, 0.0 / 6.0f), Color(1, 0, 1));
        fccPattern->createBurgersVectorFamily(4, tr("1/3<100> (Hirth)"), Vector3(1.0 / 3.0, 0.0, 0.0), Color(1, 1, 0));
        fccPattern->createBurgersVectorFamily(5, tr("1/3<111> (Frank)"), Vector3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0), Color(0, 1, 1));

        MicrostructurePhase* bccPattern = structureTypeById(StructureAnalysis::LATTICE_BCC);
        bccPattern->setCrystalSymmetryClass(MicrostructurePhase::CrystalSymmetryClass::CubicSymmetry);
        bccPattern->setShortName(QStringLiteral("bcc"));
        bccPattern->createBurgersVectorFamily();
        bccPattern->createBurgersVectorFamily(11, tr("1/2<111>"), Vector3(1.0 / 2.0, 1.0 / 2.0, 1.0 / 2.0), Color(0, 1, 0));
        bccPattern->createBurgersVectorFamily(12, tr("<100>"), Vector3(1.0, 0.0, 0.0), Color(1, 0.3f, 0.8f));
        bccPattern->createBurgersVectorFamily(13, tr("<110>"), Vector3(1.0, 1.0, 0.0), Color(0.2, 0.5, 1.0));

        MicrostructurePhase* hcpPattern = structureTypeById(StructureAnalysis::LATTICE_HCP);
        hcpPattern->setShortName(QStringLiteral("hcp"));
        hcpPattern->setCrystalSymmetryClass(MicrostructurePhase::CrystalSymmetryClass::HexagonalSymmetry);
        hcpPattern->createBurgersVectorFamily();
        hcpPattern->createBurgersVectorFamily(21, tr("1/3<1-210>"), Vector3(sqrt(0.5), 0.0, 0.0), Color(0, 1, 0));
        hcpPattern->createBurgersVectorFamily(22, tr("<0001>"), Vector3(0.0, 0.0, sqrt(4.0 / 3.0)), Color(0.2, 0.2, 1));
        hcpPattern->createBurgersVectorFamily(23, tr("<1-100>"), Vector3(0.0, sqrt(3.0 / 2.0f), 0.0), Color(1, 0, 1));
        hcpPattern->createBurgersVectorFamily(24, tr("1/3<1-100>"), Vector3(0.0, sqrt(3.0 / 2.0f) / 3.0, 0.0), Color(1, 0.5, 0));
        hcpPattern->createBurgersVectorFamily(25, tr("1/3<1-213>"), Vector3(sqrt(0.5), 0.0, sqrt(4.0 / 3.0)), Color(1, 1, 0));

        MicrostructurePhase* cubicDiaPattern = structureTypeById(StructureAnalysis::LATTICE_CUBIC_DIAMOND);
        cubicDiaPattern->setShortName(QStringLiteral("diamond"));
        cubicDiaPattern->setCrystalSymmetryClass(MicrostructurePhase::CrystalSymmetryClass::CubicSymmetry);
        cubicDiaPattern->createBurgersVectorFamily();
        cubicDiaPattern->createBurgersVectorFamily(31, tr("1/2<110>"), Vector3(1.0 / 2.0, 1.0 / 2.0, 0.0), Color(0.2, 0.2, 1));
        cubicDiaPattern->createBurgersVectorFamily(32, tr("1/6<112>"), Vector3(1.0 / 6.0, 1.0 / 6.0, 2.0 / 6.0), Color(0, 1, 0));
        cubicDiaPattern->createBurgersVectorFamily(33, tr("1/6<110>"), Vector3(1.0 / 6.0, 1.0 / 6.0, 0.0), Color(1, 0, 1));
        cubicDiaPattern->createBurgersVectorFamily(34, tr("1/3<111>"), Vector3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0), Color(0, 1, 1));

        MicrostructurePhase* hexDiaPattern = structureTypeById(StructureAnalysis::LATTICE_HEX_DIAMOND);
        hexDiaPattern->setShortName(QStringLiteral("hex_diamond"));
        hexDiaPattern->setCrystalSymmetryClass(MicrostructurePhase::CrystalSymmetryClass::HexagonalSymmetry);
        hexDiaPattern->createBurgersVectorFamily();
        hexDiaPattern->createBurgersVectorFamily(41, tr("1/3<1-210>"), Vector3(sqrt(0.5), 0.0, 0.0), Color(0, 1, 0));
        hexDiaPattern->createBurgersVectorFamily(42, tr("<0001>"), Vector3(0.0, 0.0, sqrt(4.0 / 3.0)), Color(0.2, 0.2, 1));
        hexDiaPattern->createBurgersVectorFamily(43, tr("<1-100>"), Vector3(0.0, sqrt(3.0 / 2.0), 0.0), Color(1, 0, 1));
        hexDiaPattern->createBurgersVectorFamily(44, tr("1/3<1-100>"), Vector3(0.0, sqrt(3.0 / 2.0) / 3.0, 0.0), Color(1, 0.5, 0));
    }
}

/******************************************************************************
* Replaces any references the modifier has to the given visual element with a new compatible object.
******************************************************************************/
void DislocationAnalysisModifier::replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement)
{
    if(defectMeshVis() == visElement)
        setDefectMeshVis(static_object_cast<SurfaceMeshVis>(getReplacement(tr("Defect mesh"))));
    if(interfaceMeshVis() == visElement)
        setInterfaceMeshVis(static_object_cast<SurfaceMeshVis>(getReplacement(tr("Interface mesh"))));
}

/******************************************************************************
 * Creates the engine that will perform the structure identification.
 ******************************************************************************/
std::shared_ptr<StructureIdentificationModifier::Algorithm> DislocationAnalysisModifier::createAlgorithm(
    const ModifierEvaluationRequest& request, const PipelineFlowState& input, PropertyPtr structures)
{
    // Get modifier inputs.
    const Particles* particles = input.expectObject<Particles>();
    particles->verifyIntegrity();

    const SimulationCell* simCell = input.getObject<SimulationCell>();
    if(!simCell)
        throw Exception(tr("The DXA modifier requires a simulation cell to be defined."));
    if(simCell->is2D())
        throw Exception(tr("The DXA modifier does not support 2d simulation cells."));

    // Get particle selection.
    const Property* selectionProperty = onlySelectedParticles() ? particles->expectProperty(Particles::SelectionProperty) : nullptr;

    // Build list of preferred crystal orientations.
    std::vector<Matrix3> preferredCrystalOrientations;
    if(inputCrystalStructure() == StructureAnalysis::LATTICE_FCC || inputCrystalStructure() == StructureAnalysis::LATTICE_BCC ||
       inputCrystalStructure() == StructureAnalysis::LATTICE_CUBIC_DIAMOND) {
        preferredCrystalOrientations.push_back(Matrix3::Identity());
    }

    // Get grain id property created by the GrainSegmentationModifier.
    const Property* grainProperty = particles->getProperty(QStringLiteral("Grain"));
    if(grainProperty && (grainProperty->dataType() != DataBuffer::Int64 || grainProperty->componentCount() != 1))
        grainProperty = nullptr;

    // Create the output dislocation network object.
    DataOORef<DislocationNetwork> dislocations = DataOORef<DislocationNetwork>::create(ObjectInitializationFlag::DontCreateVisElement);
    dislocations->setCreatedByNode(request.modificationNode());
    dislocations->setDomain(simCell);
    dislocations->setVisElement(dislocationVis());

    // Create an empty surface mesh object.
    DataOORef<SurfaceMesh> defectMesh = DataOORef<SurfaceMesh>::create(ObjectInitializationFlag::DontCreateVisElement, tr("Defect mesh"));
    defectMesh->setIdentifier(input.generateUniqueIdentifier<SurfaceMesh>(QStringLiteral("dxa-defect-mesh")));
    defectMesh->setCreatedByNode(request.modificationNode());
    defectMesh->setDomain(simCell);
    defectMesh->setVisElement(defectMeshVis());

    // Create an empty surface mesh object for the optional interface mesh.
    DataOORef<SurfaceMesh> interfaceMesh;
    if(outputInterfaceMesh()) {
        interfaceMesh = DataOORef<SurfaceMesh>::create(ObjectInitializationFlag::DontCreateVisElement, tr("Interface mesh"));
        interfaceMesh->setIdentifier(input.generateUniqueIdentifier<SurfaceMesh>(QStringLiteral("dxa-interface-mesh")));
        interfaceMesh->setCreatedByNode(request.modificationNode());
        interfaceMesh->setDomain(simCell);
        interfaceMesh->setVisElement(interfaceMeshVis());
    }

    return std::make_shared<DislocationAnalysisEngine>(
        std::move(structures), particles->elementCount(), inputCrystalStructure(), maxTrialCircuitSize(), circuitStretchability(),
        selectionProperty, grainProperty, std::move(preferredCrystalOrientations), onlyPerfectDislocations(), markCoreAtoms(),
        defectMeshSmoothingLevel(), std::move(dislocations), std::move(defectMesh), std::move(interfaceMesh),
        lineSmoothingEnabled() ? lineSmoothingLevel() : 0, lineCoarseningEnabled() ? linePointInterval() : 0,
        usePTM() ? StructureAnalysis::METHOD_PTM : StructureAnalysis::METHOD_CNA);
}

/******************************************************************************
* Adopts existing computation results for an interactive pipeline evaluation.
******************************************************************************/
Future<PipelineFlowState> DislocationAnalysisModifier::reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState)
{
    // Adopt the dislocation network and defect mesh objects from the cached state.
    if(const DislocationNetwork* cachedDislocations = cachedState.getObjectBy<DislocationNetwork>(request.modificationNode(), QString{})) {
        output.addObject(cachedDislocations);
    }
    if(const SurfaceMesh* cachedDefectMesh = cachedState.getObjectBy<SurfaceMesh>(request.modificationNode(), QStringLiteral("dxa-defect-mesh"))) {
        output.addObject(cachedDefectMesh);
    }
    if(const SurfaceMesh* cachedInterfaceMesh = cachedState.getObjectBy<SurfaceMesh>(request.modificationNode(), QStringLiteral("dxa-interface-mesh"))) {
        output.addObject(cachedInterfaceMesh);
    }

    // Adopt data tables.
    if(const DataTable* cachedTable = cachedState.getObjectBy<DataTable>(request.modificationNode(), QStringLiteral("disloc-lengths"))) {
        output.addObject(cachedTable);
    }
    if(const DataTable* cachedTable = cachedState.getObjectBy<DataTable>(request.modificationNode(), QStringLiteral("disloc-counts"))) {
        output.addObject(cachedTable);
    }

    return StructureIdentificationModifier::reuseCachedState(request, particles, std::move(output), cachedState);
}

}  // namespace Ovito
