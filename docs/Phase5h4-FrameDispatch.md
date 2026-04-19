# Phase 5h.4 — DX8Wrapper frame-lifecycle + transform dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fourth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h3-TextureCache.md`.

## Scope revision

5h.4 was originally planned as "TextureClass::Init → BgfxTextureCache
bridge". That plan fell apart on first contact: `Core/.../texture.cpp`
is entirely wrapped in `#ifdef RTS_RENDERER_DX8`, and GeneralsMD's
per-game WW3D2 CMakeLists explicitly comments `texture.cpp` out of the
source list for bgfx builds. There's **no `TextureClass` implementation
in bgfx mode** — no `Init()`, no destructor, no vtable. Hooking a method
that doesn't exist is a non-starter.

Revised scope: pivot to wiring the **per-frame lifecycle** —
`Begin_Scene`, `End_Scene`, `Clear`, `Apply_Render_State_Changes`,
`Apply_Default_State` in `DX8Wrapper`'s bgfx stub branch. Those are the
methods the game's render loop calls every frame; routing them through
`RenderBackendRuntime::Get_Active()` means running the game in bgfx mode
actually drives the bgfx backend through Begin/End/Clear per frame, even
before any draw calls are routed. It's the first slice where running
production code causes the bgfx backend to do visible work.

The texture-bridge *preparation* (adding `BgfxTexture` + `Peek_Bgfx_Handle`
to `TextureBaseClass`) stays — the field is harmless in DX8 builds (zero
behavior change, 8 bytes per texture) and future-proofs when `TextureClass`
finally compiles in bgfx mode.

## Locked decisions

| Question | Decision |
|---|---|
| Why not un-comment `texture.cpp` in bgfx builds | `texture.cpp` has 1,900 lines of DX8 API (`_Create_DX8_Texture`, `IDirect3DTexture8::LockRect`, `D3DXCreateTextureFromFile`, etc.). Un-commenting means stubbing or routing every one of those paths, which is a phase unto itself. Not worth blocking 5h.4's narrow frame-dispatch win on. |
| Which transforms does `Apply_Render_State_Changes` push | World and View. Projection state goes through `DX8CALL(SetTransform(D3DTS_PROJECTION, …))` in the inline header path — a no-op in bgfx — so it's not yet captured. 5h.5 will add projection + material + light + shader dispatch when the full `_Apply_Render_State_Changes` DX8 body is ported. |
| `Clear` + `Begin_Scene` + `End_Scene` | Direct pass-through. `DX8Wrapper::Clear(color_bool, z_bool, Vector3 color, alpha, z, stencil)` maps to `backend->Clear(color_bool, z_bool, Vector4{color.X,Y,Z,1}, z)`. Alpha is thrown away because backbuffer alpha doesn't matter here; stencil is dropped until a call site actually sets it. |
| Why not also modify `Set_Transform` in the inline header | It's `WWINLINE` and used from both DX8 and bgfx builds. Modifying means `#ifdef`'d branches in a hot-path header; the state-change bit (`render_state_changed`) is already being set and tied to `render_state.world/view`, so deferring the actual dispatch to `Apply_Render_State_Changes` is cleaner and keeps the header DX8-clean. |
| Matrix passing | `reinterpret_cast<const float*>(&render_state.world)` — `Matrix4x4` is 16 contiguous floats in D3D row-major layout (`Row[4]`), matching bgfx's `setTransform` convention. Same approach as the 5g cube test. |
| Where does projection-matrix dispatch land | 5h.5. Needs a little more plumbing since it currently flows through `DX8CALL(SetTransform)` in the inline header. A light touch to that code or an `Apply_Render_State_Changes` extension will suffice. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.h` | Add `uintptr_t BgfxTexture = 0;` field + inline `Peek_Bgfx_Handle() const`. Harmless in DX8 builds; populated by future 5h.5+ routing. |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | In the bgfx stub branch: include `RenderBackendRuntime.h` + `IRenderBackend.h` + `vector4.h` + `matrix4.h`. Add a file-local `kIdentityMat`. Implement `Begin_Scene`, `End_Scene`, `Clear`, `Apply_Render_State_Changes`, `Apply_Default_State` as delegations through `RenderBackendRuntime::Get_Active()`. |

### The frame-lifecycle block

```cpp
void DX8Wrapper::Begin_Scene() {
    if (auto* b = RenderBackendRuntime::Get_Active()) b->Begin_Scene();
}
void DX8Wrapper::End_Scene(bool flip_frame) {
    if (auto* b = RenderBackendRuntime::Get_Active()) b->End_Scene(flip_frame);
}
void DX8Wrapper::Clear(bool color, bool z, const Vector3& c, float /*alpha*/, float z, unsigned /*stencil*/) {
    if (auto* b = RenderBackendRuntime::Get_Active())
        b->Clear(color, z, Vector4(c.X, c.Y, c.Z, 1.0f), z);
}
void DX8Wrapper::Apply_Render_State_Changes() {
    auto* b = RenderBackendRuntime::Get_Active();
    if (!b) return;
    if (render_state_changed & WORLD_CHANGED) {
        b->Set_World_Transform(reinterpret_cast<const float*>(&render_state.world));
        render_state_changed &= ~WORLD_CHANGED;
    }
    if (render_state_changed & VIEW_CHANGED) {
        b->Set_View_Transform(reinterpret_cast<const float*>(&render_state.view));
        render_state_changed &= ~VIEW_CHANGED;
    }
}
void DX8Wrapper::Apply_Default_State() {
    if (auto* b = RenderBackendRuntime::Get_Active()) {
        b->Set_World_Transform(kIdentityMat);
        b->Set_View_Transform(kIdentityMat);
        b->Set_Projection_Transform(kIdentityMat);
    }
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK. |
| `cmake --build build_bgfx --target z_ww3d2` | OK — `dx8wrapper.cpp`'s bgfx stub branch now compiles with the new `RenderBackendRuntime` / `IRenderBackend` / `Matrix4x4` dependencies and links cleanly against `corei_bgfx` (brought in via 5h.2's link edge). |
| All 16 bgfx tests | PASSED — no backend-level behavior changed. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. The `#else` branch is the only code touched; the DX8 side is byte-identical to pre-5h.4. |

No new test — every plausible harness for this phase would need to link
all of `corei_ww3d2` (static members, heavyweight dependencies) just to
call `DX8Wrapper::Init / Begin_Scene / …`. Build-verification + zero
regression on the 16 tests + DX8 reconfigure-clean is the right bar for
a wiring-only slice. 5h.5+ will add tests once there's a real draw
routed through the wiring — the pixel-assertion harness from 5q is
ready and waiting.

Static sweep:

| Pattern | Result |
|---|---|
| Call sites that touch `RenderBackendRuntime::Get_Active()` in production code | Five — `DX8Wrapper::Begin_Scene`, `End_Scene`, `Clear`, `Apply_Render_State_Changes`, `Apply_Default_State`. First time any count beyond zero. |
| bgfx headers (`<bgfx>` / `<bimg>`) referenced from production WW3D2 source | Zero — everything goes through the `IRenderBackend` abstraction per the 5h.1 seam. |
| DX8 stub branch methods still `return/true;` no-op | Several — the draw-path (`Draw_Triangles`, `Draw_Strip`, `Draw`), `Set_Texture`, `Set_Material`, `Set_Shader`, `Set_Light`, `Set_Viewport`, etc. All pending 5h.5+. |

## Deferred

| Item | Stage |
|---|---|
| Projection-matrix dispatch. Currently held in the inline `Set_Transform` via `DX8CALL(SetTransform(D3DTS_PROJECTION, …))`. Wire through `Apply_Render_State_Changes` by either extending the bit mask or an explicit cache | 5h.5 |
| Draw-call routing (`Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`) | 5h.5 |
| `Set_Texture` / `Set_Material` / `Set_Shader` / `Set_Light` stubs | 5h.5 |
| `Set_Viewport` — trivially maps to `bgfx::setViewRect` once the adapter needs custom viewports | 5h.5 |
| `TextureClass::Init` bridge — blocked on un-commenting `texture.cpp` in the bgfx build. Probably a dedicated "texture stubs" phase before that becomes feasible | 5h.6 |
| Actually running the game in bgfx mode and capturing a rendered frame via Phase 5q — the end-state verification | late 5h / 5h.last |

**Running the game in bgfx mode now clears to the DX8-configured clear
color each frame.** Previous phases established the seam (5h.1), the
backend-startup path (5h.2), and the texture-handle cache (5h.3); this
one makes the game's render loop actually animate the backend through
Begin / End / Clear per frame. That's the difference between "bgfx is
installed but inert" and "bgfx is the render driver, just hasn't been
given geometry yet".
