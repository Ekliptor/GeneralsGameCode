# BGFXDevice — bgfx renderer backend (Phase 5).
# Standalone static library so the smoke test harness (tst_bgfx_clear,
# tst_bgfx_triangle) can link it without pulling in the full game engine
# device infrastructure.

add_library(corei_bgfx STATIC
    Common/BgfxBackend.cpp
    Common/BgfxBootstrap.cpp
    Common/BgfxTextureCache.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBootstrap.h
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxTextureCache.h
)

target_include_directories(corei_bgfx PUBLIC
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
)

target_link_libraries(corei_bgfx PUBLIC
    bgfx::bgfx
    bx            # <bgfx/embedded_shader.h> pulls <bx/platform.h>; bgfx links bx PRIVATE.
    bimg          # Phase 5j: core image containers + format tables.
    bimg_decode   # Phase 5j: DDS / KTX / PNG / TGA decoding via bimg::imageParse.
    corei_always_no_pch
)

# Phase 5e — compile the smoke-triangle .sc shaders into C headers with the
# shader bytecode embedded (one profile per platform target: glsl/essl/spirv
# plus metal on macOS and dx11 on Windows). The generated headers are included
# directly by BgfxBackend.cpp via <vs_triangle/vs_triangle.sc.bin.h> and
# <fs_triangle/fs_triangle.sc.bin.h>.
set(BGFX_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)
set(BGFX_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders_gen)

# Phase 5e (tex) + Phase 5f (solid / vcolor / tex_lit) shader pairs. Each
# (vs,fs) pair gets two bgfx_compile_shaders invocations (VERTEX + FRAGMENT)
# into its own subdir under shaders_gen/. BgfxBackend.cpp #includes each
# generated header via <<name>/<profile>/<name>.sc.bin.h>.
set(BGFX_SHADER_PAIRS
    triangle    # Phase 5e: position + uv, textured
    solid       # Phase 5f: position only, fragment color from uniform
    vcolor      # Phase 5f: position + vertex color
    tex_lit     # Phase 5f: position + normal + vertex color + uv, lit (single directional — smoke tests)
    tex2        # Phase 5k: position + uv0 + uv1, two-stage modulate
    tex_mlit    # Phase 5l: same layout as tex_lit, with 4-slot directional light accumulation
)

set(BGFX_ALL_GENERATED_OUT "")
set(BGFX_ALL_SHADER_SRCS "")

foreach(pair ${BGFX_SHADER_PAIRS})
    set(_vs_src ${BGFX_SHADER_DIR}/vs_${pair}.sc)
    set(_fs_src ${BGFX_SHADER_DIR}/fs_${pair}.sc)
    list(APPEND BGFX_ALL_SHADER_SRCS ${_vs_src} ${_fs_src})

    bgfx_compile_shaders(
        TYPE VERTEX
        SHADERS ${_vs_src}
        VARYING_DEF ${BGFX_SHADER_DIR}/varying.def.sc
        OUTPUT_DIR ${BGFX_SHADER_OUT}/vs_${pair}
        OUT_FILES_VAR _vs_out
        AS_HEADERS
    )
    bgfx_compile_shaders(
        TYPE FRAGMENT
        SHADERS ${_fs_src}
        VARYING_DEF ${BGFX_SHADER_DIR}/varying.def.sc
        OUTPUT_DIR ${BGFX_SHADER_OUT}/fs_${pair}
        OUT_FILES_VAR _fs_out
        AS_HEADERS
    )
    list(APPEND BGFX_ALL_GENERATED_OUT ${_vs_out} ${_fs_out})
endforeach()

target_sources(corei_bgfx PRIVATE
    ${BGFX_ALL_SHADER_SRCS}
    ${BGFX_SHADER_DIR}/varying.def.sc
    ${BGFX_ALL_GENERATED_OUT}
)

target_include_directories(corei_bgfx PRIVATE
    ${BGFX_SHADER_OUT}
)

# Shader sources are inputs to custom commands, not C++ sources — tell the
# generator not to try to compile them.
set_source_files_properties(
    ${BGFX_ALL_SHADER_SRCS}
    ${BGFX_SHADER_DIR}/varying.def.sc
    PROPERTIES HEADER_FILE_ONLY TRUE
)
