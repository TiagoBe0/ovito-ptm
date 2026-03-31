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
#include <ovito/delaunay/DelaunayTessellation.h>
#include <ovito/particles/modifier/analysis/StructureIdentificationModifier.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include "StructureAnalysis.h"
#include "ElasticMapping.h"
#include "InterfaceMesh.h"
#include "DislocationTracer.h"

namespace Ovito {

/*
 * Computation engine of the DislocationAnalysisModifier, which performs the actual dislocation analysis.
 */
class DislocationAnalysisEngine : public StructureIdentificationModifier::Algorithm
{
public:
    /// Constructor.
    DislocationAnalysisEngine(PropertyPtr structures, size_t particleCount, int inputCrystalStructure, int maxTrialCircuitSize,
                              int maxCircuitElongation, ConstPropertyPtr particleSelection, ConstPropertyPtr crystalClusters,
                              std::vector<Matrix3> preferredCrystalOrientations, bool onlyPerfectDislocations, bool markCoreAtoms,
                              int defectMeshSmoothingLevel, DataOORef<DislocationNetwork> dislocationNetwork,
                              DataOORef<SurfaceMesh> defectMesh, DataOORef<SurfaceMesh> outputInterfaceMesh, int lineSmoothingLevel,
                              FloatType linePointInterval,
                              StructureAnalysis::IdentificationMethod identificationMethod = StructureAnalysis::METHOD_PTM);

    /// Performs the atomic structure classification.
    virtual void identifyStructures(const Particles* particles, const SimulationCell* simulationCell, const Property* selection) override;

    /// Computes the structure identification statistics.
    virtual std::vector<int64_t> computeStructureStatistics(const Property* structures, PipelineFlowState& state,
                                                            const OOWeakRef<const PipelineNode>& createdByNode,
                                                            const std::any& modifierParameters) const override;

    /// Returns the array of atom cluster IDs.
    const PropertyPtr& atomClusters() const { return _atomClusters; }

    /// Assigns the array of atom cluster IDs.
    void setAtomClusters(PropertyPtr prop) { _atomClusters = std::move(prop); }

    /// Returns the created cluster graph.
    decltype(auto) clusterGraph() const { return dislocationNetwork()->clusterGraph(); }

    /// Returns the extracted dislocations.
    const DataOORef<DislocationNetwork>& dislocationNetwork() const { return _dislocationNetwork; }

    /// Returns the total volume of the input simulation cell.
    FloatType simCellVolume() const { return _simCellVolume; }

    /// Returns the computed interface mesh.
    const InterfaceMesh& interfaceMesh() const { return *_interfaceMesh; }

    /// Gives access to the elastic mapping computation engine.
    ElasticMapping& elasticMapping() { return *_elasticMapping; }

    /// Returns the input particle property that stores the cluster assignment of atoms.
    const ConstPropertyPtr& crystalClusters() const { return _crystalClusters; }

    /// Computes statistical information on the identified dislocation lines and outputs it to the pipeline as data tables and global
    /// attributes.
    static FloatType generateDislocationStatistics(const OOWeakRef<const PipelineNode>& pipelineNode, PipelineFlowState& state,
                                                   const DislocationNetwork* dislocationsObj, bool replaceDataObjects,
                                                   const MicrostructurePhase* defaultStructure);

private:
    /// Create the output dislocation ID atom property and assign determined values
    void assignCoreAtomDislocationIDs(size_t numParticles);

private:
    int _inputCrystalStructure;
    bool _onlyPerfectDislocations;
    StructureAnalysis::IdentificationMethod _identificationMethod;
    bool _markCoreAtoms;
    int _defectMeshSmoothingLevel;
    int _lineSmoothingLevel;
    int _maxTrialCircuitSize;
    int _maxCircuitElongation;
    FloatType _linePointInterval;
    std::vector<Matrix3> _preferredCrystalOrientations;
    std::optional<StructureAnalysis> _structureAnalysis;
    std::optional<DelaunayTessellation> _tessellation;
    std::optional<ElasticMapping> _elasticMapping;
    std::optional<InterfaceMesh> _interfaceMesh;
    std::optional<DislocationTracer> _dislocationTracer;
    ConstPropertyPtr _crystalClusters;

    /// The defect mesh produced by the modifier.
    DataOORef<SurfaceMesh> _defectMesh;

    /// This stores the interface mesh produced by the modifier for visualization purposes.
    DataOORef<SurfaceMesh> _outputInterfaceMesh;

    /// This stores the atom-to-cluster assignments computed by the modifier.
    PropertyPtr _atomClusters;

    /// This stores the cached atom-to-dislocation assignments computed by the modifier.
    PropertyPtr _atomDislocations;

    /// The dislocations computed by the modifier.
    DataOORef<DislocationNetwork> _dislocationNetwork;

    /// The total volume of the input simulation cell.
    FloatType _simCellVolume;
};

}  // namespace Ovito
