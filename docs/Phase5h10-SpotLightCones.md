# Phase 5h.10 — Spot-light cones

Companion doc to `docs/CrossPlatformPort-Plan.md`. Tenth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h9-PointLightAttenuation.md`.

## Scope gate

5h.9 closed the point-light gap: lights with a world-space position
and a positive attenuation range now fall off linearly in the shader
with per-vertex `clamp(1 − d/range, 0, 1)`. Spot lights still landed
as point lights — the cone angle the game sent through
`D3DLIGHT8::Theta/Phi` or `LightClass::SpotAngle` was discarded.

5h.10 closes that gap. A new uniform `u_lightSpotArr[4]` carries
spot-cone direction + outer-cone cosine per slot; the inner cosine
rides on the previously-unused `u_lightColorArr[i].w` channel. The
shader multiplies the existing Lambertian × attenuation term by a
`smoothstep(outer, inner, dot(spotDir, -L))` cone mask. A `< 0`
outer-cos value disables the mask entirely, so directional + point
lights stay cost-neutral. DX8Wrapper's two `Set_Light` translators
populate the spot fields from `D3DLIGHT8::Theta/Phi` (DirectX full
cone angles, radians) and `LightClass::Get_Spot_Angle_Cos`.

Specular remains deferred — it needs a `MaterialDesc` extension
(specular color + shininess) and a Blinn-Phong path in the fragment
shader. That's 5h.11.

## Locked decisions

| Question | Decision |
|---|---|
| Encoding of "is this a spot" | `u_lightSpotArr[i].w < 0` means not a spot. Directional + point lights upload `w = -1`. Uses the same sentinel pattern as 5h.9's `u_lightPosArr[i].w = 0` for directional vs point. |
| Where does inner-cone cos live | Packed into `u_lightColorArr[i].w` (previously unused — reserved for future specular mask). Saves adding a fifth vec4 uniform array per slot. The shader only reads it when the slot's outer cos is ≥ 0, so non-spot slots upload whatever (zero). |
| Cone mask shape | `smoothstep(outerCos, innerCos, dot(spotDir, -L))`. Hard cutoff at outer cone; fully lit at inner; smooth fade between. Matches the `smoothstep` convention other Generals-era engines use for soft-edge spots. |
| Cone direction normalization | `normalize(u_lightSpotArr[i].xyz)` in the shader. Saves callers the obligation to send a unit vector and keeps the adapter's job simple (just forward the raw direction from LightClass / D3DLIGHT8). |
| D3DLIGHT8 Theta/Phi convention | Full cone angles in radians (per DirectX 8 docs: "the fully illuminated cone"). Translator uses `cos(Theta/2)` for inner and `cos(Phi/2)` for outer. |
| LightClass has only one cone angle | `Get_Spot_Angle_Cos()` is a half-angle cosine. Use it as outer; derive inner as `min(outer + 0.04, 0.9999)` (~5° soft edge). Hardcoded softness is a pragmatic choice — LightClass doesn't expose an "inner" concept, and the game seldom uses spots that need a specific soft-edge width. |
| Spot exponent | D3DLIGHT8's `Falloff`/`SpotExponent` is ignored. The `smoothstep` soft-edge is visually close enough for Generals' use cases (minimap shroud, vehicle spotlights); a dedicated `pow(cos, exp)` path could be added later without an interface break. |
| Uniform upload cost | `u_lightSpotArr` only uploaded when the multi-lit program is selected — same guard the other 3 light arrays live under. Cost is a 64-byte push per draw that uses lighting. |
| DX8 impact | Zero. Spot handling is bgfx-only; DX8's fixed-function pipeline already handles `D3DLIGHT8` spot fields through `SetLight`. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | `LightDesc`: add `spotDirection[3]`, `spotInnerCos`, `spotOuterCos`. Defaults (`spotOuterCos = -1`) make a fresh `LightDesc` not-a-spot. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex_mlit.sc` | Add `uniform vec4 u_lightSpotArr[4]`. Compute per-slot `coneMask = mix(1.0, smoothstep(outerCos, max(innerCos, outerCos+0.001), dot(spotDir, -L)), step(0.0, outerCos))`. Multiply into `lighting` accumulator. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Add `bgfx::UniformHandle m_uLightSpotArr`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `InitPipelineResources` creates the uniform; `DestroyPipelineResources` destroys it. `ApplyDrawState` uploads the fourth vec4 array; spot slots write `{sDir.xyz, outerCos}` and pack `spotInnerCos` into `colors[i*4+3]`; non-spot slots write `spots[i*4+3] = -1`. Enabled `isPointOrSpot` check widens the position path to include spots (they carry `position` + `attenuationRange` too). |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx `Set_Light(D3DLIGHT8*)`: when `Type == D3DLIGHT_SPOT`, populate `spotDirection = Direction`, `spotInnerCos = cos(Theta/2)`, `spotOuterCos = cos(Phi/2)`. bgfx `Set_Light(LightClass&)`: when `Get_Type() == SPOT`, populate from `Get_Spot_Direction` + `Get_Spot_Angle_Cos`, deriving inner as `min(outer+0.04, 0.9999)`. Added `#include <algorithm>` for `std::min`. |

### The shader's new cone block

```glsl
float isSpot    = step(0.0, u_lightSpotArr[i].w);
vec3  spotDir   = normalize(u_lightSpotArr[i].xyz);
float spotCos   = dot(spotDir, -L);
float outerCos  = u_lightSpotArr[i].w;
float innerCos  = u_lightColorArr[i].w;
float spotMask  = smoothstep(outerCos, max(innerCos, outerCos + 0.001), spotCos);
float coneMask  = mix(1.0, spotMask, isSpot);

lighting += u_lightColorArr[i].rgb * ndotl * atten * coneMask;
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK — shaderc recompiles `vs_tex_mlit.sc`. |
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| All 19 bgfx tests (18 prior + `tst_bgfx_spot_light`) | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

### The new test: `tst_bgfx_spot_light`

33×33 tessellated quad at `z=0.5` (normals facing the camera),
identity transforms, white 2×2 texture, single spot light at origin
pointing at +Z, `range=2`, outer half-angle ~41° (cos=0.75), inner
~35° (cos=0.82). Captures a 64×64 RT and asserts:

| Sample | Expected | Observed |
|---|---|---|
| `center (32,32)` | ≥ 150, theory ≈ 191 | 190 |
| `quarter-out (16,32)` | ≤ 20 (outside cone) | 0 |
| `corners (2,2) / (61,61)` | ≤ 5 | 0 / 0 |
| `center − quarter` | ≥ 120 | 190 |

Theoretical center: `N·L = 1`, `atten = 1 − 0.5/2 = 0.75`, `mask = 1`
→ `0.75 × 255 = 191`. Observed 190 — within 1 LSB. Quarter-out pixel
at NDC `(−0.484, 0.016, 0.5)` gives `cos(angle to cone axis) = 0.72`,
below the 0.75 outer cutoff → mask = 0, observed 0.

Static sweep:

| Pattern | Result |
|---|---|
| `u_lightSpotArr` references in shaders | One — `vs_tex_mlit.sc`. |
| `m_uLightSpotArr` uses in backend | Three — create, destroy, upload. |
| `LightDesc::LIGHT_SPOT` producers | Two — both `DX8Wrapper::Set_Light` overloads. `LightEnvironmentClass` never emits SPOT (it collapses to DIRECTIONAL / POINT), so `Set_Light_Environment` doesn't need a spot code path. |
| `LightDesc::spotOuterCos` default value | `-1.0f` — a fresh descriptor is not a spot. Consumers get "non-spot" for free. |

## Deferred

| Item | Stage |
|---|---|
| Specular. `LightDesc` still has no specular field; `vs_tex_mlit` is Lambertian only. Needs `MaterialDesc::specularColor` + `specularPower` + a Blinn-Phong fragment path | 5h.11 |
| Spot exponent / power falloff. D3DLIGHT8's `Falloff` + `SpotExponent` are ignored; smoothstep is the substitute | later, optional |
| Quadratic attenuation. Still linear; unlikely to matter for Generals | later |
| Per-fragment lighting | later, optional |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode | 5h.12 |
| Shader / material translators | 5h.13 |
| `TextureClass::Init` bridge | 5h.14 |

After this phase, **every light type the game can construct
(`LightClass::POINT / DIRECTIONAL / SPOT`, `D3DLIGHT8::DIRECTIONAL /
POINT / SPOT`) renders with its correct geometric shape in the bgfx
backend**. The lighting subsystem is feature-complete except for
specular highlights — which most of Generals' materials don't use
anyway, but which lands in 5h.11 before the draw-path slice.
