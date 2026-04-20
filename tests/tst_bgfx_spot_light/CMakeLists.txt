# Phase 5h.10 smoke test: spot-light cone mask via u_lightSpotArr.
# Only built when RTS_RENDERER=bgfx and RTS_BUILD_CORE_EXTRAS=ON.

if(NOT RTS_RENDERER_LOWER STREQUAL "bgfx")
    return()
endif()

if(NOT RTS_BUILD_CORE_EXTRAS)
    return()
endif()

add_executable(tst_bgfx_spot_light
    main.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp
)

target_include_directories(tst_bgfx_spot_light PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
)

target_link_libraries(tst_bgfx_spot_light PRIVATE
    corei_bgfx
    SDL3::SDL3
)
