# Phase 5h.31 smoke test: SurfaceClass bgfx-mode CPU-backed path + auto-upload
# to a bgfx texture via Update_Texture_RGBA8 on Unlock().

if(NOT RTS_RENDERER_LOWER STREQUAL "bgfx")
    return()
endif()

if(NOT RTS_BUILD_CORE_EXTRAS)
    return()
endif()

add_executable(tst_bgfx_surface
    main.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWStub/wwallocstub.cpp
)

target_include_directories(tst_bgfx_surface PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include/Common
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2/compat
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression/LZHCompress
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression/EAC
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression/RefPack
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression/HuffmanCompress
)

target_link_libraries(tst_bgfx_surface PRIVATE
    corei_bgfx
    SDL3::SDL3
)

# Surface test compiles surfaceclass.cpp directly rather than linking z_ww3d2,
# which would drag in the game's transitive INTERFACE deps (core_browserengine,
# milesstub, stlport — some of which aren't defined on macOS). Pull only the
# include paths we need.

target_compile_definitions(tst_bgfx_surface PRIVATE RTS_RENDERER_BGFX=1)

# Force-include CppMacros.h so CPP_11()/nullptr/noexcept shims resolve inside
# WWLib headers (STLUtils.h, always.h). The ww3d2 lib gets this via PCH; test
# TUs have to ask for it explicitly.
if(MSVC)
    target_compile_options(tst_bgfx_surface PRIVATE /FI"Utility/CppMacros.h")
else()
    target_compile_options(tst_bgfx_surface PRIVATE -include Utility/CppMacros.h)
endif()
