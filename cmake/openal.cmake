# OpenAL Soft audio backend (Phase 1 — see docs/CrossPlatformPort-Plan.md).
# Provided via vcpkg port `openal-soft`. Loaded for RTS_AUDIO=openal and also
# RTS_AUDIO=null: OpenALAudioManagerNull shares its translation unit with the
# real OpenALAudioManager, so the symbol set (and thus the link) still needs
# the OpenAL lib even when playback is a no-op.
if(NOT (RTS_AUDIO_LOWER STREQUAL "openal" OR RTS_AUDIO_LOWER STREQUAL "null"))
    return()
endif()

if(APPLE AND NOT OPENAL_PREFER_FRAMEWORK)
    # openal-soft is keg-only on Homebrew (it conflicts with Apple's framework),
    # so its OpenALConfig.cmake isn't on CMake's default search paths. Probe the
    # Cellar's opt/ symlink for both Apple Silicon and Intel layouts and prepend
    # to CMAKE_PREFIX_PATH so the CONFIG-mode find_package below picks up
    # libopenal.1.dylib instead of falling through to the broken Apple framework
    # (see docs/OpenAL-Apple-Silicon-Crash.md). Opt out via
    # -DOPENAL_PREFER_FRAMEWORK=ON for bug-reproduction builds.
    foreach(_brew_prefix "/opt/homebrew/opt/openal-soft" "/usr/local/opt/openal-soft")
        if(EXISTS "${_brew_prefix}/lib/cmake/OpenAL/OpenALConfig.cmake")
            list(PREPEND CMAKE_PREFIX_PATH "${_brew_prefix}")
            break()
        endif()
    endforeach()
    # Belt-and-suspenders: even if the probe misses, tell the MODULE-mode
    # fallback to consult frameworks LAST so any /usr/local OpenAL wins over
    # /System/Library/Frameworks/OpenAL.framework.
    set(CMAKE_FIND_FRAMEWORK LAST)
endif()

# Prefer vcpkg's OpenALConfig.cmake (CONFIG mode), but fall back to CMake's
# built-in FindOpenAL.cmake so Homebrew/system/framework OpenAL works too.
find_package(OpenAL CONFIG QUIET)
if(NOT OpenAL_FOUND)
    find_package(OpenAL REQUIRED)
    if(OPENAL_FOUND AND NOT TARGET OpenAL::OpenAL)
        # The Module-mode FindOpenAL.cmake on older CMake doesn't always define
        # the imported target; synthesize one so call sites can link OpenAL::OpenAL.
        add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
        set_target_properties(OpenAL::OpenAL PROPERTIES
            IMPORTED_LOCATION "${OPENAL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
    endif()
endif()

# FFmpeg is required for audio-file decoding on this backend regardless of
# RTS_VIDEO (we use libavcodec+libavformat to decode .wav/.mp3/.ogg samples).
# ffmpeg.cmake handles the actual find_package; we just record the implicit
# dependency so the video module runs even when RTS_VIDEO=bink.
# include() doesn't introduce a new scope, so this set() is visible to ffmpeg.cmake
# and to later target definitions in the caller.
set(RTS_AUDIO_NEEDS_FFMPEG TRUE)
