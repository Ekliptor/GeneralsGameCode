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

# Phase 6b — opt-in macOS universal (arm64 + x86_64).
# The project's own static libs compile clean for both slices; distribution-
# level universal additionally requires every linked dep (SDL3, OpenAL, bgfx
# FetchContent, ffmpeg, zlib, etc.) to be universal. Homebrew installs are
# typically single-arch, so universal linking fails at the leaf executables
# until those are rebuilt universal. Default stays native (flagless = host
# arch). See docs/Phase6b-MacOSUniversal.md.
option(RTS_MACOS_UNIVERSAL "macOS: build universal arm64+x86_64 (requires universal deps)." OFF)
if(APPLE AND RTS_MACOS_UNIVERSAL AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS target architectures" FORCE)
endif()

# Phase 6c — opt-in 64-bit Windows. The root CMakeLists gates the retail
# middleware (miles/bink/dx8) behind `WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4`.
# Turning this on lifts the 32-bit requirement for bgfx-stack Windows builds
# (the modern stack has no 32-bit assumptions). Cannot be verified from a
# non-Windows host; see docs/Phase6c-Windows64.md for the verification plan.
option(RTS_WINDOWS_64BIT "Windows: allow 64-bit build with the bgfx/openal/ffmpeg stack." OFF)

# Phase 6d — escalate pointer-truncation and 64-to-32 narrowing warnings.
# Consumed in cmake/compilers.cmake. Off by default (legacy DX8/Miles/Bink
# code is noisy); intended for CI jobs that audit 64-bit readiness.
option(RTS_ENABLE_64BIT_WARNINGS "Enable -Wshorten-64-to-32 / MSVC C4267 etc. as errors." OFF)

# Platform-appropriate defaults for the retail-compat vs cross-platform stacks
# (see docs/Phase6a-NonWindowsDefaults.md). The retail middleware (dx8/miles/bink)
# is Windows-only and gated behind the 32-bit SDK block in the root CMakeLists;
# on non-Windows hosts flip the defaults to the cross-platform stack so a flag-
# less `cmake -S . -B build` does the right thing. Windows keeps the retail-
# compat defaults until Phase 7.
if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    set(_rts_default_renderer "dx8")
    set(_rts_default_audio    "miles")
    set(_rts_default_video    "bink")
    set(_rts_default_platform "win32")
else()
    set(_rts_default_renderer "bgfx")
    set(_rts_default_audio    "openal")
    set(_rts_default_video    "ffmpeg")
    set(_rts_default_platform "sdl")
endif()

# Cross-platform renderer backend selector (see docs/Phase0-RHI-Seam.md, docs/CrossPlatformPort-Plan.md).
# dx8 = legacy DirectX 8 fixed-function (Windows 32-bit only).
# bgfx = cross-platform (Metal on macOS, D3D11 on Windows; Phase 5).
set(RTS_RENDERER "${_rts_default_renderer}" CACHE STRING "Renderer backend. Values: dx8 | bgfx")
set_property(CACHE RTS_RENDERER PROPERTY STRINGS "dx8" "bgfx")
string(TOLOWER "${RTS_RENDERER}" RTS_RENDERER_LOWER)
if(RTS_RENDERER_LOWER STREQUAL "dx8")
    if(NOT (WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows"))
        message(FATAL_ERROR "RTS_RENDERER=dx8 requires a Windows host (retail reference). Use -DRTS_RENDERER=bgfx on non-Windows.")
    endif()
    target_compile_definitions(core_config INTERFACE RTS_RENDERER_DX8=1)
elseif(RTS_RENDERER_LOWER STREQUAL "bgfx")
    target_compile_definitions(core_config INTERFACE RTS_RENDERER_BGFX=1)
else()
    message(FATAL_ERROR "RTS_RENDERER=${RTS_RENDERER} is not supported. Use dx8|bgfx.")
endif()
add_feature_info(RendererBackend TRUE "Renderer backend: ${RTS_RENDERER_LOWER}")

# Audio backend selector (Phase 1 — see docs/CrossPlatformPort-Plan.md).
# miles = Windows retail reference (Miles Sound System, 32-bit only).
# openal = cross-platform (OpenAL Soft, Phase 1 target).
# null = headless; no device opened, no playback. For tests and dedicated servers.
set(RTS_AUDIO "${_rts_default_audio}" CACHE STRING "Audio backend. Values: miles | openal | null")
set_property(CACHE RTS_AUDIO PROPERTY STRINGS "miles" "openal" "null")
string(TOLOWER "${RTS_AUDIO}" RTS_AUDIO_LOWER)
if(RTS_AUDIO_LOWER STREQUAL "miles")
    if(NOT (WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows"))
        message(FATAL_ERROR "RTS_AUDIO=miles requires a Windows host (retail reference). Use -DRTS_AUDIO=openal on non-Windows.")
    endif()
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
set(RTS_VIDEO "${_rts_default_video}" CACHE STRING "Video backend. Values: bink | ffmpeg")
set_property(CACHE RTS_VIDEO PROPERTY STRINGS "bink" "ffmpeg")
string(TOLOWER "${RTS_VIDEO}" RTS_VIDEO_LOWER)
if(RTS_VIDEO_LOWER STREQUAL "bink")
    if(NOT (WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows"))
        message(FATAL_ERROR "RTS_VIDEO=bink requires a Windows host (retail reference). Use -DRTS_VIDEO=ffmpeg on non-Windows.")
    endif()
    target_compile_definitions(core_config INTERFACE RTS_VIDEO_BINK=1)
elseif(RTS_VIDEO_LOWER STREQUAL "ffmpeg")
    # RTS_HAS_FFMPEG kept for compatibility with existing call sites that predate RTS_VIDEO.
    target_compile_definitions(core_config INTERFACE RTS_VIDEO_FFMPEG=1 RTS_HAS_FFMPEG=1)
else()
    message(FATAL_ERROR "RTS_VIDEO=${RTS_VIDEO} is not supported. Use bink|ffmpeg.")
endif()
add_feature_info(VideoBackend TRUE "Video backend: ${RTS_VIDEO_LOWER}")

# Platform backend selector (Phase 2 — window/input/timing).
# win32 = legacy WinMain + WndProc + DirectInput (Windows retail reference).
# sdl   = cross-platform SDL3 (window, input, events); required on non-Windows hosts.
set(RTS_PLATFORM "${_rts_default_platform}" CACHE STRING
    "Platform backend (window/input/timing). Values: win32 (Windows retail) | sdl (cross-platform SDL3)")
set_property(CACHE RTS_PLATFORM PROPERTY STRINGS "win32" "sdl")
string(TOLOWER "${RTS_PLATFORM}" RTS_PLATFORM_LOWER)
if(RTS_PLATFORM_LOWER STREQUAL "win32")
    if(NOT (WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows"))
        message(FATAL_ERROR "RTS_PLATFORM=win32 requires a Windows host. Use -DRTS_PLATFORM=sdl on non-Windows.")
    endif()
    target_compile_definitions(core_config INTERFACE RTS_PLATFORM_WIN32=1)
elseif(RTS_PLATFORM_LOWER STREQUAL "sdl")
    target_compile_definitions(core_config INTERFACE RTS_PLATFORM_SDL=1)
else()
    message(FATAL_ERROR "RTS_PLATFORM=${RTS_PLATFORM} is not supported. Use win32|sdl.")
endif()
add_feature_info(PlatformBackend TRUE "Platform backend: ${RTS_PLATFORM_LOWER}")

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
