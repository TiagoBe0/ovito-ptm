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

# Optional LibTorch (PyTorch C++) integration — reuses the same option and
# detection logic that the ML plugin already sets up at the parent scope level.
# If LibTorch was found, TORCH_FOUND is set and TORCH_LIBRARIES etc. are defined.

# Define the plugin module.
OVITO_STANDARD_PLUGIN(MLTrainingPlugin
    SOURCES
        MLTrainingModifier.cpp
    PLUGIN_DEPENDENCIES Particles
    PRECOMPILED_HEADERS MLTrainingPlugin.h
)

# Link and configure LibTorch when available.
IF(OVITO_USE_LIBTORCH AND Torch_FOUND)
    SEPARATE_ARGUMENTS(TORCH_CXX_FLAGS_LIST NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
    TARGET_COMPILE_OPTIONS(MLTrainingPlugin PRIVATE ${TORCH_CXX_FLAGS_LIST})

    IF(TARGET Torch::Torch)
        TARGET_LINK_LIBRARIES(MLTrainingPlugin PRIVATE Torch::Torch)
    ELSE()
        TARGET_LINK_LIBRARIES(MLTrainingPlugin PRIVATE ${TORCH_LIBRARIES})
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(MLTrainingPlugin PRIVATE "${TORCH_INCLUDE_DIRS}")
    TARGET_COMPILE_DEFINITIONS(MLTrainingPlugin PRIVATE "OVITO_ML_HAS_LIBTORCH=1")
ENDIF()

# Build the corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
