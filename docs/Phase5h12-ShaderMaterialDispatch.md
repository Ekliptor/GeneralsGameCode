# Phase 5h.12 — Shader + Material translator dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twelfth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h11-Specular.md`.

## Scope pivot

The 5h.11 deferred table slotted **draw-call routing** as 5h.12 and
**Shader / Material translators** as 5h.13. First-contact with the
draw-call work showed a hard block: `dx8vertexbuffer.cpp` and
`dx8indexbuffer.cpp` are wholly wrapped in `#ifdef RTS_RENDERER_DX8`
and contribute **zero symbols** in bgfx-mode builds. Every call site
that would route a `Draw_Triangles` through `IRenderBackend::Draw_Indexed`
needs a `VertexBufferClass` / `IndexBufferClass` implementation to
source vertex data from — and building one is its own substantial
un-`#ifdef`'ing slice (both files are ~900 lines of DX8-API-bound
resource management).

5h.12 pivots to the previously-5h.13 work (translators), which is
**unblocked** because:

  * `ShaderClass` is a 32-bit packed state — every field the uber-shader
    can honor is accessible through inline `Get_*` getters in `shader.h`.
    `shader.cpp` is `#ifdef`'d out in bgfx mode, but the translator
    doesn't need the implementation unit (preset statics, static `Apply`
    machinery) — only the inline header.
  * `VertexMaterialClass`'s `vertmaterial.cpp` has **no** `#ifdef
    RTS_RENDERER_DX8` guard. It already compiles into `z_ww3d2` in
    bgfx mode. All the accessors (`Get_Diffuse/Ambient/Emissive/Specular/
    Opacity/Shininess/Lighting`) link cleanly.

So instead of 5h.12 being "route draws" and 5h.13 being "translate
state", the shuffle makes them the other way round: 5h.12 is the
state translators, and draw-call routing drops to 5h.13+ (gated on a
`dx8vertexbuffer.cpp` un-`#ifdef`, which becomes its own dedicated
phase — call it 5h.13a or 5i depending on how big it ends up).

## Scope gate

5h.4 through 5h.5 drained world/view/projection from
`render_state_changed` to `IRenderBackend::Set_*_Transform`. 5h.7
through 5h.11 drained per-slot light state directly from the `Set_Light`
entry points (not via a `render_state_changed` bit). The two remaining
bits the game sets on every `Set_Shader` / `Set_Material` call —
`SHADER_CHANGED` (1 << 15) and `MATERIAL_CHANGED` (1 << 14) — were
never drained in the bgfx build; the inline `Set_Shader` /
`Set_Material` at `dx8wrapper.h:1220/1204` cached into `render_state`
but `Apply_Render_State_Changes` didn't look at those bits.

5h.12 closes that gap. `Apply_Render_State_Changes` now translates
`render_state.shader` (ShaderClass) → `ShaderStateDesc` and
`render_state.material` (VertexMaterialClass*) → `MaterialDesc`, and
forwards both to the backend. This is the last state-surface slice
blocked by compile-unit issues; the remaining work is resource
binding (textures, vertex/index buffers) and the draw call itself.

## Locked decisions

| Question | Decision |
|---|---|
| Where do translators live | In the bgfx branch of `dx8wrapper.cpp` (inside the existing `#else` body), in an anonymous namespace alongside the Phase 5h.7 `TranslateLightType` helpers. Forward declarations near the top so `Apply_Render_State_Changes` can reach them before the definition block (which sits logically with the other adapters). |
| Why inline in the same TU | The translators are ~20 lines each of `switch`-on-enum. Putting them in a separate TU would add a header + a new object file for zero gain. |
| Fields dropped | ShaderClass: `DetailColor / DetailAlpha / NPatch / PrimaryGradient / SecondaryGradient / BumpEnvmap`. None of these have a `ShaderStateDesc` counterpart today; the uber-shader has no detail texture mode, no N-patch tessellation, no gradient ops. When the backend grows those features, the translator extends — the `ShaderClass` side is already capturing them. |
| Fog treatment | `ShaderStateDesc::fogEnable` is set from `Shader.Get_Fog_Func() != FOG_DISABLE`. The `fogStart/fogEnd/fogColor` scene-globals are NOT sourced from ShaderClass — they come from the scene-global fog-range / color state (captured pre-5h.12 via separate `ww3d::Set_Fog_Range` code paths that already flow through the backend). This matches D3D8 semantics: D3DRS_FOG{ENABLE,START,END,COLOR} are independent of the per-material blend state. |
| Material-nullptr handling | `render_state.material = nullptr` maps to a default `MaterialDesc` (diffuse white, no specular, lighting off). The unlit solid/vcolor/tex uber programs render correctly off that; the lit mlit program falls back to white diffuse + zero ambient, matching DX8's `SetMaterial(nullptr)` convention. |
| When is the translator reached | Only when the dirty bit is set — same gating the DX8 body uses. A series of `Set_Shader(sameShader)` calls that cached by value-equality skip the bit flip entirely (see the ShaderClass inline body's early-return on `(unsigned&)shader == (unsigned&)render_state.shader`). |
| Specular power scale | `VMC::Get_Shininess()` returns the D3DMATERIAL8 `Power` value directly. Our shader's `pow(N·H, power)` expects the same convention, so it's a straight copy — no `* 128` / `/ 128` scale factor. |
| Translator helper forward declarations | In the upper anonymous namespace (the one that also owns `s_pendingHwnd` + `kIdentityMat`) so `Apply_Render_State_Changes` can see the prototypes. Definitions live in the lower anonymous namespace alongside `TranslateLightType` / `LuminanceOf`. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx branch: `#include "shader.h"` + `"vertmaterial.h"`. Forward-declare `TranslateShader{DepthCmp,SrcBlend,DstBlend}` in the upper anonymous namespace. Define them in the lower anonymous namespace alongside `TranslateLightType`. Extend `Apply_Render_State_Changes` to drain `SHADER_CHANGED` → `Set_Shader(ShaderStateDesc)` and `MATERIAL_CHANGED` → `Set_Material(MaterialDesc)`. |

### The translator bodies

```cpp
ShaderStateDesc::DepthCmp TranslateShaderDepthCmp(ShaderClass::DepthCompareType d)
{
    switch (d) {
    case ShaderClass::PASS_NEVER:    return ShaderStateDesc::DEPTH_NEVER;
    case ShaderClass::PASS_LESS:     return ShaderStateDesc::DEPTH_LESS;
    // ...
    case ShaderClass::PASS_ALWAYS:   return ShaderStateDesc::DEPTH_ALWAYS;
    }
    return ShaderStateDesc::DEPTH_LEQUAL;
}
// (+ src / dst blend translators of the same shape)
```

And the new `Apply_Render_State_Changes` drain:

```cpp
if (render_state_changed & SHADER_CHANGED)
{
    const ShaderClass& s = render_state.shader;
    ShaderStateDesc desc;
    desc.depthCmp        = TranslateShaderDepthCmp(s.Get_Depth_Compare());
    desc.depthWrite      = (s.Get_Depth_Mask() != ShaderClass::DEPTH_WRITE_DISABLE);
    desc.colorWrite      = (s.Get_Color_Mask() != ShaderClass::COLOR_WRITE_DISABLE);
    desc.cullEnable      = (s.Get_Cull_Mode() != ShaderClass::CULL_MODE_DISABLE);
    desc.alphaTest       = (s.Get_Alpha_Test() != ShaderClass::ALPHATEST_DISABLE);
    desc.texturingEnable = (s.Get_Texturing() != ShaderClass::TEXTURING_DISABLE);
    desc.srcBlend        = TranslateShaderSrcBlend(s.Get_Src_Blend_Func());
    desc.dstBlend        = TranslateShaderDstBlend(s.Get_Dst_Blend_Func());
    desc.fogEnable       = (s.Get_Fog_Func() != ShaderClass::FOG_DISABLE);
    b->Set_Shader(desc);
    render_state_changed &= ~SHADER_CHANGED;
}
if (render_state_changed & MATERIAL_CHANGED)
{
    MaterialDesc desc;
    if (const VertexMaterialClass* m = render_state.material)
    {
        Vector3 v;
        m->Get_Diffuse(&v);  desc.diffuse[0]=v.X;  desc.diffuse[1]=v.Y;  desc.diffuse[2]=v.Z;
        m->Get_Ambient(&v);  desc.ambient[0]=v.X;  desc.ambient[1]=v.Y;  desc.ambient[2]=v.Z;
        m->Get_Emissive(&v); desc.emissive[0]=v.X; desc.emissive[1]=v.Y; desc.emissive[2]=v.Z;
        m->Get_Specular(&v); desc.specularColor[0]=v.X; desc.specularColor[1]=v.Y; desc.specularColor[2]=v.Z;
        desc.specularPower = m->Get_Shininess();
        desc.opacity       = m->Get_Opacity();
        desc.useLighting   = m->Get_Lighting();
    }
    b->Set_Material(desc);
    render_state_changed &= ~MATERIAL_CHANGED;
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK — `shader.h` is header-only inline; `vertmaterial.cpp` already compiles. |
| All 20 bgfx tests | PASSED. No regressions — none of the tests wire through `DX8Wrapper::Set_Shader` / `Set_Material` (they build descriptors directly for the backend), so the translator change is inert to them. The value surfaces when `z_ww3d2` is actually running real game code. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures clean. |

No new test — same pattern as 5h.4 / 5h.7 / 5h.8. A test that exercises `DX8Wrapper::Set_Shader` through the runtime would need to link all of `z_ww3d2` (mesh / assetmgr / scene graph) just to call a single static. The translator is `switch`-on-enum; the equivalent backend behavior (reading a `ShaderStateDesc`) is already exercised by every existing test that calls `backend.Set_Shader(...)` directly. Build + 20-test regression + DX8 reconfigure-clean is the right bar.

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Set_Shader` / `Set_Material` callers in production code | Two — `DX8Wrapper::Apply_Render_State_Changes` (bgfx branch), both drained per-frame from `render_state_changed`. |
| `TranslateShader*` references | Four — three definitions + one call site each (all in `Apply_Render_State_Changes`). |
| DX8 production `Apply_Render_State_Changes` shader/material body | Unchanged at line 2239. Still runs through `_Apply_Render_State_Changes()`'s DX8 drains. |

## Deferred

| Item | Stage |
|---|---|
| Un-`#ifdef` `dx8vertexbuffer.cpp` / `dx8indexbuffer.cpp` for bgfx mode. ~900 lines each of DX8-API-bound resource management. Needs a bgfx-compatible replacement for `IDirect3DVertexBuffer8::Lock/Unlock`, `CreateVertexBuffer`, etc. — likely as a CPU-side `std::vector<uint8_t>` payload plus a lazy `bgfx::createVertexBuffer` on first bind | 5h.13 |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Gated on the above | 5h.14 |
| `TextureClass::Init` → `BgfxTextureCache` bridge. `texture.cpp` is also wholly `#ifdef`'d; same un-`#ifdef` pattern | 5h.15 |
| Detail-texture mode / BumpEnvmap / N-patch in the uber-shader. Currently dropped in the translator; landing them is a backend extension + a translator field re-enable | later |
| `ShaderClass::Get_Primary_Gradient() == GRADIENT_ADD` / `MODULATE2X` — edge cases dropped into `GRADIENT_MODULATE` today | later |

After this phase, **every `DX8Wrapper::Set_*` entry point that the game
calls during scene setup has a real bgfx-mode body**. The game can set
world/view/projection, lights, viewport, light-environment, shader,
and material — all land correctly. The only state surface still inert
is texture binding (which needs `TextureClass::Init` to route into
`BgfxTextureCache`) and the draw calls themselves, both gated on the
vertex/index/texture compile units being un-`#ifdef`'d.
