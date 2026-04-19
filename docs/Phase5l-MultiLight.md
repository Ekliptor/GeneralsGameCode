# Phase 5l — Multi-light (4-slot directional accumulation)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Eleventh stage of Phase 5
(Renderer bgfx backend). Follows `Phase5k-MultiStageTextures.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5l slots
in before it so the bgfx backend exposes the multi-light accumulation that
Generals' production shaders rely on — typically sun + fill + rim for unit
meshes, plus a scene-global ambient.)

## Objective

Replace the single-directional-light production path with a 4-slot
accumulation. Each slot is an independent directional light (direction +
color × intensity); slot 0 also carries the scene-global ambient term.
Disabled slots upload as zero-color so the shader's fixed-count loop
contributes nothing for them — no shader permutations.

The `Set_Light(index, desc)` signature already accepts an index from Phase
5g; 5g just silently dropped anything past slot 0. 5l honors all slots up
to `kMaxLights = 4`.

## Locked decisions

| Question | Decision |
|---|---|
| How many slots | 4. Covers Generals' common case (sun + fill + rim + ambient-dir) with headroom. More is cheap in the shader; the cost is 2 × vec4[4] uploads per lit draw. |
| Dynamic permutation or fixed loop | Fixed loop (N=4 hard-coded) with zero-fill for disabled slots. No shader permutations, no extra programs to ship, no selector branching beyond "is it lit at all." The alternative — separate `tex_lit_N` programs compiled for N=1/2/3/4 — was rejected as premature: the extra compile-time targets and selector branches aren't paying for themselves until real shipping profile data says they do. |
| Single-light backwards compat | Retained exactly. The cube tests (`tst_bgfx_mesh`, `tst_bgfx_texture`) set only slot 0; the backend uploads slot 0's descriptor plus three zero-color slots; the shader's accumulation loop collapses to the old single-light result. Visually identical to the 5g/5i output. |
| Program identity | New `tex_mlit` (multi-lit) program replaces `tex_lit` on the production selector path. `m_progTexLit` is retired from production; the Phase 5f `DrawSmokeLitQuad` still uses a separately-owned `m_litProgram` built from the unchanged `vs_tex_lit` / `fs_tex_lit` shader so 5e/5f smoke tests keep their existing behavior. |
| Uniform naming | `u_lightDirArr` + `u_lightColorArr` (deliberately distinct from the `u_lightDir` / `u_lightColor` singletons that the smoke-test path still uses). Keeps the two uniform sets from colliding in bgfx's name-keyed uniform registry. |
| Ambient encoding | Packed into slot 0's `.w` channel of the direction array (shader reads `u_lightDirArr[0].w`). Mirrors the singleton Phase 5g encoding. Slots 1..3's `.w` get uploaded with the same global ambient value but the shader ignores them — no semantic claim there. |
| What a disabled slot looks like on the wire | Direction = (0, -1, 0, globalAmbient); color = (0, 0, 0, 0). The down-pointing direction is just a safe unit vector for `normalize()`; the zero color is what guarantees the loop iteration is a no-op. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Append `tex_mlit` to `BGFX_SHADER_PAIRS`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Include the 6 generated `vs_tex_mlit` / `fs_tex_mlit` headers (metal/glsl/essl). Add `BGFX_EMBEDDED_SHADER(vs_tex_mlit)` and `BGFX_EMBEDDED_SHADER(fs_tex_mlit)` to `s_embeddedShaders`. Replace `m_progTexLit` creation with `m_progTexMLit`; replace `u_lightDir` / `u_lightColor` uniform creation with `u_lightDirArr` / `u_lightColorArr` (Vec4, count = `kMaxLights`). Rewrite `Set_Light` to honor `index` up to `kMaxLights-1`. Extend `SelectProgram` signature to take `texMLit` and route all lit geometry to it. In `Draw_Indexed` and `Draw`, upload `kMaxLights × vec4` arrays with disabled slots zero-filled — only when the selector actually picked `m_progTexMLit`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Add `kMaxLights = 4`. Swap `bool m_lightEnabled; LightDesc m_light;` for the per-slot arrays. Swap `m_progTexLit` for `m_progTexMLit`, `m_uLightDir` / `m_uLightColor` for `m_uLightDirArr` / `m_uLightColorArr`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_multilight)`. |

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex_mlit.sc` | Same input/output signature as `vs_tex_lit.sc`, but iterates 4 light slots via `u_lightDirArr[i]` + `u_lightColorArr[i]` and accumulates `u_lightColor.rgb * max(dot(n, -normalize(dir)), 0)`. Ambient is `vec3_splat(u_lightDirArr[0].w)`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_tex_mlit.sc` | Trivial `texture × v_color0` — identical to `fs_tex_lit.sc` (all lighting is per-vertex in both). |
| `tests/tst_bgfx_multilight/main.cpp` | Lit cube with three directional lights (red from +X, green from +Y, blue from +Z) plus ambient in slot 0; slot 3 left unset to verify zero-fill. Uses a dim grey 16×16 texture so the RGB contributions dominate — faces facing two lights blend to yellow / magenta / cyan. |
| `tests/tst_bgfx_multilight/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5l-MultiLight.md` | This doc. |

### Shader — the entire accumulation

```glsl
vec3 worldNormal = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);

vec3 lighting = vec3_splat(u_lightDirArr[0].w);  // global ambient
for (int i = 0; i < 4; ++i)
{
    float ndotl = max(dot(worldNormal, -normalize(u_lightDirArr[i].xyz)), 0.0);
    lighting += u_lightColorArr[i].rgb * ndotl;
}

v_color0    = vec4(a_color0.rgb * lighting, a_color0.a);
v_texcoord0 = a_texcoord0;
```

No per-slot branch, no `enabled` flag. Zero color for a disabled slot
kills its contribution; a `normalize` on a zero direction would be NaN
and poison the accumulation, so the backend uploads `(0, -1, 0)` for
disabled slots — a safe unit vector.

### Backend upload (identical in Draw and Draw_Indexed)

```cpp
const float globalAmbient = m_lightEnabled[0] ? m_lights[0].ambient : 0.0f;
for (unsigned i = 0; i < kMaxLights; ++i)
{
    if (m_lightEnabled[i])
    {
        dirs[i*4+0..2] = m_lights[i].direction;
        dirs[i*4+3]    = globalAmbient;
        colors[i*4+0..2] = m_lights[i].color * m_lights[i].intensity;
        colors[i*4+3]    = 0;
    }
    else
    {
        dirs[i*4] = {0, -1, 0, globalAmbient};
        colors[i*4] = {0, 0, 0, 0};
    }
}
bgfx::setUniform(m_uLightDirArr,   dirs,   kMaxLights);
bgfx::setUniform(m_uLightColorArr, colors, kMaxLights);
```

Gated on `prog.idx == m_progTexMLit.idx` so unlit submits don't pay the
upload cost.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK — shaderc runs 1 new VERTEX + 1 new FRAGMENT × 3 profiles. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — `libcorei_bgfx.a` embeds `vs_tex_mlit` / `fs_tex_mlit` bytecode. |
| `cmake --build build_bgfx --target tst_bgfx_multilight` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_clear … tst_bgfx_multilight` (all 8) | OK — every test links. |
| `./tests/tst_bgfx_multilight/…` | Cube visible with per-face RGB contributions from three lights + dim grey albedo + ambient; `tst_bgfx_multilight: PASSED`. |
| `./tests/tst_bgfx_mesh/…` / `tst_bgfx_texture/…` | Both still render an identically-shaded cube (single-light regression). |
| All other 5x tests | PASSED (regression). |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `u_lightDir` / `u_lightColor` singletons still reachable from production path | Zero — only the smoke-test `DrawSmokeLitQuad` references them, via `m_litLightDir` / `m_litLightColor`. |

## Deferred

| Item | Stage |
|---|---|
| Point / spot light support — requires per-light position + attenuation uniforms and a type tag in the shader. Slot already has `position[3]` + `attenuationRange` on `LightDesc` so no descriptor churn when it lands | later |
| Per-pixel (vs. per-vertex) lighting pass for high-poly detail meshes | later |
| Specular (Phong / Blinn) — needs eye position + material shininess; adds a second accumulation loop | later |
| Lighting + two-stage textures combined (`tex2_mlit`) — picks up where Phase 5k stopped so lit terrain can modulate a detail map at the same time | 5h+ if the adapter surfaces a real call site |
| DX8Wrapper → IRenderBackend adapter — populate `LightDesc` from `D3DLIGHT8` per-slot (the adapter's `Set_Light_Environment` translates DX8's up-to-8 lights down to our 4 slots, or we bump `kMaxLights`) | 5h |
| Golden-image regression so a multi-light regression in a future shader edit is caught automatically | 5k+ if one appears |
