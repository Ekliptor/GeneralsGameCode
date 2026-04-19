# Phase 5e — First textured triangle in bgfx

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fifth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5d-D3D8PortableShim.md`.

## Objective

Prove bgfx can render geometry in this project — not just clear. Ship a new
smoke test `tst_bgfx_triangle` that draws a single textured triangle through
the bgfx backend, backed by a build-time shader-compile pipeline so later 5f+
stages can add `.sc` files without new plumbing. The main game executables
still do not render through bgfx — that is Phase 5f/5g scope.

## Locked decisions

| Question | Decision |
|---|---|
| Shader compile tooling | `shaderc` + `bin2c` via bgfx.cmake's `bgfx_compile_shaders(... AS_HEADERS)` helper. Generated headers embed the shader bytes as C arrays and are `#include`d from `BgfxBackend.cpp`. No runtime shader compilation, no checked-in `.bin` files. |
| bgfx tools pulled in | Only `BGFX_BUILD_TOOLS_SHADER` and `BGFX_BUILD_TOOLS_BIN2C` (required by `bgfx_compile_shaders` — the helper is gated on `TARGET bgfx::bin2c`). `BGFX_BUILD_TOOLS_GEOMETRY` and `BGFX_BUILD_TOOLS_TEXTURE` stay off. |
| Profiles compiled on macOS | `metal`, `glsl`, `essl`. SPIRV skipped (see "bgfx.cmake patches" below). WGSL not needed for macOS runtime. `BGFX_PLATFORM_SUPPORTS_SPIRV` / `_WGSL` are `#define`d to `0` in the backend TU so `BGFX_EMBEDDED_SHADER` does not reference symbols we never generated. |
| Draw entry point | **Not** a new `IRenderBackend` method. A test-only `BgfxBackend::DrawSmokeTriangle()` lazy-inits its own program/texture/sampler on first call and submits via transient vertex/index buffers. Keeps the IRenderBackend interface unchanged — real `Draw_Indexed` via `VertexBufferClass`/`IndexBufferClass` is Phase 5g. |
| Geometry | Three clip-space verts (`±0.5, ±0.5, 0`) with UVs. Identity view/projection so the triangle renders regardless of transform state. |
| Test texture | 2×2 RGBA8 magenta/yellow checkerboard created in-code with `bgfx::copy`. Point-sampled, clamp-to-edge. DDS loading via `bimg` is 5i scope. |
| Shader set | Single `vs_triangle` + `fs_triangle` pair using `bgfx_shader.sh`. Vertex layout: `Position (vec3) + TexCoord0 (vec2)`. Fragment samples one 2D texture. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/varying.def.sc` | vertex attribute declarations (`a_position`, `a_texcoord0`, `v_texcoord0`) |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_triangle.sc` | pos+uv vertex shader; emits `gl_Position = mul(u_modelViewProj, vec4(a_position,1))` and passes UV through |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_triangle.sc` | samples `s_texture` with `v_texcoord0`; writes `gl_FragColor` |
| `tests/tst_bgfx_triangle/main.cpp` | SDL+bgfx harness calling `DrawSmokeTriangle()` for ~2s, then shutdown |
| `tests/tst_bgfx_triangle/CMakeLists.txt` | gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`; links `corei_bgfx` + `SDL3::SDL3` |
| `docs/Phase5e-FirstTriangle.md` | this doc |

### Edited files

| File | Change |
|---|---|
| `cmake/bgfx.cmake` | Flip `BGFX_BUILD_TOOLS=ON` with `BGFX_BUILD_TOOLS_GEOMETRY=OFF` / `_TEXTURE=OFF`. After FetchContent, patch two bugs in the fetched `bgfxToolUtils.cmake`, `include(bgfxToolUtils.cmake)` to expose `bgfx_compile_shaders`, and set `BGFX_SHADER_INCLUDE_PATH` to bgfx's shader stdlib (`bgfx/src`). |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | `#include <bgfx/bgfx.h>` for the handle types. Add `void DrawSmokeTriangle();` public method + private members (`m_smokeInit`, `m_smokeLayout`, `m_smokeProgram`, `m_smokeTexture`, `m_smokeSampler`), all initialized to `BGFX_INVALID_HANDLE`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `#include <bgfx/embedded_shader.h>` with `BGFX_PLATFORM_SUPPORTS_SPIRV=0` / `_WGSL=0` pre-defined, `#include` one generated header per (shader, profile), define `s_embeddedShaders[]` via `BGFX_EMBEDDED_SHADER`, implement `DrawSmokeTriangle()` (lazy init + transient VB/IB + single submit), release handles in `Shutdown()`. |
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Invoke `bgfx_compile_shaders(... AS_HEADERS)` twice (vertex + fragment). Add generated-headers dir to `corei_bgfx` PRIVATE include path. Link `bx` PUBLIC (bgfx links bx PRIVATE but `<bgfx/embedded_shader.h>` pulls `<bx/platform.h>`). Mark `.sc` sources `HEADER_FILE_ONLY`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_triangle)`. |

### bgfx.cmake patches

Two bugs in the pinned tag (`v1.143.9216-529`) are patched at configure time
by reading/rewriting the fetched `bgfxToolUtils.cmake` before anyone
`include()`s it:

1. **Malformed generator expression** for shaderc's optimization flag:
   `"$<IF:$<CONFIG:Debug>:0,3>"` uses colon separators where `$<IF>` requires
   commas. CMake rejects the whole configure. String-replaced to
   `"$<IF:$<CONFIG:Debug>,0,3>"`.
2. **SPIRV profile is unconditionally seeded** (`set(PROFILES spirv)` at the
   top of `bgfx_compile_shaders`). Compiling `bgfx_shader.sh` through shaderc's
   SPIRV backend fails with `HLSL parsing failed` on this tag (a known
   upstream regression around the `select(bvec2, ...)` helper overloads).
   Rewritten to `set(PROFILES "")` — platform-appropriate profiles still get
   appended (metal/glsl/essl on macOS), and the runtime Metal path is what
   macOS actually uses. On Windows this will need revisiting when that host
   comes online (DX11/DX12 profiles only, so SPIRV skip is still safe).

Both patches are idempotent (the `string(REPLACE)` no-ops if the pattern is
already fixed). Upstream fixes should let both go away — revisit on the next
bgfx tag bump.

## `BgfxBackend::DrawSmokeTriangle` internals

- Vertex layout: 20-byte `{ vec3 Position; vec2 TexCoord0 }`, built once via
  `bgfx::VertexLayout::begin() ... .end()`.
- Program: `bgfx::createProgram(bgfx::createEmbeddedShader(s_embeddedShaders,
  bgfx::getRendererType(), "vs_triangle"), …, true)`. Shader handles are
  owned by the program and released with it.
- Texture: 2×2 RGBA8 checkerboard (`0xFF,0x00,0xFF,0xFF` ↔ `0xFF,0xFF,0x00,0xFF`)
  via `bgfx::createTexture2D` + `bgfx::copy`. Sampler flags:
  `MIN_POINT|MAG_POINT|MIP_POINT|U_CLAMP|V_CLAMP`.
- Sampler uniform: `bgfx::createUniform("s_texture", Sampler)`.
- Per-frame: `setViewTransform(0, identity, identity)`, alloc a 3-vert
  transient VB and 3-index transient IB, `memcpy` the geometry,
  `setVertexBuffer` / `setIndexBuffer` / `setTexture`, state =
  `WRITE_RGB|WRITE_A|WRITE_Z|DEPTH_TEST_LESS|MSAA`, `submit(0, program)`.
- `Shutdown()` destroys the program, texture, and sampler handles if
  initialized; transient buffers need no teardown.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529
(built from source via FetchContent).

| Command | Outcome |
|---|---|
| `cmake .. -DRTS_RENDERER=bgfx -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg -DRTS_BUILD_CORE_EXTRAS=ON` | OK. `RendererBackend: bgfx`. Logs `bgfx: patched bgfxToolUtils.cmake (genex + spirv skip)` once on first configure. |
| `cmake --build . --target shaderc` | OK. `_deps/.../shaderc` built (with glslang/spirv-opt/spirv-cross/glsl-optimizer/fcpp support libs). |
| `cmake --build . --target corei_bgfx` | OK. `vs_triangle.sc` / `fs_triangle.sc` compile to `shaders_gen/{vs,fs}_triangle/{metal,glsl,essl}/*.sc.bin.h`. |
| `cmake --build . --target tst_bgfx_clear` | OK (regression check — still builds). |
| `cmake --build . --target tst_bgfx_triangle` | OK. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `BgfxBackend::Init: Metal (800x600, windowed)` → `tst_bgfx_clear: PASSED`. |
| `./tests/tst_bgfx_triangle/tst_bgfx_triangle` | `BgfxBackend::Init: Metal (800x600, windowed)` → visible magenta/yellow checkerboard triangle on `#306080` for ~2s → `BgfxBackend::Shutdown: complete` → `tst_bgfx_triangle: PASSED`. |
| `cmake-build-release` reconfigure with `RTS_RENDERER=dx8` | OK. DX8 game targets still configure — no regression. |

Static sweep:

| Pattern | Result |
|---|---|
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero (unchanged from 5a–5d). |
| `bgfx::` calls in `Core/Libraries/Source/WWVegas/WW3D2/` | Zero. |
| Hand-written shader bytecode checked in | Zero — everything goes through shaderc at build time. |

## Deferred to later Phase 5 stages

| Item | Stage |
|---|---|
| Fixed-function uber-shaders (vertex-color, lit, multi-texture permutations) | 5f |
| `IRenderBackend::Draw_Indexed` via `VertexBufferClass`/`IndexBufferClass` — mesh rendering parity | 5g |
| Terrain + water shader rewrite | 5h |
| DDS loading via `bimg` | 5i |
| Full scene parity + golden-image CI | 5j |
| macOS `.app` bundle + CI jobs | 5k |
| Bump past bgfx v1.143.9216-529 and drop both upstream patches | any 5x |
| Windows bgfx profile set (`s_5_0`, `s_6_0`) — once a Windows dev host is online | 5f/5g |
