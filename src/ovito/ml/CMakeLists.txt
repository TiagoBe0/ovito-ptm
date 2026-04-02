########################################################################################
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
########################################################################################

# Optional LibTorch (PyTorch C++) integration.
# To enable: cmake -DOVITO_USE_LIBTORCH=ON -DCMAKE_PREFIX_PATH=/path/to/libtorch ..
OPTION(OVITO_USE_LIBTORCH "Enable LibTorch (PyTorch C++) support in the ML plugin." "OFF")
IF(OVITO_USE_LIBTORCH)
    FIND_PACKAGE(Torch REQUIRED)
    MESSAGE(STATUS "LibTorch found: ${TORCH_VERSION}")
ENDIF()

# Define the plugin module.
OVITO_STANDARD_PLUGIN(MLPlugin
    SOURCES
        MLStructureModifier.cpp
    PLUGIN_DEPENDENCIES Particles
    PRECOMPILED_HEADERS MLPlugin.h
)

# Link LibTorch when available.
IF(OVITO_USE_LIBTORCH)
    TARGET_LINK_LIBRARIES(MLPlugin PRIVATE "${TORCH_LIBRARIES}")
    TARGET_INCLUDE_DIRECTORIES(MLPlugin PRIVATE "${TORCH_INCLUDE_DIRS}")
    TARGET_COMPILE_DEFINITIONS(MLPlugin PRIVATE "OVITO_ML_HAS_LIBTORCH=1")
ENDIF()

# Build corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
