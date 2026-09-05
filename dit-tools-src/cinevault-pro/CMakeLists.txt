cmake_minimum_required(VERSION 3.24)

set(CINEVAULT_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
if(NOT EXISTS "${CINEVAULT_VERSION_FILE}")
    message(FATAL_ERROR "CineVault version source is missing: ${CINEVAULT_VERSION_FILE}")
endif()
file(STRINGS "${CINEVAULT_VERSION_FILE}" CINEVAULT_SOURCE_VERSION LIMIT_COUNT 1)
string(STRIP "${CINEVAULT_SOURCE_VERSION}" CINEVAULT_SOURCE_VERSION)
if(NOT CINEVAULT_SOURCE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one semantic version, got: ${CINEVAULT_SOURCE_VERSION}")
endif()

project(CineVault VERSION ${CINEVAULT_SOURCE_VERSION} LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(CINEVAULT_BUILD_MINIMAL_GUI "Build the milestone A minimal GUI shell" ON)
option(CINEVAULT_ENABLE_FFMPEG "Enable FFmpeg-backed media modules" OFF)
option(CINEVAULT_ENABLE_LOCAL_SEARCH "Enable offline BGE and USearch semantic search" OFF)
option(CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT "Enable the offline text query assistant" OFF)
option(CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL "Copy the Qwen model into build and install payloads" OFF)
option(CINEVAULT_ENABLE_EXIFTOOL "Bundle ExifTool for embedded media metadata" ON)
option(CINEVAULT_ENABLE_RAW_PREVIEW "Enable isolated camera RAW preview support" ON)
option(CINEVAULT_ENABLE_RAW_EXTENDED_SDKS "Enable license-approved extended RAW SDKs" ON)
set(QT_ROOT "" CACHE PATH "Qt 6 kit root directory")
set(FFMPEG_DEV_ROOT "" CACHE PATH "FFmpeg development package root directory")
set(CINEVAULT_LOCAL_SEARCH_CACHE_ROOT "" CACHE PATH "Prepared local-search dependency cache")
set(CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT "" CACHE PATH "Prepared search-assistant dependency cache")
set(CINEVAULT_EXIFTOOL_CACHE_ROOT "" CACHE PATH "Prepared ExifTool dependency cache")
set(CINEVAULT_LIBRAW_CACHE_ROOT "" CACHE PATH "Prepared LibRaw dependency cache")
if(DEFINED CINEVAULT_APP_VERSION
   AND NOT CINEVAULT_APP_VERSION STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "CINEVAULT_APP_VERSION (${CINEVAULT_APP_VERSION}) must match VERSION (${PROJECT_VERSION})")
endif()
set(CINEVAULT_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version from VERSION" FORCE)
set(CINEVAULT_UPDATE_SIGNER_SHA256 "" CACHE STRING "Pinned SHA-256 thumbprint for the trusted Windows update signer")

if(QT_ROOT AND EXISTS "${QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

set(CINEVAULT_HAS_BUNDLED_EXIFTOOL OFF)
if(CINEVAULT_ENABLE_EXIFTOOL AND NOT CINEVAULT_BUILD_MINIMAL_GUI AND WIN32)
    if(NOT CINEVAULT_EXIFTOOL_CACHE_ROOT
       AND DEFINED ENV{CINEVAULT_EXIFTOOL_CACHE}
       AND NOT "$ENV{CINEVAULT_EXIFTOOL_CACHE}" STREQUAL "")
        set(CINEVAULT_EXIFTOOL_CACHE_ROOT "$ENV{CINEVAULT_EXIFTOOL_CACHE}")
    endif()
    if(CINEVAULT_EXIFTOOL_CACHE_ROOT)
        cmake_path(ABSOLUTE_PATH CINEVAULT_EXIFTOOL_CACHE_ROOT NORMALIZE
                   OUTPUT_VARIABLE CINEVAULT_EXIFTOOL_CACHE_ROOT)
        set(CINEVAULT_EXIFTOOL_LOCK
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/exiftool-dependencies.lock.json")
        set(CINEVAULT_EXIFTOOL_READY
            "${CINEVAULT_EXIFTOOL_CACHE_ROOT}/exiftool-dependency.ready")
        if(NOT EXISTS "${CINEVAULT_EXIFTOOL_READY}")
            message(FATAL_ERROR
                "ExifTool cache is not prepared. Run cmake/PrepareExifToolDependency.cmake first.")
        endif()
        file(SHA256 "${CINEVAULT_EXIFTOOL_LOCK}" CINEVAULT_EXIFTOOL_LOCK_SHA256)
        file(READ "${CINEVAULT_EXIFTOOL_READY}" CINEVAULT_EXIFTOOL_READY_CONTENTS)
        string(FIND "${CINEVAULT_EXIFTOOL_READY_CONTENTS}"
                    "lock_sha256=${CINEVAULT_EXIFTOOL_LOCK_SHA256}"
                    CINEVAULT_EXIFTOOL_LOCK_MATCH)
        if(CINEVAULT_EXIFTOOL_LOCK_MATCH EQUAL -1)
            message(FATAL_ERROR "ExifTool cache does not match the dependency lock")
        endif()
        file(READ "${CINEVAULT_EXIFTOOL_LOCK}" CINEVAULT_EXIFTOOL_LOCK_JSON)
        string(JSON CINEVAULT_EXIFTOOL_VERSION GET
               "${CINEVAULT_EXIFTOOL_LOCK_JSON}" dependencies exifTool version)
        set(CINEVAULT_EXIFTOOL_RUNTIME_ROOT
            "${CINEVAULT_EXIFTOOL_CACHE_ROOT}/runtimes/exiftool-${CINEVAULT_EXIFTOOL_VERSION}-win-x64")
        if(NOT EXISTS "${CINEVAULT_EXIFTOOL_RUNTIME_ROOT}/exiftool.exe"
           OR NOT EXISTS "${CINEVAULT_EXIFTOOL_RUNTIME_ROOT}/exiftool_files/exiftool.pl")
            message(FATAL_ERROR "Prepared ExifTool runtime is incomplete")
        endif()
        set(CINEVAULT_HAS_BUNDLED_EXIFTOOL ON)
    else()
        message(WARNING
            "ExifTool cache is not configured; runtime will fall back to CINEVAULT_EXIFTOOL_PATH or PATH")
    endif()
endif()

set(CINEVAULT_HAS_BUNDLED_LIBRAW OFF)
set(CINEVAULT_HAS_BUNDLED_GPR OFF)
if(CINEVAULT_ENABLE_RAW_PREVIEW AND NOT CINEVAULT_BUILD_MINIMAL_GUI AND WIN32)
    if(NOT CINEVAULT_LIBRAW_CACHE_ROOT
       AND DEFINED ENV{CINEVAULT_LIBRAW_CACHE}
       AND NOT "$ENV{CINEVAULT_LIBRAW_CACHE}" STREQUAL "")
        set(CINEVAULT_LIBRAW_CACHE_ROOT "$ENV{CINEVAULT_LIBRAW_CACHE}")
    endif()
    if(CINEVAULT_LIBRAW_CACHE_ROOT)
        cmake_path(ABSOLUTE_PATH CINEVAULT_LIBRAW_CACHE_ROOT NORMALIZE
                   OUTPUT_VARIABLE CINEVAULT_LIBRAW_CACHE_ROOT)
        set(CINEVAULT_LIBRAW_LOCK
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/raw-preview-dependencies.lock.json")
        set(CINEVAULT_LIBRAW_READY
            "${CINEVAULT_LIBRAW_CACHE_ROOT}/libraw-dependency.ready")
        if(NOT EXISTS "${CINEVAULT_LIBRAW_READY}")
            message(FATAL_ERROR
                "LibRaw cache is not prepared. Run cmake/PrepareRawPreviewDependencies.cmake first.")
        endif()
        file(SHA256 "${CINEVAULT_LIBRAW_LOCK}" CINEVAULT_LIBRAW_LOCK_SHA256)
        file(READ "${CINEVAULT_LIBRAW_READY}" CINEVAULT_LIBRAW_READY_CONTENTS)
        string(FIND "${CINEVAULT_LIBRAW_READY_CONTENTS}"
                    "lock_sha256=${CINEVAULT_LIBRAW_LOCK_SHA256}"
                    CINEVAULT_LIBRAW_LOCK_MATCH)
        string(FIND "${CINEVAULT_LIBRAW_READY_CONTENTS}"
                    "selected_license=CDDL-1.0"
                    CINEVAULT_LIBRAW_LICENSE_MATCH)
        if(CINEVAULT_LIBRAW_LOCK_MATCH EQUAL -1
           OR CINEVAULT_LIBRAW_LICENSE_MATCH EQUAL -1)
            message(FATAL_ERROR "LibRaw cache does not match the audited dependency lock and license")
        endif()
        file(READ "${CINEVAULT_LIBRAW_LOCK}" CINEVAULT_LIBRAW_LOCK_JSON)
        string(JSON CINEVAULT_LIBRAW_VERSION GET
               "${CINEVAULT_LIBRAW_LOCK_JSON}" dependencies libRaw version)
        set(CINEVAULT_LIBRAW_RUNTIME_ROOT
            "${CINEVAULT_LIBRAW_CACHE_ROOT}/runtimes/libraw-${CINEVAULT_LIBRAW_VERSION}-win-x64")
        foreach(CINEVAULT_LIBRAW_REQUIRED_FILE IN ITEMS
                "include/libraw/libraw.h"
                "lib/libraw.lib"
                "bin/libraw.dll"
                "licenses/LICENSE.CDDL"
                "licenses/LICENSE.LGPL"
                "licenses/COPYRIGHT")
            if(NOT EXISTS
               "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/${CINEVAULT_LIBRAW_REQUIRED_FILE}")
                message(FATAL_ERROR
                    "Prepared LibRaw runtime is incomplete: ${CINEVAULT_LIBRAW_REQUIRED_FILE}")
            endif()
        endforeach()
        set(CINEVAULT_HAS_BUNDLED_LIBRAW ON)
        if(CINEVAULT_ENABLE_RAW_EXTENDED_SDKS)
            string(JSON CINEVAULT_GPR_SDK_VERSION GET
                   "${CINEVAULT_LIBRAW_LOCK_JSON}" extendedSdks 0 version)
            set(CINEVAULT_GPR_RUNTIME_ROOT
                "${CINEVAULT_LIBRAW_CACHE_ROOT}/runtimes/gpr-sdk-${CINEVAULT_GPR_SDK_VERSION}-win-x64")
            string(FIND "${CINEVAULT_LIBRAW_READY_CONTENTS}"
                        "gpr_version=${CINEVAULT_GPR_SDK_VERSION}"
                        CINEVAULT_GPR_VERSION_MATCH)
            string(FIND "${CINEVAULT_LIBRAW_READY_CONTENTS}"
                        "gpr_selected_license=MIT"
                        CINEVAULT_GPR_LICENSE_MATCH)
            if(CINEVAULT_GPR_VERSION_MATCH EQUAL -1
               OR CINEVAULT_GPR_LICENSE_MATCH EQUAL -1)
                message(FATAL_ERROR
                    "GoPro GPR SDK cache does not match the audited dependency lock and license")
            endif()
            foreach(CINEVAULT_GPR_REQUIRED_FILE IN ITEMS
                    "bin/gpr_tools.exe"
                    "licenses/LICENSE-MIT"
                    "licenses/LICENSE-APACHE"
                    "licenses/LICENSE.txt"
                    "licenses/AUTHORS.md")
                if(NOT EXISTS
                   "${CINEVAULT_GPR_RUNTIME_ROOT}/${CINEVAULT_GPR_REQUIRED_FILE}")
                    message(FATAL_ERROR
                        "Prepared GoPro GPR SDK runtime is incomplete: ${CINEVAULT_GPR_REQUIRED_FILE}")
                endif()
            endforeach()
            set(CINEVAULT_HAS_BUNDLED_GPR ON)
        endif()
    else()
        message(WARNING
            "LibRaw cache is not configured; RAW preview will use non-LibRaw fallback providers")
    endif()
endif()

set(CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT OFF)
if(CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL
   AND NOT CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT)
    message(FATAL_ERROR
        "CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL requires CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT")
endif()
if(CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT AND NOT CINEVAULT_BUILD_MINIMAL_GUI)
    if(NOT WIN32)
        message(FATAL_ERROR "The locked search-assistant runtime currently supports Windows x64 only")
    endif()
    if(NOT CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT
       AND DEFINED ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}
       AND NOT "$ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}" STREQUAL "")
        set(CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT "$ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}")
    endif()
    if(NOT CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT)
        message(FATAL_ERROR
            "CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT requires a prepared cache. "
            "Run cmake/PrepareSearchAssistantDependencies.cmake first.")
    endif()
    if(NOT IS_ABSOLUTE "${CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT}")
        message(FATAL_ERROR "CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT must be absolute")
    endif()
    cmake_path(NORMAL_PATH CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT
               OUTPUT_VARIABLE CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT)

    set(CINEVAULT_SEARCH_ASSISTANT_LOCK
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/search-assistant-dependencies.lock.json")
    set(CINEVAULT_SEARCH_ASSISTANT_READY
        "${CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT}/search-assistant-dependencies.ready")
    if(NOT EXISTS "${CINEVAULT_SEARCH_ASSISTANT_READY}")
        message(FATAL_ERROR
            "Search-assistant cache is not prepared: ${CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT}")
    endif()
    file(SHA256 "${CINEVAULT_SEARCH_ASSISTANT_LOCK}" CINEVAULT_SEARCH_ASSISTANT_LOCK_SHA256)
    file(READ "${CINEVAULT_SEARCH_ASSISTANT_READY}" CINEVAULT_SEARCH_ASSISTANT_READY_CONTENTS)
    string(FIND "${CINEVAULT_SEARCH_ASSISTANT_READY_CONTENTS}"
                "lock_sha256=${CINEVAULT_SEARCH_ASSISTANT_LOCK_SHA256}"
                CINEVAULT_SEARCH_ASSISTANT_LOCK_MATCH)
    if(CINEVAULT_SEARCH_ASSISTANT_LOCK_MATCH EQUAL -1)
        message(FATAL_ERROR
            "Search-assistant cache does not match the dependency lock. "
            "Run the preparation script again.")
    endif()

    file(READ "${CINEVAULT_SEARCH_ASSISTANT_LOCK}" CINEVAULT_SEARCH_ASSISTANT_LOCK_JSON)
    string(JSON CINEVAULT_SEARCH_ASSISTANT_RUNTIME_VERSION GET
           "${CINEVAULT_SEARCH_ASSISTANT_LOCK_JSON}" dependencies runtime version)
    string(JSON CINEVAULT_SEARCH_ASSISTANT_MODEL_FILE GET
           "${CINEVAULT_SEARCH_ASSISTANT_LOCK_JSON}" dependencies model file)
    set(CINEVAULT_SEARCH_ASSISTANT_RUNTIME_ROOT
        "${CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT}/runtimes/llama-${CINEVAULT_SEARCH_ASSISTANT_RUNTIME_VERSION}-win-vulkan-x64")
    set(CINEVAULT_SEARCH_ASSISTANT_MODEL_ROOT
        "${CINEVAULT_SEARCH_ASSISTANT_CACHE_ROOT}/models/qwen3-0.6b")
    set(CINEVAULT_SEARCH_ASSISTANT_REQUIRED_FILES
        "${CINEVAULT_SEARCH_ASSISTANT_RUNTIME_ROOT}/llama-server.exe"
    )
    if(CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL)
        list(APPEND CINEVAULT_SEARCH_ASSISTANT_REQUIRED_FILES
            "${CINEVAULT_SEARCH_ASSISTANT_MODEL_ROOT}/${CINEVAULT_SEARCH_ASSISTANT_MODEL_FILE}")
    endif()
    foreach(_required_file IN LISTS CINEVAULT_SEARCH_ASSISTANT_REQUIRED_FILES)
        if(NOT EXISTS "${_required_file}")
            message(FATAL_ERROR "Prepared search-assistant cache is incomplete: ${_required_file}")
        endif()
    endforeach()
    set(CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT ON)
endif()

set(CINEVAULT_QT_COMPONENTS
    Core
    Gui
    Pdf
    PdfQuick
    Qml
    Quick
    QuickControls2
    Multimedia
    Network
    Widgets
)

if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
    list(APPEND CINEVAULT_QT_COMPONENTS
        Sql
        Concurrent
        WebSockets
    )
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS ${CINEVAULT_QT_COMPONENTS})

qt_standard_project_setup(REQUIRES 6.5)

set(CINEVAULT_HAS_BUNDLED_WEBP OFF)
set(CINEVAULT_LIBWEBP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libwebp")
if(EXISTS "${CINEVAULT_LIBWEBP_SOURCE_DIR}/CMakeLists.txt")
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CINEVAULT_LIBWEBP_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/third_party/libwebp"
                     EXCLUDE_FROM_ALL)
    set(CINEVAULT_HAS_BUNDLED_WEBP ON)
endif()

set(CINEVAULT_HAS_FFMPEG OFF)
if(CINEVAULT_ENABLE_FFMPEG AND NOT CINEVAULT_BUILD_MINIMAL_GUI)
    find_package(FFmpeg REQUIRED)
    set(CINEVAULT_HAS_FFMPEG ON)
endif()

set(CINEVAULT_HAS_LOCAL_SEARCH OFF)
set(CINEVAULT_LOCAL_SEARCH_MODEL_FILES
    onnx/model_quantized.onnx
    config.json
    special_tokens_map.json
    tokenizer.json
    tokenizer_config.json
    vocab.txt
)
if(CINEVAULT_ENABLE_LOCAL_SEARCH AND NOT CINEVAULT_BUILD_MINIMAL_GUI)
    if(NOT WIN32)
        message(FATAL_ERROR "The locked ONNX Runtime package currently supports Windows x64 only")
    endif()
    if(NOT CINEVAULT_LOCAL_SEARCH_CACHE_ROOT
       AND DEFINED ENV{CINEVAULT_DEPENDENCY_CACHE}
       AND NOT "$ENV{CINEVAULT_DEPENDENCY_CACHE}" STREQUAL "")
        set(CINEVAULT_LOCAL_SEARCH_CACHE_ROOT "$ENV{CINEVAULT_DEPENDENCY_CACHE}")
    endif()
    if(NOT CINEVAULT_LOCAL_SEARCH_CACHE_ROOT)
        message(FATAL_ERROR
            "CINEVAULT_ENABLE_LOCAL_SEARCH requires a prepared offline cache. "
            "Run cmake/PrepareLocalSearchDependencies.cmake explicitly first.")
    endif()
    if(NOT IS_ABSOLUTE "${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}")
        message(FATAL_ERROR "CINEVAULT_LOCAL_SEARCH_CACHE_ROOT must be an absolute path")
    endif()
    cmake_path(NORMAL_PATH CINEVAULT_LOCAL_SEARCH_CACHE_ROOT
               OUTPUT_VARIABLE CINEVAULT_LOCAL_SEARCH_CACHE_ROOT)

    set(CINEVAULT_LOCAL_SEARCH_LOCK
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-search-dependencies.lock.json")
    set(CINEVAULT_LOCAL_SEARCH_READY
        "${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}/local-search-dependencies.ready")
    if(NOT EXISTS "${CINEVAULT_LOCAL_SEARCH_READY}")
        message(FATAL_ERROR
            "Local-search cache is not prepared: ${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}")
    endif()
    file(SHA256 "${CINEVAULT_LOCAL_SEARCH_LOCK}" CINEVAULT_LOCAL_SEARCH_LOCK_SHA256)
    file(READ "${CINEVAULT_LOCAL_SEARCH_READY}" CINEVAULT_LOCAL_SEARCH_READY_CONTENTS)
    string(FIND "${CINEVAULT_LOCAL_SEARCH_READY_CONTENTS}"
                "lock_sha256=${CINEVAULT_LOCAL_SEARCH_LOCK_SHA256}"
                CINEVAULT_LOCAL_SEARCH_LOCK_MATCH)
    if(CINEVAULT_LOCAL_SEARCH_LOCK_MATCH EQUAL -1)
        message(FATAL_ERROR
            "Local-search cache does not match the current dependency lock. "
            "Run the explicit preparation script again.")
    endif()

    file(READ "${CINEVAULT_LOCAL_SEARCH_LOCK}" CINEVAULT_LOCAL_SEARCH_LOCK_JSON)
    string(JSON CINEVAULT_ONNXRUNTIME_VERSION GET
           "${CINEVAULT_LOCAL_SEARCH_LOCK_JSON}" dependencies onnxRuntime version)
    string(JSON CINEVAULT_USEARCH_COMMIT GET
           "${CINEVAULT_LOCAL_SEARCH_LOCK_JSON}" dependencies usearch commit)
    set(CINEVAULT_ONNXRUNTIME_ROOT
        "${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}/packages/onnxruntime-win-x64-${CINEVAULT_ONNXRUNTIME_VERSION}")
    set(CINEVAULT_USEARCH_ROOT
        "${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}/sources/USearch-${CINEVAULT_USEARCH_COMMIT}")
    set(CINEVAULT_LOCAL_SEARCH_MODEL_ROOT
        "${CINEVAULT_LOCAL_SEARCH_CACHE_ROOT}/models/bge-small-zh-v1.5")

    set(CINEVAULT_LOCAL_SEARCH_REQUIRED_FILES
        "${CINEVAULT_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
        "${CINEVAULT_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        "${CINEVAULT_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "${CINEVAULT_USEARCH_ROOT}/c/lib.cpp"
        "${CINEVAULT_USEARCH_ROOT}/c/usearch.h"
        "${CINEVAULT_USEARCH_ROOT}/include/usearch/index.hpp"
        "${CINEVAULT_USEARCH_ROOT}/include/usearch/index_dense.hpp"
    )
    foreach(_model_file IN LISTS CINEVAULT_LOCAL_SEARCH_MODEL_FILES)
        list(APPEND CINEVAULT_LOCAL_SEARCH_REQUIRED_FILES
            "${CINEVAULT_LOCAL_SEARCH_MODEL_ROOT}/${_model_file}")
    endforeach()
    foreach(_required_file IN LISTS CINEVAULT_LOCAL_SEARCH_REQUIRED_FILES)
        if(NOT EXISTS "${_required_file}")
            message(FATAL_ERROR "Prepared local-search cache is incomplete: ${_required_file}")
        endif()
    endforeach()

    add_library(CineVaultOnnxRuntime SHARED IMPORTED GLOBAL)
    set_target_properties(CineVaultOnnxRuntime PROPERTIES
        IMPORTED_IMPLIB "${CINEVAULT_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        IMPORTED_LOCATION "${CINEVAULT_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CINEVAULT_ONNXRUNTIME_ROOT}/include"
    )

    add_library(CineVaultUsearch STATIC
        "${CINEVAULT_USEARCH_ROOT}/c/lib.cpp"
    )
    target_include_directories(CineVaultUsearch
        PUBLIC "${CINEVAULT_USEARCH_ROOT}/c"
        PRIVATE "${CINEVAULT_USEARCH_ROOT}/include"
    )
    target_compile_definitions(CineVaultUsearch PRIVATE
        USEARCH_EXPORT=
        USEARCH_USE_NUMKONG=0
        USEARCH_USE_OPENMP=0
    )
    set_target_properties(CineVaultUsearch PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(CineVaultUsearch PRIVATE /utf-8)
    endif()
    set(CINEVAULT_HAS_LOCAL_SEARCH ON)
endif()

if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
    add_library(CineVaultLocalSearch STATIC
        src/core/search/BertTokenizer.h
        src/core/search/BertTokenizer.cpp
        src/core/search/BgeEmbeddingModel.h
        src/core/search/BgeEmbeddingModel.cpp
        src/core/search/SemanticSearchProvider.h
        src/core/search/SemanticVectorIndex.h
        src/core/search/SemanticVectorIndex.cpp
        src/core/search/SemanticSearchIndexService.h
        src/core/search/SemanticSearchIndexService.cpp
    )
    set_target_properties(CineVaultLocalSearch PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_include_directories(CineVaultLocalSearch PUBLIC src)
    target_compile_definitions(CineVaultLocalSearch PRIVATE
        CINEVAULT_HAS_LOCAL_SEARCH=$<IF:$<BOOL:${CINEVAULT_HAS_LOCAL_SEARCH}>,1,0>
    )
    target_link_libraries(CineVaultLocalSearch PUBLIC Qt6::Core Qt6::Sql)
    if(CINEVAULT_HAS_LOCAL_SEARCH)
        target_compile_definitions(CineVaultLocalSearch PRIVATE
            CINEVAULT_LOCAL_SEARCH_MODEL_ROOT="${CINEVAULT_LOCAL_SEARCH_MODEL_ROOT}"
        )
        target_link_libraries(CineVaultLocalSearch PRIVATE
            CineVaultOnnxRuntime
            CineVaultUsearch
        )
    endif()
endif()

set(APP_SOURCES
    src/app/main.cpp
    src/app/AppBootstrap.h
    src/app/AppBootstrap.cpp
    src/app/SingleInstanceGuard.h
    src/app/SingleInstanceGuard.cpp
    src/app/AppContext.h
    src/app/AppContext.cpp
    src/domain/Enums.h
    src/domain/Entities.h
    src/domain/SearchTypes.h
    src/shared/Paths.h
    src/shared/Paths.cpp
    src/shared/ThumbnailCacheQuota.h
    src/shared/ThumbnailCacheQuota.cpp
    src/shared/FolderPathMetadata.h
    src/shared/FolderPathMetadata.cpp
    src/shared/SearchConfiguration.h
    src/shared/VisualAnalysisMetadata.h
    src/shared/VisualAnalysisMetadata.cpp
    src/shared/Formatters.h
    src/shared/Formatters.cpp
    src/shared/FileRevealService.h
    src/shared/FileRevealService.cpp
    src/shared/ScopedBackgroundThreadPriority.h
    src/ui/imaging/LocalImageProvider.h
    src/ui/imaging/LocalImageProvider.cpp
    src/ui/imaging/LocalImageUrlHelper.h
    src/ui/imaging/LocalImageUrlHelper.cpp
    src/ui/window/WindowThemeController.h
    src/ui/window/WindowThemeController.cpp
    src/ui/models/SourceRootListModel.h
    src/ui/models/SourceRootListModel.cpp
    src/ui/models/ProjectLibraryListModel.h
    src/ui/models/ProjectLibraryListModel.cpp
    src/ui/models/AssetListModel.h
    src/ui/models/AssetListModel.cpp
    src/ui/models/JobListModel.h
    src/ui/models/JobListModel.cpp
)

set(APP_WINDOWS_RESOURCES)
if(WIN32)
    list(APPEND APP_WINDOWS_RESOURCES
        src/app/windows_app_icon.rc
    )
endif()

if(CINEVAULT_BUILD_MINIMAL_GUI)
    list(APPEND APP_SOURCES
        src/ui/viewmodels/MinimalShellViewModel.h
        src/ui/viewmodels/MinimalShellViewModel.cpp
        src/ui/viewmodels/MinimalSourceRailViewModel.h
        src/ui/viewmodels/MinimalSourceRailViewModel.cpp
        src/ui/viewmodels/MinimalLibraryWorkspaceViewModel.h
        src/ui/viewmodels/MinimalLibraryWorkspaceViewModel.cpp
        src/ui/viewmodels/MinimalInspectorViewModel.h
        src/ui/viewmodels/MinimalInspectorViewModel.cpp
        src/ui/viewmodels/MinimalJobTimelineViewModel.h
        src/ui/viewmodels/MinimalJobTimelineViewModel.cpp
        src/ui/viewmodels/MinimalReportWorkspaceViewModel.h
        src/ui/viewmodels/MinimalReportWorkspaceViewModel.cpp
    )
else()
    list(APPEND APP_SOURCES
    src/application/DocumentPreviewService.h
    src/application/DocumentPreviewService.cpp
    src/application/SearchDocumentSyncService.h
    src/application/SearchDocumentSyncService.cpp
    src/infrastructure/config/AppSettings.h
    src/infrastructure/config/AppSettings.cpp
    src/infrastructure/security/WindowsCredentialStore.h
    src/infrastructure/logging/Logger.h
    src/infrastructure/logging/Logger.cpp
    src/infrastructure/monitoring/PerformanceTelemetry.h
    src/infrastructure/monitoring/PerformanceTelemetry.cpp
    src/infrastructure/db/DatabaseManager.h
    src/infrastructure/db/DatabaseManager.cpp
    src/infrastructure/db/DatabaseMigration.h
    src/infrastructure/db/DatabaseMigration.cpp
    src/infrastructure/db/GlobalDatabaseManager.h
    src/infrastructure/db/GlobalDatabaseManager.cpp
    src/core/search/SearchEngine.h
    src/core/search/SearchEngine.cpp
    src/core/search/CaptureTimeResolver.h
    src/core/search/CaptureTimeResolver.cpp
    src/core/search/NaturalLanguageQueryParser.h
    src/core/search/NaturalLanguageQueryParser.cpp
    src/core/search/SearchReliabilityEvaluator.h
    src/core/search/SearchReliabilityEvaluator.cpp
    src/core/search/SearchAssistantRouter.h
    src/core/search/SearchAssistantRouter.cpp
    src/core/search/SearchResultFusion.h
    src/core/search/SearchResultFusion.cpp
    src/core/search/SearchQueryUnderstanding.h
    src/core/search/SearchQueryUnderstanding.cpp
    src/core/scan/FileTypeService.h
    src/core/scan/FileTypeService.cpp
    src/core/scan/ScanPathPolicy.h
    src/core/scan/ScanPathPolicy.cpp
    src/core/thumbnail/RawFormatRegistry.h
    src/core/thumbnail/RawFormatRegistry.cpp
    src/core/media/MediaProbeEngine.h
    src/core/media/MediaProbeEngine.cpp
    src/core/jobs/JobEngine.h
    src/core/jobs/JobEngine.cpp
    src/core/scan/ScanEngine.h
    src/core/scan/ScanEngine.cpp
    src/core/thumbnail/ThumbnailEngine.h
    src/core/thumbnail/ThumbnailEngine.cpp
    src/core/thumbnail/ContactSheetBuilder.h
    src/core/thumbnail/ContactSheetBuilder.cpp
    src/core/report/ReportModels.h
    src/core/report/ReportRenderEngine.h
    src/core/report/ReportRenderEngine.cpp
    src/application/ProjectService.h
    src/application/ProjectService.cpp
    src/application/JobService.h
    src/application/JobService.cpp
    src/application/MediaTaskService.h
    src/application/MediaTaskService.cpp
    src/application/MetadataExtractionService.h
    src/application/MetadataExtractionService.cpp
    src/application/MaterialCatalogSyncService.h
    src/application/MaterialCatalogSyncService.cpp
    src/application/MaterialCenterQueryService.h
    src/application/MaterialCenterQueryService.cpp
    src/application/MaterialCenterReadContext.h
    src/application/MaterialCenterReadContext.cpp
    src/application/websearch/WebSearchService.h
    src/application/websearch/WebSearchService.cpp
    src/application/ApplicationIdleMonitor.h
    src/application/ApplicationIdleMonitor.cpp
    src/application/SearchAssistantLifecycleController.h
    src/application/SearchAssistantLifecycleController.cpp
    src/application/VideoAnalysisService.h
    src/application/VideoAnalysisService.cpp
    src/application/ImportService.h
    src/application/ImportService.cpp
    src/application/StorageVolumeService.h
    src/application/StorageVolumeService.cpp
    src/application/SourceChangeMonitor.h
    src/application/SourceChangeMonitor.cpp
    src/application/SystemIdleMonitor.h
    src/application/SystemIdleMonitor.cpp
    src/application/IndexingWorkCoordinator.h
    src/application/IndexingWorkCoordinator.cpp
    src/application/BackgroundMaintenanceCoordinator.h
    src/application/BackgroundMaintenanceCoordinator.cpp
    src/application/LibraryQueryService.h
    src/application/LibraryQueryService.cpp
    src/application/FeedbackService.h
    src/application/FeedbackService.cpp
    src/application/ReportExportService.h
    src/application/ReportExportService.cpp
    src/application/UpdateService.h
    src/application/UpdateService.cpp
    src/application/UpdaterSession.h
    src/application/UpdaterSession.cpp
    src/infrastructure/ffmpeg/FFmpegAdapter.h
    src/infrastructure/ffmpeg/FFmpegAdapter.cpp
    src/infrastructure/metadata/ExifToolAdapter.h
    src/infrastructure/metadata/ExifToolAdapter.cpp
    src/infrastructure/raw/RawPreviewProtocol.h
    src/infrastructure/raw/RawPreviewProtocol.cpp
    src/infrastructure/raw/RawWorkerClient.h
    src/infrastructure/raw/RawWorkerClient.cpp
    src/infrastructure/network/VisionApiClient.h
    src/infrastructure/network/VisionApiClient.cpp
    src/infrastructure/network/VisionResponseParser.h
    src/infrastructure/network/VisionResponseParser.cpp
    src/infrastructure/search/LocalSearchAssistantRuntime.h
    src/infrastructure/search/LocalSearchAssistantRuntime.cpp
    src/infrastructure/search/SearchAssistantClient.h
    src/infrastructure/search/SearchAssistantClient.cpp
    src/ui/viewmodels/ProjectLibraryViewModel.h
    src/ui/viewmodels/ProjectLibraryViewModel.cpp
    src/ui/viewmodels/ShellViewModel.h
    src/ui/viewmodels/ShellViewModel.cpp
    src/ui/viewmodels/SourceRailViewModel.h
    src/ui/viewmodels/SourceRailViewModel.cpp
    src/ui/viewmodels/LibraryWorkspaceViewModel.h
    src/ui/viewmodels/LibraryWorkspaceViewModel.cpp
    src/ui/viewmodels/MaterialCenterViewModel.h
    src/ui/viewmodels/MaterialCenterViewModel.cpp
    src/ui/viewmodels/InspectorViewModel.h
    src/ui/viewmodels/InspectorViewModel.cpp
    src/ui/viewmodels/JobTimelineViewModel.h
    src/ui/viewmodels/JobTimelineViewModel.cpp
    src/ui/viewmodels/ReportWorkspaceViewModel.h
    src/ui/viewmodels/ReportWorkspaceViewModel.cpp
    src/ui/viewmodels/SettingsViewModel.h
    src/ui/viewmodels/SettingsViewModel.cpp
    src/ui/window/QuickSearchController.h
    src/ui/window/QuickSearchController.cpp
    src/ui/widgets/UpdaterWindow.h
    src/ui/widgets/UpdaterWindow.cpp
    src/ui/viewmodels/FeedbackViewModel.h
    src/ui/viewmodels/FeedbackViewModel.cpp
    src/ui/models/MaterialCenterFolderListModel.h
    src/ui/models/MaterialCenterFolderListModel.cpp
    src/ui/models/MaterialCenterFrameListModel.h
    src/ui/models/MaterialCenterFrameListModel.cpp
    src/ui/models/MaterialCenterListModel.h
    src/ui/models/MaterialCenterListModel.cpp
    src/ui/models/FeedbackMessageListModel.h
    src/ui/models/FeedbackMessageListModel.cpp
    )
endif()

qt_add_executable(CineVault
    ${APP_SOURCES}
    ${APP_WINDOWS_RESOURCES}
)

qt_add_executable(CineVaultRawWorker
    src/raw-worker/main.cpp
    src/infrastructure/raw/RawPreviewProtocol.h
    src/infrastructure/raw/RawPreviewProtocol.cpp
    src/infrastructure/raw/RawPreviewCacheKey.h
    src/infrastructure/raw/RawPreviewCacheKey.cpp
    src/infrastructure/raw/RawPreviewDecoder.h
    src/infrastructure/raw/RawPreviewDecoder.cpp
)
target_include_directories(CineVaultRawWorker PRIVATE src)
target_link_libraries(CineVaultRawWorker PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(CineVault CineVaultRawWorker)

if(CINEVAULT_HAS_BUNDLED_LIBRAW)
    target_compile_definitions(CineVaultRawWorker PRIVATE CINEVAULT_HAS_LIBRAW=1)
    target_include_directories(CineVaultRawWorker PRIVATE
        "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/include")
    target_link_libraries(CineVaultRawWorker PRIVATE
        "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/lib/libraw.lib")
    add_custom_command(TARGET CineVaultRawWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/bin/libraw.dll"
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/libraw.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/licenses/libraw"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/licenses"
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/licenses/libraw"
        VERBATIM
    )
else()
    target_compile_definitions(CineVaultRawWorker PRIVATE CINEVAULT_HAS_LIBRAW=0)
endif()

if(CINEVAULT_HAS_BUNDLED_GPR)
    target_compile_definitions(CineVaultRawWorker PRIVATE CINEVAULT_HAS_GPR_TOOLS=1)
    add_custom_command(TARGET CineVaultRawWorker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/gpr"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CINEVAULT_GPR_RUNTIME_ROOT}/bin/gpr_tools.exe"
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/gpr/gpr_tools.exe"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/licenses/gpr-sdk"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CINEVAULT_GPR_RUNTIME_ROOT}/licenses"
                "$<TARGET_FILE_DIR:CineVaultRawWorker>/licenses/gpr-sdk"
        VERBATIM
    )
else()
    target_compile_definitions(CineVaultRawWorker PRIVATE CINEVAULT_HAS_GPR_TOOLS=0)
endif()

if(WIN32)
    target_link_libraries(CineVaultRawWorker PRIVATE Ole32 Windowscodecs)
endif()

if(WIN32)
    set_target_properties(CineVault PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(CineVault PRIVATE Dwmapi Gdi32 Ole32 Psapi Shell32)
endif()

qt_add_resources(CineVault "CineVault_assets"
    PREFIX "/"
    BASE resources
    FILES
        resources/icons/app.ico
        resources/websearch/index.html
        resources/websearch/styles.css
        resources/websearch/app.js
)

set_source_files_properties(src/ui/qml/themes/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(CineVault
    URI CineVault
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/themes/Theme.qml
        src/ui/qml/components/TopCommandBar.qml
        src/ui/qml/components/SourceRail.qml
        src/ui/qml/components/InspectorPane.qml
        src/ui/qml/components/JobProgressInspectorPane.qml
        src/ui/qml/components/JobTimelineBar.qml
        src/ui/qml/components/OnboardingWizard.qml
        src/ui/qml/components/ActionButton.qml
        src/ui/qml/components/AssetPreviewOverlay.qml
        src/ui/qml/components/ThemedTextField.qml
        src/ui/qml/components/ThemedSwitch.qml
        src/ui/qml/components/ThemedComboBox.qml
        src/ui/qml/components/ThemedSpinBox.qml
        src/ui/qml/components/ThemedProgressBar.qml
        src/ui/qml/components/ThemedSlider.qml
        src/ui/qml/components/ThemedScrollBar.qml
        src/ui/qml/components/MiddleDragScrollHandler.qml
        src/ui/qml/components/ThemedMenu.qml
        src/ui/qml/components/ThemedMenuItem.qml
        src/ui/qml/components/ThemedMenuSeparator.qml
        src/ui/qml/components/SourceCard.qml
        src/ui/qml/components/StatusChip.qml
        src/ui/qml/components/AssetCard.qml
        src/ui/qml/components/VideoPreviewPlayer.qml
        src/ui/qml/components/SettingsPage.qml
        src/ui/qml/components/QuickSearchWindow.qml
        src/ui/qml/workspaces/ProjectLibraryWorkspace.qml
        src/ui/qml/workspaces/LibraryWorkspace.qml
        src/ui/qml/workspaces/MaterialCenterWorkspace.qml
        src/ui/qml/workspaces/ReportWorkspace.qml
        src/ui/qml/workspaces/JobsWorkspace.qml
        src/ui/qml/workspaces/FeedbackWorkspace.qml
)

target_include_directories(CineVault PRIVATE src)

if(QT_ROOT)
    set(CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR "${QT_ROOT}/include/QtCore/${Qt6Core_VERSION}")
    if(EXISTS "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}/QtCore/private/qzipreader_p.h")
        target_include_directories(CineVault PRIVATE "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}")
    endif()
endif()

target_compile_definitions(CineVault PRIVATE
    CINEVAULT_BUILD_MINIMAL_GUI=$<IF:$<BOOL:${CINEVAULT_BUILD_MINIMAL_GUI}>,1,0>
    CINEVAULT_HAS_FFMPEG=$<IF:$<BOOL:${CINEVAULT_HAS_FFMPEG}>,1,0>
    CINEVAULT_HAS_BUNDLED_WEBP=$<IF:$<BOOL:${CINEVAULT_HAS_BUNDLED_WEBP}>,1,0>
    CINEVAULT_HAS_LOCAL_SEARCH=$<IF:$<BOOL:${CINEVAULT_HAS_LOCAL_SEARCH}>,1,0>
    CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT=$<IF:$<BOOL:${CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT}>,1,0>
    CINEVAULT_HAS_BUNDLED_EXIFTOOL=$<IF:$<BOOL:${CINEVAULT_HAS_BUNDLED_EXIFTOOL}>,1,0>
    CINEVAULT_HAS_BUNDLED_LIBRAW=$<IF:$<BOOL:${CINEVAULT_HAS_BUNDLED_LIBRAW}>,1,0>
    CINEVAULT_APP_VERSION="${CINEVAULT_APP_VERSION}"
    CINEVAULT_UPDATE_SIGNER_SHA256="${CINEVAULT_UPDATE_SIGNER_SHA256}"
)

target_link_libraries(CineVault PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Pdf
    Qt6::PdfQuick
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    Qt6::Widgets
)

if(CINEVAULT_HAS_BUNDLED_WEBP)
    target_link_libraries(CineVault PRIVATE webpdecoder)
endif()

if(CINEVAULT_HAS_BUNDLED_EXIFTOOL)
    add_custom_target(CineVaultStageExifTool
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:CineVault>/exiftool"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CINEVAULT_EXIFTOOL_RUNTIME_ROOT}"
                "$<TARGET_FILE_DIR:CineVault>/exiftool"
        VERBATIM
    )
    add_dependencies(CineVault CineVaultStageExifTool)
endif()

if(CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT)
    set(CINEVAULT_INSTALLED_ASSISTANT_RUNTIME_ROOT
        "$<TARGET_FILE_DIR:CineVault>/search-assistant")
    add_custom_command(TARGET CineVault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CINEVAULT_INSTALLED_ASSISTANT_RUNTIME_ROOT}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CINEVAULT_SEARCH_ASSISTANT_RUNTIME_ROOT}"
                "${CINEVAULT_INSTALLED_ASSISTANT_RUNTIME_ROOT}"
        VERBATIM
    )
    if(CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL)
        set(CINEVAULT_INSTALLED_ASSISTANT_MODEL_ROOT
            "$<TARGET_FILE_DIR:CineVault>/data/models/qwen3-0.6b")
        add_custom_command(TARGET CineVault POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CINEVAULT_INSTALLED_ASSISTANT_MODEL_ROOT}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CINEVAULT_SEARCH_ASSISTANT_MODEL_ROOT}/${CINEVAULT_SEARCH_ASSISTANT_MODEL_FILE}"
                    "${CINEVAULT_INSTALLED_ASSISTANT_MODEL_ROOT}/${CINEVAULT_SEARCH_ASSISTANT_MODEL_FILE}"
            VERBATIM
        )
    endif()
endif()

if(CINEVAULT_HAS_BUNDLED_EXIFTOOL)
    install(DIRECTORY "${CINEVAULT_EXIFTOOL_RUNTIME_ROOT}/"
            DESTINATION "exiftool")
endif()

if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
    target_link_libraries(CineVault PRIVATE
        CineVaultLocalSearch
        Qt6::Sql
        Qt6::Concurrent
        Qt6::WebSockets
    )
endif()

if(CINEVAULT_HAS_LOCAL_SEARCH)
    set(CINEVAULT_INSTALLED_MODEL_ROOT
        "$<TARGET_FILE_DIR:CineVault>/data/models/bge-small-zh-v1.5")
    add_custom_command(TARGET CineVault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:CineVaultOnnxRuntime>"
                "$<TARGET_FILE_DIR:CineVault>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CINEVAULT_INSTALLED_MODEL_ROOT}/onnx"
        VERBATIM
    )
    foreach(_model_file IN LISTS CINEVAULT_LOCAL_SEARCH_MODEL_FILES)
        cmake_path(GET _model_file PARENT_PATH _model_parent)
        if(_model_parent)
            add_custom_command(TARGET CineVault POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CINEVAULT_INSTALLED_MODEL_ROOT}/${_model_parent}"
                VERBATIM
            )
        endif()
        add_custom_command(TARGET CineVault POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CINEVAULT_LOCAL_SEARCH_MODEL_ROOT}/${_model_file}"
                    "${CINEVAULT_INSTALLED_MODEL_ROOT}/${_model_file}"
            VERBATIM
        )
    endforeach()
endif()

if(CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT)
    install(DIRECTORY "${CINEVAULT_SEARCH_ASSISTANT_RUNTIME_ROOT}/"
            DESTINATION "search-assistant")
    if(CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL)
        install(FILES
                "${CINEVAULT_SEARCH_ASSISTANT_MODEL_ROOT}/${CINEVAULT_SEARCH_ASSISTANT_MODEL_FILE}"
                DESTINATION "data/models/qwen3-0.6b")
    endif()
endif()

if(CINEVAULT_HAS_FFMPEG)
    target_link_libraries(CineVault PRIVATE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(CineVaultSmokeTest tests/unit/SmokeTest.cpp)
    target_include_directories(CineVaultSmokeTest PRIVATE src)
    target_link_libraries(CineVaultSmokeTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME CineVaultSmokeTest COMMAND CineVaultSmokeTest)

    qt_add_executable(SingleInstanceGuardTest
        tests/unit/SingleInstanceGuardTest.cpp
        src/app/SingleInstanceGuard.cpp
    )
    target_include_directories(SingleInstanceGuardTest PRIVATE src)
    target_link_libraries(SingleInstanceGuardTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME SingleInstanceGuardTest COMMAND SingleInstanceGuardTest)

    qt_add_executable(ApplicationIdleMonitorTest
        tests/unit/ApplicationIdleMonitorTest.cpp
        src/application/ApplicationIdleMonitor.cpp
    )
    target_include_directories(ApplicationIdleMonitorTest PRIVATE src)
    target_link_libraries(ApplicationIdleMonitorTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME ApplicationIdleMonitorTest COMMAND ApplicationIdleMonitorTest)

    qt_add_executable(BackgroundMonitoringTest
        tests/unit/BackgroundMonitoringTest.cpp
        src/application/SourceChangeMonitor.cpp
        src/application/SystemIdleMonitor.cpp
        src/core/scan/ScanPathPolicy.cpp
        src/shared/Paths.cpp
    )
    target_include_directories(BackgroundMonitoringTest PRIVATE src)
    target_compile_definitions(BackgroundMonitoringTest PRIVATE CINEVAULT_TESTING=1)
    target_link_libraries(BackgroundMonitoringTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BackgroundMonitoringTest
        COMMAND BackgroundMonitoringTest -v2 -o BackgroundMonitoringTest-results.txt,txt)

    qt_add_executable(IndexingWorkCoordinatorTest
        tests/unit/IndexingWorkCoordinatorTest.cpp
        src/application/IndexingWorkCoordinator.cpp
    )
    target_include_directories(IndexingWorkCoordinatorTest PRIVATE src)
    target_link_libraries(IndexingWorkCoordinatorTest PRIVATE
        Qt6::Concurrent Qt6::Core Qt6::Test)
    add_test(NAME IndexingWorkCoordinatorTest COMMAND IndexingWorkCoordinatorTest)

    if(CINEVAULT_HAS_BUNDLED_EXIFTOOL)
        qt_add_executable(ExifToolAdapterTest
            tests/unit/ExifToolAdapterTest.cpp
            src/infrastructure/metadata/ExifToolAdapter.cpp
            src/shared/FolderPathMetadata.cpp
        )
        target_include_directories(ExifToolAdapterTest PRIVATE src)
        target_link_libraries(ExifToolAdapterTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
        add_dependencies(ExifToolAdapterTest CineVaultStageExifTool)
        add_test(NAME ExifToolAdapterTest COMMAND ExifToolAdapterTest)

        qt_add_executable(MetadataExtractionServiceTest
            tests/unit/MetadataExtractionServiceTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/application/MetadataExtractionService.cpp
            src/core/jobs/JobEngine.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/logging/Logger.cpp
            src/infrastructure/metadata/ExifToolAdapter.cpp
            src/infrastructure/monitoring/PerformanceTelemetry.cpp
            src/shared/FolderPathMetadata.cpp
        )
        target_include_directories(MetadataExtractionServiceTest PRIVATE src)
        target_link_libraries(MetadataExtractionServiceTest PRIVATE
            Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
        if(WIN32)
            target_link_libraries(MetadataExtractionServiceTest PRIVATE Psapi)
        endif()
        add_dependencies(MetadataExtractionServiceTest CineVaultStageExifTool)
        add_test(NAME MetadataExtractionServiceTest COMMAND MetadataExtractionServiceTest)
    endif()

    qt_add_executable(SearchConfigurationTest tests/unit/SearchConfigurationTest.cpp)
    target_include_directories(SearchConfigurationTest PRIVATE src)
    target_compile_definitions(SearchConfigurationTest PRIVATE
        CINEVAULT_LOCAL_SEARCH_DEPENDENCY_LOCK="${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-search-dependencies.lock.json"
    )
    target_link_libraries(SearchConfigurationTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SearchConfigurationTest COMMAND SearchConfigurationTest)

    qt_add_executable(LoggerTest
        tests/unit/LoggerTest.cpp
        src/infrastructure/logging/Logger.cpp
    )
    target_include_directories(LoggerTest PRIVATE src)
    target_link_libraries(LoggerTest PRIVATE Qt6::Concurrent Qt6::Core Qt6::Test)
    add_test(NAME LoggerTest COMMAND LoggerTest)

    qt_add_executable(PerformanceTelemetryTest
        tests/unit/PerformanceTelemetryTest.cpp
        src/infrastructure/logging/Logger.cpp
        src/infrastructure/monitoring/PerformanceTelemetry.cpp
    )
    target_include_directories(PerformanceTelemetryTest PRIVATE src)
    target_link_libraries(PerformanceTelemetryTest PRIVATE Qt6::Core Qt6::Test)
    if(WIN32)
        target_link_libraries(PerformanceTelemetryTest PRIVATE Psapi)
    endif()
    add_test(NAME PerformanceTelemetryTest COMMAND PerformanceTelemetryTest)

    qt_add_executable(RawFormatRegistryTest
        tests/unit/RawFormatRegistryTest.cpp
        src/core/scan/FileTypeService.cpp
        src/core/thumbnail/RawFormatRegistry.cpp
    )
    target_include_directories(RawFormatRegistryTest PRIVATE src)
    target_link_libraries(RawFormatRegistryTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME RawFormatRegistryTest COMMAND RawFormatRegistryTest)

    add_test(NAME RawDependencyLockTest
        COMMAND ${CMAKE_COMMAND}
            -DVALIDATE_LOCK_ONLY=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrepareRawPreviewDependencies.cmake")

    qt_add_executable(RawWorkerClientTest
        tests/unit/RawWorkerClientTest.cpp
        src/infrastructure/raw/RawPreviewCacheKey.cpp
        src/infrastructure/raw/RawPreviewProtocol.cpp
        src/infrastructure/raw/RawWorkerClient.cpp
    )
    target_include_directories(RawWorkerClientTest PRIVATE src)
    target_compile_definitions(RawWorkerClientTest PRIVATE
        CINEVAULT_RAW_WORKER_TEST_PATH="$<TARGET_FILE:CineVaultRawWorker>")
    target_link_libraries(RawWorkerClientTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_dependencies(RawWorkerClientTest CineVaultRawWorker)
    add_test(NAME RawWorkerClientTest COMMAND RawWorkerClientTest)

    qt_add_executable(AppSettingsSearchTest
        tests/unit/AppSettingsSearchTest.cpp
        src/infrastructure/config/AppSettings.cpp
    )
    target_include_directories(AppSettingsSearchTest PRIVATE src)
    target_link_libraries(AppSettingsSearchTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME AppSettingsSearchTest COMMAND AppSettingsSearchTest)

    qt_add_executable(QuickSearchControllerTest
        tests/unit/QuickSearchControllerTest.cpp
        src/ui/window/QuickSearchController.cpp
        src/infrastructure/config/AppSettings.cpp
    )
    target_include_directories(QuickSearchControllerTest PRIVATE src)
    target_link_libraries(QuickSearchControllerTest PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
    )
    add_test(NAME QuickSearchControllerTest COMMAND QuickSearchControllerTest)

    qt_add_executable(WebSearchServiceTest
        tests/unit/WebSearchServiceTest.cpp
        src/application/websearch/WebSearchService.cpp
        src/application/MaterialCenterQueryService.cpp
        src/application/MaterialCenterReadContext.cpp
        src/core/search/SearchEngine.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
        src/core/search/SearchReliabilityEvaluator.cpp
        src/core/search/SearchQueryUnderstanding.cpp
        src/infrastructure/db/GlobalDatabaseManager.cpp
        src/infrastructure/db/DatabaseMigration.cpp
        src/shared/Formatters.cpp
        src/shared/Paths.cpp
        src/shared/VisualAnalysisMetadata.cpp
    )
    target_include_directories(WebSearchServiceTest PRIVATE src)
    target_link_libraries(WebSearchServiceTest PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
    qt_add_resources(WebSearchServiceTest "WebSearchServiceTest_assets"
        PREFIX "/"
        BASE resources
        FILES
            resources/websearch/index.html
            resources/websearch/styles.css
            resources/websearch/app.js
    )
    add_test(NAME WebSearchServiceTest COMMAND WebSearchServiceTest)

    qt_add_executable(VisualAnalysisMetadataTest
        tests/unit/VisualAnalysisMetadataTest.cpp
        src/shared/VisualAnalysisMetadata.cpp
    )
    target_include_directories(VisualAnalysisMetadataTest PRIVATE src)
    target_link_libraries(VisualAnalysisMetadataTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME VisualAnalysisMetadataTest COMMAND VisualAnalysisMetadataTest)

    qt_add_executable(FormattersTest
        tests/unit/FormattersTest.cpp
        src/shared/Formatters.cpp
    )
    target_include_directories(FormattersTest PRIVATE src)
    target_link_libraries(FormattersTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME FormattersTest COMMAND FormattersTest)

    qt_add_executable(FFmpegAdapterFrameExtractionTest
        tests/unit/FFmpegAdapterFrameExtractionTest.cpp
        src/infrastructure/ffmpeg/FFmpegAdapter.cpp
        src/shared/VisualAnalysisMetadata.cpp
    )
    target_include_directories(FFmpegAdapterFrameExtractionTest PRIVATE src)
    target_link_libraries(FFmpegAdapterFrameExtractionTest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME FFmpegAdapterFrameExtractionTest COMMAND FFmpegAdapterFrameExtractionTest)

    qt_add_executable(NaturalLanguageQueryParserTest
        tests/unit/NaturalLanguageQueryParserTest.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
    )
    target_include_directories(NaturalLanguageQueryParserTest PRIVATE src)
    target_link_libraries(NaturalLanguageQueryParserTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME NaturalLanguageQueryParserTest COMMAND NaturalLanguageQueryParserTest)

    qt_add_executable(SearchReliabilityEvaluatorTest
        tests/unit/SearchReliabilityEvaluatorTest.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
        src/core/search/SearchReliabilityEvaluator.cpp
    )
    target_include_directories(SearchReliabilityEvaluatorTest PRIVATE src)
    target_link_libraries(SearchReliabilityEvaluatorTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SearchReliabilityEvaluatorTest COMMAND SearchReliabilityEvaluatorTest)

    qt_add_executable(SearchAssistantRouterTest
        tests/unit/SearchAssistantRouterTest.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
        src/core/search/SearchReliabilityEvaluator.cpp
        src/core/search/SearchAssistantRouter.cpp
    )
    target_include_directories(SearchAssistantRouterTest PRIVATE src)
    target_link_libraries(SearchAssistantRouterTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SearchAssistantRouterTest COMMAND SearchAssistantRouterTest)

    qt_add_executable(SearchAssistantClientRecoveryTest
        tests/unit/SearchAssistantClientRecoveryTest.cpp
        src/infrastructure/search/SearchAssistantClient.cpp
        src/infrastructure/network/VisionResponseParser.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
        src/core/search/SearchQueryUnderstanding.cpp
    )
    target_include_directories(SearchAssistantClientRecoveryTest PRIVATE src)
    target_link_libraries(SearchAssistantClientRecoveryTest PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME SearchAssistantClientRecoveryTest COMMAND SearchAssistantClientRecoveryTest)

    qt_add_executable(LocalSearchAssistantRuntimeAsyncTest
        tests/unit/LocalSearchAssistantRuntimeAsyncTest.cpp
        src/infrastructure/search/LocalSearchAssistantRuntime.cpp
    )
    target_include_directories(LocalSearchAssistantRuntimeAsyncTest PRIVATE src)
    target_link_libraries(LocalSearchAssistantRuntimeAsyncTest PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME LocalSearchAssistantRuntimeAsyncTest COMMAND LocalSearchAssistantRuntimeAsyncTest)

    qt_add_executable(SearchResultFusionTest
        tests/unit/SearchResultFusionTest.cpp
        src/core/search/SearchReliabilityEvaluator.cpp
        src/core/search/SearchResultFusion.cpp
    )
    target_include_directories(SearchResultFusionTest PRIVATE src)
    target_link_libraries(SearchResultFusionTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SearchResultFusionTest COMMAND SearchResultFusionTest)

    if(CINEVAULT_HAS_LOCAL_SEARCH_ASSISTANT
       AND CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL)
        qt_add_executable(SearchAssistantIntegrationTest
            tests/unit/SearchAssistantIntegrationTest.cpp
            src/infrastructure/search/LocalSearchAssistantRuntime.cpp
            src/infrastructure/search/SearchAssistantClient.cpp
            src/infrastructure/network/VisionResponseParser.cpp
            src/core/search/NaturalLanguageQueryParser.cpp
            src/core/search/SearchQueryUnderstanding.cpp
        )
        target_include_directories(SearchAssistantIntegrationTest PRIVATE src)
        target_link_libraries(SearchAssistantIntegrationTest PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
        )
        add_dependencies(SearchAssistantIntegrationTest CineVault)
        add_test(NAME SearchAssistantIntegrationTest COMMAND SearchAssistantIntegrationTest)
        set_tests_properties(SearchAssistantIntegrationTest PROPERTIES
            LABELS "gpu-integration"
        )
    endif()

    qt_add_executable(SearchQueryUnderstandingTest
        tests/unit/SearchQueryUnderstandingTest.cpp
        src/core/search/NaturalLanguageQueryParser.cpp
        src/core/search/SearchQueryUnderstanding.cpp
    )
    target_include_directories(SearchQueryUnderstandingTest PRIVATE src)
    target_link_libraries(SearchQueryUnderstandingTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SearchQueryUnderstandingTest COMMAND SearchQueryUnderstandingTest)

    qt_add_executable(CaptureTimeResolverTest
        tests/unit/CaptureTimeResolverTest.cpp
        src/core/search/CaptureTimeResolver.cpp
    )
    target_include_directories(CaptureTimeResolverTest PRIVATE src)
    target_link_libraries(CaptureTimeResolverTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME CaptureTimeResolverTest COMMAND CaptureTimeResolverTest)

    qt_add_executable(VisionApiClientJsonTest
        tests/unit/VisionApiClientJsonTest.cpp
        src/infrastructure/network/VisionResponseParser.cpp
    )
    target_include_directories(VisionApiClientJsonTest PRIVATE src)
    target_link_libraries(VisionApiClientJsonTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME VisionApiClientJsonTest COMMAND VisionApiClientJsonTest)

    qt_add_executable(UpdateServiceTest
        tests/unit/UpdateServiceTest.cpp
        src/application/UpdateService.cpp
        src/application/UpdaterSession.cpp
        src/infrastructure/config/AppSettings.cpp
        src/shared/Paths.cpp
    )
    target_include_directories(UpdateServiceTest PRIVATE src)
    target_compile_definitions(UpdateServiceTest PRIVATE
        CINEVAULT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CINEVAULT_UPDATE_SIGNER_SHA256="${CINEVAULT_UPDATE_SIGNER_SHA256}"
    )
    target_link_libraries(UpdateServiceTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME UpdateServiceTest
        COMMAND UpdateServiceTest -o UpdateServiceTest-results.txt,txt)

    qt_add_executable(LocalImageUrlHelperTest
        tests/unit/LocalImageUrlHelperTest.cpp
        src/ui/imaging/LocalImageUrlHelper.cpp
        src/shared/FolderPathMetadata.cpp
        src/shared/Paths.cpp
        src/shared/ThumbnailCacheQuota.cpp
    )
    target_include_directories(LocalImageUrlHelperTest PRIVATE src)
    target_link_libraries(LocalImageUrlHelperTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME LocalImageUrlHelperTest COMMAND LocalImageUrlHelperTest)

    qt_add_executable(ThumbnailCacheQuotaTest
        tests/unit/ThumbnailCacheQuotaTest.cpp
        src/shared/FolderPathMetadata.cpp
        src/shared/Paths.cpp
        src/shared/ThumbnailCacheQuota.cpp
    )
    target_include_directories(ThumbnailCacheQuotaTest PRIVATE src)
    target_link_libraries(ThumbnailCacheQuotaTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ThumbnailCacheQuotaTest
        COMMAND ThumbnailCacheQuotaTest -v2 -o ThumbnailCacheQuotaTest-results.txt,txt)

    qt_add_executable(JobListModelTest
        tests/unit/JobListModelTest.cpp
        src/ui/models/JobListModel.cpp
        src/shared/Formatters.cpp
    )
    target_include_directories(JobListModelTest PRIVATE src)
    target_link_libraries(JobListModelTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME JobListModelTest COMMAND JobListModelTest)

    qt_add_executable(JobEngineTest
        tests/unit/JobEngineTest.cpp
        src/application/IndexingWorkCoordinator.cpp
        src/core/jobs/JobEngine.cpp
        src/infrastructure/db/DatabaseManager.cpp
        src/infrastructure/db/DatabaseMigration.cpp
        src/shared/FolderPathMetadata.cpp
    )
    target_include_directories(JobEngineTest PRIVATE src)
    target_link_libraries(JobEngineTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME JobEngineTest COMMAND JobEngineTest)

    qt_add_executable(FolderPathMetadataTest
        tests/unit/FolderPathMetadataTest.cpp
        src/shared/FolderPathMetadata.cpp
    )
    target_include_directories(FolderPathMetadataTest PRIVATE src)
    target_link_libraries(FolderPathMetadataTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME FolderPathMetadataTest COMMAND FolderPathMetadataTest)

    qt_add_executable(FolderDatabaseMigrationTest
        tests/unit/FolderDatabaseMigrationTest.cpp
        src/infrastructure/db/DatabaseManager.cpp
        src/infrastructure/db/GlobalDatabaseManager.cpp
        src/infrastructure/db/DatabaseMigration.cpp
        src/shared/FolderPathMetadata.cpp
        src/shared/Paths.cpp
    )
    target_include_directories(FolderDatabaseMigrationTest PRIVATE src)
    target_link_libraries(FolderDatabaseMigrationTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME FolderDatabaseMigrationTest COMMAND FolderDatabaseMigrationTest)

    if(CINEVAULT_HAS_LOCAL_SEARCH)
        qt_add_executable(SemanticSearchIndexServiceTest
            tests/unit/SemanticSearchIndexServiceTest.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(SemanticSearchIndexServiceTest PRIVATE src)
        target_link_libraries(SemanticSearchIndexServiceTest PRIVATE
            CineVaultLocalSearch
            Qt6::Core
            Qt6::Sql
            Qt6::Test
        )
        add_custom_command(TARGET SemanticSearchIndexServiceTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:CineVaultOnnxRuntime>"
                    "$<TARGET_FILE_DIR:SemanticSearchIndexServiceTest>"
            VERBATIM
        )
        add_test(NAME SemanticSearchIndexServiceTest COMMAND SemanticSearchIndexServiceTest)

        qt_add_executable(SearchDocumentSyncServiceTest
            tests/unit/SearchDocumentSyncServiceTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/application/SearchDocumentSyncService.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/Formatters.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(SearchDocumentSyncServiceTest PRIVATE src)
        target_link_libraries(SearchDocumentSyncServiceTest PRIVATE
            CineVaultLocalSearch
            Qt6::Concurrent
            Qt6::Core
            Qt6::Sql
            Qt6::Test
        )
        add_custom_command(TARGET SearchDocumentSyncServiceTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:CineVaultOnnxRuntime>"
                    "$<TARGET_FILE_DIR:SearchDocumentSyncServiceTest>"
            VERBATIM
        )
        add_test(NAME SearchDocumentSyncServiceTest COMMAND SearchDocumentSyncServiceTest)
    endif()

    if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
        qt_add_executable(DocumentPreviewServiceTest
            tests/unit/DocumentPreviewServiceTest.cpp
            src/application/DocumentPreviewService.cpp
        )
        target_include_directories(DocumentPreviewServiceTest PRIVATE src)
        if(QT_ROOT AND EXISTS "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}/QtCore/private/qzipreader_p.h")
            target_include_directories(DocumentPreviewServiceTest PRIVATE "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}")
        endif()
        if(TARGET Qt6::CorePrivate)
            target_link_libraries(DocumentPreviewServiceTest PRIVATE Qt6::CorePrivate)
        endif()
        target_link_libraries(DocumentPreviewServiceTest PRIVATE Qt6::Core Qt6::Gui Qt6::Pdf Qt6::Test)
        add_test(NAME DocumentPreviewServiceTest COMMAND DocumentPreviewServiceTest)

        qt_add_executable(MaterialCenterQueryServiceTest
            tests/unit/MaterialCenterQueryServiceTest.cpp
            src/application/MaterialCenterQueryService.cpp
            src/core/search/SearchEngine.cpp
            src/core/search/NaturalLanguageQueryParser.cpp
            src/core/search/SearchReliabilityEvaluator.cpp
            src/core/search/SearchQueryUnderstanding.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/shared/Formatters.cpp
            src/shared/Paths.cpp
            src/shared/VisualAnalysisMetadata.cpp
        )
        target_include_directories(MaterialCenterQueryServiceTest PRIVATE src)
        target_link_libraries(MaterialCenterQueryServiceTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
        add_test(NAME MaterialCenterQueryServiceTest COMMAND MaterialCenterQueryServiceTest)

        qt_add_executable(SearchEngineTest
            tests/unit/SearchEngineTest.cpp
            src/core/search/SearchEngine.cpp
            src/core/search/NaturalLanguageQueryParser.cpp
            src/core/search/SearchQueryUnderstanding.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(SearchEngineTest PRIVATE src)
        target_link_libraries(SearchEngineTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
        add_test(NAME SearchEngineTest COMMAND SearchEngineTest)

        qt_add_executable(MaterialCatalogSyncServiceTest
            tests/unit/MaterialCatalogSyncServiceTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/application/MaterialCatalogSyncService.cpp
            src/core/search/CaptureTimeResolver.cpp
            src/application/ProjectService.cpp
            src/core/jobs/JobEngine.cpp
            src/infrastructure/config/AppSettings.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/logging/Logger.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/Formatters.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(MaterialCatalogSyncServiceTest PRIVATE src)
        target_compile_definitions(MaterialCatalogSyncServiceTest PRIVATE CINEVAULT_TESTING=1)
        target_link_libraries(MaterialCatalogSyncServiceTest PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
        add_test(NAME MaterialCatalogSyncServiceTest COMMAND MaterialCatalogSyncServiceTest)

        qt_add_executable(ImportServiceLegacyRescanTest
            tests/unit/ImportServiceLegacyRescanTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/application/ImportService.cpp
            src/application/JobService.cpp
            src/core/jobs/JobEngine.cpp
            src/core/scan/FileTypeService.cpp
            src/core/scan/ScanPathPolicy.cpp
            src/core/scan/ScanEngine.cpp
            src/core/thumbnail/RawFormatRegistry.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/logging/Logger.cpp
            src/infrastructure/monitoring/PerformanceTelemetry.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(ImportServiceLegacyRescanTest PRIVATE src)
        target_link_libraries(ImportServiceLegacyRescanTest PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
        if(WIN32)
            target_link_libraries(ImportServiceLegacyRescanTest PRIVATE Psapi)
        endif()
        add_test(NAME ImportServiceLegacyRescanTest
            COMMAND ImportServiceLegacyRescanTest -v2 -o ImportServiceLegacyRescanTest-results.txt,txt)

        qt_add_executable(LargeCatalogStressTest
            tests/stress/LargeCatalogStressTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/core/jobs/JobEngine.cpp
            src/core/scan/FileTypeService.cpp
            src/core/scan/ScanPathPolicy.cpp
            src/core/scan/ScanEngine.cpp
            src/core/thumbnail/RawFormatRegistry.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/logging/Logger.cpp
            src/infrastructure/monitoring/PerformanceTelemetry.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(LargeCatalogStressTest PRIVATE src)
        target_link_libraries(LargeCatalogStressTest PRIVATE
            Qt6::Concurrent Qt6::Core Qt6::Sql Qt6::Test)
        if(WIN32)
            target_link_libraries(LargeCatalogStressTest PRIVATE Psapi)
        endif()
        add_test(NAME LargeCatalogStressTest COMMAND LargeCatalogStressTest -v1)
        set_tests_properties(LargeCatalogStressTest PROPERTIES LABELS "stress")

        qt_add_executable(MediaTaskServiceRecoveryTest
            tests/unit/MediaTaskServiceRecoveryTest.cpp
            src/application/IndexingWorkCoordinator.cpp
            src/application/MediaTaskService.cpp
            src/core/jobs/JobEngine.cpp
            src/core/media/MediaProbeEngine.cpp
            src/core/thumbnail/RawFormatRegistry.cpp
            src/core/thumbnail/ThumbnailEngine.cpp
            src/infrastructure/config/AppSettings.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/ffmpeg/FFmpegAdapter.cpp
            src/infrastructure/logging/Logger.cpp
            src/infrastructure/monitoring/PerformanceTelemetry.cpp
            src/infrastructure/raw/RawPreviewProtocol.cpp
            src/infrastructure/raw/RawWorkerClient.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/VisualAnalysisMetadata.cpp
            src/shared/Paths.cpp
            src/shared/ThumbnailCacheQuota.cpp
        )
        target_include_directories(MediaTaskServiceRecoveryTest PRIVATE src)
        target_link_libraries(MediaTaskServiceRecoveryTest PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Concurrent Qt6::Test)
        if(WIN32)
            target_link_libraries(MediaTaskServiceRecoveryTest PRIVATE Psapi)
        endif()
        add_test(NAME MediaTaskServiceRecoveryTest
            COMMAND MediaTaskServiceRecoveryTest -o MediaTaskServiceRecoveryTest-results.txt,txt)

        qt_add_executable(LibraryWorkspacePaginationTest
            tests/unit/LibraryWorkspacePaginationTest.cpp
            src/application/LibraryQueryService.cpp
            src/ui/models/AssetListModel.cpp
            src/ui/viewmodels/LibraryWorkspaceViewModel.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/DatabaseMigration.cpp
            src/infrastructure/logging/Logger.cpp
            src/shared/FileRevealService.cpp
            src/shared/FolderPathMetadata.cpp
            src/shared/Formatters.cpp
        )
        target_include_directories(LibraryWorkspacePaginationTest PRIVATE src)
        target_link_libraries(LibraryWorkspacePaginationTest PRIVATE
            Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
        add_test(NAME LibraryWorkspacePaginationTest COMMAND LibraryWorkspacePaginationTest)

        qt_add_executable(FeedbackMessageListModelTest
            tests/unit/FeedbackMessageListModelTest.cpp
            src/ui/models/FeedbackMessageListModel.cpp
            src/shared/Formatters.cpp
        )
        target_include_directories(FeedbackMessageListModelTest PRIVATE src)
        target_link_libraries(FeedbackMessageListModelTest PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME FeedbackMessageListModelTest COMMAND FeedbackMessageListModelTest)

        qt_add_executable(MaterialCenterUiContractTest
            tests/unit/MaterialCenterUiContractTest.cpp
            src/ui/models/MaterialCenterFolderListModel.cpp
            src/ui/models/MaterialCenterFrameListModel.cpp
            src/ui/models/MaterialCenterListModel.cpp
            src/shared/Formatters.cpp
        )
        target_include_directories(MaterialCenterUiContractTest PRIVATE src)
        target_compile_definitions(MaterialCenterUiContractTest PRIVATE
            CINEVAULT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(MaterialCenterUiContractTest PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME MaterialCenterUiContractTest
            COMMAND MaterialCenterUiContractTest -v2 -o MaterialCenterUiContractTest-results.txt,txt)

        qt_add_executable(FeedbackAttachmentCacheTest
            tests/unit/FeedbackAttachmentCacheTest.cpp
            src/application/FeedbackService.cpp
            src/infrastructure/config/AppSettings.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(FeedbackAttachmentCacheTest PRIVATE src)
        target_link_libraries(FeedbackAttachmentCacheTest PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            Qt6::WebSockets
        )
        add_test(NAME FeedbackAttachmentCacheTest COMMAND FeedbackAttachmentCacheTest)
    endif()

    if(CINEVAULT_HAS_BUNDLED_WEBP)
        qt_add_executable(VisionApiClientImageTest
            tests/unit/VisionApiClientImageTest.cpp
            src/infrastructure/network/VisionApiClient.cpp
            src/infrastructure/network/VisionResponseParser.cpp
            src/core/search/SearchQueryUnderstanding.cpp
            src/infrastructure/logging/Logger.cpp
            src/shared/VisualAnalysisMetadata.cpp
        )
        target_include_directories(VisionApiClientImageTest PRIVATE src)
        target_compile_definitions(VisionApiClientImageTest PRIVATE
            CINEVAULT_HAS_BUNDLED_WEBP=1
        )
        target_link_libraries(VisionApiClientImageTest PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Test
            webpdecoder
        )
        add_test(NAME VisionApiClientImageTest COMMAND VisionApiClientImageTest)

        qt_add_executable(LocalImageProviderTest
            tests/unit/LocalImageProviderTest.cpp
            src/ui/imaging/LocalImageProvider.cpp
        )
        target_include_directories(LocalImageProviderTest PRIVATE src)
        target_compile_definitions(LocalImageProviderTest PRIVATE
            CINEVAULT_HAS_BUNDLED_WEBP=1
        )
        target_link_libraries(LocalImageProviderTest PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Test
            webpdecoder
        )
        add_test(NAME LocalImageProviderTest COMMAND LocalImageProviderTest)
    endif()

    set(CINEVAULT_TEST_QT_BIN_DIR "")
    if(TARGET Qt6::qmake)
        get_target_property(CINEVAULT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(CINEVAULT_TEST_QT_BIN_DIR "${CINEVAULT_QMAKE_EXECUTABLE}" DIRECTORY)
    elseif(QT_ROOT)
        set(CINEVAULT_TEST_QT_BIN_DIR "${QT_ROOT}/bin")
    endif()

    if(WIN32 AND CINEVAULT_TEST_QT_BIN_DIR)
        set(CINEVAULT_PATH_AWARE_TESTS
            CineVaultSmokeTest
            SingleInstanceGuardTest
            ApplicationIdleMonitorTest
            BackgroundMonitoringTest
            IndexingWorkCoordinatorTest
            SearchConfigurationTest
            LoggerTest
            PerformanceTelemetryTest
            RawFormatRegistryTest
            RawWorkerClientTest
            AppSettingsSearchTest
            QuickSearchControllerTest
            WebSearchServiceTest
            VisualAnalysisMetadataTest
            FormattersTest
            FFmpegAdapterFrameExtractionTest
            NaturalLanguageQueryParserTest
            SearchReliabilityEvaluatorTest
            SearchAssistantRouterTest
            SearchAssistantClientRecoveryTest
            LocalSearchAssistantRuntimeAsyncTest
            SearchResultFusionTest
            SearchQueryUnderstandingTest
            CaptureTimeResolverTest
            VisionApiClientJsonTest
            UpdateServiceTest
            LocalImageUrlHelperTest
            ThumbnailCacheQuotaTest
            JobListModelTest
            JobEngineTest
            FolderPathMetadataTest
            FolderDatabaseMigrationTest
        )
        if(TARGET ExifToolAdapterTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS ExifToolAdapterTest)
        endif()
        if(TARGET MetadataExtractionServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MetadataExtractionServiceTest)
        endif()
        if(TARGET SearchAssistantIntegrationTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS SearchAssistantIntegrationTest)
        endif()
        if(TARGET SemanticSearchIndexServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS SemanticSearchIndexServiceTest)
        endif()
        if(TARGET SearchDocumentSyncServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS SearchDocumentSyncServiceTest)
        endif()
        if(TARGET FeedbackMessageListModelTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS FeedbackMessageListModelTest)
        endif()
        if(TARGET MaterialCenterUiContractTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MaterialCenterUiContractTest)
        endif()
        if(TARGET DocumentPreviewServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS DocumentPreviewServiceTest)
        endif()
        if(TARGET MaterialCenterQueryServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MaterialCenterQueryServiceTest)
        endif()
        if(TARGET SearchEngineTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS SearchEngineTest)
        endif()
        if(TARGET MaterialCatalogSyncServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MaterialCatalogSyncServiceTest)
        endif()
        if(TARGET ImportServiceLegacyRescanTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS ImportServiceLegacyRescanTest)
        endif()
        if(TARGET LargeCatalogStressTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS LargeCatalogStressTest)
        endif()
        if(TARGET MediaTaskServiceRecoveryTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MediaTaskServiceRecoveryTest)
        endif()
        if(TARGET LibraryWorkspacePaginationTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS LibraryWorkspacePaginationTest)
        endif()
        if(TARGET FeedbackAttachmentCacheTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS FeedbackAttachmentCacheTest)
        endif()
        if(TARGET LocalImageProviderTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS LocalImageProviderTest)
        endif()
        if(TARGET VisionApiClientImageTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS VisionApiClientImageTest)
        endif()
        set_target_properties(${CINEVAULT_PATH_AWARE_TESTS} PROPERTIES
            WIN32_EXECUTABLE FALSE
        )
        set_tests_properties(${CINEVAULT_PATH_AWARE_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CINEVAULT_TEST_QT_BIN_DIR}"
        )
    endif()
endif()

install(TARGETS CineVault CineVaultRawWorker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

if(CINEVAULT_HAS_BUNDLED_LIBRAW)
    install(FILES "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/bin/libraw.dll" DESTINATION .)
    install(DIRECTORY "${CINEVAULT_LIBRAW_RUNTIME_ROOT}/licenses/"
            DESTINATION "licenses/libraw")
endif()

if(CINEVAULT_HAS_BUNDLED_GPR)
    install(PROGRAMS "${CINEVAULT_GPR_RUNTIME_ROOT}/bin/gpr_tools.exe"
            DESTINATION "gpr")
    install(DIRECTORY "${CINEVAULT_GPR_RUNTIME_ROOT}/licenses/"
            DESTINATION "licenses/gpr-sdk")
endif()

if(CINEVAULT_HAS_LOCAL_SEARCH)
    install(FILES "${CINEVAULT_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll" DESTINATION .)
    install(DIRECTORY "${CINEVAULT_LOCAL_SEARCH_MODEL_ROOT}/"
            DESTINATION "data/models/bge-small-zh-v1.5")
endif()
