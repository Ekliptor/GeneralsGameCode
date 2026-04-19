# Phase 5h.5 — Projection-matrix dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fifth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h4-FrameDispatch.md`.

## Scope gate

5h.4 wired world / view transforms through `Apply_Render_State_Changes`
but left projection trapped in the inline `Set_Transform`'s
`DX8CALL(SetTransform(D3DTS_PROJECTION, …))` side-effect. 5h.5 closes
that gap so all three transforms flow through the `IRenderBackend` seam
identically.

Draw-call routing (`Draw_Triangles` / `Draw_Strip` / `Draw`) and
shader / material / light translation are still deferred — those need
`VertexBufferClass` / `IndexBufferClass` / `ShaderClass` /
`VertexMaterialClass` compiled in bgfx mode, which is a separate (larger)
slice.

## Locked decisions

| Question | Decision |
|---|---|
| Where to capture projection | `RenderStateStruct::projection` — a new `Matrix4x4` field alongside the existing `world` and `view`. Zero-initialized statically; only read when `PROJECTION_CHANGED` is set. |
| How to signal a change | New `PROJECTION_CHANGED = 1 << 20` in the `ChangedStates` enum. Follows the exact shape of `WORLD_CHANGED` / `VIEW_CHANGED`. No `PROJECTION_IDENTITY` bit (nothing short-circuits projection on "identity" like the world/view paths do, and production callers always set a real projection). |
| Where to set the flag | In the inline `Set_Transform(D3DTRANSFORMSTATETYPE, const Matrix4x4&)` at the `D3DTS_PROJECTION` case, **before** the existing `DX8CALL(SetTransform(…))` fires. The DX8 side-effect is unchanged — DX8 builds still push to the D3D device directly; bgfx builds read from `render_state.projection` inside `Apply_Render_State_Changes`. |
| Why no change to the `Matrix3D` overload of `Set_Transform` | That overload has no `D3DTS_PROJECTION` case — projection is always passed as `Matrix4x4`. Production callers that want a `Matrix3D` projection would hit the `default` case and go straight to `DX8CALL(SetTransform(…))`, which is a bgfx no-op. Not worth a defensive capture until a real caller surfaces. |
| DX8 build impact | One extra field in `RenderStateStruct` (16 floats, never written in DX8 mode — the capture path fires but DX8's `Apply_Render_State_Changes` doesn't read it). One extra bit set in `render_state_changed` on every projection change — dwarfed by the `DX8CALL` it's followed by. Functionally zero. |
| RenderStateStruct copy assignment | The existing `operator=` doesn't assign `world` or `view` either (world/view are reset each frame by the game loop; the copy is used for shader-state snapshot/restore of material + buffers + lights, not transforms). Keeping projection consistent with that pattern — not copied. Deviating would be a bug magnet. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.h` | Add `Matrix4x4 projection;` to `RenderStateStruct`. Add `PROJECTION_CHANGED = 1 << 20` to the `ChangedStates` enum. Extend the `Set_Transform(… Matrix4x4&)` inline's `D3DTS_PROJECTION` case with `render_state.projection = m; render_state_changed |= PROJECTION_CHANGED;` before the existing `DX8CALL`. |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | Extend the bgfx stub's `Apply_Render_State_Changes` to drain `PROJECTION_CHANGED`: `b->Set_Projection_Transform(reinterpret_cast<const float*>(&render_state.projection))`. |

### Post-change `Apply_Render_State_Changes` (bgfx branch)

```cpp
void DX8Wrapper::Apply_Render_State_Changes()
{
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (!b) return;
    if (render_state_changed & WORLD_CHANGED) {
        b->Set_World_Transform(reinterpret_cast<const float*>(&render_state.world));
        render_state_changed &= ~WORLD_CHANGED;
    }
    if (render_state_changed & VIEW_CHANGED) {
        b->Set_View_Transform(reinterpret_cast<const float*>(&render_state.view));
        render_state_changed &= ~VIEW_CHANGED;
    }
    if (render_state_changed & PROJECTION_CHANGED) {
        b->Set_Projection_Transform(reinterpret_cast<const float*>(&render_state.projection));
        render_state_changed &= ~PROJECTION_CHANGED;
    }
}
```

Three matrices in, three identical drain blocks. No visible behavior
change for DX8 builds — the new bit just doesn't get tested anywhere in
the DX8 `Apply_Render_State_Changes` body.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target z_ww3d2` | OK — `dx8wrapper.cpp` bgfx branch compiles the new drain block, `dx8wrapper.h` inline compiles in both configs. |
| All 16 bgfx tests | PASSED. Backend behavior unchanged. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. DX8 full compile only runs on Windows — `d3d8types.h` isn't on the macOS SDK — so reconfigure-clean is the usual sanity signal. |

Static sweep:

| Pattern | Result |
|---|---|
| `Set_Projection_Transform` callers in production code | One — `DX8Wrapper::Apply_Render_State_Changes` (bgfx branch). |
| `PROJECTION_CHANGED` references | Three — the enum declaration, the inline setter's `|=`, and the drain. |
| Production callers broken by the new field in `RenderStateStruct` | Zero — the struct's `operator=` is conservative about which fields it copies, and nobody reaches in to initialize transforms via `memset` except DX8Wrapper itself (which already zeroes the whole struct). |

## Deferred

| Item | Stage |
|---|---|
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed` (or `Draw_Triangles_Dynamic`). Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode first, which is a substantial stub-out effort | 5h.6 |
| `Set_Shader(ShaderClass)` → `ShaderStateDesc` translator. Needs the ShaderClass DX8 compile units to be either ported or stubbed in bgfx mode | 5h.7 |
| `Set_Material(VertexMaterialClass)` → `MaterialDesc` translator. Same VMC-not-compiled problem | 5h.7 |
| `Set_Light(LightClass)` + `Set_Light_Environment(LightEnvironmentClass*)` → `IRenderBackend::Set_Light`. LightClass is in WW3D2 core, should be compilable; LightEnvironmentClass is per-game | 5h.6 if straightforward |
| `Set_Viewport(D3DVIEWPORT8*)` → `bgfx::setViewRect`. Needs a new `IRenderBackend::Set_Viewport(x, y, w, h)` method first | 5h.7 |
| `TextureClass::Init` → `BgfxTextureCache::Get_Or_Load_File`. Blocked on `texture.cpp` being un-commented in bgfx per-game builds | 5h.8 |

Every matrix the game sets now flows to bgfx. The render loop that was
previously "bgfx clears the screen then stalls" now has correctly-bound
world / view / projection transforms ready for the first geometry the
next sub-phase routes through.
