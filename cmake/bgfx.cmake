# bgfx — cross-platform rendering library (Phase 5 — see docs/CrossPlatformPort-Plan.md).
# Metal on macOS, D3D11/D3D12 on Windows, OpenGL/Vulkan as fallbacks.
# Only included when RTS_RENDERER=bgfx.
#
# Uses FetchContent with the bkaradzic/bgfx.cmake CMake wrapper (same pattern
# as cmake/dx8.cmake and cmake/gamespy.cmake for platform SDKs). This builds
# bgfx + bx + bimg from source. First build takes a few minutes; subsequent
# builds are incremental.

# Try a pre-installed bgfx first (vcpkg or system).
find_package(bgfx CONFIG QUIET)
if(bgfx_FOUND)
    message(STATUS "Found pre-installed bgfx")
    return()
endif()

message(STATUS "bgfx not found — fetching via FetchContent (bkaradzic/bgfx.cmake)")

# Suppress bgfx example targets but keep the shaderc/bin2c build host tools —
# Phase 5e compiles .sc shaders into embedded headers at build time, which
# requires both bgfx::shaderc and bgfx::bin2c (bgfx_compile_shaders helper
# in bgfxToolUtils.cmake is gated on `TARGET bgfx::bin2c`). Texture and
# geometry tool converters are not needed.
set(BGFX_BUILD_TOOLS             ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER      ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C       ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY    OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE     OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES          OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL                 OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS          OFF CACHE BOOL "" FORCE)
set(BX_AMALGAMATED               OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.143.9216-529
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(bgfx_cmake)

# Workarounds for bugs in the pinned bgfx.cmake (v1.143.9216-529) applied to
# the fetched `bgfxToolUtils.cmake` before anyone includes it:
#
# (1) Malformed generator expression for the shaderc optimization level:
#     "$<IF:$<CONFIG:Debug>:0,3>" (colon) instead of
#     "$<IF:$<CONFIG:Debug>,0,3>" (comma) — CMake rejects at generate time.
#
# (2) The SPIRV shader profile is unconditionally compiled (`set(PROFILES spirv)`)
#     but bgfx_shader.sh fails to compile through shaderc's SPIRV backend for
#     this tag. On macOS we target Metal (auto-detected) and ship GLSL/ESSL as
#     fallbacks; SPIRV is unused at runtime. Replace the initial profile list
#     with an empty seed so only the platform-appropriate profiles get added.
set(_bgfx_tool_utils "${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake")
if(EXISTS "${_bgfx_tool_utils}")
    file(READ "${_bgfx_tool_utils}" _bgfx_tool_utils_contents)
    set(_bgfx_tool_utils_fixed "${_bgfx_tool_utils_contents}")
    string(REPLACE
        "\"\$<IF:\$<CONFIG:Debug>:0,3>\""
        "\"\$<IF:\$<CONFIG:Debug>,0,3>\""
        _bgfx_tool_utils_fixed "${_bgfx_tool_utils_fixed}")
    string(REPLACE
        "set(PROFILES spirv)"
        "set(PROFILES \"\") # patched: skip spirv (broken in this bgfx tag)"
        _bgfx_tool_utils_fixed "${_bgfx_tool_utils_fixed}")
    if(NOT "${_bgfx_tool_utils_fixed}" STREQUAL "${_bgfx_tool_utils_contents}")
        file(WRITE "${_bgfx_tool_utils}" "${_bgfx_tool_utils_fixed}")
        message(STATUS "bgfx: patched bgfxToolUtils.cmake (genex + spirv skip)")
    endif()
endif()

# Expose the bgfx_compile_shaders helper for downstream use and point it at
# the bgfx shader stdlib (bgfx_shader.sh, bgfx_compute.sh).
include(${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake)
set(BGFX_SHADER_INCLUDE_PATH "${bgfx_cmake_SOURCE_DIR}/bgfx/src" CACHE PATH
    "Path to bgfx shader stdlib headers (bgfx_shader.sh)" FORCE)

# bkaradzic/bgfx.cmake creates targets named "bgfx", "bimg", "bx".
# Alias to bgfx::bgfx so downstream code uses the same imported-target name
# regardless of whether we found a system install or built from source.
if(NOT TARGET bgfx::bgfx)
    if(TARGET bgfx)
        add_library(bgfx::bgfx ALIAS bgfx)
    else()
        message(FATAL_ERROR "bgfx.cmake did not create a 'bgfx' target.")
    endif()
endif()
