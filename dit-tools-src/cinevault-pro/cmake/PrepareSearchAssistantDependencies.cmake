cmake_minimum_required(VERSION 3.24)

set(_project_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _project_root OUTPUT_VARIABLE _project_root)

if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    if(DEFINED ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}
       AND NOT "$ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}" STREQUAL "")
        set(OUTPUT_ROOT "$ENV{CINEVAULT_SEARCH_ASSISTANT_CACHE}")
    else()
        message(FATAL_ERROR
            "OUTPUT_ROOT is required. Pass -DOUTPUT_ROOT=<absolute build cache path> "
            "or set CINEVAULT_SEARCH_ASSISTANT_CACHE.")
    endif()
endif()

cmake_path(ABSOLUTE_PATH OUTPUT_ROOT NORMALIZE OUTPUT_VARIABLE _output_root)
cmake_path(IS_PREFIX _project_root "${_output_root}" NORMALIZE _output_is_in_source)
if(_output_is_in_source)
    message(FATAL_ERROR "The search-assistant cache must be outside the source tree: ${_output_root}")
endif()

set(_lock_file "${CMAKE_CURRENT_LIST_DIR}/search-assistant-dependencies.lock.json")
if(NOT DEFINED INCLUDE_MODEL)
    set(INCLUDE_MODEL OFF)
endif()
file(READ "${_lock_file}" _lock_json)
file(SHA256 "${_lock_file}" _lock_sha256)
string(JSON _runtime_version GET "${_lock_json}" dependencies runtime version)
string(JSON _model_file GET "${_lock_json}" dependencies model file)
string(JSON _artifact_count LENGTH "${_lock_json}" artifacts)
if(_artifact_count LESS 2)
    message(FATAL_ERROR "The search-assistant lock must define runtime and model artifacts")
endif()

math(EXPR _last_artifact "${_artifact_count} - 1")
foreach(_index RANGE 0 ${_last_artifact})
    string(JSON _id GET "${_lock_json}" artifacts ${_index} id)
    string(JSON _url GET "${_lock_json}" artifacts ${_index} url)
    string(JSON _relative_path GET "${_lock_json}" artifacts ${_index} installPath)
    string(JSON _expected_size GET "${_lock_json}" artifacts ${_index} size)
    string(JSON _expected_hash GET "${_lock_json}" artifacts ${_index} sha256)

    if(_id STREQUAL "qwen3-0.6b-q8-model" AND NOT INCLUDE_MODEL)
        message(STATUS "Skipping Qwen model; this build uses the model-free release policy")
        continue()
    endif()

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
            message(STATUS "Verified cached search-assistant artifact: ${_id}")
        else()
            message(STATUS "Cached search-assistant artifact failed verification: ${_id}")
            file(REMOVE "${_destination}")
        endif()
    endif()

    if(_needs_download)
        set(_temporary "${_destination}.part")
        file(REMOVE "${_temporary}")
        message(STATUS "Downloading search-assistant artifact: ${_id}")
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

    if(_id STREQUAL "llama-cpp-windows-x64-vulkan")
        set(_runtime_archive "${_destination}")
    elseif(_id STREQUAL "qwen3-0.6b-q8-model")
        set(_model_path "${_destination}")
    endif()
endforeach()

if(NOT DEFINED _runtime_archive)
    message(FATAL_ERROR "The search-assistant lock is missing the required runtime artifact")
endif()
if(INCLUDE_MODEL AND NOT DEFINED _model_path)
    message(FATAL_ERROR "The search-assistant lock is missing the requested model artifact")
endif()

set(_runtime_root "${_output_root}/runtimes/llama-${_runtime_version}-win-vulkan-x64")
if(NOT EXISTS "${_runtime_root}/llama-server.exe")
    set(_extract_root "${_output_root}/.extract-search-assistant-runtime")
    file(REMOVE_RECURSE "${_extract_root}")
    file(MAKE_DIRECTORY "${_extract_root}")
    message(STATUS "Extracting llama.cpp ${_runtime_version}")
    file(ARCHIVE_EXTRACT INPUT "${_runtime_archive}" DESTINATION "${_extract_root}")
    if(NOT EXISTS "${_extract_root}/llama-server.exe")
        file(REMOVE_RECURSE "${_extract_root}")
        message(FATAL_ERROR "The llama.cpp archive has an unexpected layout")
    endif()
    file(REMOVE_RECURSE "${_runtime_root}")
    file(MAKE_DIRECTORY "${_output_root}/runtimes")
    file(RENAME "${_extract_root}" "${_runtime_root}")
else()
    message(STATUS "Using extracted llama.cpp runtime: ${_runtime_root}")
endif()

if(INCLUDE_MODEL AND NOT EXISTS "${_model_path}")
    message(FATAL_ERROR "The prepared search-assistant model is missing: ${_model_path}")
endif()

file(WRITE "${_output_root}/search-assistant-dependencies.ready"
    "schema=1\n"
    "lock_sha256=${_lock_sha256}\n"
    "runtime_root=${_runtime_root}\n"
    "includes_model=${INCLUDE_MODEL}\n")
message(STATUS "Search-assistant dependency cache is ready: ${_output_root}")
