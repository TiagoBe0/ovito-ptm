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

# Determines the SONAME of a shared library, which is required
# to install the library in the OVITO program directory.
# The SONAME is the name of the library file without the full version number.
# This function is only available on Unix/Linux based platforms.
FUNCTION(get_library_soname OUTPUT_VAR LIBRARY_FILE)

    # Use the objdump command to read out the SONAME of the shared library.
    EXECUTE_PROCESS(COMMAND objdump -p "${LIBRARY_FILE}" COMMAND grep "SONAME" OUTPUT_VARIABLE _output_var OUTPUT_STRIP_TRAILING_WHITESPACE)
    STRING(REPLACE "SONAME" "" lib_soname "${_output_var}")
    STRING(STRIP "${lib_soname}" lib_soname)
    IF(NOT lib_soname)

        IF(APPLE)
            # On macOS platform, fall back to using the otool to determine the install name of the dyld library.
            EXECUTE_PROCESS(COMMAND otool -D "${LIBRARY_FILE}" OUTPUT_VARIABLE _output_var OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
            # _output_var contains a two lines: The first line is the file path of the library, the second line is the install name.
            # Extract the second line.
            STRING(REGEX REPLACE "\n" ";" _output_var "${_output_var}")
            LIST(GET _output_var 1 _output_var)
            # If an absolute path is returned, extract the file name from it.
            GET_FILENAME_COMPONENT(lib_soname "${_output_var}" NAME)
        ENDIF()

        IF(NOT lib_soname)
            MESSAGE(FATAL_ERROR "Failed to determine SONAME of shared library: ${LIBRARY_FILE}")
        ENDIF()
    ENDIF()

    SET(${OUTPUT_VAR} ${lib_soname} PARENT_SCOPE)

ENDFUNCTION()

# This function installs a third-party shared library/DLL in the OVITO program directory
# so that it can be distributed together with the program.
# On Unix/Linux based platforms it takes care of installing symbolic links as well.
# This macro creates an OVITO plugin module.
MACRO(OVITO_INSTALL_SHARED_LIB shared_lib)

    # Parse macro arguments.
    CMAKE_PARSE_ARGUMENTS(ARG
        "OPTIONAL" # options
        "DESTINATION"  # one-value keywords
        "" # multi-value keywords
        ${ARGN}) # strings to parse

    # Validate argument values.
    IF(ARG_UNPARSED_ARGUMENTS)
        MESSAGE(FATAL_ERROR "Bad macro arguments: ${ARG_UNPARSED_ARGUMENTS}")
    ENDIF()
    SET(destination_dir ${ARG_DESTINATION})

    # Install libs in third-party library directory by default.
    IF(NOT destination_dir)
        SET(destination_dir ".")
    ENDIF()

    IF(WIN32 OR OVITO_REDISTRIBUTABLE_PACKAGE OR OVITO_BUILD_PYPI)
        # Replace backslashes in the path with regular slashes.
        STRING(REGEX REPLACE "\\\\" "/" _shared_lib ${shared_lib})
        # Make sure the destination directory exists.
        SET(_abs_dest_dir "${Ovito_BINARY_DIR}/${OVITO_RELATIVE_3RDPARTY_LIBRARY_DIRECTORY}/${destination_dir}")
        FILE(MAKE_DIRECTORY "${_abs_dest_dir}")
        # Strip version number from shared lib filename.
        GET_FILENAME_COMPONENT(shared_lib_ext "${_shared_lib}" EXT)
        STRING(REPLACE ${shared_lib_ext} "" lib_base_name "${_shared_lib}")

        # Find all files/symlinks in the same directory having the same base name.
        FILE(GLOB lib_versions LIST_DIRECTORIES FALSE "${_shared_lib}" "${lib_base_name}.*${CMAKE_SHARED_LIBRARY_SUFFIX}" "${lib_base_name}${CMAKE_SHARED_LIBRARY_SUFFIX}.*")
        IF(NOT lib_versions)
            IF(${ARG_OPTIONAL})
                MESSAGE(STATUS "Did not find any library files matching the file path ${_shared_lib} --> skipping installation because this lib is optional")
            ELSE()
                MESSAGE(FATAL_ERROR "Did not find any library files that match the file path ${_shared_lib} (globbing patterns: ${lib_base_name}.*${CMAKE_SHARED_LIBRARY_SUFFIX}; ${lib_base_name}${CMAKE_SHARED_LIBRARY_SUFFIX}.*)")
            ENDIF()
        ENDIF()

        # Find all variants of the shared library name, including symbolic links.
        UNSET(lib_files)
        FOREACH(lib_version ${lib_versions})
            WHILE(IS_SYMLINK ${lib_version})
                GET_FILENAME_COMPONENT(symlink_target "${lib_version}" REALPATH)
                GET_FILENAME_COMPONENT(symlink_target_name "${symlink_target}" NAME)
                GET_FILENAME_COMPONENT(lib_version_name "${lib_version}" NAME)
                IF(NOT lib_version_name STREQUAL symlink_target_name AND NOT OVITO_BUILD_PYPI)
                    MESSAGE("Installing symlink ${lib_version_name} to ${symlink_target_name}")
                    EXECUTE_PROCESS(COMMAND "${CMAKE_COMMAND}" -E create_symlink ${symlink_target_name} "${_abs_dest_dir}/${lib_version_name}" COMMAND_ERROR_IS_FATAL ANY)
                    IF(NOT APPLE)
                        INSTALL(FILES "${_abs_dest_dir}/${lib_version_name}" DESTINATION "${OVITO_RELATIVE_3RDPARTY_LIBRARY_DIRECTORY}/${destination_dir}/")
                    ENDIF()
                ENDIF()
                SET(lib_version "${symlink_target}")
            ENDWHILE()
            IF(NOT IS_SYMLINK ${lib_version})
                LIST(APPEND lib_files "${lib_version}")
            ENDIF()
        ENDFOREACH()
        LIST(REMOVE_DUPLICATES lib_files)

        FOREACH(lib_file ${lib_files})
            MESSAGE("Installing shared library ${lib_file}")
            EXECUTE_PROCESS(COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${lib_file}" "${_abs_dest_dir}/" COMMAND_ERROR_IS_FATAL ANY)
            IF(WIN32 OR NOT OVITO_BUILD_PYPI)
                INSTALL(FILES "${lib_file}" DESTINATION "${OVITO_RELATIVE_3RDPARTY_LIBRARY_DIRECTORY}/${destination_dir}/")
            ELSE()
                # Detect if this .so file is a linker script starting with the string "INPUT".
                # The TBB libraries use this special GNU ld feature instead of regular symbolic links to create aliases of a shared library in the same directory.
                FILE(READ "${lib_file}" _SO_FILE_HEADER LIMIT 5 HEX)
                IF("${_SO_FILE_HEADER}" STREQUAL "494e505554") # 0x494e505554 = "INPUT"
                    INSTALL(FILES "${lib_file}" DESTINATION "${OVITO_RELATIVE_3RDPARTY_LIBRARY_DIRECTORY}/${destination_dir}/")
                ELSE()
                    # When building a Python wheel, we need to rename the shared library to its SONAME.
                    # That's because the Python wheel format does not support symbolic links.
                    # Use the objdump command to read out the SONAME of the shared library.
                    get_library_soname(lib_soname "${lib_file}")
                    GET_FILENAME_COMPONENT(lib_filename "${lib_file}" NAME)
                    FILE(RENAME "${_abs_dest_dir}/${lib_filename}" "${_abs_dest_dir}/${lib_soname}")
                    INSTALL(PROGRAMS "${_abs_dest_dir}/${lib_soname}" DESTINATION "${OVITO_RELATIVE_3RDPARTY_LIBRARY_DIRECTORY}/${destination_dir}/")
                ENDIF()
            ENDIF()
        ENDFOREACH()
        UNSET(lib_files)
    ENDIF()
ENDMACRO()

# Helper function that recursively gathers a list of libraries and other targets that the given
# target depends on directly and indirectly.
FUNCTION(OVITO_GET_ALL_TARGET_DEPENDENCIES OUTPUT_LIST TARGET)

    # This special handling was adopted from __qt_internal_walk_libs() to avoid an error produced by older CMake versions:
    IF(${TARGET} STREQUAL "Qt6::EntryPoint")
        # We can't (and don't need to) process EntryPoint because it brings in $<TARGET_PROPERTY:prop>
        # genexes which get replaced with $<TARGET_PROPERTY:EntryPoint,prop> genexes in the code below
        # and that causes 'INTERFACE_LIBRARY targets may only have whitelisted properties.' errors
        # with CMake versions equal to or lower than 3.18. These errors are super unintuitive to
        # debug because there's no mention that it's happening during a file(GENERATE) call.
        RETURN()
    ENDIF()

    # Skip the following targets, because they cause problems when querying the LINK_LIBRARIES property below.
    IF(${TARGET} MATCHES "pybind11" OR ${TARGET} STREQUAL "documentation" OR ${TARGET} STREQUAL "scripting_documentation" OR ${TARGET} STREQUAL "scripting_documentation_prerun")
        RETURN()
    ENDIF()

    GET_TARGET_PROPERTY(IMPORTED ${TARGET} IMPORTED)
    IF(IMPORTED)
        GET_TARGET_PROPERTY(LIBS ${TARGET} INTERFACE_LINK_LIBRARIES)
    ELSE()
        GET_TARGET_PROPERTY(LIBS ${TARGET} LINK_LIBRARIES)
    ENDIF()
    GET_TARGET_PROPERTY(DEPENDENCIES ${TARGET} MANUALLY_ADDED_DEPENDENCIES)
    LIST(APPEND LIBS ${DEPENDENCIES})
    FOREACH(LIB ${LIBS})
        IF(NOT LIB IN_LIST ${OUTPUT_LIST})
            LIST(APPEND ${OUTPUT_LIST} ${LIB})
            IF(TARGET ${LIB})
                OVITO_GET_ALL_TARGET_DEPENDENCIES(${OUTPUT_LIST} ${LIB})
            ENDIF()
        ENDIF()
    ENDFOREACH()
    SET(${OUTPUT_LIST} ${${OUTPUT_LIST}} PARENT_SCOPE)
ENDFUNCTION()

# This function deploys the required Qt libraries with the program package.
FUNCTION(OVITO_DEPLOY_QT_FRAMEWORK_FILES)

    # Gather all indirect dependencies of the main executable including all plugins.
    IF(OVITO_BUILD_APP)
        OVITO_GET_ALL_TARGET_DEPENDENCIES(ovito_dependency_libraries Ovito)
    ENDIF()
    # When building just the Python bindings, get the dependencies from this target.
    IF(TARGET ovito_bindings)
        OVITO_GET_ALL_TARGET_DEPENDENCIES(ovito_dependency_libraries ovito_bindings)
    ENDIF()

    # Filter dependency list to find all Qt framework modules (targets starting with Qt6::).
    # Qt's internal interface targets (e.g. Qt6::Platform, Qt6::GlobalConfig, *Private) have no library of their own.
    FOREACH(lib ${ovito_dependency_libraries})
        IF(lib MATCHES "^Qt6::(.+)" AND NOT CMAKE_MATCH_1 MATCHES "^(Platform.*|GlobalConfig.*|EntryPoint.*)$|(Private|Internal)$")
            LIST(APPEND OVITO_REQUIRED_QT_COMPONENTS ${CMAKE_MATCH_1})
        ENDIF()
    ENDFOREACH()
    # The dependency scan does not necessarily report the basic modules; the Windows deployment below also takes
    # the location of the Qt binaries from the Core module.
    LIST(PREPEND OVITO_REQUIRED_QT_COMPONENTS Core Gui Widgets)
    LIST(REMOVE_DUPLICATES OVITO_REQUIRED_QT_COMPONENTS)

    # Amend Qt modules list with indirect dependencies.
    # DBus module is an indirect dependency of the Xcb platform plugin under Linux.
    IF(NOT "DBus" IN_LIST OVITO_REQUIRED_QT_COMPONENTS)
        LIST(APPEND OVITO_REQUIRED_QT_COMPONENTS DBus)
    ENDIF()
    # Svg module is an indirect dependency of the SVG icon engine plugin.
    IF(NOT "Svg" IN_LIST OVITO_REQUIRED_QT_COMPONENTS)
        LIST(APPEND OVITO_REQUIRED_QT_COMPONENTS Svg)
    ENDIF()

    # Pull in the Qt modules as CMake targets.
    FOREACH(qtmodule IN LISTS OVITO_REQUIRED_QT_COMPONENTS)
        FIND_PACKAGE(Qt6 ${OVITO_MINIMUM_REQUIRED_QT_VERSION} COMPONENTS ${qtmodule} REQUIRED)
    ENDFOREACH()

    IF(LINUX AND OVITO_REDISTRIBUTABLE_PACKAGE)

        # Install copies of the Qt libraries.
        FILE(MAKE_DIRECTORY "${OVITO_LIBRARY_DIRECTORY}/lib")
        FOREACH(component IN LISTS OVITO_REQUIRED_QT_COMPONENTS)
            GET_TARGET_PROPERTY(lib Qt6::${component} LOCATION)
            GET_TARGET_PROPERTY(lib_soname Qt6::${component} IMPORTED_SONAME_RELEASE)
            CONFIGURE_FILE("${lib}" "${OVITO_LIBRARY_DIRECTORY}" COPYONLY)
            GET_FILENAME_COMPONENT(lib_realname "${lib}" NAME)
            EXECUTE_PROCESS(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${lib_realname}" "${OVITO_LIBRARY_DIRECTORY}/${lib_soname}" COMMAND_ERROR_IS_FATAL ANY)
            EXECUTE_PROCESS(COMMAND "${CMAKE_COMMAND}" -E create_symlink "../${lib_soname}" "${OVITO_LIBRARY_DIRECTORY}/lib/${lib_soname}" COMMAND_ERROR_IS_FATAL ANY)
            INSTALL(FILES "${lib}" DESTINATION "${OVITO_RELATIVE_LIBRARY_DIRECTORY}/")
            INSTALL(FILES "${OVITO_LIBRARY_DIRECTORY}/${lib_soname}" DESTINATION "${OVITO_RELATIVE_LIBRARY_DIRECTORY}/")
            INSTALL(FILES "${OVITO_LIBRARY_DIRECTORY}/lib/${lib_soname}" DESTINATION "${OVITO_RELATIVE_LIBRARY_DIRECTORY}/lib/")
            GET_FILENAME_COMPONENT(QtBinaryPath ${lib} PATH)
        ENDFOREACH()

        # Install Qt plugins.
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platforms/libqminimal.so" DESTINATION "./plugins_qt/platforms")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platforms/libqwayland-generic.so" DESTINATION "./plugins_qt/platforms" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platforms/libqwayland-egl.so" DESTINATION "./plugins_qt/platforms" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platforms/libqxcb.so" DESTINATION "./plugins_qt/platforms")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-shell-integration/libqt-shell.so" DESTINATION "./plugins_qt/wayland-shell-integration" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-shell-integration/libwl-shell-plugin.so" DESTINATION "./plugins_qt/wayland-shell-integration" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-shell-integration/libxdg-shell.so" DESTINATION "./plugins_qt/wayland-shell-integration" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-shell-integration/libivi-shell.so" DESTINATION "./plugins_qt/wayland-shell-integration" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" DESTINATION "./plugins_qt/wayland-graphics-integration-client" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-graphics-integration-client/libdmabuf-server.so" DESTINATION "./plugins_qt/wayland-graphics-integration-client" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-graphics-integration-client/libdrm-egl-server.so" DESTINATION "./plugins_qt/wayland-graphics-integration-client" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-graphics-integration-client/libshm-emulation-server.so" DESTINATION "./plugins_qt/wayland-graphics-integration-client" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/wayland-decoration-client/libbradient.so" DESTINATION "./plugins_qt/wayland-decoration-client" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqgif.so" DESTINATION "./plugins_qt/imageformats")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqico.so" DESTINATION "./plugins_qt/imageformats" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqicns.so" DESTINATION "./plugins_qt/imageformats" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqjpeg.so" DESTINATION "./plugins_qt/imageformats")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqwebp.so" DESTINATION "./plugins_qt/imageformats")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/libqtiff.so" DESTINATION "./plugins_qt/imageformats")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/iconengines/libqsvgicon.so" DESTINATION "./plugins_qt/iconengines")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/xcbglintegrations/libqxcb-glx-integration.so" DESTINATION "./plugins_qt/xcbglintegrations")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platformthemes/libqxdgdesktopportal.so" DESTINATION "./plugins_qt/platformthemes")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/networkinformation/libqnetworkmanager.so" DESTINATION "./plugins_qt/networkinformation")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/networkinformation/libqglib.so" DESTINATION "./plugins_qt/networkinformation" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/tls/libqcertonlybackend.so" DESTINATION "./plugins_qt/tls" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/tls/libqopensslbackend.so" DESTINATION "./plugins_qt/tls")
        # libQt6XcbQpa.so is required by the Qt Gui module and XCB platform plugin.
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/libQt6XcbQpa.so" DESTINATION "./lib")
        # libQt6WaylandClient.so and others are required by the Qt platform plugin libqwayland-generic.so and its sub-plugins.
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/libQt6WaylandClient.so" DESTINATION "./lib")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/libQt6WlShellIntegration.so" DESTINATION "./lib")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/libQt6WaylandEglClientHwIntegration.so" DESTINATION "./lib")

        # Distribute libxkbcommon.so with OVITO, which is a dependency of the Qt XCB plugin that might not be present on all systems.
        FIND_LIBRARY(OVITO_XKBCOMMON_DEP NAMES libxkbcommon.so.0 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64 NO_DEFAULT_PATH REQUIRED)
        OVITO_INSTALL_SHARED_LIB("${OVITO_XKBCOMMON_DEP}" DESTINATION "./lib")
        UNSET(OVITO_XKBCOMMON_DEP CACHE)
        # Additionally, place a symlink into the parent lib/ovito/ directory.
        EXECUTE_PROCESS(COMMAND "${CMAKE_COMMAND}" -E create_symlink "lib/libxkbcommon.so.0" "${OVITO_LIBRARY_DIRECTORY}/libxkbcommon.so.0" COMMAND_ERROR_IS_FATAL ANY)
        INSTALL(FILES "${OVITO_LIBRARY_DIRECTORY}/libxkbcommon.so.0" DESTINATION "${OVITO_RELATIVE_LIBRARY_DIRECTORY}/")

        # Distribute libxkbcommon-x11.so with OVITO, which is a dependency of the Qt XCB plugin that might not be present on all systems.
        FIND_LIBRARY(OVITO_XKBCOMMONX11_DEP NAMES libxkbcommon-x11.so.0 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64 NO_DEFAULT_PATH REQUIRED)
        OVITO_INSTALL_SHARED_LIB("${OVITO_XKBCOMMONX11_DEP}" DESTINATION "./lib")
        UNSET(OVITO_XKBCOMMONX11_DEP CACHE)

        # Distribute libxcb-xinerama.so with OVITO, which is a dependency of the Qt XCB plugin that might not be present on all systems.
        FIND_LIBRARY(OVITO_XINERAMA_DEP NAMES libxcb-xinerama.so.0 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64 NO_DEFAULT_PATH REQUIRED)
        OVITO_INSTALL_SHARED_LIB("${OVITO_XINERAMA_DEP}" DESTINATION "./lib")
        UNSET(OVITO_XINERAMA_DEP CACHE)

        # Distribute ICU libraries.
        FOREACH(iculib libicui18n.so libicuuc.so libicudata.so)
            FIND_LIBRARY(OVITO_ICU_DEP NAMES ${iculib} ${iculib}.50 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64 NO_DEFAULT_PATH REQUIRED)
            OVITO_INSTALL_SHARED_LIB("${OVITO_ICU_DEP}" DESTINATION ".")
            UNSET(OVITO_ICU_DEP CACHE)
        ENDFOREACH()

    ELSEIF(WIN32 AND NOT OVITO_BUILD_PYPI AND NOT OVITO_BUILD_CONDA)

        # On Windows, the third-party library DLLs need to be installed in the OVITO directory.
        # Gather Qt dynamic link libraries.
        FOREACH(component IN LISTS OVITO_REQUIRED_QT_COMPONENTS)
            GET_TARGET_PROPERTY(dll Qt6::${component} LOCATION_${CMAKE_BUILD_TYPE})
            IF(NOT TARGET Qt6::${component} OR NOT dll)
                MESSAGE(FATAL_ERROR "Target does not exist or has no LOCATION property: Qt6::${component}")
            ENDIF()
            OVITO_INSTALL_SHARED_LIB("${dll}")
            IF(${component} MATCHES "Core")
                GET_FILENAME_COMPONENT(QtBinaryPath ${dll} PATH)
                IF(dll MATCHES "Cored.dll$")
                    SET(_qt_dll_suffix "d")
                ELSE()
                    SET(_qt_dll_suffix "")
                ENDIF()
            ENDIF()
        ENDFOREACH()

        # Install Qt plugins required by OVITO.
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/platforms/qwindows${_qt_dll_suffix}.dll" DESTINATION "plugins/platforms/")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/qjpeg${_qt_dll_suffix}.dll" DESTINATION "plugins/imageformats/")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/qgif${_qt_dll_suffix}.dll" DESTINATION "plugins/imageformats/")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/qwebp${_qt_dll_suffix}.dll" DESTINATION "plugins/imageformats/" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/qtiff${_qt_dll_suffix}.dll" DESTINATION "plugins/imageformats/" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/imageformats/qsvg${_qt_dll_suffix}.dll" DESTINATION "plugins/imageformats/")
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/iconengines/qsvgicon${_qt_dll_suffix}.dll" DESTINATION "plugins/iconengines/")
        IF(Qt6Core_VERSION VERSION_LESS "6.7")
            OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/styles/qwindowsvistastyle${_qt_dll_suffix}.dll" DESTINATION "plugins/styles/")
        ELSE()
            OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/styles/qmodernwindowsstyle${_qt_dll_suffix}.dll" DESTINATION "plugins/styles/")
        ENDIF()
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/tls/qcertonlybackend${_qt_dll_suffix}.dll" DESTINATION "plugins/tls/" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/tls/qopensslbackend${_qt_dll_suffix}.dll" DESTINATION "plugins/tls/" OPTIONAL)
        OVITO_INSTALL_SHARED_LIB("${QtBinaryPath}/../plugins/tls/qschannelbackend${_qt_dll_suffix}.dll" DESTINATION "plugins/tls/")

    ENDIF()
ENDFUNCTION()
