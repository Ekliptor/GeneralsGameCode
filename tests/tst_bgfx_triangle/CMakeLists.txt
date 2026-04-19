# Phase 5e smoke test: bgfx textured triangle via SDL3 native window handle.
# Only built when RTS_RENDERER=bgfx and RTS_BUILD_CORE_EXTRAS=ON.

if(NOT RTS_RENDERER_LOWER STREQUAL "bgfx")
    return()
endif()

if(NOT RTS_BUILD_CORE_EXTRAS)
    return()
endif()

add_executable(tst_bgfx_triangle
    main.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp
)

target_include_directories(tst_bgfx_triangle PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
)

target_link_libraries(tst_bgfx_triangle PRIVATE
    corei_bgfx
    SDL3::SDL3
)
