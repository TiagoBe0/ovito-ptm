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

SET(SourceFiles
    importer/CAImporter.cpp
    exporter/CAExporter.cpp
    exporter/VTKDislocationsExporter.cpp
    objects/BurgersVectorFamily.cpp
    objects/ClusterGraph.cpp
    objects/DislocationNetwork.cpp
    objects/DislocationVis.cpp
    objects/MicrostructurePhase.cpp
    modifier/dxa/DislocationAnalysisModifier.cpp
    modifier/dxa/DislocationAnalysisEngine.cpp
    modifier/dxa/StructureAnalysis.cpp
    modifier/dxa/CrystalPathFinder.cpp
    modifier/dxa/ElasticMapping.cpp
    modifier/dxa/InterfaceMesh.cpp
    modifier/dxa/DislocationTracer.cpp
    modifier/tracking/DislocationTrackingModifier.cpp
    modifier/elasticstrain/ElasticStrainModifier.cpp
    modifier/elasticstrain/ElasticStrainEngine.cpp
    modifier/grains/GrainSegmentationModifier.cpp
    modifier/grains/GrainSegmentationEngine.cpp
    modifier/grains/NodePairSampling.cpp
)

IF(OVITO_BUILD_PLUGIN_STDMOD)
    LIST(APPEND SourceFiles
        objects/DislocationSliceModifierDelegate.cpp
        objects/DislocationReplicateModifierDelegate.cpp
        objects/DislocationAffineTransformationModifierDelegate.cpp
        )
ENDIF()

# Define the plugin module.
OVITO_STANDARD_PLUGIN(CrystalAnalysis
    SOURCES ${SourceFiles}
    PLUGIN_DEPENDENCIES Particles Mesh StdObj Grid Delaunay
    PRIVATE_LIB_DEPENDENCIES PolyhedralTemplateMatching
    OPTIONAL_PLUGIN_DEPENDENCIES StdMod
    PRECOMPILED_HEADERS CrystalAnalysis.h
)

# Build corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
