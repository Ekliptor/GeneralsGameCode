# Phase 5h.35 smoke test: SurfaceClass format-aware Update_Texture routing.

if(NOT RTS_RENDERER_LOWER STREQUAL "bgfx")
    return()
endif()

if(NOT RTS_BUILD_CORE_EXTRAS)
    return()
endif()

add_executable(tst_bgfx_surface_fmt
    main.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWStub/wwallocstub.cpp
)

target_include_directories(tst_bgfx_surface_fmt PRIVATE
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

target_link_libraries(tst_bgfx_surface_fmt PRIVATE
    corei_bgfx
    SDL3::SDL3
)

target_compile_definitions(tst_bgfx_surface_fmt PRIVATE RTS_RENDERER_BGFX=1)

if(MSVC)
    target_compile_options(tst_bgfx_surface_fmt PRIVATE /FI"Utility/CppMacros.h")
else()
    target_compile_options(tst_bgfx_surface_fmt PRIVATE -include Utility/CppMacros.h)
endif()
