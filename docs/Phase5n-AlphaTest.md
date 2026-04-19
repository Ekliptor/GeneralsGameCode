# Phase 5n — Cutout alpha-test via fragment `discard`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirteenth stage of
Phase 5 (Renderer bgfx backend). Follows `Phase5m-DynamicVBIB.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5n slots
in before it to fill a specific ShaderClass gap: the `ALPHATEST_ENABLE`
renderstate that Generals uses for foliage, fences, damage decals, muzzle
flashes, and every sprite that wants anti-aliased edges against arbitrary
backgrounds.)

## Objective

Give `IRenderBackend` callers a way to discard fragments below an alpha
threshold, without adding a new shader permutation and without touching
any blend state. One new POD field (`ShaderStateDesc::alphaTestRef`), one
new uniform (`u_cutoutRef`), three-line shader inserts in the three
textured fragment shaders. When the caller leaves `alphaTest == false`,
the uniform goes to zero and the `discard` branch is never taken (alpha
is always non-negative), so existing callers see zero behavioral change.

Fixed-function `ALPHATEST` in DX8 does the compare on the GPU's
pre-rasterizer alpha reference state. Modern GL / Metal / D3D12 don't
have that stage anymore; the industry-standard port is a `discard` in
the fragment shader driven by a uniform. 5n does exactly that.

## Locked decisions

| Question | Decision |
|---|---|
| HW alpha-test or shader discard | Shader `discard` with a per-draw `u_cutoutRef.x`. bgfx has a `BGFX_STATE_ALPHA_REF(r)` in its state mask and a corresponding predefined `u_alphaRef4` uniform — but on modern backends that reduces to the same shader-side discard, and it doesn't integrate with the backend's uber-shader selector (the shaders would still need the `discard` line). Doing it ourselves keeps the discard visible in source. |
| Uniform name | `u_cutoutRef`, **not** `u_alphaRef`. Discovered the hard way: bgfx ships a predefined uniform literally named `u_alphaRef4`, and shaderc's uniform scanner collapses anything starting with `u_alphaRef` into that name. Our first attempt at `u_alphaRef` produced a spectacularly malformed preprocessor output (`uniform vec4 u_alphaRef4.x;`) because the scanner was treating `u_alphaRef.x` as a fresh identifier distinct from `u_alphaRef`. Renaming to `u_cutoutRef` sidesteps the collision entirely. |
| Which programs get the discard | `fs_triangle` (the single-stage textured program), `fs_tex_mlit` (multi-lit), and `fs_tex2` (multi-stage modulate). `fs_solid` and `fs_vcolor` are intentionally untouched — neither samples a texture, so there's no meaningful alpha source to cut against. |
| Threshold default | `0.5f`. Matches Generals' `D3DRS_ALPHAREF = 128` for foliage / fence / cutout sprites, which is what `TheW3DShaderManager` writes when alpha-test is enabled. |
| Uniform upload gate | `ApplyDrawState` uploads `u_cutoutRef` only when the selected program is one of the three textured ones. Avoids bgfx validation warnings about binding a uniform to a program that doesn't declare it. |
| Disabled-alpha-test encoding | `u_cutoutRef.x = 0.0`. Since alpha is always `>= 0`, `if (a < 0) discard;` is unreachable — the GPU's branch predictor will mark it cold and the fragment shader runs exactly as the pre-5n version did. No dynamic shader recompile, no state mask mutation. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | Append `float alphaTestRef = 0.5f;` to `ShaderStateDesc`. The existing `alphaTest` bool gates it. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare `m_uCutoutRef`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Create `u_cutoutRef` in `InitPipelineResources`; destroy in `DestroyPipelineResources`. In `ApplyDrawState`, upload `(alphaTest ? alphaTestRef : 0.0)` as `u_cutoutRef.x` when the selected program is one of `m_progTex` / `m_progTexMLit` / `m_progTex2`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_triangle.sc` | Add `uniform vec4 u_cutoutRef;` and the discard before the final `gl_FragColor` write. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_tex_mlit.sc` | Same — sample → discard → modulate-by-vertex-color → output. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_tex2.sc` | Same — sample both stages → modulate → discard → output. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_alphatest)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_alphatest/main.cpp` | Same 24-vertex cube as the texture tests, but the 16×16 RGBA8 pattern is 4×4-block-checkerboard alternating fully-opaque orange and fully-transparent (RGBA `00 00 00 00`). `ShaderStateDesc::alphaTest = true`, `alphaTestRef = 0.5`, `cullEnable = false`. Result: holes punched through every face, back-face visible through front-face holes, depth buffer updated correctly for the opaque texels. |
| `tests/tst_bgfx_alphatest/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5n-AlphaTest.md` | This doc. |

### Shader insert (identical in all three files)

```glsl
uniform vec4 u_cutoutRef;  // .x = threshold; 0 disables

// Inside main(), right after sampling:
vec4 c = /* sample expression */;
if (c.a < u_cutoutRef.x) discard;
/* then write c (or c * vertex color) to gl_FragColor */
```

### Backend upload block (inside `ApplyDrawState`)

```cpp
const bool isTexturedProg = (prog.idx == m_progTex.idx)
                         || (prog.idx == m_progTexMLit.idx)
                         || (prog.idx == m_progTex2.idx);
if (isTexturedProg)
{
    const float ar = m_shader.alphaTest ? m_shader.alphaTestRef : 0.0f;
    const float cutoutRef[4] = { ar, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(m_uCutoutRef, cutoutRef);
}
```

## The shaderc naming pitfall

`u_alphaRef` looked like the natural name. It collides with bgfx's
predefined uniform `u_alphaRef4` (see `bgfx/src/bgfx.cpp` predefined-uniform
table). shaderc's uniform scanner couldn't disambiguate, and produced
both of these in its pre-compile GLSL:

```
uniform vec4 u_alphaRef4;        // correct
uniform vec4 u_alphaRef4.x;      // wrong — syntax error
```

…plus substituted every bare `u_alphaRef` in the source with
`u_alphaRef4.x`, corrupting even the assignment `vec4 aref = u_alphaRef;`.
Renaming to any non-`u_alphaRef*` identifier fixes it outright. A future
generalization of the uber-shader system should avoid the reserved-prefix
list entirely; `u_cutoutRef` is an instance, not a theme.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — the three textured fragment shaders recompile (9 `.sc.bin.h` regenerated: 3 shaders × 3 profiles). |
| `cmake --build build_bgfx --target tst_bgfx_alphatest` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic,alphatest}` (all 10) | OK. |
| `./tests/tst_bgfx_alphatest/…` | Spinning cube with 4×4 checker of opaque orange and fully-transparent holes; back face visible through front face holes; `tst_bgfx_alphatest: PASSED`. |
| `./tests/tst_bgfx_texture/…`, `tst_bgfx_multilight/…`, etc. | All 9 prior tests PASSED — the shader inserts default to zero threshold, so they render identically to pre-5n output. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `u_alphaRef` anywhere in our source (should be zero after the rename) | Zero — only `u_alphaRef4` in bgfx's own predefined table remains. |
| Shader programs without `u_cutoutRef` that the backend tries to upload it to | Zero — `ApplyDrawState` gates on program identity. |

## Deferred

| Item | Stage |
|---|---|
| Alpha-to-coverage (MSAA-aware smooth cutout edges) — replaces the binary `discard` with a coverage-sampled fade. Needs `BGFX_STATE_A2C` + a shader path that emits the raw alpha instead of discarding; separate permutation if we want both at once | later |
| Bgfx's native `u_alphaRef4` via `BGFX_STATE_ALPHA_REF(r)` state mask — skips the per-draw uniform upload in exchange for a state-mask bit. Worth measuring once 5h surfaces a caller that's actually alpha-test-bottlenecked | later |
| Alpha-test + two-stage lit (`fs_tex2_mlit`) — the combination that doesn't exist yet. Straightforward shader permutation if 5h surfaces a caller | 5h+ |
| Separate compare functions beyond `LESS` (D3D allows `GREATER`, `EQUAL`, etc.). All observed Generals call sites use `GREATEREQUAL` with a reference, which is equivalent to our `if (a < ref) discard;` — no other compare functions needed yet | later |
| DX8Wrapper → IRenderBackend adapter (`ShaderClass::Set_Alpha_Test(enable)` + `D3DRS_ALPHAREF` both map into `ShaderStateDesc.alphaTest` / `alphaTestRef`) | 5h |
