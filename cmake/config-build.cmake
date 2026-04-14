# Do we want to build extra SDK stuff or just the game binary?
option(RTS_BUILD_CORE_TOOLS "Build core tools" ON)
option(RTS_BUILD_CORE_EXTRAS "Build core extra tools/tests" OFF)
option(RTS_BUILD_ZEROHOUR "Build Zero Hour code." ON)
option(RTS_BUILD_GENERALS "Build Generals code." ON)
option(RTS_BUILD_OPTION_PROFILE "Build code with the \"Profile\" configuration." OFF)
option(RTS_BUILD_OPTION_DEBUG "Build code with the \"Debug\" configuration." OFF)
option(RTS_BUILD_OPTION_ASAN "Build code with Address Sanitizer." OFF)
option(RTS_BUILD_OPTION_VC6_FULL_DEBUG "Build VC6 with full debug info." OFF)
option(RTS_BUILD_OPTION_FFMPEG "Deprecated. Use -DRTS_VIDEO=ffmpeg instead. Enables FFmpeg video backend." OFF)
if(RTS_BUILD_OPTION_FFMPEG)
    message(DEPRECATION "RTS_BUILD_OPTION_FFMPEG is deprecated; use -DRTS_VIDEO=ffmpeg.")
    set(RTS_VIDEO "ffmpeg" CACHE STRING "" FORCE)
endif()

# Cross-platform renderer backend selector (see docs/Phase0-RHI-Seam.md, docs/CrossPlatformPort-Plan.md).
# Phase 0 only accepts "dx8" — "bgfx" will be added in Phase 5.
set(RTS_RENDERER "dx8" CACHE STRING "Renderer backend. Values: dx8 (legacy Windows-only) | bgfx (planned, Phase 5)")
set_property(CACHE RTS_RENDERER PROPERTY STRINGS "dx8" "bgfx")
string(TOLOWER "${RTS_RENDERER}" RTS_RENDERER_LOWER)
if(NOT RTS_RENDERER_LOWER STREQUAL "dx8")
    message(FATAL_ERROR "RTS_RENDERER=${RTS_RENDERER} is not supported yet. Only 'dx8' is available until Phase 5 lands the bgfx backend.")
endif()
target_compile_definitions(core_config INTERFACE RTS_RENDERER_DX8=1)
add_feature_info(RendererBackend TRUE "Renderer backend: ${RTS_RENDERER_LOWER}")

# Audio backend selector (Phase 1 — see docs/CrossPlatformPort-Plan.md).
# miles = Windows retail reference (Miles Sound System, 32-bit only).
# openal = cross-platform (OpenAL Soft, Phase 1 target).
# null = headless; no device opened, no playback. For tests and dedicated servers.
set(RTS_AUDIO "miles" CACHE STRING "Audio backend. Values: miles | openal | null")
set_property(CACHE RTS_AUDIO PROPERTY STRINGS "miles" "openal" "null")
string(TOLOWER "${RTS_AUDIO}" RTS_AUDIO_LOWER)
if(RTS_AUDIO_LOWER STREQUAL "miles")
    target_compile_definitions(core_config INTERFACE RTS_AUDIO_MILES=1)
elseif(RTS_AUDIO_LOWER STREQUAL "openal")
    target_compile_definitions(core_config INTERFACE RTS_AUDIO_OPENAL=1)
elseif(RTS_AUDIO_LOWER STREQUAL "null")
    target_compile_definitions(core_config INTERFACE RTS_AUDIO_NULL=1)
else()
    message(FATAL_ERROR "RTS_AUDIO=${RTS_AUDIO} is not supported. Use miles|openal|null.")
endif()
add_feature_info(AudioBackend TRUE "Audio backend: ${RTS_AUDIO_LOWER}")

# Video backend selector (Phase 1).
# bink = Windows retail reference (Bink SDK, 32-bit only).
# ffmpeg = cross-platform (libavcodec/libavformat/libswscale via vcpkg).
set(RTS_VIDEO "bink" CACHE STRING "Video backend. Values: bink | ffmpeg")
set_property(CACHE RTS_VIDEO PROPERTY STRINGS "bink" "ffmpeg")
string(TOLOWER "${RTS_VIDEO}" RTS_VIDEO_LOWER)
if(RTS_VIDEO_LOWER STREQUAL "bink")
    target_compile_definitions(core_config INTERFACE RTS_VIDEO_BINK=1)
elseif(RTS_VIDEO_LOWER STREQUAL "ffmpeg")
    # RTS_HAS_FFMPEG kept for compatibility with existing call sites that predate RTS_VIDEO.
    target_compile_definitions(core_config INTERFACE RTS_VIDEO_FFMPEG=1 RTS_HAS_FFMPEG=1)
else()
    message(FATAL_ERROR "RTS_VIDEO=${RTS_VIDEO} is not supported. Use bink|ffmpeg.")
endif()
add_feature_info(VideoBackend TRUE "Video backend: ${RTS_VIDEO_LOWER}")

if(NOT RTS_BUILD_ZEROHOUR AND NOT RTS_BUILD_GENERALS)
    set(RTS_BUILD_ZEROHOUR TRUE)
    message("You must select one project to build, building Zero Hour by default.")
endif()

add_feature_info(CoreTools RTS_BUILD_CORE_TOOLS "Build Core Mod Tools")
add_feature_info(CoreExtras RTS_BUILD_CORE_EXTRAS "Build Core Extra Tools/Tests")
add_feature_info(ZeroHourStuff RTS_BUILD_ZEROHOUR "Build Zero Hour code")
add_feature_info(GeneralsStuff RTS_BUILD_GENERALS "Build Generals code")
add_feature_info(ProfileBuild RTS_BUILD_OPTION_PROFILE "Building as a \"Profile\" build")
add_feature_info(DebugBuild RTS_BUILD_OPTION_DEBUG "Building as a \"Debug\" build")
add_feature_info(AddressSanitizer RTS_BUILD_OPTION_ASAN "Building with address sanitizer")
add_feature_info(Vc6FullDebug RTS_BUILD_OPTION_VC6_FULL_DEBUG "Building VC6 with full debug info")

set(RTS_BUILD_OUTPUT_SUFFIX "" CACHE STRING "Suffix appended to output names of installable targets")

if(RTS_BUILD_ZEROHOUR)
    option(RTS_BUILD_ZEROHOUR_TOOLS "Build tools for Zero Hour" ON)
    option(RTS_BUILD_ZEROHOUR_EXTRAS "Build extra tools/tests for Zero Hour" OFF)
    option(RTS_BUILD_ZEROHOUR_DOCS "Build documentation for Zero Hour" OFF)

    add_feature_info(ZeroHourTools RTS_BUILD_ZEROHOUR_TOOLS "Build Zero Hour Mod Tools")
    add_feature_info(ZeroHourExtras RTS_BUILD_ZEROHOUR_EXTRAS "Build Zero Hour Extra Tools/Tests")
    add_feature_info(ZeroHourDocs RTS_BUILD_ZEROHOUR_DOCS "Build Zero Hour Documentation")
endif()

if(RTS_BUILD_GENERALS)
    option(RTS_BUILD_GENERALS_TOOLS "Build tools for Generals" ON)
    option(RTS_BUILD_GENERALS_EXTRAS "Build extra tools/tests for Generals" OFF)
    option(RTS_BUILD_GENERALS_DOCS "Build documentation for Generals" OFF)

    add_feature_info(GeneralsTools RTS_BUILD_GENERALS_TOOLS "Build Generals Mod Tools")
    add_feature_info(GeneralsExtras RTS_BUILD_GENERALS_EXTRAS "Build Generals Extra Tools/Tests")
    add_feature_info(GeneralsDocs RTS_BUILD_GENERALS_DOCS "Build Generals Documentation")
endif()

if(NOT IS_VS6_BUILD)
    # Because we set CMAKE_CXX_STANDARD_REQUIRED and CMAKE_CXX_EXTENSIONS in the compilers.cmake this should be enforced.
    target_compile_features(core_config INTERFACE cxx_std_20)
endif()

if(IS_VS6_BUILD AND RTS_BUILD_OPTION_VC6_FULL_DEBUG)
    target_compile_options(core_config INTERFACE ${RTS_FLAGS} /Zi)
else()
    target_compile_options(core_config INTERFACE ${RTS_FLAGS})
endif()

# This disables a lot of warnings steering developers to use windows only functions/function names.
if(MSVC)
    target_compile_definitions(core_config INTERFACE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS $<$<CONFIG:DEBUG>:_DEBUG_CRT>)
endif()

if(UNIX)
    target_compile_definitions(core_config INTERFACE _UNIX)
endif()

if(RTS_BUILD_OPTION_DEBUG)
    target_compile_definitions(core_config INTERFACE RTS_DEBUG WWDEBUG DEBUG)
else()
    target_compile_definitions(core_config INTERFACE RTS_RELEASE NDEBUG)
endif()

if(RTS_BUILD_OPTION_PROFILE)
    target_compile_definitions(core_config INTERFACE RTS_PROFILE)
endif()
