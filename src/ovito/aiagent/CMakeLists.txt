########################################################################################
#
#  Copyright 2026 OVITO GmbH, Germany
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

# The AI agent integration exists only in the desktop application: it embeds a terminal
# into the main window and hands the agent a bridge to the live session.
OVITO_STANDARD_PLUGIN(AIAgentGui
    SOURCES
        AIAgentApplet.cpp
        AgentBridgeServer.cpp
        PtyProcess.cpp
        TerminalWidget.cpp
    PLUGIN_DEPENDENCIES StdObj
    GUI_PLUGIN
    HAS_NO_EXPORTS
    PRECOMPILED_HEADERS AIAgent.h
)

# forkpty() lives in libutil on Linux and in libutil/System on macOS.
IF(UNIX AND NOT APPLE)
    TARGET_LINK_LIBRARIES(AIAgentGui PRIVATE util)
ENDIF()

# Propagate list of plugins to parent scope.
SET(OVITO_PLUGIN_LIST ${OVITO_PLUGIN_LIST} PARENT_SCOPE)
