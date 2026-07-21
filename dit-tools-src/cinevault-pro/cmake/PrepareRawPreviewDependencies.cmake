cmake_minimum_required(VERSION 3.24)

set(_project_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _project_root OUTPUT_VARIABLE _project_root)
set(_lock_file "${CMAKE_CURRENT_LIST_DIR}/raw-preview-dependencies.lock.json")
file(READ "${_lock_file}" _lock_json)
file(SHA256 "${_lock_file}" _lock_sha256)

string(JSON _schema_version GET "${_lock_json}" schemaVersion)
string(JSON _profile_version GET "${_lock_json}" profileVersion)
string(JSON _default_provider GET "${_lock_json}" policy defaultProvider)
string(JSON _extended_sdk_policy GET "${_lock_json}" policy extendedSdkPolicy)
string(JSON _require_redistributable GET "${_lock_json}" policy requireRedistributable)
string(JSON _name GET "${_lock_json}" dependencies libRaw name)
string(JSON _version GET "${_lock_json}" dependencies libRaw version)
string(JSON _license_expression GET "${_lock_json}" dependencies libRaw licenseExpression)
string(JSON _selected_license GET "${_lock_json}" dependencies libRaw selectedLicense)
string(JSON _selected_license_file GET "${_lock_json}" dependencies libRaw selectedLicenseFile)
string(JSON _redistributable GET "${_lock_json}" dependencies libRaw redistributable)
string(JSON _default_bundle GET "${_lock_json}" dependencies libRaw defaultBundle)
string(JSON _gpr_name GET "${_lock_json}" extendedSdks 0 name)
string(JSON _gpr_version GET "${_lock_json}" extendedSdks 0 version)
string(JSON _gpr_revision GET "${_lock_json}" extendedSdks 0 sourceRevision)
string(JSON _gpr_license_expression GET "${_lock_json}" extendedSdks 0 licenseExpression)
string(JSON _gpr_selected_license GET "${_lock_json}" extendedSdks 0 selectedLicense)
string(JSON _gpr_selected_license_file GET "${_lock_json}" extendedSdks 0 selectedLicenseFile)
string(JSON _gpr_redistributable GET "${_lock_json}" extendedSdks 0 redistributable)
string(JSON _gpr_default_bundle GET "${_lock_json}" extendedSdks 0 defaultBundle)
string(JSON _gpr_license_approved GET "${_lock_json}" extendedSdks 0 licenseGateApproved)

if(NOT _schema_version EQUAL 1)
    message(FATAL_ERROR "Unsupported RAW dependency lock schema: ${_schema_version}")
endif()
if(NOT _profile_version STREQUAL "raw-preview-v1")
    message(FATAL_ERROR "Unexpected RAW preview profile: ${_profile_version}")
endif()
if(NOT _default_provider STREQUAL "libRaw")
    message(FATAL_ERROR "The default RAW provider must be the audited LibRaw package")
endif()
if(NOT _extended_sdk_policy STREQUAL "license-gated")
    message(FATAL_ERROR "Extended RAW SDKs must remain license-gated")
endif()
if(NOT _require_redistributable OR NOT _redistributable OR NOT _default_bundle)
    message(FATAL_ERROR "The default RAW dependency must be explicitly redistributable")
endif()
if(NOT _name STREQUAL "LibRaw" OR NOT _version STREQUAL "0.22.2")
    message(FATAL_ERROR "The audited default RAW dependency must remain LibRaw 0.22.2")
endif()
if(NOT _license_expression STREQUAL "CDDL-1.0 OR LGPL-2.1-only")
    message(FATAL_ERROR "Unexpected LibRaw license expression: ${_license_expression}")
endif()
if(NOT _selected_license STREQUAL "CDDL-1.0"
   OR NOT _selected_license_file STREQUAL "LICENSE.CDDL")
    message(FATAL_ERROR "The default LibRaw bundle must use the audited CDDL-1.0 license path")
endif()

string(JSON _extended_sdk_count LENGTH "${_lock_json}" extendedSdks)
if(NOT _extended_sdk_count EQUAL 1)
    message(FATAL_ERROR "The RAW dependency lock must contain exactly one approved extended SDK")
endif()
if(_extended_sdk_count GREATER 0)
    math(EXPR _extended_sdk_last "${_extended_sdk_count} - 1")
    foreach(_index RANGE 0 ${_extended_sdk_last})
        string(JSON _sdk_name GET "${_lock_json}" extendedSdks ${_index} name)
        string(JSON _sdk_default_bundle GET "${_lock_json}" extendedSdks ${_index} defaultBundle)
        string(JSON _sdk_redistributable GET "${_lock_json}" extendedSdks ${_index} redistributable)
        string(JSON _sdk_license_approved GET "${_lock_json}" extendedSdks ${_index} licenseGateApproved)
        if(_sdk_default_bundle AND (NOT _sdk_redistributable OR NOT _sdk_license_approved))
            message(FATAL_ERROR
                "Extended RAW SDK '${_sdk_name}' cannot be bundled before its license gate is approved")
        endif()
    endforeach()
endif()
if(NOT _gpr_name STREQUAL "GoPro GPR SDK"
   OR NOT _gpr_version STREQUAL "446c736"
   OR NOT _gpr_revision STREQUAL "446c736a38fb14f51343605c0780d347dc602f89")
    message(FATAL_ERROR "Unexpected GoPro GPR SDK source revision")
endif()
if(NOT _gpr_license_expression STREQUAL "MIT OR Apache-2.0"
   OR NOT _gpr_selected_license STREQUAL "MIT"
   OR NOT _gpr_selected_license_file STREQUAL "LICENSE-MIT"
   OR NOT _gpr_redistributable
   OR NOT _gpr_default_bundle
   OR NOT _gpr_license_approved)
    message(FATAL_ERROR "The GoPro GPR SDK license gate is incomplete")
endif()

if(DEFINED VALIDATE_LOCK_ONLY AND VALIDATE_LOCK_ONLY)
    message(STATUS
        "Validated RAW dependency lock: LibRaw ${_version}, ${_selected_license}, extended SDKs=${_extended_sdk_count}")
    return()
endif()

if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    if(DEFINED ENV{CINEVAULT_LIBRAW_CACHE}
       AND NOT "$ENV{CINEVAULT_LIBRAW_CACHE}" STREQUAL "")
        set(OUTPUT_ROOT "$ENV{CINEVAULT_LIBRAW_CACHE}")
    else()
        message(FATAL_ERROR
            "OUTPUT_ROOT is required. Pass -DOUTPUT_ROOT=<absolute build cache path> "
            "or set CINEVAULT_LIBRAW_CACHE.")
    endif()
endif()

cmake_path(ABSOLUTE_PATH OUTPUT_ROOT NORMALIZE OUTPUT_VARIABLE _output_root)
cmake_path(IS_PREFIX _project_root "${_output_root}" NORMALIZE _output_is_in_source)
if(_output_is_in_source)
    message(FATAL_ERROR "The LibRaw cache must be outside the source tree: ${_output_root}")
endif()

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
        message(STATUS "Verified cached LibRaw ${_version} archive")
    elseif(DEFINED OFFLINE AND OFFLINE)
        message(FATAL_ERROR "Cached LibRaw archive does not match the dependency lock in offline mode")
    else()
        file(REMOVE "${_archive}")
    endif()
endif()

if(_needs_download)
    if(DEFINED OFFLINE AND OFFLINE)
        message(FATAL_ERROR "LibRaw archive is not available in offline mode: ${_archive}")
    endif()
    set(_temporary "${_archive}.part")
    file(REMOVE "${_temporary}")
    message(STATUS "Downloading LibRaw ${_version}")
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
        message(FATAL_ERROR "Failed to download LibRaw: ${_download_message}")
    endif()
    file(SIZE "${_temporary}" _downloaded_size)
    if(NOT _downloaded_size EQUAL _expected_size)
        file(REMOVE "${_temporary}")
        message(FATAL_ERROR
            "Downloaded LibRaw size mismatch: expected ${_expected_size}, got ${_downloaded_size}")
    endif()
    file(RENAME "${_temporary}" "${_archive}")
endif()

set(_runtime_root "${_output_root}/runtimes/libraw-${_version}-win-x64")
set(_runtime_complete TRUE)
foreach(_required_file IN ITEMS
        "include/libraw/libraw.h"
        "lib/libraw.lib"
        "bin/libraw.dll"
        "licenses/LICENSE.CDDL"
        "licenses/LICENSE.LGPL"
        "licenses/COPYRIGHT")
    if(NOT EXISTS "${_runtime_root}/${_required_file}")
        set(_runtime_complete FALSE)
    endif()
endforeach()

if(NOT _runtime_complete)
    set(_extract_root "${_output_root}/.extract-libraw")
    set(_prepare_root "${_output_root}/.prepare-libraw-${_version}")
    file(REMOVE_RECURSE "${_extract_root}" "${_prepare_root}")
    file(MAKE_DIRECTORY "${_extract_root}" "${_prepare_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract_root}")
    set(_package_root "${_extract_root}/LibRaw-${_version}")
    foreach(_package_file IN ITEMS
            "libraw/libraw.h"
            "lib/libraw.lib"
            "bin/libraw.dll"
            "LICENSE.CDDL"
            "LICENSE.LGPL"
            "COPYRIGHT")
        if(NOT EXISTS "${_package_root}/${_package_file}")
            file(REMOVE_RECURSE "${_extract_root}" "${_prepare_root}")
            message(FATAL_ERROR "The LibRaw archive has an unexpected layout: ${_package_file}")
        endif()
    endforeach()

    file(MAKE_DIRECTORY
        "${_prepare_root}/include"
        "${_prepare_root}/lib"
        "${_prepare_root}/bin"
        "${_prepare_root}/licenses")
    file(COPY "${_package_root}/libraw" DESTINATION "${_prepare_root}/include")
    file(COPY "${_package_root}/lib/libraw.lib" DESTINATION "${_prepare_root}/lib")
    file(COPY "${_package_root}/bin/libraw.dll" DESTINATION "${_prepare_root}/bin")
    file(COPY
        "${_package_root}/LICENSE.CDDL"
        "${_package_root}/LICENSE.LGPL"
        "${_package_root}/COPYRIGHT"
        DESTINATION "${_prepare_root}/licenses")
    file(REMOVE_RECURSE "${_runtime_root}")
    file(MAKE_DIRECTORY "${_output_root}/runtimes")
    file(RENAME "${_prepare_root}" "${_runtime_root}")
    file(REMOVE_RECURSE "${_extract_root}")
endif()

string(JSON _gpr_url GET "${_lock_json}" artifacts 1 url)
string(JSON _gpr_relative_path GET "${_lock_json}" artifacts 1 installPath)
string(JSON _gpr_expected_size GET "${_lock_json}" artifacts 1 size)
string(JSON _gpr_expected_hash GET "${_lock_json}" artifacts 1 sha256)
set(_gpr_archive "${_output_root}/${_gpr_relative_path}")
cmake_path(GET _gpr_archive PARENT_PATH _gpr_archive_dir)
file(MAKE_DIRECTORY "${_gpr_archive_dir}")

set(_gpr_needs_download TRUE)
if(EXISTS "${_gpr_archive}")
    file(SIZE "${_gpr_archive}" _gpr_actual_size)
    file(SHA256 "${_gpr_archive}" _gpr_actual_hash)
    string(TOUPPER "${_gpr_actual_hash}" _gpr_actual_hash)
    if(_gpr_actual_size EQUAL _gpr_expected_size
       AND _gpr_actual_hash STREQUAL _gpr_expected_hash)
        set(_gpr_needs_download FALSE)
        message(STATUS "Verified cached GoPro GPR SDK ${_gpr_version} archive")
    elseif(DEFINED OFFLINE AND OFFLINE)
        message(FATAL_ERROR "Cached GoPro GPR SDK archive does not match the dependency lock in offline mode")
    else()
        file(REMOVE "${_gpr_archive}")
    endif()
endif()

if(_gpr_needs_download)
    if(DEFINED OFFLINE AND OFFLINE)
        message(FATAL_ERROR "The GoPro GPR SDK archive is not available in offline mode: ${_gpr_archive}")
    endif()
    set(_gpr_temporary "${_gpr_archive}.part")
    file(REMOVE "${_gpr_temporary}")
    message(STATUS "Downloading GoPro GPR SDK ${_gpr_version}")
    file(DOWNLOAD
        "${_gpr_url}"
        "${_gpr_temporary}"
        EXPECTED_HASH "SHA256=${_gpr_expected_hash}"
        STATUS _gpr_download_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _gpr_download_status 0 _gpr_download_code)
    list(GET _gpr_download_status 1 _gpr_download_message)
    if(NOT _gpr_download_code EQUAL 0)
        file(REMOVE "${_gpr_temporary}")
        message(FATAL_ERROR "Failed to download GoPro GPR SDK: ${_gpr_download_message}")
    endif()
    file(SIZE "${_gpr_temporary}" _gpr_downloaded_size)
    if(NOT _gpr_downloaded_size EQUAL _gpr_expected_size)
        file(REMOVE "${_gpr_temporary}")
        message(FATAL_ERROR
            "Downloaded GoPro GPR SDK size mismatch: expected ${_gpr_expected_size}, got ${_gpr_downloaded_size}")
    endif()
    file(RENAME "${_gpr_temporary}" "${_gpr_archive}")
endif()

set(_gpr_runtime_root "${_output_root}/runtimes/gpr-sdk-${_gpr_version}-win-x64")
set(_gpr_runtime_complete TRUE)
foreach(_gpr_required_file IN ITEMS
        "bin/gpr_tools.exe"
        "licenses/LICENSE-MIT"
        "licenses/LICENSE-APACHE"
        "licenses/LICENSE.txt"
        "licenses/AUTHORS.md")
    if(NOT EXISTS "${_gpr_runtime_root}/${_gpr_required_file}")
        set(_gpr_runtime_complete FALSE)
    endif()
endforeach()

if(NOT _gpr_runtime_complete)
    set(_gpr_extract_root "${_output_root}/.extract-gpr")
    set(_gpr_build_root "${_output_root}/.build-gpr-${_gpr_version}")
    set(_gpr_prepare_root "${_output_root}/.prepare-gpr-${_gpr_version}")
    file(REMOVE_RECURSE
        "${_gpr_extract_root}"
        "${_gpr_build_root}"
        "${_gpr_prepare_root}")
    file(MAKE_DIRECTORY
        "${_gpr_extract_root}"
        "${_gpr_build_root}"
        "${_gpr_prepare_root}/bin"
        "${_gpr_prepare_root}/licenses")
    file(ARCHIVE_EXTRACT INPUT "${_gpr_archive}" DESTINATION "${_gpr_extract_root}")
    set(_gpr_source_root "${_gpr_extract_root}/gpr-${_gpr_revision}")
    foreach(_gpr_source_file IN ITEMS
            "CMakeLists.txt"
            "LICENSE-MIT"
            "LICENSE-APACHE"
            "LICENSE.txt"
            "AUTHORS.md"
            "source/lib/expat_lib/xmltok.c"
            "source/app/gpr_tools/main_c.c")
        if(NOT EXISTS "${_gpr_source_root}/${_gpr_source_file}")
            message(FATAL_ERROR "The GoPro GPR SDK archive has an unexpected layout: ${_gpr_source_file}")
        endif()
    endforeach()

    set(_gpr_xmltok_path "${_gpr_source_root}/source/lib/expat_lib/xmltok.c")
    file(READ "${_gpr_xmltok_path}" _gpr_xmltok)
    set(_gpr_xmltok_original "${_gpr_xmltok}")
    set(_gpr_msvc_attribute_patch [=[#include <stddef.h>

#ifdef _MSC_VER
#define __attribute(x)
#endif]=])
    string(REPLACE "#include <stddef.h>" "${_gpr_msvc_attribute_patch}"
           _gpr_xmltok "${_gpr_xmltok}")
    if(_gpr_xmltok STREQUAL _gpr_xmltok_original)
        message(FATAL_ERROR "Failed to apply the audited MSVC Expat compatibility patch")
    endif()
    file(WRITE "${_gpr_xmltok_path}" "${_gpr_xmltok}")

    set(_gpr_main_c_path "${_gpr_source_root}/source/app/gpr_tools/main_c.c")
    file(READ "${_gpr_main_c_path}" _gpr_main_c)
    set(_gpr_main_c_original "${_gpr_main_c}")
    string(REPLACE
        "#include <stdio.h>\n#include <strings.h>\n#include <string.h>\n#include <stdbool.h>"
        "#include <stdio.h>\n#include <string.h>\n#include <stdbool.h>\n\n#if !defined(_MSC_VER)\n#include <strings.h>\n#endif"
        _gpr_main_c "${_gpr_main_c}")
    string(REPLACE
        "#if defined __GNUC__\n#define stricmp strcasecmp\n#else"
        "#if defined __GNUC__\n#define stricmp strcasecmp\n#elif defined _MSC_VER\n#define stricmp _stricmp\n#else"
        _gpr_main_c "${_gpr_main_c}")
    if(_gpr_main_c STREQUAL _gpr_main_c_original)
        message(FATAL_ERROR "Failed to apply the audited MSVC gpr_tools compatibility patch")
    endif()
    file(WRITE "${_gpr_main_c_path}" "${_gpr_main_c}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${_gpr_source_root}"
            -B "${_gpr_build_root}"
            -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            "-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG"
            "-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG"
        RESULT_VARIABLE _gpr_configure_result
        OUTPUT_VARIABLE _gpr_configure_output
        ERROR_VARIABLE _gpr_configure_error
    )
    if(NOT _gpr_configure_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to configure GoPro GPR SDK:\n${_gpr_configure_output}\n${_gpr_configure_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${_gpr_build_root}" --target gpr_tools --parallel
        RESULT_VARIABLE _gpr_build_result
        OUTPUT_VARIABLE _gpr_build_output
        ERROR_VARIABLE _gpr_build_error
    )
    if(NOT _gpr_build_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to build GoPro GPR SDK:\n${_gpr_build_output}\n${_gpr_build_error}")
    endif()
    set(_gpr_tools_executable
        "${_gpr_build_root}/source/app/gpr_tools/gpr_tools.exe")
    if(NOT EXISTS "${_gpr_tools_executable}")
        message(FATAL_ERROR "GoPro GPR SDK build did not produce gpr_tools.exe")
    endif()
    file(COPY "${_gpr_tools_executable}" DESTINATION "${_gpr_prepare_root}/bin")
    file(COPY
        "${_gpr_source_root}/LICENSE-MIT"
        "${_gpr_source_root}/LICENSE-APACHE"
        "${_gpr_source_root}/LICENSE.txt"
        "${_gpr_source_root}/AUTHORS.md"
        DESTINATION "${_gpr_prepare_root}/licenses")
    file(REMOVE_RECURSE "${_gpr_runtime_root}")
    file(MAKE_DIRECTORY "${_output_root}/runtimes")
    file(RENAME "${_gpr_prepare_root}" "${_gpr_runtime_root}")
    file(REMOVE_RECURSE "${_gpr_extract_root}" "${_gpr_build_root}")
endif()

file(WRITE "${_output_root}/libraw-dependency.ready"
    "schema=1\n"
    "lock_sha256=${_lock_sha256}\n"
    "profile_version=${_profile_version}\n"
    "selected_license=${_selected_license}\n"
    "runtime_root=${_runtime_root}\n"
    "gpr_version=${_gpr_version}\n"
    "gpr_selected_license=${_gpr_selected_license}\n"
    "gpr_runtime_root=${_gpr_runtime_root}\n")
message(STATUS "RAW dependency cache is ready: ${_runtime_root}; ${_gpr_runtime_root}")
