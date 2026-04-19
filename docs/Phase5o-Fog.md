# Phase 5o — Linear vertex fog

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fourteenth stage of
Phase 5 (Renderer bgfx backend). Follows `Phase5n-AlphaTest.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5o slots
in to close the last ShaderClass renderstate gap Generals uses day-to-day.
Once this lands, the bgfx backend covers every fixed-function state the
game's `TheW3DShaderManager` can set.)

## Objective

Match DX8's `D3DFOG_LINEAR` / `D3DRS_FOGVERTEXMODE` path: per-vertex,
distance-based fog factor computed in the vertex shader, interpolated to
the fragment shader, and used to lerp the final RGB toward a configurable
fog color. No exp / exp² modes yet — the game shipped with linear fog
for its distance-limited view and atmospheric effects, and that's what 5o
implements.

Scope is deliberately wide on the shader side — every one of the five
uber-programs (`solid`, `vcolor`, `tex`, `tex_mlit`, `tex2`) gets the fog
wiring, because fog is a scene-global effect that shouldn't depend on
what material path a draw landed in. Scope is narrow on the API side —
four new `ShaderStateDesc` fields, no new methods.

## Locked decisions

| Question | Decision |
|---|---|
| Mode | Linear only. DX8 supports LINEAR / EXP / EXP2; Generals uses LINEAR and its handful of config knobs all assume LINEAR. Adding EXP² is a one-shader-line change when / if something surfaces a caller. |
| Per-vertex vs. per-pixel | Per-vertex. Matches DX8's fixed-function `D3DRS_FOGVERTEXMODE` behavior exactly; any Generals content authored against DX8 will render identically. Per-pixel fog is a later optimization for huge triangles; not relevant for the game's triangle budget. |
| Distance metric | `abs(viewSpacePos.z)` — i.e. depth along the view direction, not true radial distance. DX8 calls this "Z fog" and it's the default (plane-based) metric; radial ("range-based") fog is a different `D3DRS_RANGEFOGENABLE` path that Generals doesn't enable. |
| Varying | One `float v_fog : TEXCOORD2 = 1.0;` added to `varying.def.sc`. Default of `1.0` means "no fog" (fully clear); a vertex shader that forgets to write it, or isn't recompiled, still renders unfogged. Varying floats cost the same interpolator bandwidth as a vec4 on any modern GPU. |
| Disabled-fog encoding | `u_fogRange.z = 0.0` → the vertex shader's `mix(1.0, fogFactor, enable)` drops to `1.0`; the fragment's `mix(fogColor, finalColor, v_fog)` then selects `finalColor` verbatim. No dynamic recompile, no state-mask bit. |
| Divide-by-zero safety | `max(fogEnd - fogStart, 0.001)` in the vertex shader. Zero-initialized uniforms on a caller that never touches fog would otherwise produce NaN in the factor before the clamp. Cheap, eliminates the footgun. |
| Where fog is applied | In the fragment shader, **after** sampling + alpha-test + any material modulate. Matches DX8 pipeline ordering (fog blend happens post-alphatest, pre-framebuffer-blend). Foliage with an alpha-cut edge therefore fogs only the texels that survive the discard. |
| Uniform upload gate | Unconditional in `ApplyDrawState`. All five uber-programs now declare `u_fogColor` + `u_fogRange`; there's no program that wouldn't use them. The solid / vcolor smoke-path shaders pay one extra `mix` per fragment — trivial; and the common disable-by-default path leaves `v_fog = 1.0` which makes the `mix` a nop. |
| Should smoke-test `tex_lit` also get fog? | No. `tex_lit` / `fs_tex_lit` are unchanged — they're used exclusively by `DrawSmokeLitQuad`, never by production code. Leaving them alone keeps Phase 5e / 5f smoke outputs byte-identical. Production's lit path is `tex_mlit` (Phase 5l); that one does get fog. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | Append `fogEnable`, `fogStart`, `fogEnd`, `fogColor[3]` to `ShaderStateDesc`. Defaults disable fog. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare `m_uFogColor`, `m_uFogRange`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Create / destroy the two uniforms. In `ApplyDrawState`, unconditionally upload `u_fogColor = (rgb, 0)` and `u_fogRange = (start, end, enable ? 1 : 0, 0)`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/varying.def.sc` | Add `float v_fog : TEXCOORD2 = 1.0;`. |
| `Shaders/vs_solid.sc`, `vs_vcolor.sc`, `vs_triangle.sc`, `vs_tex2.sc`, `vs_tex_mlit.sc` | Each declares `uniform vec4 u_fogRange;`, computes `vp = mul(u_modelView, pos)`, `d = abs(vp.z)`, `raw = (fogEnd - d) / max(fogEnd - fogStart, 0.001)`, `v_fog = mix(1.0, clamp(raw, 0, 1), u_fogRange.z)`. |
| `Shaders/fs_solid.sc`, `fs_vcolor.sc`, `fs_triangle.sc`, `fs_tex2.sc`, `fs_tex_mlit.sc` | Each declares `uniform vec4 u_fogColor;` and finishes with `c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog); gl_FragColor = c;`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_fog)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_fog/main.cpp` | 24×24 vertex ground plane from Z=1 (near) to Z=25 (far), tiled pale-yellow / deep-blue checker, fog range 4..18 with warm orange-brown fog color. Clear color matches the fog color so the horizon vanishes seamlessly — the visual payoff is immediate. |
| `tests/tst_bgfx_fog/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5o-Fog.md` | This doc. |

### Vertex-side math (same in all five vs shaders)

```glsl
uniform vec4 u_fogRange;  // .x=start, .y=end, .z=enable (0 or 1)

vec4  vp    = mul(u_modelView, vec4(a_position, 1.0));
float d     = abs(vp.z);
float range = u_fogRange.y - u_fogRange.x;
float raw   = (u_fogRange.y - d) / max(range, 0.001);
v_fog       = mix(1.0, clamp(raw, 0.0, 1.0), u_fogRange.z);
```

### Fragment-side blend (same in all five fs shaders)

```glsl
uniform vec4 u_fogColor;  // rgb = fog color; alpha unused

// ...after whatever sampling / material / alphaTest logic produced `c`...
c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);
gl_FragColor = c;
```

### Backend upload (in `ApplyDrawState`)

```cpp
const float fogColor[4] = { m_shader.fogColor[0], m_shader.fogColor[1], m_shader.fogColor[2], 0 };
const float fogRange[4] = { m_shader.fogStart, m_shader.fogEnd, m_shader.fogEnable ? 1.0f : 0.0f, 0 };
bgfx::setUniform(m_uFogColor, fogColor);
bgfx::setUniform(m_uFogRange, fogRange);
```

No gate on program identity — every production program declares both uniforms.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK — 5 vs + 5 fs shaders recompile × 3 profiles each (metal/glsl/essl) = 30 new `.sc.bin.h` regenerations. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — zero new warnings beyond the three pre-existing AppleClang ones in WWMath. |
| `cmake --build build_bgfx --target tst_bgfx_fog` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic,alphatest,fog}` (all 11) | OK. |
| `./tests/tst_bgfx_fog/…` | Ground plane stretching into distance; near texels pristine yellow / blue checker, far texels blended into the warm fog color; clear color matches so horizon dissolves cleanly; `tst_bgfx_fog: PASSED`. |
| All 10 prior bgfx tests | PASSED — disabled-fog (default) leaves `v_fog = 1.0` → fragment `mix(fogColor, c, 1.0) == c`, so rendering is pixel-identical to pre-5o. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| Vertex-shader branches on fog enable | One, inside `mix(…, enable)` which most drivers compile to a `select` opcode — no real branch. |
| Fragment-shader branches on fog enable | Zero. Disabled-fog is encoded as `v_fog = 1.0` upstream; the fragment unconditionally runs the `mix` and gets the identity. |

## Deferred

| Item | Stage |
|---|---|
| Exponential (`D3DFOG_EXP`) and `D3DFOG_EXP2` fog modes. A one-liner shader-side swap on `raw`: `exp(-density * d)` / `exp(-density * d * density * d)`. Not needed yet; Generals ships LINEAR everywhere | later if a caller surfaces |
| Range-based (radial) fog (`D3DRS_RANGEFOGENABLE`) — swap `abs(vp.z)` for `length(vp.xyz)`. Minimal cost, but Generals' tables all leave this disabled | later |
| Height-based / volumetric fog — fully outside the DX8 envelope; would live in a new shader permutation | later |
| Fog-per-object overrides — individual meshes can override global fog via `D3DRS_FOG*` mid-frame. Our `ShaderStateDesc` already supports per-draw changes because it's per-draw state; adapter just needs to pipe through | 5h |
| Alpha-test + fog interaction for foliage with heavy cutout — works already (fog is applied after the discard), but no dedicated smoke test yet. Combine once 5h surfaces a canonical foliage caller | 5h |
| DX8Wrapper → IRenderBackend adapter — `D3DRS_FOGCOLOR` → `fogColor[3]`, `D3DRS_FOGSTART` / `FOGEND` → `fogStart` / `fogEnd`, `D3DRS_FOGENABLE` → `fogEnable`. Trivial mapping; all four DX8 states have direct ShaderStateDesc fields now | 5h |
