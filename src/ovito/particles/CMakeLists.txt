#######################################################################################
#
#  Copyright 2025 OVITO GmbH, Germany
#
#  This file is part of OVITO (Open Visualization Tool).
#
#  OVITO is free software; you can redistribute it and/or modify it either under the
#  terms of the GNU General Public License version 3 as published by the Free Software
#  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
#  If you do not alter this notice, a recipient may use your version of this
#  file under either the GPL or the MIT License.
#
#  You should have received a copy of the GPL along with this program in a
#  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
#  with this program in a file LICENSE.MIT.txt
#
#  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
#  either express or implied. See the GPL or the MIT License for the specific language
#  governing rights and limitations.
#
#######################################################################################

# Find the required Qt modules.
FIND_PACKAGE(Qt6 ${OVITO_MINIMUM_REQUIRED_QT_VERSION} COMPONENTS Xml REQUIRED)

SET(SourceFiles
    objects/ParticlesVis.cpp
    objects/ParticleType.cpp
    objects/Particles.cpp
    objects/BondType.cpp
    objects/ParticleBondMap.cpp
    objects/Bonds.cpp
    objects/BondsVis.cpp
    objects/Angles.cpp
    objects/Dihedrals.cpp
    objects/Impropers.cpp
    modifier/coloring/AmbientOcclusionModifier.cpp
    modifier/modify/WrapPeriodicImagesModifier.cpp
    modifier/modify/UnwrapTrajectoriesModifier.cpp
    modifier/modify/CreateBondsModifier.cpp
    modifier/modify/LoadTrajectoryModifier.cpp
    modifier/modify/CoordinationPolyhedraModifier.cpp
    modifier/analysis/StructureIdentificationModifier.cpp
    modifier/analysis/ReferenceConfigurationModifier.cpp
    modifier/analysis/ackland_jones/AcklandJonesModifier.cpp
    modifier/analysis/cna/CommonNeighborAnalysisModifier.cpp
    modifier/analysis/coordination/CoordinationAnalysisModifier.cpp
    modifier/analysis/chill_plus/ChillPlusModifier.cpp
    modifier/analysis/displacements/CalculateDisplacementsModifier.cpp
    modifier/analysis/centrosymmetry/CentroSymmetryModifier.cpp
    modifier/analysis/strain/AtomicStrainModifier.cpp
    modifier/analysis/diamond/IdentifyDiamondModifier.cpp
    modifier/analysis/voronoi/VoronoiAnalysisModifier.cpp
    modifier/analysis/cluster/ClusterAnalysisModifier.cpp
    modifier/analysis/wignerseitz/WignerSeitzAnalysisModifier.cpp
    modifier/analysis/vacancy/VacancyAnalysisModifier.cpp
    modifier/analysis/ptm/PolyhedralTemplateMatchingModifier.cpp
    modifier/analysis/ptm/PTMAlgorithm.cpp
    modifier/analysis/surface/ConstructSurfaceModifier.cpp
    modifier/selection/ExpandSelectionModifier.cpp
    modifier/properties/SmoothTrajectoryModifier.cpp
    modifier/properties/GenerateTrajectoryLinesModifier.cpp
    import/ParticleImporter.cpp
    import/lammps/LAMMPSTextDumpImporter.cpp
    import/lammps/LAMMPSBinaryDumpImporter.cpp
    import/lammps/LAMMPSDataImporter.cpp
    import/lammps/LAMMPSDumpLocalImporter.cpp
    import/lammps/LAMMPSDumpYAMLImporter.cpp
    import/lammps/ReaxFFBondImporter.cpp
    import/vasp/POSCARImporter.cpp
    import/xyz/XYZImporter.cpp
    import/imd/IMDImporter.cpp
    import/parcas/ParcasFileImporter.cpp
    import/cfg/CFGImporter.cpp
    import/pdb/PDBImporter.cpp
    import/fhi_aims/FHIAimsImporter.cpp
    import/fhi_aims/FHIAimsLogFileImporter.cpp
    import/gsd/GSDImporter.cpp
    import/castep/CastepCellImporter.cpp
    import/castep/CastepMDImporter.cpp
    import/cube/GaussianCubeImporter.cpp
    import/xsf/XSFImporter.cpp
    import/dcd/DCDImporter.cpp
    import/dl_poly/DLPOLYImporter.cpp
    import/quantumespresso/QuantumEspressoImporter.cpp
    import/cif/CIFImporter.cpp
    import/cif/mmCIFImporter.cpp
    import/gromacs/GroImporter.cpp
    import/gromacs/XTCImporter.cpp
    import/gromacs/TRRImporter.cpp
    import/vtk/ParaViewVTPParticleImporter.cpp
    import/vtk/ParaViewVTPParticleParticleContactsImporter.cpp
    import/vtk/ParaViewVTPParticleWallContactsImporter.cpp
    import/mercurydpm/MercuryDPMImporter.cpp
    export/ParticleExporter.cpp
    export/FileColumnParticleExporter.cpp
    export/vasp/POSCARExporter.cpp
    export/fhi_aims/FHIAimsExporter.cpp
    export/lammps/LAMMPSDataExporter.cpp
    export/lammps/LAMMPSDumpExporter.cpp
    export/xyz/XYZExporter.cpp
    export/imd/IMDExporter.cpp
    export/gsd/GSDExporter.cpp
    util/NearestNeighborFinder.cpp
    util/CutoffNeighborFinder.cpp
    util/PTMNeighborFinder.cpp
    util/ParticleExpressionEvaluator.cpp
)

IF(OVITO_BUILD_PLUGIN_STDMOD)
    LIST(APPEND SourceFiles
        modifier/coloring/ParticlesColorCodingModifierDelegate.cpp
        modifier/coloring/ParticlesAssignColorModifierDelegate.cpp
        modifier/modify/ParticlesSliceModifierDelegate.cpp
        modifier/modify/ParticlesAffineTransformationModifierDelegate.cpp
        modifier/modify/ParticlesDeleteSelectedModifierDelegate.cpp
        modifier/modify/ParticlesReplicateModifierDelegate.cpp
        modifier/properties/ParticlesComputePropertyModifierDelegate.cpp
        modifier/properties/BondsComputePropertyModifierDelegate.cpp
        modifier/selection/ParticlesExpressionSelectionModifierDelegate.cpp
        modifier/modify/ParticlesCombineDatasetsModifierDelegate.cpp
    )
ENDIF()

IF(OVITO_USE_SYCL)
    LIST(APPEND SourceFiles
        util/SyclNeighborFinderBase.cpp
        util/SyclCutoffNeighborFinder.cpp
#        util/SyclNearestNeighborFinder.cpp
    )
ENDIF()

# Define the plugin module.
OVITO_STANDARD_PLUGIN(Particles
    SOURCES ${SourceFiles}
    PLUGIN_DEPENDENCIES Mesh Grid StdObj Delaunay OpenGLRenderer
    PRIVATE_LIB_DEPENDENCIES VoroPlusPlus PolyhedralTemplateMatching MinimumWeightMatchingCentrosymmetry xdrfile gsd
    OPTIONAL_PLUGIN_DEPENDENCIES StdMod
    PRECOMPILED_HEADERS Particles.h
    LIB_DEPENDENCIES Qt6::Xml
)

# Let the code know that we are building the plugin.
TARGET_COMPILE_DEFINITIONS(Particles PUBLIC OVITO_BUILD_PLUGIN_PARTICLES)

# Build corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
