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

# Define the GUI plugin that provides the editor panel for MLTrainingModifier.
OVITO_STANDARD_PLUGIN(MLTrainingPluginGui
    SOURCES
        MLTrainingModifierEditor.cpp
    PLUGIN_DEPENDENCIES MLTrainingPlugin ParticlesGui
    GUI_PLUGIN
    HAS_NO_EXPORTS
)

# The GUI plugin also needs LibTorch for the inline training code.
# Torch may be discovered in a sibling directory (src/ovito/ml), so rely on
# globally available imported targets/libraries instead of directory-local flags.
IF(OVITO_USE_LIBTORCH AND (TARGET Torch::Torch OR TORCH_LIBRARIES))
    SEPARATE_ARGUMENTS(TORCH_CXX_FLAGS_LIST NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
    TARGET_COMPILE_OPTIONS(MLTrainingPluginGui PRIVATE ${TORCH_CXX_FLAGS_LIST})

    IF(TARGET Torch::Torch)
        TARGET_LINK_LIBRARIES(MLTrainingPluginGui PRIVATE Torch::Torch)
    ELSE()
        TARGET_LINK_LIBRARIES(MLTrainingPluginGui PRIVATE ${TORCH_LIBRARIES})
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(MLTrainingPluginGui PRIVATE "${TORCH_INCLUDE_DIRS}")
    TARGET_COMPILE_DEFINITIONS(MLTrainingPluginGui PRIVATE "OVITO_ML_HAS_LIBTORCH=1")
ENDIF()
