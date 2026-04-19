# Phase 5k — Multi-stage textures (two-sampler modulate)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Tenth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5j-BimgDecoder.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5k slots in
before it so the bgfx backend exposes the two-sampler modulate that the
game's terrain / decals / lightmap passes will route through once 5h wires
production call sites.)

## Objective

Teach the bgfx backend to sample two textures in the same draw — the first
real generalization of the Phase 5i single-stage path. This is the minimum
capability needed by the game's most common fixed-function combine: terrain
base × lightmap, building decal over hull texture, muzzle-flash overlay.

The scope is intentionally narrow: one new uber-shader permutation (`tex2`),
one more sampler uniform, a selector rule that prefers `tex2` when the
caller supplied both UV1 and a stage-1 texture. Lighting stays single-stage
(`tex_lit`); bringing lighting and two-stage together is a later combined
permutation, not this phase.

`BgfxBackend::kMaxTextureStages` was already `2` after 5i but stage 1 was
never actually bound to a shader. 5k makes it do something.

## Locked decisions

| Question | Decision |
|---|---|
| Combine op | Fragment does `base * overlay` (per-component RGBA). Mirrors DX8 fixed-function `D3DTOP_MODULATE` with `D3DTA_TEXTURE` for both args — the default for Generals terrain passes and the only combine most production meshes rely on. Additive / subtract / alpha-blend combines are left to a later permutation (`tex2_add` etc.) if a use case shows up. |
| UV routing | Two independent UV channels (`a_texcoord0`, `a_texcoord1`), both 2 floats. Passed as separate varyings (`v_texcoord0`, `v_texcoord1`). Rationale: lightmaps in Generals typically use a baked second UV set; forcing a single UV would require adding a transform uniform later to replicate the common 2× tile pattern, which is just more runtime state. Two streams is the more faithful DX8 mapping. |
| Selector | `SelectProgram` picks `tex2` when (a) `shader.texturingEnable`, (b) `m_stageTexture[1] != 0`, and (c) both UV0 **and** UV1 are present in the vertex layout's attribute mask. Ordering-wise `tex2` is checked *before* `tex_lit` so a lit-but-multi-stage caller still gets the modulate; lit-multi-stage is out of scope for 5k. |
| Binding discipline | `bgfx::setTexture(1, m_uSampler1, …)` is issued **only** when the selector actually picked `tex2`. Binding a sampler to a program that doesn't declare it triggers a bgfx validation warning; the guard keeps the other programs' frames clean in CI logs. |
| Uniform | New `s_texture1` sampler uniform, created once in `InitPipelineResources` alongside the existing `s_texture`. Program-scoped sampler slots (0 and 1) match the `SAMPLER2D(..., N)` declarations in `fs_tex2.sc`. |
| Handle model | Stage-1 handles reuse the existing `Create_Texture_RGBA8` / `Create_Texture_From_Memory` / `Destroy_Texture` machinery — no new API. `Set_Texture(1, handle)` stores into `m_stageTexture[1]`; `Set_Texture(1, 0)` clears. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/varying.def.sc` | Add `v_texcoord1 : TEXCOORD1` varying and `a_texcoord1 : TEXCOORD1` vertex input. |
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Append `tex2` to `BGFX_SHADER_PAIRS`. The shaderc compile loop picks it up automatically — one VERTEX + one FRAGMENT invocation per platform profile. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `#include` the 6 generated `vs_tex2` / `fs_tex2` headers (metal/glsl/essl on macOS). Register `vs_tex2` / `fs_tex2` in `s_embeddedShaders`. Allocate `m_progTex2` + `m_uSampler1` in `InitPipelineResources`; release in `DestroyPipelineResources`. Extend `SelectProgram` signature with `hasStage1` and a `tex2` program parameter; add the selector rule. In both `Draw_Indexed` and `Draw`, compute `hasStage1` from `m_stageTexture[1]`, pass it to the selector, and guard `bgfx::setTexture(1, m_uSampler1, …)` on `prog.idx == m_progTex2.idx`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare `m_progTex2` (uber-program handle) and `m_uSampler1` (second sampler uniform). |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_multitex)`. |

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/vs_tex2.sc` | Passes `a_texcoord0` + `a_texcoord1` through, transforms position via `u_modelViewProj`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Shaders/fs_tex2.sc` | Samples `s_texture` and `s_texture1`, outputs `base * overlay`. |
| `tests/tst_bgfx_multitex/main.cpp` | Cube with pos + uv0 + uv1 layout. UV0 is per-face 0..1; UV1 is per-face 0..2 (overlay tiles 2× per edge). Uploads a 16×16 orange/teal base and a 16×16 white-with-black-grid overlay. Renders for ~3s. |
| `tests/tst_bgfx_multitex/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5k-MultiStageTextures.md` | This doc. |

### Shader source

`fs_tex2.sc` — the entire combine logic:

```glsl
SAMPLER2D(s_texture,  0);
SAMPLER2D(s_texture1, 1);

void main()
{
	vec4 base    = texture2D(s_texture,  v_texcoord0);
	vec4 overlay = texture2D(s_texture1, v_texcoord1);
	gl_FragColor = base * overlay;
}
```

### Selector rule

```cpp
if (shader.texturingEnable && hasStage1 && hasUV0 && hasUV1)
    return tex2;
if (material.useLighting && hasNormal && hasUV0)
    return texLit;
if (shader.texturingEnable && hasUV0)
    return tex;
if (hasColor0)
    return vcolor;
return solid;
```

`hasStage1` is the only signal that's *not* derivable from the vertex layout
mask — it's the runtime bind state (`m_stageTexture[1] != 0`). A caller that
supplies a UV1 stream but never calls `Set_Texture(1, h)` still gets the
single-stage `tex` program, which is the safer failure mode than sampling
the 2×2 white placeholder as the overlay (which would multiply to the base
verbatim — visually fine, but confuses the selector intent in traces).

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK — `shaderc` runs 2 new VERTEX + 2 new FRAGMENT invocations (×3 profiles = 6 new embedded-header emits). |
| `cmake --build build_bgfx --target corei_bgfx` | OK — `libcorei_bgfx.a` now embeds `vs_tex2`/`fs_tex2` bytecode for metal/glsl/essl. |
| `cmake --build build_bgfx --target tst_bgfx_multitex` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_clear tst_bgfx_triangle tst_bgfx_uber tst_bgfx_mesh tst_bgfx_texture tst_bgfx_bimg tst_bgfx_multitex` | OK — all seven link. |
| `./tests/tst_bgfx_multitex/…` | Cube visible with orange/teal checker × tiled black-grid overlay; `tst_bgfx_multitex: PASSED`. |
| `./tests/tst_bgfx_clear/…` through `tst_bgfx_bimg` | All six prior tests still PASSED (regression). |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` / `#include.*<bimg` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| New bgfx API surface | None — existing `Set_Texture(stage, handle)` with stage 1 was already declared in 5i. |

## Deferred

| Item | Stage |
|---|---|
| `tex2_lit` — two-stage modulate **with** per-vertex lighting. Needed for lit terrain where a detail map modulates the base before lighting is applied. Straightforward: copy `fs_tex2.sc`, fold in the `lighting` computation from `fs_tex_lit.sc` on the `base * overlay` product. Gated on a real use case from 5h — the DX8 adapter will tell us whether the game actually hits this combo on lit geometry. | 5h+ |
| Additional combine ops (`add`, `subtract`, `blend`, `bumpenv`) beyond `modulate` | later — add as new permutations when a concrete pass needs them |
| Three-plus texture stages (specular + detail + normal on a single prop, etc.) | later — bgfx allows up to `BGFX_CONFIG_MAX_TEXTURE_SAMPLERS` (16) slots; the descriptor max was bumped to 2 in 5i and stays at 2 in 5k |
| Per-stage sampler state (wrap vs. clamp, anisotropy) pulled from a `TextureStageDesc` POD | later |
| DX8Wrapper → IRenderBackend adapter (multi-stage SetTexture call sites in terrain / decal renderers route through here) | 5h |
