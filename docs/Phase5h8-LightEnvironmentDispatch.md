# Phase 5h.8 — LightEnvironment dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Eighth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h7-LightDispatch.md`.

## Scope gate

5h.7 wired `Set_Light(D3DLIGHT8*)` and `Set_Light(LightClass&)` through
the `IRenderBackend` seam, but `Set_Light_Environment` still only
cached the incoming pointer — the per-slot fan-out that the DX8 body
at `dx8wrapper.cpp:3065` performs was still a no-op in the bgfx
branch. That matters because the **game's main per-draw lighting path
goes through `Set_Light_Environment`**, not individual `Set_Light`
calls: `MeshClass::Render` queries the scene for lights near the
object, stuffs them into a `LightEnvironmentClass`, and calls
`Set_Light_Environment` once per mesh. Without per-slot fan-out,
every mesh would draw as if unlit.

5h.8 closes that gap by iterating `env->Get_Light_Count()`, translating
each input slot to a `LightDesc`, and pushing it through the seam.
Scene-global ambient from `Get_Equivalent_Ambient` is collapsed to
luminance and folded into slot 0's ambient term. Trailing slots up
to the backend's `kMaxLights = 4` are explicitly disabled each frame so
stale state from a prior mesh doesn't leak into the next draw.

## Locked decisions

| Question | Decision |
|---|---|
| Where does the translator live | The bgfx `#else` branch of `DX8Wrapper::Set_Light_Environment` in `dx8wrapper.cpp`. The DX8 body at line 3065 is unchanged — it still walks the env to build `RenderLight` + `D3DRS_AMBIENT` state. |
| How many slots to walk | `env->Get_Light_Count()`, capped at `kMaxBackendLights = 4`. Both `LightEnvironmentClass::MAX_LIGHTS` and `BgfxBackend::kMaxLights` happen to equal 4; the constant is kept as a literal here so the mismatch surfaces loudly if either side ever diverges. |
| Direction sign convention | `-env->Get_Light_Direction(i)`. Matches the DX8 body at `dx8wrapper.cpp:3091`: LightEnvironmentClass stores "direction from light to object center", and the shader expects "direction the light points toward the surface". Negation is correct on both sides — flipping here would only cause the two paths to disagree. |
| Scene-global ambient | Folded into slot 0's `LightDesc::ambient`. The uber-shader accumulates `ambient * light_color` per slot; the tst_bgfx_multilight smoke test already drives it that way with scene ambient in slot 0. |
| Point-light attenuation | 5h.8 uses `attenuationRange = env->getPointOrad(i)` (outer radius) as the backend's single-range proxy; the DX8 body builds a 1/(1+k·D) rational-attenuation triple from `(inner, outer)`. The backend's uber-shader currently treats attenuation as a hard step at `attenuationRange` — a point-lit scene will look slightly different between the two paths until the shader gains a linear-falloff path. |
| Specular | DX8 body gives slot 0 a hardcoded `(1,1,1)` specular (line 3095 `if (l==0) rl.Specular = Vector3(1,1,1);`). `LightDesc` has no specular field — the backend's uber-shader doesn't compute specular yet, so there's nothing to forward. When specular lands, this is one of the hooks that'll need to grow. Marked in the deferred table. |
| Null env | `Set_Light_Environment(nullptr)` disables all 4 backend slots. The DX8 body does nothing on null (just caches `Light_Environment = nullptr`), because D3D keeps its own D3DLIGHT8 state that gets cleared by the next `Set_Light` call. The bgfx backend has no equivalent, so we disable explicitly. |
| Disable trailing slots | Yes — every call explicitly disables slots from `lightCount` to 4. Otherwise a mesh with 1 active light following a mesh with 3 active lights would pick up the stale 2nd and 3rd slots. |
| Why not rebuild `Light_Environment` handling on top of `Set_Light` | The LightEnvironmentClass-specific semantics (point-light attenuation via inner/outer radius, scene ambient folded into slot 0, implicit slot clearing) don't map 1:1 onto `Set_Light(LightClass&)`. Doing the fan-out here keeps per-method translators focused and matches the DX8 body's layered structure. |
| Intensity field | Each emitted `LightDesc` carries `intensity = 1.0`. LightEnvironmentClass has already folded per-light attenuation into its output diffuse values (that's its core job), so the backend's color term is pre-scaled. Leaving `intensity` at 1 keeps it a no-op for future shader code that wants it. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx branch: `#include "lightenvironment.h"`. Replace the one-line `Light_Environment = env;` stub with a full per-slot translator that pushes each light through `IRenderBackend::Set_Light`, folds `Get_Equivalent_Ambient` into slot 0, and disables trailing slots. |

### Post-change body (bgfx branch)

```cpp
void DX8Wrapper::Set_Light_Environment(LightEnvironmentClass* env)
{
    Light_Environment = env;
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (b == nullptr) return;

    constexpr unsigned kMaxBackendLights = 4;

    if (env == nullptr) {
        for (unsigned i = 0; i < kMaxBackendLights; ++i) b->Set_Light(i, nullptr);
        return;
    }

    const int    lightCount   = env->Get_Light_Count();
    const float  ambientScalar = LuminanceOf(env->Get_Equivalent_Ambient());

    for (int i = 0; i < lightCount && i < int(kMaxBackendLights); ++i) {
        LightDesc desc;
        if (env->isPointLight(i)) {
            desc.type             = LightDesc::LIGHT_POINT;
            const Vector3& d      = env->getPointDiffuse(i);
            const Vector3& a      = env->getPointAmbient(i);
            const Vector3& c      = env->getPointCenter(i);
            desc.color[0]         = d.X; desc.color[1] = d.Y; desc.color[2] = d.Z;
            desc.position[0]      = c.X; desc.position[1] = c.Y; desc.position[2] = c.Z;
            desc.attenuationRange = env->getPointOrad(i);
            desc.ambient          = LuminanceOf(a);
        } else {
            desc.type      = LightDesc::LIGHT_DIRECTIONAL;
            const Vector3& d   = env->Get_Light_Diffuse(i);
            const Vector3  dir = -env->Get_Light_Direction(i);
            desc.color[0]     = d.X;   desc.color[1] = d.Y;   desc.color[2] = d.Z;
            desc.direction[0] = dir.X; desc.direction[1] = dir.Y; desc.direction[2] = dir.Z;
            desc.ambient      = 0.0f;
        }
        if (i == 0) desc.ambient += ambientScalar;
        desc.intensity = 1.0f;
        b->Set_Light(static_cast<unsigned>(i), &desc);
    }

    for (unsigned i = static_cast<unsigned>(lightCount); i < kMaxBackendLights; ++i)
        b->Set_Light(i, nullptr);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `lightenvironment.cpp` + `light.cpp` already compile in bgfx mode via GeneralsMD's source list, so no CMake changes. |
| All 17 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. DX8 full compile is Windows-only; reconfigure-clean is the usual sanity signal. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Set_Light` callers | Three — `DX8Wrapper::Set_Light(D3DLIGHT8*)`, `DX8Wrapper::Set_Light(LightClass&)`, and the new `DX8Wrapper::Set_Light_Environment` loop. Up from two at end of 5h.7. |
| `env->Get_Light_Count` call sites in the adapter | Two — DX8 body at line 3072 and the new bgfx branch. Identical iteration bound. |
| DX8 production `Set_Light_Environment` | Unchanged at line 3065 — still populates `render_state.Lights[]` + `D3DRS_AMBIENT` via `Set_DX8_Render_State`. |

No new test — same rationale as 5h.4 / 5h.7. Any harness hitting `DX8Wrapper::Set_Light_Environment` needs to link all of `z_ww3d2` plus construct a LightClass (which drags in rendobj + scene). The backend-side per-slot behavior is already covered by `tst_bgfx_multilight`; the translator itself is three-field-per-slot copy math. Build + regression + reconfigure-clean is the right bar.

## Deferred

| Item | Stage |
|---|---|
| Point-light linear attenuation in the uber-shader. Currently the backend hard-steps at `attenuationRange`; the DX8 path produces a 1/(1+kD) smooth falloff via Attenuation0/1/2 triples | 5h.9 |
| Specular color. DX8 body gives slot 0 a hardcoded `(1,1,1)` specular. `LightDesc` has no specular field and the uber-shader is Lambertian-only. Both need to grow when specular lands | 5h.10 |
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode first | 5h.11 |
| `Set_Shader(ShaderClass)` → `ShaderStateDesc` translator | 5h.12 |
| `Set_Material(VertexMaterialClass)` → `MaterialDesc` translator | 5h.12 |
| `TextureClass::Init` → `BgfxTextureCache::Get_Or_Load_File`. Blocked on un-commenting `texture.cpp` in bgfx per-game builds | 5h.13 |

After this phase, **every lighting-state entry point the game uses
during scene setup has a real bgfx-mode body**. A mesh passed through
`MeshClass::Render` → `DX8Wrapper::Set_Light_Environment` now arrives
at the bgfx backend as 4 concrete light slots (or 4 disabled slots);
the uber-shader has the data it needs to do Lambertian lighting on
the next draw. Only the draw itself (5h.11+) is still a stub.
