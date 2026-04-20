# Phase 5h.7 — Light dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Seventh sub-phase of
the DX8Wrapper adapter. Follows `Phase5h6-ViewportDispatch.md`.

## Scope gate

5h.1 through 5h.6 routed the frame lifecycle, the runtime backend
singleton, the texture cache, the bootstrap, all three transform
matrices, and the viewport through the `IRenderBackend` seam.
`Set_Light(unsigned, const D3DLIGHT8*)` and
`Set_Light(unsigned, const LightClass&)` were still the DX8Wrapper's
empty-body bgfx stubs on entry: the game could call them, they
accepted the state, they just vanished.

5h.7 makes both signatures translate the D3DLIGHT8 / LightClass input
into a `LightDesc` and forward to `IRenderBackend::Set_Light`, the same
entry point that `tst_bgfx_multilight` has been driving directly since
Phase 5l. `Set_Light_Environment` stays partial — it caches the
`LightEnvironmentClass*` pointer in `DX8Wrapper::Light_Environment` but
does **not** walk the per-slot directional lights yet; that's 5h.8.

Draw-call routing, shader / material translators, and texture
bridging are still deferred for the reasons 5h.6's deferred table
enumerates.

## Locked decisions

| Question | Decision |
|---|---|
| Where does the translator live | Inside the bgfx `#else` branch of `dx8wrapper.cpp`, as two separate `DX8Wrapper::Set_Light` function bodies plus a file-local anonymous namespace of `TranslateLightType` helpers. The DX8 branch is byte-identical to pre-5h.7 — both production `Set_Light` bodies at lines 2990 and 3002 still populate `render_state.Lights[]` and flip `render_state_changed` bits; 5h.7 only replaces the bgfx stubs. |
| How to reach the backend | `RenderBackendRuntime::Get_Active()` — the same runtime singleton 5h.4 / 5h.5 / 5h.6 use. No new wiring; the backend self-registers in `BgfxBackend::Init`. |
| Pass-through semantics | Direct push, not cached state: the DX8 wrapper doesn't own a per-slot dirty bit the bgfx path reads from. That's fine because `IRenderBackend::Set_Light` is already a no-op when the slot's state is identical (the backend caches internally), and calls are per-frame from `Set_Light_Environment` / `W3DScene::Render`. |
| Why `LightClass::Get_Spot_Direction` for the non-spot directional channel | The DX8 production body does the same thing at `dx8wrapper.cpp:3035`: `light.Get_Spot_Direction(temp); rl.Direction = temp;`. LightClass stores "direction" in the `SpotDirection` field regardless of type — a Westwood quirk we preserve so a DX8 build and a bgfx build see the same direction vector. |
| Ambient scalar vs Vector3 | `LightDesc::ambient` is a single float; `LightClass::Get_Ambient` and `D3DLIGHT8::Ambient` are Vector3 / RGBA. Collapse with Rec. 709 luminance weights (`0.2126 R + 0.7152 G + 0.0722 B`). The uber-shader's accumulation loop adds `ambient * light_color` per-slot, so a single scalar is the right shape for the "how bright is the ambient contribution from this slot" intent — colored ambient would tint it twice. |
| LightClass intensity | Multiplied into both `color[]` and the separate `intensity` field. The shader ignores `intensity` today (color is pre-scaled), but the field exists in `LightDesc` for future point/spot attenuation math that wants the pre-multiplied value. |
| D3DLIGHT8 has no intensity | Set `desc.intensity = 1.0f`. D3DLIGHT8 bakes intensity into the Diffuse color; there's no "separate" value to surface. |
| `nullptr` path | `DX8Wrapper::Set_Light(i, nullptr)` (the D3DLIGHT8 overload) forwards `nullptr` through to the backend, which disables that slot. The LightClass overload can't receive nullptr (reference type) — disabling only happens through the D3DLIGHT8 signature in the game today. |
| `Set_Light_Environment` scope | Phase 5h.7 keeps it partial. `Light_Environment` is still remembered, but no iteration yet. This is intentional: LightEnvironmentClass has per-game differences (Generals vs GeneralsMD have separate source trees, and the iteration logic touches scene-global ambient + MATERIAL_CHANGED flags). Walking it correctly is its own slice. |
| Why no new test | Following Phase 5h.4's pattern: a test harness exercising the new translators would need to link all of `z_ww3d2` (which pulls in mesh.cpp, assetmgr.cpp, scene graph, etc. — ~50 TUs) just to call one DX8Wrapper static. The equivalent backend behavior is already covered by `tst_bgfx_multilight`, and the translators are pure arithmetic + forwarding. Build-verification + 17-test regression + DX8 reconfigure-clean is the right bar for a wiring-only slice. 5h.8+ will add tests once the draw path is routed. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx branch: `#include "WW3D2/BackendDescriptors.h"`, `"vector3.h"`, `"light.h"`. Add anonymous-namespace helpers `TranslateLightType(LightClass::LightType)` and `TranslateLightType(D3DLIGHTTYPE)` plus `LuminanceOf(Vector3)`. Replace the three stub bodies (`Set_Light(D3DLIGHT8*)`, `Set_Light(LightClass&)`, `Set_Light_Environment`) with translators that forward to `IRenderBackend::Set_Light`. `Set_Light_Environment` keeps the pointer cache. |

### The bgfx branch, post-5h.7

```cpp
void DX8Wrapper::Set_Light(unsigned index, const D3DLIGHT8* light)
{
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (b == nullptr) return;
    if (light == nullptr) { b->Set_Light(index, nullptr); return; }

    LightDesc desc;
    desc.type             = TranslateLightType(light->Type);
    desc.direction[0]     = light->Direction.x;
    desc.direction[1]     = light->Direction.y;
    desc.direction[2]     = light->Direction.z;
    desc.color[0]         = light->Diffuse.r;
    desc.color[1]         = light->Diffuse.g;
    desc.color[2]         = light->Diffuse.b;
    desc.ambient          = 0.2126f * light->Ambient.r
                          + 0.7152f * light->Ambient.g
                          + 0.0722f * light->Ambient.b;
    desc.intensity        = 1.0f;
    desc.position[0]      = light->Position.x;
    desc.position[1]      = light->Position.y;
    desc.position[2]      = light->Position.z;
    desc.attenuationRange = light->Range;
    b->Set_Light(index, &desc);
}

void DX8Wrapper::Set_Light(unsigned index, const LightClass& light)
{
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (b == nullptr) return;

    LightDesc desc;
    desc.type = TranslateLightType(light.Get_Type());

    Vector3 diffuse;
    light.Get_Diffuse(&diffuse);
    const float intensity = light.Get_Intensity();
    desc.color[0] = diffuse.X * intensity;
    desc.color[1] = diffuse.Y * intensity;
    desc.color[2] = diffuse.Z * intensity;
    desc.intensity = intensity;

    Vector3 ambient;
    light.Get_Ambient(&ambient);
    desc.ambient = LuminanceOf(ambient) * intensity;

    const Vector3 pos = light.Get_Position();
    desc.position[0] = pos.X;
    desc.position[1] = pos.Y;
    desc.position[2] = pos.Z;

    Vector3 dir;
    light.Get_Spot_Direction(dir);
    desc.direction[0] = dir.X;
    desc.direction[1] = dir.Y;
    desc.direction[2] = dir.Z;

    desc.attenuationRange = light.Get_Attenuation_Range();
    b->Set_Light(index, &desc);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK — backend unchanged. |
| `cmake --build build_bgfx --target z_ww3d2` | OK — `dx8wrapper.cpp` bgfx branch now includes `"light.h"` + `"vector3.h"` + `"WW3D2/BackendDescriptors.h"` and compiles with `LightClass` visible (the symbol is compiled into the same z_ww3d2 target via GeneralsMD's WW3D2 source list). |
| All 17 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. DX8 full compile is Windows-only — `d3d8types.h` absent on macOS SDK — so reconfigure-clean is the usual sanity signal. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Set_Light` callers in production code | Two — `DX8Wrapper::Set_Light(D3DLIGHT8*)` bgfx branch and `DX8Wrapper::Set_Light(LightClass&)` bgfx branch. Up from zero at end of 5h.6. |
| DX8 production `Set_Light` bodies | Unchanged — lines 2990 (D3DLIGHT8) and 3002 (LightClass) still populate `render_state.Lights[]` and flip `LIGHT0_CHANGED<<index` bits. |
| Remaining DX8Wrapper no-op bgfx stubs | `Draw_Triangles` / `Draw_Strip` / `Draw` / `Draw_Sorting_IB_VB`, `Set_Vertex_Buffer` / `Set_Index_Buffer`, `_Create_DX8_Texture` family, `Set_Render_State`, `Apply_Render_State_Changes` still drains only world/view/projection — it does NOT republish lights to the backend because the translator already pushed them on each `Set_Light` call. |

## Deferred

| Item | Stage |
|---|---|
| `LightEnvironmentClass` iteration in `Set_Light_Environment`. Walk `env->Get_Light_Count()` and translate each slot through `Set_Light`, matching the DX8 body's scene-global ambient handling | 5h.8 |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode first | 5h.9 |
| `Set_Shader(ShaderClass)` → `ShaderStateDesc` translator. Needs `ShaderClass` compile units | 5h.10 |
| `Set_Material(VertexMaterialClass)` → `MaterialDesc` translator. Needs VMC compile units | 5h.10 |
| `TextureClass::Init` → `BgfxTextureCache::Get_Or_Load_File`. Blocked on un-commenting `texture.cpp` in bgfx per-game builds | 5h.11 |

After this phase, every `Set_*` entry point on `DX8Wrapper` that the game currently calls during scene setup **except** the three translator slices above has a real bgfx-mode body. The light path in particular is a milestone: it's the first per-slot state the game can drive through the adapter that the uber-shader will read on the next draw once routing lands.
