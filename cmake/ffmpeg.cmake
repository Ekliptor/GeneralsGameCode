# FFmpeg video backend + shared audio-file decoder for the OpenAL audio backend.
# Provided via vcpkg. Only loaded when RTS_VIDEO=ffmpeg or when the OpenAL audio
# backend declared it needs FFmpeg for sample decoding (see openal.cmake).
if(NOT RTS_VIDEO_LOWER STREQUAL "ffmpeg" AND NOT RTS_AUDIO_NEEDS_FFMPEG)
    return()
endif()

# Prefer vcpkg's FFMPEGConfig.cmake; fall back to pkg-config (Homebrew, Linux pkgs).
find_package(FFMPEG CONFIG QUIET)
if(NOT FFMPEG_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
    # pkg_check_modules already exposes FFMPEG_INCLUDE_DIRS / FFMPEG_LIBRARY_DIRS /
    # FFMPEG_LIBRARIES in the expected names, so downstream target_* calls keep working.
endif()
