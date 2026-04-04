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
SET(OVITO_LIBTORCH_ROOT "" CACHE PATH "Root folder of a LibTorch distribution (contains share/cmake/Torch).")
IF(OVITO_USE_LIBTORCH)
    # Help CMake find TorchConfig.cmake in common LibTorch layouts.
    IF(OVITO_LIBTORCH_ROOT)
        LIST(APPEND CMAKE_PREFIX_PATH
            "${OVITO_LIBTORCH_ROOT}"
            "${OVITO_LIBTORCH_ROOT}/share/cmake/Torch"
        )
    ENDIF()

    # Favor the user-specified Torch_DIR over any stale paths coming from a
    # globally configured CMAKE_PREFIX_PATH. This avoids mixing multiple
    # LibTorch installations, which can produce RPATH cycles and link errors.
    IF(Torch_DIR)
        FIND_PACKAGE(Torch QUIET CONFIG PATHS "${Torch_DIR}" NO_DEFAULT_PATH)
    ELSE()
        FIND_PACKAGE(Torch QUIET CONFIG)
    ENDIF()
    IF(NOT Torch_FOUND)
        SET(_torch_hint "")
        IF(Torch_DIR)
            SET(_torch_hint "Torch_DIR was set to: ${Torch_DIR}\n")
        ELSEIF(OVITO_LIBTORCH_ROOT)
            SET(_torch_hint "OVITO_LIBTORCH_ROOT was set to: ${OVITO_LIBTORCH_ROOT}\n")
        ENDIF()
        MESSAGE(FATAL_ERROR
            "LibTorch requested, but TorchConfig.cmake was not found.\n"
            "${_torch_hint}"
            "Set one of:\n"
            "  -DOVITO_LIBTORCH_ROOT=/path/to/libtorch\n"
            "  -DTorch_DIR=/path/to/libtorch/share/cmake/Torch\n"
            "  -DCMAKE_PREFIX_PATH=/path/to/libtorch\n"
        )
    ENDIF()
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
    # Apply compiler flags required by the LibTorch package (e.g. the
    # _GLIBCXX_USE_CXX11_ABI setting), otherwise the linker may fail with
    # unresolved torch::jit / at:: symbols due to ABI mismatch.
    SEPARATE_ARGUMENTS(TORCH_CXX_FLAGS_LIST NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
    TARGET_COMPILE_OPTIONS(MLPlugin PRIVATE ${TORCH_CXX_FLAGS_LIST})

    # Prefer the imported CMake target from TorchConfig when available. It
    # carries a consistent set of link/interface properties for one LibTorch
    # install and avoids manually expanding ${TORCH_LIBRARIES}.
    IF(TARGET Torch::Torch)
        TARGET_LINK_LIBRARIES(MLPlugin PRIVATE Torch::Torch)
    ELSE()
        TARGET_LINK_LIBRARIES(MLPlugin PRIVATE ${TORCH_LIBRARIES})
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(MLPlugin PRIVATE "${TORCH_INCLUDE_DIRS}")
    TARGET_COMPILE_DEFINITIONS(MLPlugin PRIVATE "OVITO_ML_HAS_LIBTORCH=1")
ENDIF()

# Build corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
