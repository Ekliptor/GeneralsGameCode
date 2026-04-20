# Phase 5h.30 — Texture update path

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirtieth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h29-RenderTargetTextureRouting.md`.

## Scope gate

Phase 5h.28 added a null-pixel code path to
`IRenderBackend::Create_Texture_RGBA8`, letting procedural textures
allocate without initial pixel data. Phase 5h.29 wired the RT variant
through `Create_Render_Target`. Both left a gap: **once a procedural
texture has allocated storage, there's no way to upload real pixels
into it**. The contents stay uninitialized until the next phase adds
an update path.

5h.30 closes that gap with a one-method interface extension:

```cpp
virtual void Update_Texture_RGBA8(uintptr_t handle,
                                  const void* pixels,
                                  uint16_t width,
                                  uint16_t height) = 0;
```

`BgfxBackend` wraps `bgfx::updateTexture2D` at mip level 0. Callers
that want to fill a procedural texture (UI paint buffers, damage
decals, scrolling effects) now have an entry point.

## Locked decisions

| Question | Decision |
|---|---|
| Full-texture update only, no partial rects | Yes for this phase. `bgfx::updateTexture2D` supports an `(x, y, w, h)` rect + bytes-per-row, but exposing that through the interface adds parameters without an immediate caller. Keep the API at "replace all pixels" until a partial-update caller shows up. |
| Mip level 0 only | Yes. The update targets the base mip; no mip regeneration. For a procedural texture allocated with `mipmap = false` this is the only level that matters. For `mipmap = true` callers that want a full chain refill, a future `Update_Texture_With_Mips` could accept a pre-computed chain — deferred. |
| `handle == 0` / `pixels == nullptr` | Silent no-op. Simplifies callers that may hold an unallocated handle (e.g. `TextureClass` instances constructed before the backend was ready). Same defensive stance as `Destroy_Texture`. |
| Width/height zero | Same — no-op. Matches `Create_Texture_RGBA8` gating from 5h.28. |
| `bgfx::copy` vs `bgfx::makeRef` | `bgfx::copy` — same pattern 5h.28's `Create_Texture_RGBA8` uses. Copies the data into bgfx's internal buffer; callers can free or mutate their pixel buffer immediately. `makeRef` would be faster but requires the caller to keep the buffer alive until bgfx renders the frame, which complicates lifetime. Can revisit when a perf-critical caller surfaces. |
| Format | RGBA8, same as Create. A `WW3DFormat`-parameterized Update would need format translation; out of scope. |
| DX8 impact | The new virtual method is a pure virtual on `IRenderBackend` — DX8 doesn't implement this interface (the adapter still uses `DX8CALL` for texture operations). The DX8 build is unaffected. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Added `virtual void Update_Texture_RGBA8(uintptr_t, const void*, uint16_t, uint16_t) = 0;`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Added `override` declaration. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Implemented: null/zero-guard, handle validity check, `bgfx::updateTexture2D(handle, 0, 0, 0, 0, w, h, bgfx::copy(pixels, w*h*4))`. |

### New files

| File | Purpose |
|---|---|
| `tests/tst_bgfx_update/{main.cpp, CMakeLists.txt}` | End-to-end capture test: allocate 4×4 empty texture, update with solid red, render a full-NDC quad sampling the texture to a 32×32 RT, capture + assert center pixel is red within ±3 LSB. |

### The backend body

```cpp
void BgfxBackend::Update_Texture_RGBA8(uintptr_t handle,
                                        const void* pixels,
                                        uint16_t width,
                                        uint16_t height)
{
    if (!m_initialized || handle == 0 || pixels == nullptr || width == 0 || height == 0)
        return;

    auto* owned = reinterpret_cast<bgfx::TextureHandle*>(handle);
    if (!bgfx::isValid(*owned)) return;

    const uint32_t byteSize = uint32_t(width) * uint32_t(height) * 4u;
    bgfx::updateTexture2D(*owned, 0, 0, 0, 0, width, height,
                          bgfx::copy(pixels, byteSize));
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK. `BgfxBackend` now overrides `Update_Texture_RGBA8`. |
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| `tst_bgfx_update` | PASSED — center pixel is BGRA 0,0,255,255 (red) within tolerance. Without Update the center would be uninitialized driver-dependent garbage. |
| All 21 bgfx tests (20 prior + new `tst_bgfx_update`) | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Update_Texture_RGBA8` callers | One — `tst_bgfx_update`. Production game-code callers land in a future slice (5h.31+) when the surface-update path routes through this. |
| `bgfx::updateTexture2D` call sites in the backend | One — inside `BgfxBackend::Update_Texture_RGBA8`. |

## What this phase buys

**Procedural textures can now receive real pixels.** Combined with
5h.28 (null-allocation) + 5h.29 (RT allocation), every kind of
texture the game constructs can now:

1. Allocate backend storage of the right dimensions.
2. Be populated with real data (via the file loader for disk textures,
   or via `Update_Texture_RGBA8` for CPU-generated content).
3. Bind to a shader sampler and render.

The game's dynamic texture update paths — `IDirect3DSurface8::LockRect` /
`UnlockRect`, `IDirect3DTexture8::LockRect` — would route through this
backend method once the adapter's `TextureBaseClass::Load_Locked_Surface`
and related Surface paths wire it in. That wire-up is the next phase
(5h.31) once the Surface class itself is un-`#ifdef`'d enough to
compile in bgfx mode.

## Deferred

| Item | Stage | Note |
|---|---|---|
| Partial-rect updates (`Update_Texture_RGBA8_Rect`) | later | No caller yet |
| Mip-chain updates | later | Same — no caller |
| Surface-path integration: when the game calls `IDirect3DTexture8::LockRect` / `UnlockRect` on a bgfx-allocated texture, route to `Update_Texture_RGBA8` | 5h.31 | Blocked on `surface.cpp` un-`#ifdef` |
| `BgfxTextureCache` refcounting + `Invalidate` integration | 5h.32 | Separate concern; doesn't block above |
| Sampler-state routing from `TextureFilterClass::Apply` | 5h.33 | Separate concern |

## Meta-status on the 5h arc

- **30 sub-phases** complete (5h.1 → 5h.30).
- **21 bgfx tests**, all green; DX8 reconfigures clean after every phase.
- Every data-plane primitive the game's renderer needs now has a bgfx-mode
  implementation: transforms, lights, materials, shaders, viewport,
  vertex/index buffers, draws, file-loaded textures, procedural textures,
  render-target textures, **and texture updates**. The IRenderBackend
  interface surface is 26 virtual methods — all implemented.

The remaining 5h phases target integration cleanup and verification
rather than new capabilities. After 5h.30 the backend is functionally
complete.
