if(WIN32)
    add_executable(z_generals WIN32)
else()
    add_executable(z_generals)
endif()

# Use a binary name that doesn't conflict with original game.
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
else()
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

target_link_libraries(z_generals PRIVATE
    core_debug
    core_profile
    z_gameengine
    z_gameenginedevice
    zi_always
)

# Platform-conditional link libraries (Phase 5d).
if(RTS_RENDERER_LOWER STREQUAL "dx8")
    target_link_libraries(z_generals PRIVATE d3d8 d3dx8)
endif()
if(RTS_PLATFORM_LOWER STREQUAL "win32")
    target_link_libraries(z_generals PRIVATE dinput8 dxguid)
endif()
if(RTS_AUDIO_LOWER STREQUAL "miles")
    target_link_libraries(z_generals PRIVATE milesstub)
endif()
if(RTS_VIDEO_LOWER STREQUAL "bink")
    target_link_libraries(z_generals PRIVATE binkstub)
endif()
if(WIN32)
    target_link_libraries(z_generals PRIVATE comctl32 imm32 vfw32 winmm)
endif()
if(RTS_RENDERER_LOWER STREQUAL "bgfx")
    target_link_libraries(z_generals PRIVATE corei_bgfx)
endif()

# TODO Originally referred to build host and user, replace with git info perhaps?
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
"
)

# Based on original binary values for these variables.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 4
#define VERSION_BUILDNUM 601
"
)

if(MSVC)
    target_link_options(z_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(z_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(RTS_PLATFORM_LOWER STREQUAL "sdl")
    target_sources(z_generals PRIVATE
        AppMain.cpp
    )
    target_link_libraries(z_generals PRIVATE SDL3::SDL3)
else()
    target_sources(z_generals PRIVATE
        WinMain.cpp
        WinMain.h
    )
endif()

# RC files optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(z_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(z_generals PRIVATE
        RTS.RC
    )
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalszh${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(z_generals)
endif()
