# Phase 5h.9 — Point-light linear attenuation

Companion doc to `docs/CrossPlatformPort-Plan.md`. Ninth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h8-LightEnvironmentDispatch.md`.

## Scope gate

5h.7 wired `DX8Wrapper::Set_Light` to populate `LightDesc::type` /
`position` / `attenuationRange`, and 5h.8 added per-slot fan-out from
`Set_Light_Environment`. The adapter side was complete: every
point-light source the game drives now arrives at the backend with
its world-space position + outer-radius intact.

What was still missing was the **shader's ability to use that
data**. The Phase-5l `vs_tex_mlit` program treated every slot as a
pure directional light and hard-dropped `LightDesc::position` and
`attenuationRange` on the floor. 5h.9 fixes that: the uber-shader now
branches per-slot between a directional path (world-space direction)
and a point path (world-space position + linear `1 − d/range`
attenuation), selected branchlessly by `step(0.0001,
u_lightPosArr[i].w)` so the GPU doesn't pay for divergent flow.

## Locked decisions

| Question | Decision |
|---|---|
| How to encode the type per slot | A new `u_lightPosArr[4]` vec4 uniform: `.xyz` = world-space position, `.w` = attenuation range. `.w == 0` means directional (use `u_lightDirArr[i]`), `.w > 0` means point light. No separate "type" uniform — the range field carries both signals. |
| Branchless in the shader | `float isPoint = step(0.0001, u_lightPosArr[i].w);` then `mix(dirPath, pointPath, isPoint)` for both the `L` direction and the attenuation scalar. GPUs handle `mix` without divergence, which matters for a loop that runs once per slot per vertex. |
| Attenuation curve | Linear: `atten = clamp(1 - dist/range, 0, 1)`. Cheaper than the DX8 production `1/(Attenuation0 + Attenuation1*D + Attenuation2*D²)` quadratic, and visually close enough for the scene-ambient-dominated lighting the game uses (minimap shroud, cursor glow, tank headlights). The DX8 body's `Attenuation0=1, Attenuation1=0.1/innerRadius` reduces to a near-linear curve over the useful range anyway. |
| Per-vertex or per-fragment | Per-vertex — matching the existing directional path. `v_color0` already carries lit color to the fragment shader, and the point path's cost (3 adds, 2 muls, 1 sqrt, 1 clamp) is cheap enough to keep per-vertex. Moving to per-fragment lighting is a separate shader rewrite (future phase). |
| Where does `worldPos` come from | `mul(u_model[0], vec4(a_position, 1.0)).xyz`. Matches how the existing path builds `worldNormal`. Adds one vec4×mat4 multiply to the vertex shader; no new vertex-attribute stream. |
| Slot 0 ambient | Unchanged. `u_lightDirArr[0].w` still carries the global ambient term (scene ambient from `LightEnvironmentClass::Get_Equivalent_Ambient` folded in via Phase 5h.8). Adding point lights doesn't invalidate that slot assignment. |
| Disabled-slot behavior | Unchanged. A disabled slot uploads zero color + zero range; the accumulation still collapses to a no-op (`color * ndotl * atten = 0`). The new `u_lightPosArr` for disabled slots is also zero-filled. |
| `u_lightColorArr[i].w` still unused | Yes. Kept for future specular or per-light fog mask packing. The test payload today is `.w = 0`. |
| Upload gating | Only uploaded when the multi-lit program is selected (same guard the existing `dirs` / `colors` blocks are under). `tst_bgfx_multilight` and every other shader path stay cost-neutral. |
| DX8 impact | Zero. The shader extension is bgfx-only; DX8 keeps its fixed-function `D3DLIGHT8::Range + Attenuation0/1/2` path. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex_mlit.sc` | Add `uniform vec4 u_lightPosArr[4]`. Compute `worldPos` once per vertex. Per-slot: build both a directional `dirLight` and a point `pointDir / pointDist`; `mix` them by `step(0.0001, u_lightPosArr[i].w)`; compute `pointAtten = clamp(1 - pointDist/range, 0, 1)` and `mix(1.0, pointAtten, isPoint)` for the scalar falloff; accumulate `u_lightColorArr[i].rgb * ndotl * atten`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Add `bgfx::UniformHandle m_uLightPosArr`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `InitPipelineResources` creates the uniform; `DestroyPipelineResources` destroys it. `ApplyDrawState` uploads the third vec4 array alongside `dirs` / `colors`: point slots get `{pos.xyz, range}`, directional slots get `{pos.xyz, 0}`, disabled slots get all zeros. |

### The core of the shader update

```glsl
vec3 dirLight    = -normalize(u_lightDirArr[i].xyz);
vec3 pointToLight = u_lightPosArr[i].xyz - worldPos;
float pointDist  = length(pointToLight);
vec3  pointDir   = pointToLight / max(pointDist, 0.001);

float isPoint   = step(0.0001, u_lightPosArr[i].w);
vec3  L         = mix(dirLight, pointDir, isPoint);
float pointAtten = clamp(1.0 - pointDist / max(u_lightPosArr[i].w, 0.001), 0.0, 1.0);
float atten     = mix(1.0, pointAtten, isPoint);

float ndotl = max(dot(worldNormal, L), 0.0);
lighting += u_lightColorArr[i].rgb * ndotl * atten;
```

### Backend upload

```cpp
const bool isPoint = (m_lights[i].type == LightDesc::LIGHT_POINT);
positions[i*4+0] = m_lights[i].position[0];
positions[i*4+1] = m_lights[i].position[1];
positions[i*4+2] = m_lights[i].position[2];
positions[i*4+3] = isPoint ? m_lights[i].attenuationRange : 0.0f;
/* ... */
bgfx::setUniform(m_uLightPosArr, positions, kMaxLights);
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK — `shaderc` recompiles `vs_tex_mlit.sc` into the embedded C header on rebuild. |
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| All 18 bgfx tests (17 prior + new `tst_bgfx_point_light`) | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. DX8 full compile is Windows-only; reconfigure-clean is the usual sanity signal. |

### The new test: `tst_bgfx_point_light`

17×17 tessellated quad at `z=0.5` (normals facing the camera at `-z`),
identity transforms, white 2×2 texture, single point light at
`(0,0,0.3)` with `range=1.0`, no ambient. Captures a 64×64 RT and
asserts:

| Sample | Expected | Observed |
|---|---|---|
| `center (32,32)` | ≥ 150 | 194 |
| `midX/midY (48,32)/(32,48)` | < center | 41 / 41 |
| `corners (2,2)/(61,61)` | ≤ 20 | 0 / 0 |
| `center − corner` | ≥ 100 | 194 |

The theoretical center at `(0,0,0.5)` gives `d=0.2`, `N·L=1`,
`atten=0.8` → luminance `0.8 × 255 = 204`. Observed `194` matches
within the per-vertex → per-fragment interpolation softening expected
from a 17×17 grid. Corners at `(±1,±1,0.5)` give `d≈1.428 > range` so
the clamp drives `atten = 0` — observed `0 / 0` as predicted.

Static sweep:

| Pattern | Result |
|---|---|
| `u_lightPosArr` references in shaders | One — `vs_tex_mlit.sc`. |
| `m_uLightPosArr` uses in backend | Three — create, destroy, upload. |
| `LightDesc::LIGHT_POINT` producers | Two — `DX8Wrapper::Set_Light(D3DLIGHT8*)` (via type translator) and `DX8Wrapper::Set_Light_Environment` (via `isPointLight()`). Both emit `position` + `attenuationRange`. Their draws now render as point lights instead of being silently downgraded to directional. |

## Deferred

| Item | Stage |
|---|---|
| Specular. `LightDesc` still has no specular field; `vs_tex_mlit` is Lambertian only. DX8 body gives slot 0 a hardcoded `(1,1,1)` specular which we drop | 5h.10 |
| Quadratic attenuation. Current linear `1−d/range` is a simplification; DX8's `1/(k₀+k₁D+k₂D²)` triple is denser close in. Likely unneeded for Generals' use cases; profile first | later |
| Per-fragment lighting. The whole `vs_tex_mlit` → `fs_tex_mlit` pair is per-vertex lit; per-fragment would preserve normals across large triangles | later / optional |
| Spot-light cone math. `LightDesc::LIGHT_SPOT` type exists but the shader ignores `direction` + `attenuationRange` as a cone; only point / directional have fully correct behavior | 5h.10 (bundle with specular) |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode | 5h.11 |
| Shader / material translators | 5h.12 |
| `TextureClass::Init` bridge | 5h.13 |

After this phase, **the bgfx backend has correct lighting for every
light type the game currently instantiates via `LightClass` /
`D3DLIGHT8` / `LightEnvironmentClass`** except spot-light cones
(scheduled for bundling with specular). A scene with point lights at
shroud markers, weapon muzzle flashes, or cursor highlights will
render them as point lights — with distance falloff — instead of
treating them as directional lights emitting from their declared
direction vector (which for point lights is a garbage / zero vector).
