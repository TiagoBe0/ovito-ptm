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

# GUI plugin that provides the properties editor for DistortionScoreModifier.
OVITO_STANDARD_PLUGIN(DistortionScorePluginGui
    SOURCES
        DistortionScoreModifierEditor.cpp
    PLUGIN_DEPENDENCIES DistortionScorePlugin ParticlesGui
    GUI_PLUGIN
    HAS_NO_EXPORTS
)
