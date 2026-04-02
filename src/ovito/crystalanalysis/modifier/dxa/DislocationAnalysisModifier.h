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

#pragma once


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationVis.h>
#include <ovito/crystalanalysis/objects/MicrostructurePhase.h>
#include <ovito/crystalanalysis/modifier/dxa/StructureAnalysis.h>
#include <ovito/particles/modifier/analysis/StructureIdentificationModifier.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshVis.h>

namespace Ovito {

/*
 * Identifies dislocation lines in a crystal and generates a line model of these defects.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationAnalysisModifier : public StructureIdentificationModifier
{
    OVITO_CLASS(DislocationAnalysisModifier)

public:

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Returns the crystal structure with the given ID, or null if no such structure exists.
    MicrostructurePhase* structureTypeById(int id) const {
        return dynamic_object_cast<MicrostructurePhase>(StructureIdentificationModifier::structureTypeById(id));
    }

    /// Asks the modifier to replace a visual element owned by this modifier with a new compatible object, which is created
    /// on demand by the provided callback function. The callback function accepts an optional human-readable title, which will
    /// be used for the new visual element.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

protected:

    /// Creates the engine that will perform the structure identification.
    virtual std::shared_ptr<Algorithm> createAlgorithm(const ModifierEvaluationRequest& request, const PipelineFlowState& input, PropertyPtr structures) override;

    /// Adopts existing computation results for an interactive pipeline evaluation.
    virtual Future<PipelineFlowState> reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState) override;

private:

    /// The type of crystal to be analyzed.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(StructureAnalysis::LatticeStructureType{StructureAnalysis::LATTICE_FCC}, inputCrystalStructure, setInputCrystalStructure, PROPERTY_FIELD_MEMORIZE);

    /// The maximum length of trial circuits.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{14}, maxTrialCircuitSize, setMaxTrialCircuitSize, PROPERTY_FIELD_RESETTABLE);

    /// The maximum elongation of Burgers circuits while they are being advanced.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{9}, circuitStretchability, setCircuitStretchability, PROPERTY_FIELD_RESETTABLE);

    /// Controls the output of the interface mesh.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, outputInterfaceMesh, setOutputInterfaceMesh);

    /// Restricts the identification to perfect lattice dislocations.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, onlyPerfectDislocations, setOnlyPerfectDislocations);

    /// Selects PTM (true) or CNA (false) for local structure identification.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, usePTM, setUsePTM);

    /// Mark atoms belonging to the dislocation cores.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, markCoreAtoms, setMarkCoreAtoms);

    /// The number of iterations of the mesh smoothing algorithm.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{8}, defectMeshSmoothingLevel, setDefectMeshSmoothingLevel, PROPERTY_FIELD_RESETTABLE);

    /// Stores whether smoothing is enabled.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, lineSmoothingEnabled, setLineSmoothingEnabled);

    /// Controls the degree of smoothing applied to the dislocation lines.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{1}, lineSmoothingLevel, setLineSmoothingLevel, PROPERTY_FIELD_RESETTABLE);

    /// Stores whether coarsening is enabled.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, lineCoarseningEnabled, setLineCoarseningEnabled);

    /// Controls the coarsening of dislocation lines.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{2.5}, linePointInterval, setLinePointInterval, PROPERTY_FIELD_RESETTABLE);

    /// The visualization element for rendering the defect mesh.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<SurfaceMeshVis>, defectMeshVis, setDefectMeshVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE);

    /// The visualization element for rendering the interface mesh.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<SurfaceMeshVis>, interfaceMeshVis, setInterfaceMeshVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE);

    /// The visualization element for rendering the dislocations.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<DislocationVis>, dislocationVis, setDislocationVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE);
};

}   // End of namespace
