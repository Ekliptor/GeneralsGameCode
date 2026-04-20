if(WIN32)
    add_executable(g_generals WIN32)
elseif(APPLE)
    add_executable(g_generals MACOSX_BUNDLE)
    include(${CMAKE_SOURCE_DIR}/cmake/macos/bundle.cmake)
    rts_configure_macos_bundle(g_generals "Generals" "com.community.generals")
else()
    add_executable(g_generals)
endif()

# Use a binary name that doesn't conflict with original game.
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(g_generals PROPERTIES OUTPUT_NAME "generalsv${RTS_BUILD_OUTPUT_SUFFIX}")
else()
    set_target_properties(g_generals PROPERTIES OUTPUT_NAME "generalsv${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

target_link_libraries(g_generals PRIVATE
    g_gameengine
    g_gameenginedevice
    gi_always
)

# Platform-conditional link libraries (Phase 5d).
if(RTS_RENDERER_LOWER STREQUAL "dx8")
    target_link_libraries(g_generals PRIVATE d3d8 d3dx8)
endif()
if(RTS_PLATFORM_LOWER STREQUAL "win32")
    target_link_libraries(g_generals PRIVATE dinput8 dxguid)
endif()
if(RTS_AUDIO_LOWER STREQUAL "miles")
    target_link_libraries(g_generals PRIVATE milesstub)
endif()
if(RTS_VIDEO_LOWER STREQUAL "bink")
    target_link_libraries(g_generals PRIVATE binkstub)
endif()
if(WIN32)
    target_link_libraries(g_generals PRIVATE comctl32 imm32 vfw32 winmm)
endif()
if(RTS_RENDERER_LOWER STREQUAL "bgfx")
    target_link_libraries(g_generals PRIVATE corei_bgfx)
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
if (IS_VS6_BUILD)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 7
#define VERSION_BUILDNUM 601
"
)
else()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 8
#define VERSION_BUILDNUM 601
"
)
endif()

if(MSVC)
    target_link_options(g_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(g_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/Code/Libraries
)

if(RTS_PLATFORM_LOWER STREQUAL "sdl")
    target_sources(g_generals PRIVATE
        AppMain.cpp
    )
    target_link_libraries(g_generals PRIVATE SDL3::SDL3)
else()
    target_sources(g_generals PRIVATE
        WinMain.cpp
        WinMain.h
    )
endif()

# RC files are optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
# TYPELIB: Broken (0 bytes) and causes windres memory exhaustion errors
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(g_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(g_generals PRIVATE
        RTS.RC
    )
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalsv${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(g_generals)
endif()
