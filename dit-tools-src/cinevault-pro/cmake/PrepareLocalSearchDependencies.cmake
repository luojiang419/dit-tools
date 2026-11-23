cmake_minimum_required(VERSION 3.24)

set(_project_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _project_root OUTPUT_VARIABLE _project_root)

if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    if(DEFINED ENV{CINEVAULT_DEPENDENCY_CACHE} AND NOT "$ENV{CINEVAULT_DEPENDENCY_CACHE}" STREQUAL "")
        set(OUTPUT_ROOT "$ENV{CINEVAULT_DEPENDENCY_CACHE}")
    else()
        message(FATAL_ERROR
            "OUTPUT_ROOT is required. Pass -DOUTPUT_ROOT=<absolute build cache path> "
            "or set CINEVAULT_DEPENDENCY_CACHE.")
    endif()
endif()

cmake_path(ABSOLUTE_PATH OUTPUT_ROOT NORMALIZE OUTPUT_VARIABLE _output_root)
cmake_path(IS_PREFIX _project_root "${_output_root}" NORMALIZE _output_is_in_source)
if(_output_is_in_source)
    message(FATAL_ERROR "The dependency cache must be outside the source tree: ${_output_root}")
endif()

set(_lock_file "${CMAKE_CURRENT_LIST_DIR}/local-search-dependencies.lock.json")
file(READ "${_lock_file}" _lock_json)
file(SHA256 "${_lock_file}" _lock_sha256)
string(JSON _onnxruntime_version GET "${_lock_json}" dependencies onnxRuntime version)
string(JSON _usearch_commit GET "${_lock_json}" dependencies usearch commit)
string(JSON _artifact_count LENGTH "${_lock_json}" artifacts)
if(_artifact_count LESS 1)
    message(FATAL_ERROR "No artifacts are defined in ${_lock_file}")
endif()

math(EXPR _last_artifact "${_artifact_count} - 1")
foreach(_index RANGE 0 ${_last_artifact})
    string(JSON _id GET "${_lock_json}" artifacts ${_index} id)
    string(JSON _url GET "${_lock_json}" artifacts ${_index} url)
    string(JSON _relative_path GET "${_lock_json}" artifacts ${_index} installPath)
    string(JSON _expected_size GET "${_lock_json}" artifacts ${_index} size)
    string(JSON _expected_hash GET "${_lock_json}" artifacts ${_index} sha256)

    set(_destination "${_output_root}/${_relative_path}")
    cmake_path(GET _destination PARENT_PATH _destination_dir)
    file(MAKE_DIRECTORY "${_destination_dir}")

    set(_needs_download TRUE)
    if(EXISTS "${_destination}")
        file(SIZE "${_destination}" _actual_size)
        file(SHA256 "${_destination}" _actual_hash)
        string(TOUPPER "${_actual_hash}" _actual_hash)
        if(_actual_size EQUAL _expected_size AND _actual_hash STREQUAL _expected_hash)
            set(_needs_download FALSE)
            message(STATUS "Verified cached local-search artifact: ${_id}")
        else()
            message(STATUS "Cached artifact failed verification and will be replaced: ${_id}")
            file(REMOVE "${_destination}")
        endif()
    endif()

    if(_needs_download)
        set(_temporary "${_destination}.part")
        file(REMOVE "${_temporary}")
        message(STATUS "Downloading local-search artifact: ${_id}")
        file(DOWNLOAD
            "${_url}"
            "${_temporary}"
            EXPECTED_HASH "SHA256=${_expected_hash}"
            STATUS _download_status
            TLS_VERIFY ON
            SHOW_PROGRESS
        )
        list(GET _download_status 0 _download_code)
        list(GET _download_status 1 _download_message)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${_temporary}")
            message(FATAL_ERROR "Failed to download ${_id}: ${_download_message}")
        endif()
        file(SIZE "${_temporary}" _downloaded_size)
        if(NOT _downloaded_size EQUAL _expected_size)
            file(REMOVE "${_temporary}")
            message(FATAL_ERROR
                "Downloaded size mismatch for ${_id}: expected ${_expected_size}, got ${_downloaded_size}")
        endif()
        file(RENAME "${_temporary}" "${_destination}")
    endif()

    if(_id STREQUAL "onnxruntime-windows-x64-cpu")
        set(_onnxruntime_archive "${_destination}")
    elseif(_id STREQUAL "usearch-source")
        set(_usearch_archive "${_destination}")
    endif()
endforeach()

if(NOT DEFINED _onnxruntime_archive OR NOT DEFINED _usearch_archive)
    message(FATAL_ERROR "The lock file does not define the required native source archives")
endif()

set(_onnxruntime_package_name "onnxruntime-win-x64-${_onnxruntime_version}")
set(_onnxruntime_root "${_output_root}/packages/${_onnxruntime_package_name}")
set(_onnxruntime_required_files
    "include/onnxruntime_cxx_api.h"
    "lib/onnxruntime.lib"
    "lib/onnxruntime.dll"
)
set(_onnxruntime_complete TRUE)
foreach(_relative_path IN LISTS _onnxruntime_required_files)
    if(NOT EXISTS "${_onnxruntime_root}/${_relative_path}")
        set(_onnxruntime_complete FALSE)
    endif()
endforeach()
if(NOT _onnxruntime_complete)
    set(_extract_root "${_output_root}/.extract-onnxruntime")
    file(REMOVE_RECURSE "${_extract_root}")
    file(MAKE_DIRECTORY "${_extract_root}")
    message(STATUS "Extracting ONNX Runtime ${_onnxruntime_version}")
    file(ARCHIVE_EXTRACT INPUT "${_onnxruntime_archive}" DESTINATION "${_extract_root}")
    if(NOT EXISTS "${_extract_root}/${_onnxruntime_package_name}/include/onnxruntime_cxx_api.h")
        file(REMOVE_RECURSE "${_extract_root}")
        message(FATAL_ERROR "The ONNX Runtime archive has an unexpected layout")
    endif()
    file(REMOVE_RECURSE "${_onnxruntime_root}")
    file(MAKE_DIRECTORY "${_output_root}/packages")
    file(RENAME "${_extract_root}/${_onnxruntime_package_name}" "${_onnxruntime_root}")
    file(REMOVE_RECURSE "${_extract_root}")
else()
    message(STATUS "Using extracted ONNX Runtime package: ${_onnxruntime_root}")
endif()

set(_usearch_archive_root "USearch-${_usearch_commit}")
set(_usearch_root "${_output_root}/sources/${_usearch_archive_root}")
set(_usearch_required_files
    "c/lib.cpp"
    "c/usearch.h"
    "include/usearch/index.hpp"
    "include/usearch/index_dense.hpp"
)
set(_usearch_complete TRUE)
foreach(_relative_path IN LISTS _usearch_required_files)
    if(NOT EXISTS "${_usearch_root}/${_relative_path}")
        set(_usearch_complete FALSE)
    endif()
endforeach()
if(NOT _usearch_complete)
    set(_extract_root "${_output_root}/.extract-usearch")
    file(REMOVE_RECURSE "${_extract_root}")
    file(MAKE_DIRECTORY "${_extract_root}")
    message(STATUS "Extracting USearch ${_usearch_commit}")
    file(ARCHIVE_EXTRACT INPUT "${_usearch_archive}" DESTINATION "${_extract_root}")
    if(NOT EXISTS "${_extract_root}/${_usearch_archive_root}/c/usearch.h")
        file(REMOVE_RECURSE "${_extract_root}")
        message(FATAL_ERROR "The USearch archive has an unexpected layout")
    endif()
    file(REMOVE_RECURSE "${_usearch_root}")
    file(MAKE_DIRECTORY "${_output_root}/sources")
    file(RENAME "${_extract_root}/${_usearch_archive_root}" "${_usearch_root}")
    file(REMOVE_RECURSE "${_extract_root}")
else()
    message(STATUS "Using extracted USearch source: ${_usearch_root}")
endif()

file(WRITE "${_output_root}/local-search-dependencies.ready"
    "schema=2\n"
    "lock_sha256=${_lock_sha256}\n"
    "onnxruntime_root=${_onnxruntime_root}\n"
    "usearch_root=${_usearch_root}\n")
message(STATUS "Local-search dependency cache is ready: ${_output_root}")
