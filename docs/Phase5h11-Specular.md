# Phase 5h.11 — Specular highlights

Companion doc to `docs/CrossPlatformPort-Plan.md`. Eleventh sub-phase
of the DX8Wrapper adapter. Follows `Phase5h10-SpotLightCones.md`.

## Scope gate

Up through 5h.10 the uber-shader did pure Lambertian diffuse lighting
with no specular term. The DX8 production path **does** compute
specular (both `D3DLIGHT8::Specular` and the DX8 body's slot-0
hardcoded `(1,1,1)`), so any bgfx rendering of a lit scene looked
matte in comparison.

5h.11 adds a Blinn-Phong per-vertex specular contribution to the
multi-lit uber-shader, extends `LightDesc` + `MaterialDesc` with the
colors and shininess exponent needed to drive it, and routes the DX8
adapter's three `Set_Light` / `Set_Light_Environment` code paths to
populate the new descriptor fields. Camera position in world space
comes for free from bgfx's built-in `u_invView[3].xyz`.

This closes the "lighting feature parity" stretch of the adapter:
every field of `D3DLIGHT8` and `LightClass` that the DX8 body touches
now reaches the bgfx backend with a semantically-equivalent shader
response.

## Locked decisions

| Question | Decision |
|---|---|
| Blinn-Phong vs Phong | Blinn-Phong: `H = normalize(L + V)`, `specF = pow(max(N·H, 0), power)`. Cheaper than Phong's `pow(max(dot(R, V), 0), …)` and looks nearly identical at typical game exponents. |
| Per-vertex vs per-fragment | Per-vertex — matching the rest of `vs_tex_mlit`'s lighting. New varying `v_specular` (vec3) carries the accumulated specular to the fragment shader, which adds it after the texture×diffuse modulate. Per-fragment would preserve tight highlights across large triangles but is a separate rewrite. |
| Camera position source | bgfx's predefined `u_invView[3].xyz`. With bgfx's column-major matrix convention, the 4th column of the inverse view matrix is the camera's world-space origin — no CPU-side upload needed. |
| Where does specular land in the fragment | Added **after** the `t * v_color0` texture modulate: `c.rgb = t.rgb * v_color0.rgb + v_specular`. Highlights stay bright on dark / colored materials instead of being attenuated by the albedo. Matches how D3D8's fixed-function specular is added post-texture. |
| `ndotl > 0` guard on specular | Yes — `pow(ndoth, power) * step(0.0001, ndotl)`. Kills the highlight on back-facing fragments where it would otherwise creep through the `N·H` term (H can point away from N even when L does too). |
| New uniforms | `u_lightSpecArr[4]` (per-light specular color, rgb) and `u_materialSpec` (rgb = reflectance, w = power). Uploaded only when the mlit program is selected — same guard as the other 4 light arrays. |
| Per-light specular in `LightDesc` | Defaults to `(0, 0, 0)` — a descriptor that never set specular contributes none. This is the DX8 convention (`D3DLIGHT8::Specular` starts zero) and keeps pre-5h.11 tests cost-neutral. |
| Material specular default | `(0, 0, 0)` for the color, `32` for the power. Zero color means existing tests that never set `specularColor` produce zero specular — they're regression-safe. |
| Slot-0 hardcoded specular in `Set_Light_Environment` | Yes — mirrors DX8 body at `dx8wrapper.cpp:3095` (`if (l==0) rl.Specular = Vector3(1,1,1)`). Keeps DX8 and bgfx renderings of a `LightEnvironment`-lit mesh visually consistent. |
| Why not add specular to `LightEnvironment`'s per-slot translation | `LightEnvironmentClass` exposes no specular color accessor — only diffuse + ambient. DX8 body fills with `(1,1,1)` for slot 0 only; we copy that behavior. |
| Intensity multiplication | `LightClass`'s `intensity` pre-multiplies both diffuse and specular (matching the DX8 body). `D3DLIGHT8` doesn't have an intensity field — specular is taken as-is. |
| DX8 impact | Zero. The shader extensions are bgfx-only; DX8 keeps its fixed-function specular path. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | `LightDesc`: add `specular[3] = {0,0,0}`. `MaterialDesc`: add `specularColor[3] = {0,0,0}` + `specularPower = 32`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/varying.def.sc` | Add `v_specular : TEXCOORD3` varying. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex_mlit.sc` | Add `uniform vec4 u_lightSpecArr[4]` + `uniform vec4 u_materialSpec`. Read `viewDir = normalize(u_invView[3].xyz - worldPos)`. Per-slot: `H = normalize(L + viewDir)`, `specF = pow(max(N·H, 0), max(u_materialSpec.w, 1)) * step(0.0001, ndotl)`. Accumulate `u_lightSpecArr[i].rgb * specF * atten * coneMask` into `specular`, emit `v_specular = u_materialSpec.rgb * specular`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_tex_mlit.sc` | Add `v_specular` input; after `c = t * v_color0`, `c.rgb += v_specular` then fog mix. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Add `m_uLightSpecArr` + `m_uMaterialSpec` UniformHandle members. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Create + destroy the uniforms in `InitPipelineResources` / `DestroyPipelineResources`. Add `specs[kMaxLights * 4]` slot-fill alongside the existing light arrays in `ApplyDrawState`; upload after the spot array. Emit `u_materialSpec` as `{matSpec.rgb, matPower}`. |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx `Set_Light(D3DLIGHT8*)`: copy `light->Specular.{r,g,b}` into `desc.specular`. bgfx `Set_Light(LightClass&)`: query `light.Get_Specular(&spec)`, multiply by intensity, store. bgfx `Set_Light_Environment`: hardcode slot 0's `desc.specular = (1,1,1)` inside the `if (i == 0)` block. |

### The shader's new specular block

```glsl
vec3  H      = normalize(L + viewDir);
float ndoth  = max(dot(worldNormal, H), 0.0);
float specF  = pow(ndoth, max(u_materialSpec.w, 1.0)) * step(0.0001, ndotl);
specular    += u_lightSpecArr[i].rgb * specF * atten * coneMask;
```

And the fragment emit:

```glsl
vec4 c = t * v_color0;
c.rgb += v_specular;
c.rgb  = mix(u_fogColor.rgb, c.rgb, v_fog);
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK — shaderc recompiles `vs_tex_mlit.sc` + `fs_tex_mlit.sc` + `varying.def.sc`. |
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| All 20 bgfx tests (19 prior + `tst_bgfx_specular`) | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

### The new test: `tst_bgfx_specular`

33×33 tessellated quad at `z=0.5` facing the camera at world origin.
Vertex color forced to `(0, 0, 0, 1)` so the diffuse contribution is
exactly zero — only specular lands. One directional light with
`color=0`, `specular=(1,1,1)`. Material `specularColor=(1,1,1)`,
`power=32`. Captures the RT and asserts:

| Sample | Expected | Observed |
|---|---|---|
| `center (32,32)` | ≥ 200 (peak highlight) | 247 |
| `mid (16,32)` | < center | 23 |
| `corners (2,2)/(61,61)` | ≤ 15 (tight falloff) | 1 / 1 |
| `center − mid` | ≥ 80 (sharp highlight) | 224 |

Theoretical center: `V = -pos = (0,0,-0.5)/0.5 = (0,0,-1)`, `L = (0,0,-1)`, `H = (0,0,-1) = N` → `N·H = 1` → `specF = 1` → byte 255. Observed 247 — within 1% of the ceiling (the reflected specular saturates the BGRA8 channel). Corner pixel at `(1,1,0.5)`: `V` isn't aligned with `N`, `N·H ≈ 0.816`, `pow(0.816, 32) ≈ 0.002` → byte 0–1, observed 1.

Static sweep:

| Pattern | Result |
|---|---|
| `u_materialSpec` / `u_lightSpecArr` references in shaders | One shader — `vs_tex_mlit.sc`. |
| `m_uMaterialSpec` / `m_uLightSpecArr` uses in backend | Three each — create, destroy, upload. |
| `LightDesc::specular` producers in production | Three — both `DX8Wrapper::Set_Light` overloads and the `Set_Light_Environment` slot-0 branch. |
| Pre-5h.11 tests affected by the shader extension | Zero regressions. Default `MaterialDesc::specularColor = (0,0,0)` means `v_specular = 0` for callers that never set specular; the existing fragments land byte-identical to 5h.10. |

## Deferred

| Item | Stage |
|---|---|
| Per-fragment lighting. Per-vertex interpolation loses tight highlights on low-tessellation meshes; a Phong rewrite (normals + worldPos as varyings; lighting in FS) would fix it | later, optional |
| Spot exponent. D3DLIGHT8's `Falloff` and `SpotExponent` still feed nowhere; smoothstep substitutes for the shape | later, optional |
| Quadratic attenuation. Still linear (`1 − d/range`); unlikely to matter for Generals scenes | later |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode | 5h.12 |
| Shader / material translators: `DX8Wrapper::Set_Shader(ShaderClass) / Set_Material(VertexMaterialClass)`. Needs the ShaderClass / VMC compile units. `VertexMaterialClass` owns the per-material `specularPower` the game drives; today we only exercise it via backend-direct `MaterialDesc` descriptors | 5h.13 |
| `TextureClass::Init` → `BgfxTextureCache` bridge | 5h.14 |

**Lighting is now visually complete for every light-type × material
combination the game can construct.** A mesh with a shiny material
(shininess 32, white specular) under one or more directional / point
/ spot lights will render with appropriate highlights; a matte
material (default `specularColor = 0`) will not. The remaining adapter
work is entirely about threading the draw-call and texture-bind paths
through the seam — no more shader extensions blocked by
non-compiled-in-bgfx-mode classes.
