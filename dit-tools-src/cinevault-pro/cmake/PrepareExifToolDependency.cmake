cmake_minimum_required(VERSION 3.24)

set(_project_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _project_root OUTPUT_VARIABLE _project_root)

if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    if(DEFINED ENV{CINEVAULT_EXIFTOOL_CACHE}
       AND NOT "$ENV{CINEVAULT_EXIFTOOL_CACHE}" STREQUAL "")
        set(OUTPUT_ROOT "$ENV{CINEVAULT_EXIFTOOL_CACHE}")
    else()
        message(FATAL_ERROR
            "OUTPUT_ROOT is required. Pass -DOUTPUT_ROOT=<absolute build cache path> "
            "or set CINEVAULT_EXIFTOOL_CACHE.")
    endif()
endif()

cmake_path(ABSOLUTE_PATH OUTPUT_ROOT NORMALIZE OUTPUT_VARIABLE _output_root)
cmake_path(IS_PREFIX _project_root "${_output_root}" NORMALIZE _output_is_in_source)
if(_output_is_in_source)
    message(FATAL_ERROR "The ExifTool cache must be outside the source tree: ${_output_root}")
endif()

set(_lock_file "${CMAKE_CURRENT_LIST_DIR}/exiftool-dependencies.lock.json")
file(READ "${_lock_file}" _lock_json)
file(SHA256 "${_lock_file}" _lock_sha256)
string(JSON _version GET "${_lock_json}" dependencies exifTool version)
string(JSON _url GET "${_lock_json}" artifacts 0 url)
string(JSON _relative_path GET "${_lock_json}" artifacts 0 installPath)
string(JSON _expected_size GET "${_lock_json}" artifacts 0 size)
string(JSON _expected_hash GET "${_lock_json}" artifacts 0 sha256)

set(_archive "${_output_root}/${_relative_path}")
cmake_path(GET _archive PARENT_PATH _archive_dir)
file(MAKE_DIRECTORY "${_archive_dir}")

set(_needs_download TRUE)
if(EXISTS "${_archive}")
    file(SIZE "${_archive}" _actual_size)
    file(SHA256 "${_archive}" _actual_hash)
    string(TOUPPER "${_actual_hash}" _actual_hash)
    if(_actual_size EQUAL _expected_size AND _actual_hash STREQUAL _expected_hash)
        set(_needs_download FALSE)
        message(STATUS "Verified cached ExifTool ${_version} archive")
    else()
        file(REMOVE "${_archive}")
    endif()
endif()

if(_needs_download)
    set(_temporary "${_archive}.part")
    file(REMOVE "${_temporary}")
    message(STATUS "Downloading ExifTool ${_version}")
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
        message(FATAL_ERROR "Failed to download ExifTool: ${_download_message}")
    endif()
    file(SIZE "${_temporary}" _downloaded_size)
    if(NOT _downloaded_size EQUAL _expected_size)
        file(REMOVE "${_temporary}")
        message(FATAL_ERROR
            "Downloaded ExifTool size mismatch: expected ${_expected_size}, got ${_downloaded_size}")
    endif()
    file(RENAME "${_temporary}" "${_archive}")
endif()

set(_runtime_root "${_output_root}/runtimes/exiftool-${_version}-win-x64")
if(NOT EXISTS "${_runtime_root}/exiftool.exe"
   OR NOT EXISTS "${_runtime_root}/exiftool_files/exiftool.pl")
    set(_extract_root "${_output_root}/.extract-exiftool")
    file(REMOVE_RECURSE "${_extract_root}")
    file(MAKE_DIRECTORY "${_extract_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract_root}")
    set(_package_root "${_extract_root}/exiftool-${_version}_64")
    if(NOT EXISTS "${_package_root}/exiftool(-k).exe"
       OR NOT EXISTS "${_package_root}/exiftool_files/exiftool.pl")
        file(REMOVE_RECURSE "${_extract_root}")
        message(FATAL_ERROR "The ExifTool archive has an unexpected layout")
    endif()
    file(RENAME "${_package_root}/exiftool(-k).exe" "${_package_root}/exiftool.exe")
    file(REMOVE_RECURSE "${_runtime_root}")
    file(MAKE_DIRECTORY "${_output_root}/runtimes")
    file(RENAME "${_package_root}" "${_runtime_root}")
    file(REMOVE_RECURSE "${_extract_root}")
endif()

file(WRITE "${_output_root}/exiftool-dependency.ready"
    "schema=1\n"
    "lock_sha256=${_lock_sha256}\n"
    "runtime_root=${_runtime_root}\n")
message(STATUS "ExifTool dependency cache is ready: ${_runtime_root}")
