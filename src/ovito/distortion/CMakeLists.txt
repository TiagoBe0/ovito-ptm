########################################################################################
#
#  Copyright 2025 OVITO GmbH, Germany
#
#  This file is part of OVITO (Open Visualization Tool).
#
#  OVITO is free software; you can redistribute it and/or modify it either under the
#  terms of the GNU General Public License version 3 as published by the Free Software
#  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
#
########################################################################################

# Distortion Score Plugin
# Implements the robust Mahalanobis / MCD-based distortion score from:
#   Goryaeva et al., Nature Communications 11, 4129 (2020).
# No external ML library required — only OVITO core and Qt6.

OVITO_STANDARD_PLUGIN(DistortionScorePlugin
    SOURCES
        DistortionScoreModifier.cpp
    PLUGIN_DEPENDENCIES Particles
    PRECOMPILED_HEADERS DistortionScorePlugin.h
)

# Build the corresponding GUI plugin.
IF(OVITO_BUILD_APP)
    ADD_SUBDIRECTORY(gui)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
