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

# Find ffmpeg libraries for video encoding support.
IF(NOT EMSCRIPTEN)
    FIND_PACKAGE(FFMPEG)
ENDIF()

# Find the zlib and zstd libraries, needed for reading/writing compressed data files.
IF(NOT EMSCRIPTEN)
    FIND_PACKAGE(ZLIB)
    FIND_PACKAGE(zstd)
ENDIF()

# Locate the libssh library, needed for the built-in SSH client.
IF(OVITO_BUILD_SSH_CLIENT)
    FIND_PACKAGE(libssh REQUIRED)
ENDIF()

# Locate the Boost library.
FIND_PACKAGE(Boost REQUIRED)
IF(NOT TARGET Boost::boost)
    MESSAGE(FATAL_ERROR "The Boost library was not found on your system. Please make sure it is available.")
ENDIF()

SET(SourceFiles
    utilities/Exception.cpp
    utilities/linalg/AffineDecomposition.cpp
    utilities/io/SaveStream.cpp
    utilities/io/LoadStream.cpp
    app/Application.cpp
    app/ApplicationService.cpp
    app/PluginManager.cpp
    app/StandaloneApplication.cpp
    app/UserInterface.cpp
    app/undo/UndoableOperation.cpp
    app/undo/UndoStack.cpp
    oo/CloneHelper.cpp
    oo/OvitoClass.cpp
    oo/OvitoObject.cpp
    oo/PropertyFieldDescriptor.cpp
    oo/PropertyField.cpp
    oo/RefMaker.cpp
    oo/RefMakerClass.cpp
    oo/RefTarget.cpp
    oo/RefTargetListener.cpp
    dataset/animation/AnimationSettings.cpp
    dataset/animation/AnimationFrameLabel.cpp
    dataset/animation/controller/Controller.cpp
    dataset/animation/controller/ConstantControllers.cpp
    dataset/animation/controller/KeyframeController.cpp
    dataset/animation/controller/LinearInterpolationControllers.cpp
    dataset/animation/controller/SplineInterpolationControllers.cpp
    dataset/animation/controller/TCBInterpolationControllers.cpp
    dataset/animation/controller/AnimationKeys.cpp
    dataset/animation/controller/LookAtController.cpp
    dataset/animation/controller/PRSTransformationController.cpp
    dataset/DataSet.cpp
    dataset/DataSetContainer.cpp
    dataset/io/FileImporter.cpp
    dataset/io/FileExporter.cpp
    dataset/io/FileSourceImporter.cpp
    dataset/io/FileSource.cpp
    dataset/io/AttributeFileExporter.cpp
    dataset/scene/SceneNode.cpp
    dataset/scene/Pipeline.cpp
    dataset/scene/Scene.cpp
    dataset/scene/ScenePreparation.cpp
    dataset/scene/SceneAnimationPlayback.cpp
    dataset/scene/SelectionSet.cpp
    dataset/data/DataVis.cpp
    dataset/data/DataObject.cpp
    dataset/data/DataBuffer.cpp
    dataset/data/DataCollection.cpp
    dataset/data/camera/AbstractCameraObject.cpp
    dataset/data/camera/AbstractCameraSource.cpp
    dataset/data/AttributeDataObject.cpp
    dataset/data/mesh/TriangleMesh.cpp
    dataset/data/mesh/TriangleMeshVis.cpp
    dataset/pipeline/ActiveObject.cpp
    dataset/pipeline/PipelineFlowState.cpp
    dataset/pipeline/PipelineCache.cpp
    dataset/pipeline/PipelineNode.cpp
    dataset/pipeline/PipelineStatus.cpp
    dataset/pipeline/Modifier.cpp
    dataset/pipeline/ModificationNode.cpp
    dataset/pipeline/ModifierGroup.cpp
    dataset/pipeline/StaticSource.cpp
    dataset/pipeline/BasePipelineSource.cpp
    dataset/pipeline/DelegatingModifier.cpp
    utilities/units/UnitsManager.cpp
    utilities/units/PrescribedScaleUnit.cpp
    utilities/io/ObjectSaveStream.cpp
    utilities/io/ObjectLoadStream.cpp
    utilities/io/FileManager.cpp
    utilities/io/RemoteFileJob.cpp
    utilities/io/CompressedTextReader.cpp
    utilities/io/CompressedTextWriter.cpp
    utilities/concurrent/Task.cpp
    utilities/concurrent/TaskManager.cpp
    utilities/concurrent/MainThreadOperation.cpp
    utilities/io/ssh/SshConnection.cpp
    utilities/io/ssh/openssh/OpensshConnection.cpp
    utilities/io/ssh/openssh/SshRequest.cpp
    utilities/io/ssh/openssh/DownloadRequest.cpp
    utilities/io/ssh/openssh/FileListingRequest.cpp
    rendering/SceneRenderer.cpp
    rendering/RenderingJob.cpp
    rendering/FrameGraph.cpp
    rendering/FrameGraphBuilder.cpp
    rendering/MeshPrimitive.cpp
    rendering/TextPrimitive.cpp
    rendering/RenderSettings.cpp
    rendering/FrameBuffer.cpp
    rendering/ColorCodingGradient.cpp
    rendering/ObjectPickingMap.cpp
    rendering/OpacityFunction.cpp
    viewport/Viewport.cpp
    viewport/ViewportWindow.cpp
    viewport/ViewportConfiguration.cpp
    viewport/ViewportLayout.cpp
    viewport/ViewportSettings.cpp
    viewport/overlays/ViewportOverlay.cpp
    viewport/overlays/CoordinateTripodOverlay.cpp
    viewport/overlays/TextLabelOverlay.cpp
)

# Build the askpass utility, which is needed for the external OpenSSH-based client.
IF(OVITO_BUILD_APP AND NOT EMSCRIPTEN)
    ADD_SUBDIRECTORY(utilities/io/ssh/openssh/askpass)
ENDIF()

# Make use of the zlib library provided by Emscripten.
IF(EMSCRIPTEN)
    MESSAGE("Using Emscripten port of zlib library.")
    ADD_LIBRARY(ZLIB::ZLIB INTERFACE IMPORTED)
    TARGET_COMPILE_OPTIONS(ZLIB::ZLIB INTERFACE "SHELL:-s USE_ZLIB=1")
    SET(ZLIB_FOUND TRUE)
ENDIF()

# Define library target.
OVITO_STANDARD_PLUGIN(Core
    SOURCES ${SourceFiles} resources/core.qrc
    LIB_DEPENDENCIES Boost::boost function2::function2
    PRECOMPILED_HEADERS Core.h
)

# Disable Qt AUTOMOC processing of the header OvitoClass.h.
# This is necessary, because the header contains the definition of the OVITO_CLASS macro, which
# is added to the AUTOMOC_MACRO_NAMES list.
SET_PROPERTY(SOURCE oo/OvitoClass PROPERTY SKIP_AUTOMOC ON)

IF(NOT EMSCRIPTEN)
    # Qt Network module is used by the Core module, except in the wasm build.
    FIND_PACKAGE(Qt6 ${OVITO_MINIMUM_REQUIRED_QT_VERSION} COMPONENTS Network REQUIRED)
    TARGET_LINK_LIBRARIES(Core PUBLIC Qt6::Network)
ENDIF()

# Use zlib library if available.
IF(ZLIB_FOUND)
    TARGET_SOURCES(Core PRIVATE utilities/io/gzdevice/GzipIODevice.cpp)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_ZLIB_SUPPORT)
    TARGET_LINK_LIBRARIES(Core PUBLIC ZLIB::ZLIB)
    # Use zstd library if available. The zlib wrapper sources come from the zstd git submodule, which source
    # archives such as GitHub's "Download ZIP" do not contain.
    IF(zstd_FOUND AND NOT EXISTS "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/zstd_zlibwrapper.c")
        MESSAGE(WARNING "The zstd submodule (src/3rdparty/zstd) is empty. OVITO will be built without I/O support for zstandard compressed files. Run 'git submodule update --init' to enable it.")
    ELSEIF(zstd_FOUND)
        TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_ZSTD_SUPPORT)
        # Use shared library version of zstd when building the Conda package or when building against the system version of zstd on Linux platforms.
        # Otherwise, use the static library to avoid an extra deployment step. The static lib must have been built with the -fPIC option.
        IF(OVITO_BUILD_CONDA)
            TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd)
        ELSEIF(NOT OVITO_REDISTRIBUTABLE_PACKAGE AND LINUX)
            # zstd::libzstd only exists since zstd 1.5.6; older system packages (e.g. Ubuntu 24.04's 1.5.5) export just libzstd_shared.
            IF(TARGET zstd::libzstd)
                TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd)
            ELSE()
                TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd_shared)
            ENDIF()
        ELSEIF(TARGET zstd::libzstd_static)
            TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd_static)
        ELSEIF(TARGET zstd::libzstd)
            # Only a shared build is available (e.g. vcpkg's x64-windows triplet); its DLL must be deployed with OVITO.
            TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd)
        ELSE()
            TARGET_LINK_LIBRARIES(Core PUBLIC zstd::libzstd_shared)
        ENDIF()
        # Build the zstd zlib-wrapper source files.
        TARGET_INCLUDE_DIRECTORIES(Core PUBLIC "$<BUILD_INTERFACE:${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper>")
        TARGET_SOURCES(Core PRIVATE
            "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/zstd_zlibwrapper.c"
            "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/gzclose.c"
            "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/gzlib.c"
            "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/gzread.c"
            "${OVITO_SOURCE_BASE_DIR}/src/3rdparty/zstd/zlibWrapper/gzwrite.c"
        )
    ELSE()
        MESSAGE(WARNING "zstd library not found. OVITO will be built without I/O support for zstandard compressed files.")
    ENDIF()
ELSE()
    MESSAGE(WARNING "zlib library not found. OVITO will be built without I/O support for gzip compressed files.")
ENDIF()

# Link to Libssh.
IF(OVITO_BUILD_SSH_CLIENT)
    TARGET_SOURCES(Core PRIVATE
        utilities/io/ssh/libssh/LibsshConnection.cpp
        utilities/io/ssh/libssh/SshChannel.cpp
        utilities/io/ssh/libssh/ProcessChannel.cpp
        utilities/io/ssh/libssh/ScpChannel.cpp
        utilities/io/ssh/libssh/LsChannel.cpp
    )
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_SSH_CLIENT)
    TARGET_LINK_LIBRARIES(Core PRIVATE ssh)
ENDIF()

# Link to video encoding lib.
IF(FFMPEG_FOUND)
    TARGET_SOURCES(Core PRIVATE utilities/io/video/VideoEncoder.cpp)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_VIDEO_OUTPUT_SUPPORT)
    TARGET_COMPILE_DEFINITIONS(Core PRIVATE __STDC_CONSTANT_MACROS __STDC_LIMIT_MACROS)
    TARGET_LINK_LIBRARIES(Core PRIVATE "${FFMPEG_LIBRARIES}")
    TARGET_INCLUDE_DIRECTORIES(Core BEFORE PRIVATE "${FFMPEG_INCLUDE_DIRS}")
ELSE()
    MESSAGE(WARNING "ffmpeg video encoding library not found. OVITO will be built without support for video output.")
ENDIF()

# Define the OVITO_DEBUG macro in debug builds.
TARGET_COMPILE_DEFINITIONS(Core PUBLIC "$<$<CONFIG:Debug>:OVITO_DEBUG>")
# Define QT_NO_DEBUG_OUTPUT in release builds.
TARGET_COMPILE_DEFINITIONS(Core PUBLIC "$<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT=1>")

# Enable SYCL in source code.
IF(OVITO_USE_SYCL STREQUAL AdaptiveCpp)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_USE_SYCL)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_USE_SYCL_ACPP)
ELSEIF(OVITO_USE_SYCL STREQUAL DPC++)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_USE_SYCL)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_USE_SYCL_DPCPP)
ENDIF()

# Enable CUDA code.
IF(OVITO_USE_CUDA)
    # Tell source code that the use of CUDA is enabled.
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC OVITO_USE_CUDA)
    # Deploy CUDA runtime library with the application.
    IF(NOT OVITO_BUILD_CONDA)
        FIND_PACKAGE(CUDAToolkit REQUIRED)
        GET_TARGET_PROPERTY(CUDA_LIBRARY_LOCATION CUDA::cudart LOCATION)
        IF(WIN32)
            # Derive path of DLL from .lib location.
            GET_FILENAME_COMPONENT(CUDA_LIBRARY_PATH "${CUDA_LIBRARY_LOCATION}" PATH)
            SET(CUDA_LIBRARY_LOCATION "${CUDA_LIBRARY_PATH}/../../bin/cudart64_12.dll")
        ENDIF()
        OVITO_INSTALL_SHARED_LIB("${CUDA_LIBRARY_LOCATION}")
    ENDIF()
ENDIF()

# Compute the relative path from the OVITO executable to where the plugin libraries get installed.
FILE(RELATIVE_PATH OVITO_PLUGINS_RELATIVE_PATH "${OVITO_BINARY_DIRECTORY}" "${OVITO_PLUGINS_DIRECTORY}")
TARGET_COMPILE_DEFINITIONS(Core PRIVATE "OVITO_PLUGINS_RELATIVE_PATH=\"${OVITO_PLUGINS_RELATIVE_PATH}\"")

IF(NOT OVITO_BUILD_CONDA)
    # Compute the relative path from the OVITO executable to where the Python module layer gets installed.
    FILE(RELATIVE_PATH OVITO_PYTHON_LAYER_PATH "${OVITO_BINARY_DIRECTORY}" "${OVITO_PYTHON_DIRECTORY}")
    TARGET_COMPILE_DEFINITIONS(Core PRIVATE "OVITO_PYTHON_LAYER_PATH=\"${OVITO_PYTHON_LAYER_PATH}\"")
ELSE()
    # Compute the relative path from the Conda prefix to where the Python module layer gets installed.
    FILE(RELATIVE_PATH OVITO_PYTHON_LAYER_PATH "$ENV{PREFIX}" "$ENV{SP_DIR}")
    TARGET_COMPILE_DEFINITIONS(Core PRIVATE "OVITO_PYTHON_LAYER_PATH=\"${OVITO_PYTHON_LAYER_PATH}\"")
ENDIF()

# Link to the pthread library on Linux platform.
IF(LINUX)
    SET(THREADS_PREFER_PTHREAD_FLAG ON)
    FIND_PACKAGE(Threads REQUIRED)
    TARGET_LINK_LIBRARIES(Core PUBLIC Threads::Threads)
ENDIF()

IF(NOT OVITO_DOUBLE_PRECISION_FP)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "FLOATTYPE_FLOAT")
ENDIF()
IF(OVITO_BUILD_APPSTORE_VERSION)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_APPSTORE_VERSION")
ENDIF()
IF(OVITO_BUILD_MONOLITHIC)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_MONOLITHIC")
ENDIF()
IF(OVITO_DISABLE_THREADING)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_DISABLE_THREADING")
ENDIF()
IF(OVITO_BUILD_PROFESSIONAL)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_PROFESSIONAL")
ENDIF()
IF(OVITO_BUILD_PYPI)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_PYPI")
ENDIF()
IF(OVITO_BUILD_BASIC)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_BASIC")
ENDIF()
IF(OVITO_BUILD_CONDA)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_BUILD_CONDA")
ENDIF()
IF(EMSCRIPTEN)
    TARGET_COMPILE_DEFINITIONS(Core PUBLIC "OVITO_DISABLE_QSETTINGS")
ENDIF()

# Inject program release version into the source code.
SET_PROPERTY(SOURCE app/Application.cpp app/StandaloneApplication.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
    "OVITO_VERSION_MAJOR=${OVITO_VERSION_MAJOR}"
    "OVITO_VERSION_MINOR=${OVITO_VERSION_MINOR}"
    "OVITO_VERSION_REVISION=${OVITO_VERSION_REVISION}"
    "OVITO_VERSION_STRING=\"${OVITO_VERSION_STRING}\""
    "OVITO_APPLICATION_NAME=\"${OVITO_APPLICATION_NAME}\"")
SET_PROPERTY(SOURCE utilities/io/SaveStream.cpp utilities/io/LoadStream.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
    "OVITO_FILE_FORMAT_VERSION=${OVITO_FILE_FORMAT_VERSION}")

# Run clang-tidy to check C++ code and perform static analysis.
IF(OVITO_CLANG_TIDY_CMD)
    SET_TARGET_PROPERTIES(Core PROPERTIES CXX_CLANG_TIDY "${OVITO_CLANG_TIDY_CMD}")
ENDIF()

# Install ffmpeg video encoding libraries.
IF(FFMPEG_FOUND AND NOT EMSCRIPTEN)
    FOREACH(lib ${FFMPEG_LIBRARIES})
        GET_FILENAME_COMPONENT(base_name "${lib}" NAME_WE)
        GET_FILENAME_COMPONENT(lib_path "${lib}" PATH)
        IF(WIN32 AND NOT OVITO_BUILD_CONDA)
            FILE(GLOB lib_dll "${lib_path}/${base_name}-*.dll")
            IF(NOT lib_dll)
                MESSAGE(FATAL_ERROR "Could not find corresponding DLL for ffmpeg library '${lib}' in same directory.")
            ENDIF()
            FOREACH(dll ${lib_dll})
                OVITO_INSTALL_SHARED_LIB("${dll}")
            ENDFOREACH()
        ELSEIF(LINUX AND (OVITO_REDISTRIBUTABLE_PACKAGE OR OVITO_BUILD_PYPI))
            FILE(GLOB lib_versions "${lib}*")
            FOREACH(lib_version ${lib_versions})
                OVITO_INSTALL_SHARED_LIB("${lib_version}")
            ENDFOREACH()
        ENDIF()
    ENDFOREACH()
ENDIF()

IF(ZLIB_FOUND AND NOT EMSCRIPTEN)
    IF(WIN32 AND NOT OVITO_BUILD_CONDA)
        # Deploy the zlib DLL.
        GET_TARGET_PROPERTY(ZLIB_LIBRARY_LOCATION ZLIB::ZLIB LOCATION)
        GET_FILENAME_COMPONENT(ZLIB_LIBRARY_PATH "${ZLIB_LIBRARY_LOCATION}" PATH)
        # The DLL name differs between distributions (zlib.dll, zlib1.dll, z.dll in recent vcpkg). Try the name of the
        # import library first, then the known alternatives.
        GET_FILENAME_COMPONENT(ZLIB_LIBRARY_NAME_WE "${ZLIB_LIBRARY_LOCATION}" NAME_WE)
        # A static zlib (e.g. vcpkg's x64-windows-static-md triplet) has no DLL to deploy.
        UNSET(ZLIB_DLL)
        FOREACH(_name ${ZLIB_LIBRARY_NAME_WE} zlib zlib1 z)
            IF(EXISTS "${ZLIB_LIBRARY_PATH}/../bin/${_name}.dll")
                SET(ZLIB_DLL "${ZLIB_LIBRARY_PATH}/../bin/${_name}.dll")
                BREAK()
            ENDIF()
        ENDFOREACH()
        IF(ZLIB_DLL)
            OVITO_INSTALL_SHARED_LIB("${ZLIB_DLL}")
        ELSEIF(NOT EXISTS "${ZLIB_LIBRARY_PATH}/../bin")
            MESSAGE(STATUS "zlib is linked statically, no DLL to deploy.")
        ELSE()
            OVITO_INSTALL_SHARED_LIB("${ZLIB_LIBRARY_PATH}/../bin/zlib.dll")
        ENDIF()
    ENDIF()
ENDIF()

# Workaround for failing Linux conda builds (may become obsolete in the future once the issue in qt6-main has been fixed):
#   "warning: libEGL.so.1, needed by libQt6Gui.so.6.5.0, not found (try using -rpath or -rpath-link"
IF(OVITO_BUILD_CONDA AND LINUX)
    # Find and link against EGL library (optional).
    FIND_PACKAGE(OpenGL COMPONENTS EGL)
    IF(TARGET OpenGL::EGL)
        TARGET_LINK_LIBRARIES(Core PUBLIC OpenGL::EGL)
    ENDIF()
ENDIF()

# Deploy the libssh library and its dependencies.
IF(OVITO_BUILD_SSH_CLIENT AND NOT EMSCRIPTEN AND NOT OVITO_BUILD_CONDA)

    # Find OpenSSL 3.0 or newer, needed by libssh.
    FIND_PACKAGE(OpenSSL 3.0 REQUIRED COMPONENTS Crypto SSL)

    # Get the location of the libssh and OpenSSL libraries.
    GET_TARGET_PROPERTY(LIBSSH_LIBRARY_LOCATION ssh LOCATION)
    GET_TARGET_PROPERTY(OPENSSL_CRYPTO_LIBRARY_LOCATION OpenSSL::Crypto LOCATION)
    GET_TARGET_PROPERTY(OPENSSL_SSL_LIBRARY_LOCATION OpenSSL::SSL LOCATION)

    IF(WIN32)
        # Install libssh.dll
        GET_FILENAME_COMPONENT(LIBSSH_LIBRARY_DIR "${LIBSSH_LIBRARY_LOCATION}" PATH)
        GET_FILENAME_COMPONENT(LIBSSH_LIBRARY_NAME_WE "${LIBSSH_LIBRARY_LOCATION}" NAME_WE)
        OVITO_INSTALL_SHARED_LIB("${LIBSSH_LIBRARY_DIR}/../bin/${LIBSSH_LIBRARY_NAME_WE}.dll")

        # Also install OpenSSL DLLs needed by libssh.
        GET_FILENAME_COMPONENT(OPENSSL_CRYPTO_LIBRARY_DIR "${OPENSSL_CRYPTO_LIBRARY_LOCATION}" PATH)
        GET_FILENAME_COMPONENT(OPENSSL_SSL_LIBRARY_DIR "${OPENSSL_SSL_LIBRARY_LOCATION}" PATH)
        OVITO_INSTALL_SHARED_LIB("${OPENSSL_CRYPTO_LIBRARY_DIR}/../bin/libcrypto-3-x64.dll")
        OVITO_INSTALL_SHARED_LIB("${OPENSSL_SSL_LIBRARY_DIR}/../bin/libssl-3-x64.dll")
    ELSEIF(APPLE)
        # Install the OpenSSL PKCS#11 provider module needed by libssh.
        GET_FILENAME_COMPONENT(OPENSSL_LIBRARY_DIR "${OPENSSL_CRYPTO_LIBRARY_LOCATION}" PATH)
        INSTALL(FILES "${OPENSSL_LIBRARY_DIR}/ossl-modules/pkcs11.dylib" DESTINATION "${OVITO_RELATIVE_PLUGINS_DIRECTORY}/ossl-modules/")

        # Additionally, copy the provider modules directory into the build directory to make it available during testing.
        FILE(MAKE_DIRECTORY "${Ovito_BINARY_DIR}/${OVITO_RELATIVE_PLUGINS_DIRECTORY}/ossl-modules")
        FILE(COPY "${OPENSSL_LIBRARY_DIR}/ossl-modules/pkcs11.dylib" DESTINATION "${Ovito_BINARY_DIR}/${OVITO_RELATIVE_PLUGINS_DIRECTORY}/ossl-modules/")
    ELSEIF(LINUX AND OVITO_REDISTRIBUTABLE_PACKAGE)
        # Install libssh.so
        OVITO_INSTALL_SHARED_LIB("${LIBSSH_LIBRARY_LOCATION}")

        # Install OpenSSL (libcrypto.so and libssl.so) needed by libssh.
        OVITO_INSTALL_SHARED_LIB("${OPENSSL_CRYPTO_LIBRARY_LOCATION}")
        OVITO_INSTALL_SHARED_LIB("${OPENSSL_SSL_LIBRARY_LOCATION}")

        # Install the OpenSSL PKCS#11 provider module needed by libssh.
        GET_FILENAME_COMPONENT(OPENSSL_LIBRARY_DIR "${OPENSSL_CRYPTO_LIBRARY_LOCATION}" PATH)
        INSTALL(FILES "${OPENSSL_LIBRARY_DIR}/ossl-modules/pkcs11.so" DESTINATION "${OVITO_RELATIVE_PLUGINS_DIRECTORY}/ossl-modules/")
    ENDIF()
ENDIF()
