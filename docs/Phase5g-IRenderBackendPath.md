# Phase 5g — IRenderBackend production draw path

Companion doc to `docs/CrossPlatformPort-Plan.md`. Seventh stage of Phase 5
(Renderer bgfx backend). Follows `Phase5f-UberShaders.md`.

## Objective

Take the four uber-shader programs from Phase 5f and wire them end-to-end
through `IRenderBackend`: cache transforms, translate shader-state descriptors
to `BGFX_STATE_*` masks, translate vertex-layout descriptors to
`bgfx::VertexLayout`, create persistent `bgfx::VertexBufferHandle` /
`IndexBufferHandle`, and pick one of the four programs at `Draw_Indexed` time
from the layout's attribute mask + the material/shader hints.

A new harness `tst_bgfx_mesh` drives the whole path with a spinning lit cube
(24 verts × {pos, normal, color0, uv}, 36 indices). Production game code
**still** calls `DX8Wrapper::*` statics directly — the DX8Wrapper → IRenderBackend
adapter is a later stage (5h / Phase 7).

## Locked decisions

| Question | Decision |
|---|---|
| IRenderBackend vocabulary | **Core-level POD descriptors** (`Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h`): `ShaderStateDesc`, `MaterialDesc`, `LightDesc`, `VertexLayoutDesc`/`VertexAttributeDesc`. Avoids pinning `corei_bgfx` to per-game `ShaderClass` / `VertexMaterialClass` / `FVFInfoClass`, which live in `Generals/` or `GeneralsMD/` with minor enum divergences. The DX8Wrapper adapter that lands later populates these descriptors from the per-game types. |
| Transform signature | **`const float[16]`** rather than `const Matrix4x4&`. WWMath `Matrix4x4` has a transitive include cascade (`matrix3d.h` → `osdep.h`, `matrix3.h` → `wwdebug.h`, …) that would drag a dozen headers into the backend. `Matrix4x4::Row[4]` is already 16 contiguous floats in D3D row-major layout (translation at `[12..14]`), matching bgfx's `setTransform` convention — callers pass `&mtx.Row[0].X`. |
| Texture binding | **Placeholder-only in 5g.** `Set_Texture(stage, void*)` is a no-op; the backend always binds a built-in 2×2 white texture on stage 0. Real `TextureBaseClass` ↔ `bgfx::TextureHandle` integration is Phase 5i (DDS via `bimg`). The stub accepts a `const void*` so the future adapter has a place to pass a texture pointer. |
| Resource lifetime | Persistent `bgfx::VertexBufferHandle` / `IndexBufferHandle` — destroyed on the next `Set_*_Buffer` call or in `Shutdown`. No transient-buffer path on the production route (5f smoke methods still use transient). No cache keyed on `VertexBufferClass*` ID — that's 5h when the DX8Wrapper adapter lands and we need multiple concurrent VBs. |
| Program selector | Driven by the **current layout's attribute-presence mask** + `material.useLighting` + `shader.texturingEnable`. Order: `tex_lit` if (lighting ∧ normal ∧ uv), else `tex` if (texturingEnable ∧ uv), else `vcolor` if (color0), else `solid`. |
| State translator | `BuildStateMask(const ShaderStateDesc&) → uint64_t` at file scope in `BgfxBackend.cpp` (pure function — independent of backend instance so it can be unit-tested or moved to a separate TU later). Handles depth compare, depth/color write, cull (CCW), source/dest blend. Opaque default (`ONE/ZERO` + depth-lequal) emits no blend func to match bgfx's zero-cost path. |
| Single directional light | `Set_Light(0, …)`; index > 0 silently dropped. Phase 5f's `vs_tex_lit.sc` takes one directional light with world-space direction in `u_lightDir.xyz` and scalar ambient in `u_lightDir.w`. Multi-light + point/spot is 5h. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | Core-level PODs: `VertexAttributeDesc`, `VertexLayoutDesc`, `ShaderStateDesc`, `MaterialDesc`, `LightDesc`. Default-constructs to the DX8 "pass-through, opaque, no-blend, depth-lequal, cull-on, no-lighting" state. |
| `tests/tst_bgfx_mesh/main.cpp` | Unit-cube harness: 24 verts × {pos, normal, color0, uv}, 36 indices; full `IRenderBackend` sequence per frame; 3s run; `tst_bgfx_mesh: PASSED`. |
| `tests/tst_bgfx_mesh/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`; links `corei_bgfx` + `SDL3::SDL3`. |
| `docs/Phase5g-IRenderBackendPath.md` | This doc. |

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Re-vocabulary: `Set_Shader(const ShaderStateDesc&)` / `Set_Material(const MaterialDesc&)` / `Set_Light(unsigned, const LightDesc*)` / `Set_Texture(unsigned, const void*)`. Add `Set_Vertex_Buffer(const void* data, unsigned vertexCount, const VertexLayoutDesc&)` + `Set_Index_Buffer(const uint16_t*, unsigned)`. Transforms take `const float[16]` instead of `const Matrix4x4&`. Drops the `Matrix4x4` / `Vector4*` forward decls; only `Vector4` stays (used by `Clear`). |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | New private state: cached matrices, cached descriptors, per-phase-5g shared `ProgramHandle`/`UniformHandle` set, placeholder texture, current VB/IB + layout + attribute mask. `m_pipelineInit` flag + `InitPipelineResources` / `DestroyPipelineResources` helpers. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Replace the Phase 5a stub block with: descriptor→bgfx translators (`BuildStateMask`, `TranslateDepthCmp`, `TranslateBlendFactor`, `TranslateLayout`, `AttrMaskFromLayout`); `InitPipelineResources` lazy-loads the four uber programs + uniforms + placeholder texture; `Set_*_Transform` stash 16 floats; `Set_Shader/Material/Light` stash descriptors; `Set_Vertex_Buffer`/`Set_Index_Buffer` destroy previous handle and create a persistent one from the caller's bytes via `bgfx::copy`; `Draw_Indexed` pushes `setViewTransform` / `setTransform`, picks program via `SelectProgram`, pushes `u_solidColor` / `u_lightDir` / `u_lightColor` / sampler + placeholder texture, then `setState(BuildStateMask) + submit`. `Shutdown` calls `DestroyPipelineResources` before `bgfx::shutdown`. |
| `Dependencies/Utility/CMakeLists.txt` | Bug fix: `core_utility_no_pch` now also adds `Utility/` to its include path on non-Windows, matching `core_utility`. Any non-PCH Core consumer that transitively pulled `matrix3d.h` was silently broken on macOS/Linux — latent since Phase 5d. Discovered while wiring `matrix4.h` into the backend (subsequently dropped). |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_mesh)`. |

### `BgfxBackend` production path — internals

Setup (`InitPipelineResources`, lazy on first `Set_Vertex_Buffer` / `Set_Index_Buffer`):

- Create four programs via `createEmbeddedShader` + `createProgram(vs, fs, destroyShaders=true)` from the Phase 5f `s_embeddedShaders[]` table: `solid`, `vcolor`, `triangle` (tex), `tex_lit`.
- Allocate four `UniformHandle`s: `u_solidColor : Vec4`, `s_texture : Sampler`, `u_lightDir : Vec4`, `u_lightColor : Vec4`.
- Create a 2×2 white RGBA8 placeholder texture (point-sampled, clamp).

Per draw (`Draw_Indexed`):

1. If view/proj dirty, `bgfx::setViewTransform(0, m_viewMtx, m_projMtx)` (D3D row-major, no transpose).
2. `bgfx::setTransform(m_worldMtx)` every draw.
3. `SelectProgram(attrMask, shader, material, progs…)` → one of the four handles.
4. `setUniform(u_solidColor, material.diffuse+opacity)`.
5. If light enabled, `setUniform(u_lightDir, light.direction + ambient)` and `setUniform(u_lightColor, light.color × intensity)`. Otherwise zero-valued to keep the `tex_lit` shader's math defined even if selected.
6. `setTexture(0, u_sampler, placeholder)`.
7. `setVertexBuffer(0, currentVB)` + `setIndexBuffer(currentIB, startIndex, primitiveCount*3)`.
8. `setState(BuildStateMask(shader))`; `submit(0, program)`.

`Draw` (non-indexed) follows the same flow minus `setIndexBuffer`, with `setVertexBuffer(0, vb, startVertex, primitiveCount*3)`.

### `tst_bgfx_mesh` harness

- 24-vertex cube (one quad per face × 6 faces; 4 verts per face share an outward-pointing normal so the lighting is per-face flat). Distinct `abgr` color per face (red / green / blue / yellow / magenta / cyan) so per-face albedo is obvious.
- Plain-C matrix helpers built in the test (no dependency on `bx::` or `WWMath`): `MakeIdentity` / `MakeTranslate` / `MakeRotateX` / `MakeRotateY` / `Mul` / `MakePerspective` (D3D LH, `[0,1]` z) / `MakeLookAt` (LH, world-up Y).
- Each frame: build `world = rotateX(0.35) × rotateY(time × 0.9)`; push to `Set_World_Transform`; `Clear`; `Begin_Scene`; `Draw_Indexed(0, 24, 0, 12)`; `End_Scene`. View/proj + descriptors are set once before the loop.
- Exits after 3s.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK. Uber shaders still compile; new descriptor headers are header-only. |
| `cmake --build build_bgfx --target tst_bgfx_clear tst_bgfx_triangle tst_bgfx_uber tst_bgfx_mesh` | OK. All four targets link. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `tst_bgfx_clear: PASSED` (regression). |
| `./tests/tst_bgfx_triangle/tst_bgfx_triangle` | `tst_bgfx_triangle: PASSED` (regression). |
| `./tests/tst_bgfx_uber/tst_bgfx_uber` | `tst_bgfx_uber: PASSED` (regression — all 4 uber permutations still render). |
| `./tests/tst_bgfx_mesh/tst_bgfx_mesh` | Spinning lit cube visible for 3s with per-face albedo + directional-light shading → `tst_bgfx_mesh: PASSED`. |
| `cmake-build-release` reconfigure (`RTS_RENDERER=dx8`) | OK. DX8 game targets still configure — zero production call sites touched. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only `docs/` mentions remain). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero (unchanged since 5a). |
| `bgfx::` calls in `Core/Libraries/Source/WWVegas/WW3D2/` | Zero. |
| `Matrix4x4` / `ShaderClass` / `VertexMaterialClass` referenced by `corei_bgfx` | Zero. |

## Deferred to later Phase 5 / 7 stages

| Item | Stage |
|---|---|
| DX8Wrapper → IRenderBackend adapter (populate descriptors from per-game `ShaderClass` / `VertexMaterialClass` / `FVFInfoClass`) | 5h or 7 |
| Real texture binding — `TextureBaseClass` → `bgfx::TextureHandle` map, DDS via `bimg` | 5i |
| Persistent VB/IB cache keyed on `VertexBufferClass*` / `IndexBufferClass*` ID | 5h (when adapter lands) |
| Multi-stage texture / detail color/alpha / secondary gradient permutations | 5h |
| Multi-light, point/spot light support | 5h |
| Specular / shininess / emissive in `tex_lit` | 5h |
| Alpha-test ref value plumbed through `setState(… BGFX_STATE_ALPHA_REF(…))` | 5h |
| Terrain + water shader rewrite | 5h |
| Windows profile set (`s_5_0`, `s_6_0`) once a Windows dev host is online | 5g/5h |
| Bump past bgfx v1.143.9216-529 and drop the two `bgfxToolUtils.cmake` patches | any 5x |
