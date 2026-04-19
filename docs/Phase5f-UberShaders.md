# Phase 5f — Fixed-function uber-shader permutations

Companion doc to `docs/CrossPlatformPort-Plan.md`. Sixth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5e-FirstTriangle.md`.

## Objective

Expand the Phase 5e single-shader proof into a small library of shader
permutations that together cover the DX8 fixed-function surface the game
actually uses. Prove every permutation compiles on every target profile,
links into a bgfx `ProgramHandle`, and renders with the vertex layouts Phase
5g will send through `IRenderBackend::Draw_Indexed`. No production game code
is wired through these shaders yet — that is 5g.

Concretely: four shader pairs, four corresponding smoke draws on
`BgfxBackend`, and a new `tst_bgfx_uber` harness that renders one primitive
per permutation in a 2×2 quadrant layout for visual inspection.

## Locked decisions

| Question | Decision |
|---|---|
| Number of permutations this stage | **4**: `solid` (flat color, no attrs but position), `vcolor` (pos + color), `triangle`/`tex` (pos + uv + texture — from 5e, renamed conceptually), `tex_lit` (pos + normal + color + uv + directional light). Covers every axis the top-5 DX8 FVFs actually use — see `dx8fvf.h` lines 52–66. |
| Where uber-shader state lives | Not yet on `IRenderBackend`. Each `DrawSmoke*` method self-contains its lazy init + per-frame submit, mirroring the Phase 5e convention. Hooking `Set_Shader`/`Set_Material`/`Set_Texture`/`Set_*_Transform` into a real shader selector and uniform pump is **Phase 5g** scope — it needs the `VertexBufferClass` / `IndexBufferClass` bridge too, and doing both in one stage is a bigger blast radius than the 5-doc cadence warrants. |
| Lighting model | Single directional light: `u_lightDir.xyz` (world-space direction pointing from light to scene) + `u_lightDir.w` (scalar ambient). `u_lightColor.rgb` scales diffuse. One stage one light — matches the typical unit/building shader in Generals; extra lights come from per-stage gradients in DX8 fixed-function, which map to shader permutations in a later stage. |
| Vertex color encoding | `bgfx::Attrib::Color0` as 4 × `Uint8` normalized, interpreted as ABGR (little-endian) — bgfx's canonical format across all backends. Matches DX8 `D3DCOLOR` (ARGB in memory = ABGR little-endian), so Phase 5g's `FVFInfoClass` bridge can upload `D3DFVF_DIFFUSE` attribute bytes verbatim. |
| Checkerboard texture | In-code 2×2 RGBA8, bgfx point-sampled + clamp. The `tex_lit` quad uses white/dark-gray so the lighting contribution (not the albedo) drives the visible gradient. DDS loading via `bimg` is still Phase 5i. |
| Viewport layout for the test | 2×2 NDC quadrant grid — TL `solid` (crimson), TR `triangle` (5e checkerboard), BL `vcolor` (rainbow), BR `tex_lit` (lit checkerboard). Each quad lives in one quadrant so regressions on any single permutation are visible at a glance. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_solid.sc` / `fs_solid.sc` | position-only vertex, fragment color from `u_solidColor` |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_vcolor.sc` / `fs_vcolor.sc` | position + Color0, fragment passes through |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex_lit.sc` / `fs_tex_lit.sc` | position + normal + Color0 + uv, lambert directional lighting baked into `v_color0`, textured in fragment |
| `tests/tst_bgfx_uber/main.cpp` | SDL+bgfx harness, four `DrawSmoke*` calls per frame |
| `tests/tst_bgfx_uber/CMakeLists.txt` | gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`; mirrors `tst_bgfx_triangle` |
| `docs/Phase5f-UberShaders.md` | this doc |

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/varying.def.sc` | Add `v_color0` / `v_normal` varyings and `a_normal` / `a_color0` attrs. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare `DrawSmokeSolidQuad` / `DrawSmokeVColorQuad` / `DrawSmokeLitQuad` + matching `ProgramHandle` / `VertexLayout` / `UniformHandle` / `TextureHandle` private members per permutation. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `#include` 6 new generated shader headers (3 pairs × 1 profile shown per line × 3 profiles = 18 includes). Extend `s_embeddedShaders[]` with 3 new `BGFX_EMBEDDED_SHADER` entries. Implement the 3 new methods (lazy init + transient VB/IB submit). Destroy handles in `Shutdown`. Factor the identity 4×4 into file-scope `kIdentity4x4`. Move Phase 5e triangle to top-right quadrant so `tst_bgfx_uber` can show all four at once. |
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Replace the Phase 5e single-pair shader block with a `BGFX_SHADER_PAIRS` foreach loop over `triangle` / `solid` / `vcolor` / `tex_lit` — one `bgfx_compile_shaders(VERTEX …)` + one `bgfx_compile_shaders(FRAGMENT …)` per pair. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_uber)`. |

## BgfxBackend smoke-draw internals

All three new methods follow the same pattern as Phase 5e's
`DrawSmokeTriangle`:

1. Lazy init on first call: build `VertexLayout`, create program via
   `bgfx::createEmbeddedShader` + `createProgram(…, destroyShaders=true)`,
   create any textures / samplers / scalar uniforms. Cache in private members.
2. Per call: `setViewTransform(0, identity, identity)` (quads live in clip
   space), `alloc` transient VB + IB, `memcpy` verts + indices, bind
   uniforms / textures, `setState(WRITE_RGB|WRITE_A|WRITE_Z|DEPTH_TEST_LESS|MSAA)`,
   `submit(0, program)`.
3. `Shutdown` destroys each permutation's handles if its `m_*Init` flag is
   set. Transient buffers self-release.

Permutation-specific bits:

| Method | Layout | Uniforms / textures |
|---|---|---|
| `DrawSmokeSolidQuad` (TL) | `Position(3f)` | `u_solidColor : Vec4` — bound per call (crimson). |
| `DrawSmokeVColorQuad` (BL) | `Position(3f) + Color0(4×u8 norm, ABGR)` | none — fragment is the rasterized vertex color. Corners are `0xFF0000FFu` (red), `0xFF00FF00u` (green), `0xFFFF0000u` (blue), `0xFFFFFFFFu` (white), forming the diagnostic rainbow. |
| `DrawSmokeLitQuad` (BR) | `Position(3f) + Normal(3f) + Color0(4×u8 norm) + TexCoord0(2f)` | `s_texture : Sampler`, `u_lightDir : Vec4` (xyz=world-space light dir, w=ambient), `u_lightColor : Vec4` (rgb=diffuse scale). Per-vertex normals fan outward so the +X directional light produces a visible left-dark → right-bright gradient. Also calls `setTransform(identity)` so `u_model[0]` is a valid identity for the vertex shader's normal transform. |

### CMake permutation loop

Replaces the previous one-pair Phase 5e block:

```cmake
set(BGFX_SHADER_PAIRS triangle solid vcolor tex_lit)
foreach(pair ${BGFX_SHADER_PAIRS})
    bgfx_compile_shaders(TYPE VERTEX   SHADERS vs_${pair}.sc … AS_HEADERS)
    bgfx_compile_shaders(TYPE FRAGMENT SHADERS fs_${pair}.sc … AS_HEADERS)
endforeach()
```

Each pair emits three headers per stage (metal/glsl/essl on macOS) for a
total of **24 generated headers** under `<build>/Core/GameEngineDevice/Source/BGFXDevice/shaders_gen/`.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK. 24 shader headers generated across 8 `.sc` files × 3 profiles. `libcorei_bgfx.a` links. |
| `cmake --build build_bgfx --target tst_bgfx_clear` | OK (regression — still builds). |
| `cmake --build build_bgfx --target tst_bgfx_triangle` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_uber` | OK. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `BgfxBackend::Init: Metal (800x600, windowed)` → `BgfxBackend::Shutdown: complete` → `tst_bgfx_clear: PASSED`. |
| `./tests/tst_bgfx_triangle/tst_bgfx_triangle` | Textured triangle in top-right quadrant for ~2s → `tst_bgfx_triangle: PASSED`. |
| `./tests/tst_bgfx_uber/tst_bgfx_uber` | Four quadrants visible: TL crimson, TR magenta/yellow checker, BL rainbow, BR lit white/gray checker with horizontal gradient → `tst_bgfx_uber: PASSED`. |
| `cmake-build-release` reconfigure with `RTS_RENDERER=dx8` | OK. DX8 game targets still configure — no regression. |

Static sweep:

| Pattern | Result |
|---|---|
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero (unchanged from 5a–5e). |
| `bgfx::` calls in `Core/Libraries/Source/WWVegas/WW3D2/` | Zero. |
| `IRenderBackend::Set_*` / `Draw_Indexed` / `Draw` as non-stub | Zero (unchanged — still Phase 5g). |

## Deferred to later Phase 5 stages

| Item | Stage |
|---|---|
| Wire `IRenderBackend::Set_Shader` to pick among the 4 programs via ShaderClass bits | 5g |
| Wire `Set_Texture` / `Set_Material` / `Set_Light` / `Set_*_Transform` into the uber-shader uniform layout | 5g |
| Real `Draw_Indexed` via `VertexBufferClass` / `IndexBufferClass` | 5g |
| `ShaderClass` → `BGFX_STATE_*` translator (blend src/dst, depth cmp/write, cull, alpha test) | 5g |
| Multi-stage texture / detail-color / detail-alpha funcs (top 5 DX8 FVFs use 2 stages) | 5h |
| Terrain + water-specific shader rewrite | 5h |
| DDS loading via `bimg` | 5i |
| Windows profile set (`s_5_0`, `s_6_0`) once a Windows dev host is online | 5g |
| Bump past bgfx v1.143.9216-529 and drop the two `bgfxToolUtils.cmake` patches | any 5x |
