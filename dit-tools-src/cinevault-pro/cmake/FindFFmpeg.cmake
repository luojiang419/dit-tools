# FFmpeg development package discovery for CineVault.
#
# Supported layout:
#   FFMPEG_DEV_ROOT/
#     include/
#     lib/
#     bin/
#
# Exported variables:
#   FFMPEG_FOUND
#   FFMPEG_INCLUDE_DIRS
#   FFMPEG_LIBRARIES
#
# Exported imported targets when found:
#   FFmpeg::avformat
#   FFmpeg::avcodec
#   FFmpeg::avutil
#   FFmpeg::swscale

include(FindPackageHandleStandardArgs)

set(_ffmpeg_root_hints)
if(DEFINED FFMPEG_DEV_ROOT AND EXISTS "${FFMPEG_DEV_ROOT}")
    list(APPEND _ffmpeg_root_hints "${FFMPEG_DEV_ROOT}")
endif()
if(DEFINED ENV{FFMPEG_DEV_ROOT} AND EXISTS "$ENV{FFMPEG_DEV_ROOT}")
    list(APPEND _ffmpeg_root_hints "$ENV{FFMPEG_DEV_ROOT}")
endif()

find_path(FFMPEG_INCLUDE_DIR
    NAMES libavformat/avformat.h
    HINTS ${_ffmpeg_root_hints}
    PATH_SUFFIXES include
)

set(_ffmpeg_components avformat avcodec avutil swscale)
set(FFMPEG_LIBRARIES)

foreach(_component IN LISTS _ffmpeg_components)
    string(TOUPPER "${_component}" _component_upper)

    find_library(FFMPEG_${_component_upper}_LIBRARY
        NAMES ${_component}
              lib${_component}
              ${_component}.lib
              lib${_component}.dll.a
        HINTS ${_ffmpeg_root_hints}
        PATH_SUFFIXES lib
    )

    if(FFMPEG_${_component_upper}_LIBRARY AND NOT TARGET FFmpeg::${_component})
        add_library(FFmpeg::${_component} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${_component} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_${_component_upper}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        )
    endif()

    if(FFMPEG_${_component_upper}_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${_component_upper}_LIBRARY}")
    endif()
endforeach()

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIR}")

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS
        FFMPEG_INCLUDE_DIR
        FFMPEG_AVFORMAT_LIBRARY
        FFMPEG_AVCODEC_LIBRARY
        FFMPEG_AVUTIL_LIBRARY
        FFMPEG_SWSCALE_LIBRARY
)

mark_as_advanced(
    FFMPEG_INCLUDE_DIR
    FFMPEG_AVFORMAT_LIBRARY
    FFMPEG_AVCODEC_LIBRARY
    FFMPEG_AVUTIL_LIBRARY
    FFMPEG_SWSCALE_LIBRARY
)
