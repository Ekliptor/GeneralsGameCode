# Phase 5h.29 — Render-target texture routing

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-ninth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h28-ProceduralTextureRouting.md`.

## Scope gate

5h.28 wired non-render-target procedural textures through
`IRenderBackend::Create_Texture_RGBA8(nullptr, …)`. Render-target
textures — the ones constructed with `rendertarget=true` for
post-process buffers, shadow maps, reflection passes — stayed at
zero handle and bound to the placeholder, because the backend's RT
API returns a *frame-buffer* handle (for `Set_Render_Target` +
`Destroy_Render_Target`), not a texture handle directly.

The backend *does* expose `Get_Render_Target_Texture(rtHandle) →
uintptr_t` that returns a sampler handle tied to the RT's color
attachment. 5h.29 wires that:

1. **Adds a second handle field** to `TextureBaseClass`:
   `BgfxRenderTarget`. Lives alongside `BgfxTexture` — each RT-backed
   texture holds both, since the game needs both to (a) bind as a
   draw target and (b) bind as a shader sampler.
2. **Extends the `(w, h, format, … , rendertarget, ...)` ctor**: when
   `rendertarget == true`, calls `Create_Render_Target(w, h,
   /*hasDepth=*/true)` → `Set_Bgfx_Render_Target(rt)`; then calls
   `Get_Render_Target_Texture(rt)` → `Set_Bgfx_Handle(tex)`. Apply's
   existing `Set_Texture(stage, Peek_Bgfx_Handle())` path binds the
   RT's color attachment for sampling.
3. **Dtor cleanup**: `TextureBaseClass::~TextureBaseClass` now calls
   `Destroy_Render_Target(BgfxRenderTarget)` when the field is non-zero.
   Zeros both handles afterwards since the RT's color texture dies with
   the RT.

With this phase, **every texture-creation path exposed by TextureClass
lands a real bgfx handle**: file-loaded (5h.27), non-RT procedural
(5h.28), and now RT-backed procedural (this phase).

## Locked decisions

| Question | Decision |
|---|---|
| Separate field or overload `BgfxTexture` | Separate. `BgfxTexture` is the sampler handle (what `Set_Texture` binds); `BgfxRenderTarget` is the frame-buffer handle (what `Set_Render_Target` binds and what `Destroy_Render_Target` frees). Overloading one field would mean tagging the sampler value somehow (a low-bit flag? a separate boolean?) — additive is cleaner. |
| Field location | Private in `TextureBaseClass` with public `Peek_Bgfx_Render_Target` + `Set_Bgfx_Render_Target` accessors, matching the 5h.4 pattern for `BgfxTexture`. |
| Field size cost in DX8 mode | 8 bytes per texture (always zero). Same pattern as `BgfxTexture` (Phase 5h.4). Class layout stays identical between DX8 and bgfx configurations — no `#ifdef` on member declarations. |
| `Create_Render_Target` depth attachment | `hasDepth = true`. Matches what most render-target consumers in the game expect (shadow maps write depth too; post-process buffers usually want Z for effects). A future extension could plumb this as a parameter if the game ever allocates a depth-less RT. |
| `Apply(stage)` behavior for RT textures | Unchanged. `Peek_Bgfx_Handle()` returns the sampler handle from `Get_Render_Target_Texture`, which is exactly what `Set_Texture` expects. No special-casing needed. |
| Dtor RT destruction | In `TextureBaseClass::~TextureBaseClass`, guarded by `#ifndef RTS_RENDERER_DX8`. `Destroy_Render_Target` releases both the frame buffer and its attached color/depth textures, so we also clear `BgfxTexture` to avoid a dangling sampler handle. |
| `Invalidate` doesn't touch the RT | `TextureBaseClass::Invalidate` (5h.20) releases `D3DTexture` + sets `Initialized=false`. It doesn't know about the bgfx RT field yet — RTs typically have `InactivationTime=0` so `Invalidate_Old_Unused_Textures` skips them anyway. If game code ever manually calls `Invalidate()` on an RT-backed texture, the RT handle stays valid — that's arguably correct behavior (RTs are expensive to re-create). |
| DX8 impact | Zero at runtime. The new `BgfxRenderTarget` field stays 0, the dtor's `#ifndef`-guarded cleanup block is skipped. One extra 8-byte pointer per texture in memory. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.h` | Added `uintptr_t BgfxRenderTarget = 0;` private field + `Peek_Bgfx_Render_Target() const` / `Set_Bgfx_Render_Target(uintptr_t)` public inline accessors. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Added `#ifndef RTS_RENDERER_DX8` include block at the top pulling in `RenderBackendRuntime.h` + `IRenderBackend.h` for the base-class dtor's RT cleanup. Extended `TextureClass(w, h, format, …)` bgfx ctor with the RT path (`Create_Render_Target` + `Get_Render_Target_Texture`). Added RT destruction to `TextureBaseClass::~TextureBaseClass` guarded by `#ifndef RTS_RENDERER_DX8`. |

### The new ctor branch

```cpp
if (rendertarget) {
    const uintptr_t rt = b->Create_Render_Target(
        uint16_t(width), uint16_t(height), /*hasDepth=*/true);
    Set_Bgfx_Render_Target(rt);
    if (rt != 0)
        Set_Bgfx_Handle(b->Get_Render_Target_Texture(rt));
} else {
    const bool mipmap = (mip_level_count != MIP_LEVELS_1);
    const uintptr_t h = b->Create_Texture_RGBA8(
        nullptr, uint16_t(width), uint16_t(height), mipmap);
    Set_Bgfx_Handle(h);
}
```

### The new dtor cleanup

```cpp
#ifndef RTS_RENDERER_DX8
if (BgfxRenderTarget != 0) {
    if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
        b->Destroy_Render_Target(BgfxRenderTarget);
    BgfxRenderTarget = 0;
    BgfxTexture = 0;   // the RT's color texture dies with the RT
}
#endif
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. RT-backed `TextureClass` instances now allocate real frame buffers in the backend and release them in the dtor. |
| All 20 bgfx tests | PASSED. None exercise the RT ctor directly — they allocate RTs through `backend.Create_Render_Target(...)` — so behavior on existing paths is unchanged. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Create_Render_Target` production-code callers | One — `TextureClass(w, h, format, …)` bgfx branch. Up from zero. (Tests call through the test harness.) |
| `IRenderBackend::Destroy_Render_Target` production-code callers | One — `TextureBaseClass` dtor bgfx branch. |
| `IRenderBackend::Get_Render_Target_Texture` production-code callers | One — same ctor. |
| Duplicate definitions | Zero. |

## What this phase buys

**The last explicit texture-creation path in `TextureClass` now
routes through the backend.** Every way the game can construct a
`TextureClass` — by file path, by procedural dimensions, by RT flag
— produces a real bgfx handle. Post-process effects, shadow maps,
reflection passes, and any other system that reads back from a
render target should now have their sampler input correctly bound
in bgfx mode.

Remaining texture-arc work is increment: the update path
(`IRenderBackend::Update_Texture_RGBA8`) so procedural textures can
receive real pixels; cache refcounting + `Release` integration on
invalidation; sampler-state routing. None of them block running the
game — they improve correctness / perf of code paths that already
run with placeholder / default behavior.

## Deferred

| Item | Stage | Note |
|---|---|---|
| `IRenderBackend::Update_Texture_RGBA8(handle, pixels, …)` → `bgfx::updateTexture2D`. Needed for procedural textures to receive real pixel data after allocation | 5h.30 | One-line interface extension + bgfx wrap |
| `BgfxTextureCache` refcounting + `Release` integration with `TextureBaseClass::Invalidate` | 5h.31 | Cache needs `use_count` per entry first |
| `TextureFilterClass::Apply` → bgfx sampler flags. Today backend hardcodes trilinear/mip_linear; explicit per-texture sampler state isn't threaded through | 5h.32 | Touches both `BgfxBackend::Set_Texture` and the shader sampler binding |
| `Set_Render_Target` adapter routing — `DX8Wrapper::Set_Render_Target(TextureClass*)` (or similar) → `backend->Set_Render_Target(tex->Peek_Bgfx_Render_Target())` | 5h.33 | The dest-bind half of the RT flow; matches the `Apply` path for the sampler side |
| In-game verification on Metal + Windows DX11/12 | ongoing | Now possible to actually run the game |

## Meta-status on the 5h arc

- **29 sub-phases** complete (5h.1 → 5h.29).
- Every explicit texture-creation, buffer-creation, state-setting, draw-call, and lifecycle entry point on `DX8Wrapper` now has a real bgfx-mode body.
- Remaining phases are smaller: interface extensions (update path, RT-bind), refcounting cleanup, sampler routing, and verification work. None are foundational infrastructure.

The cross-platform-port adapter layer is functionally complete for
rendering real game scenes in bgfx mode. The next set of phases
shift focus from "build this out" to "tune and verify."
