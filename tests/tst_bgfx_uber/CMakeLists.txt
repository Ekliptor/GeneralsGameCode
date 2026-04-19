# Phase 5f smoke test: bgfx uber-shader permutations via SDL3 native window
# handle. Only built when RTS_RENDERER=bgfx and RTS_BUILD_CORE_EXTRAS=ON.

if(NOT RTS_RENDERER_LOWER STREQUAL "bgfx")
    return()
endif()

if(NOT RTS_BUILD_CORE_EXTRAS)
    return()
endif()

add_executable(tst_bgfx_uber
    main.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp
)

target_include_directories(tst_bgfx_uber PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
)

target_link_libraries(tst_bgfx_uber PRIVATE
    corei_bgfx
    SDL3::SDL3
)
