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

# Define the plugin module.
OVITO_STANDARD_PLUGIN(StdMod
    SOURCES
        modifiers/SliceModifier.cpp
        modifiers/AffineTransformationModifier.cpp
        modifiers/InvertSelectionModifier.cpp
        modifiers/ClearSelectionModifier.cpp
        modifiers/ColorCodingModifier.cpp
        modifiers/AssignColorModifier.cpp
        modifiers/DeleteSelectedModifier.cpp
        modifiers/SelectTypeModifier.cpp
        modifiers/HistogramModifier.cpp
        modifiers/ScatterPlotModifier.cpp
        modifiers/TimeSeriesModifier.cpp
        modifiers/AnalysisModifiersSelfTestService.cpp
        modifiers/TimeAveragingModifier.cpp
        modifiers/DifferenceBetweenFramesModifier.cpp
        modifiers/ReducePropertyModifier.cpp
        modifiers/ReplicateModifier.cpp
        modifiers/ExpressionSelectionModifier.cpp
        modifiers/LinesModifierDelegates.cpp
        modifiers/VectorsModifierDelegates.cpp
        modifiers/FreezePropertyModifier.cpp
        modifiers/ManualSelectionModifier.cpp
        modifiers/ComputePropertyModifier.cpp
        modifiers/CombineDatasetsModifier.cpp
        modifiers/CombineMultipleDatasetsModifier.cpp
        modifiers/ColorByTypeModifier.cpp
        viewport/ColorLegendOverlay.cpp
    PLUGIN_DEPENDENCIES
        StdObj
        Mesh
    PRECOMPILED_HEADERS
        StdMod.h
)

# Let the code know that we are building the plugin.
TARGET_COMPILE_DEFINITIONS(StdMod PUBLIC OVITO_BUILD_PLUGIN_STDMOD)

# Build corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
