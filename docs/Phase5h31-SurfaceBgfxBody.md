# Phase 5h.31 — SurfaceClass bgfx-mode body

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-first sub-phase
of the DX8Wrapper adapter. Follows `Phase5h30-TextureUpdatePath.md`.

## Scope gate

The 5h.30 doc flagged this slice: "when the game calls
`IDirect3DTexture8::LockRect` / `UnlockRect` on a bgfx-allocated
texture, route to `Update_Texture_RGBA8` — blocked on `surface.cpp`
un-`#ifdef`." That's what 5h.31 delivers.

`Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp` was wrapped in
a single `#ifdef RTS_RENDERER_DX8 … #endif` guard (lines 51 and 938 of
the pre-patch file). The entire 880-line body was therefore absent in
bgfx builds, meaning **any bgfx-mode caller that constructed a
`SurfaceClass` would have been an unresolved external**. No such
caller exists today — the bgfx-mode `TextureClass::Apply_New_Surface`
stub from 5h.26 swallows the `SurfaceClass*` argument — so the link
never failed. But the instant a surface-path feature is brought back
online, it would have.

5h.31 replaces the wholesale `#endif` at the end of the file with a
`#else` branch containing a bgfx-mode implementation backed by a CPU
pixel buffer. The new body honors the full header API; each method
either operates on the CPU buffer or delegates to
`IRenderBackend::Update_Texture_RGBA8` when the surface has been
associated with a bgfx texture handle.

## Locked decisions

| Question | Decision |
|---|---|
| CPU-backed storage vs pure bgfx-side storage | CPU-backed. Surface operations are inherently CPU pixel manipulation (Clear, Copy, FindBB, Is_Transparent_Column). bgfx has no read-modify-write path for textures anyway; you upload whole rects via `updateTexture2D`. The natural shape is: edit a CPU buffer → push the buffer to the GPU on Unlock. |
| Route into `Update_Texture_RGBA8` when? | Three trigger points: `Unlock()`, `Clear()`, `Copy(*)`. Any operation that mutates the CPU buffer is followed by an upload. Keeps the GPU-visible copy in sync without the caller needing to know a bgfx handle exists. |
| Surface → texture association | New header method `Set_Associated_Texture(uintptr_t)` that the caller invokes with a handle from `Create_Texture_RGBA8`. Zero-handle is the default → Unlock becomes a pure CPU op. Associating is explicit, never implicit. |
| Which formats route through `Update_Texture_RGBA8`? | Only `A8R8G8B8` / `X8R8G8B8` (BGRA8 in memory → swizzled into RGBA8 before upload). Other formats edit the CPU buffer silently; trying to upload 565/4444 through a `Update_Texture_RGBA8` method that speaks RGBA8 would be a type error. When a caller needs those, a format-aware Update path lands in a follow-on phase. |
| DX8 surface pointer member | `D3DSurface` is kept (it's in the header's private section and the DX8 branch defines it). In bgfx mode it stays `nullptr`; `Peek_D3D_Surface()` returns `nullptr`; `Attach`/`Detach` become no-ops. Keeps the header uniform. |
| Filename ctor (`SurfaceClass(const char*)`) | Allocates a placeholder 1×1 opaque-black surface. File loading needs a bimg-backed decoder (deferred, parallel to 5h.27's `BgfxTextureCache`). The ctor logs a one-shot warning so real callers surface during bring-up. |
| `IDirect3DSurface8*` ctor | bgfx has no IDirect3DSurface8 analogue. Ctor sets `D3DSurface = nullptr` and leaves `CpuPixels = nullptr`. Any subsequent `Lock()` returns `nullptr` (the DX8 branch is what exercises this path; bgfx callers shouldn't hit it, and if they do they get a well-defined null). |
| `Stretch_Copy` | Stub with a one-shot log. DX8 path delegates to `D3DXLoadSurfaceFromSurface` (filtered resampling). No bgfx-mode caller today; a CPU-side box filter can land when one surfaces. |
| `Hue_Shift` | Stub with a one-shot log. Requires the `Convert_Pixel`/`Recolor` helpers that live in the DX8-branch part of the TU. Not worth duplicating for zero current callers. |
| `::Get_Bytes_Per_Pixel` dependency | Replaced with a local `Surface_BPP()` in an anonymous namespace. Copies the table from `ww3dformat.cpp`. Reason: `ww3dformat.cpp` transitively includes `dx8wrapper.h` → `lightenvironment.h` → the full z_ww3d2 header graph — none of which the test harness can realistically pull in without re-introducing the link chain that we're specifically trying to keep off the critical path. |
| DX8 impact | None — DX8 branch body is byte-identical; the only structural change is `#endif // RTS_RENDERER_DX8` moved to the very end of the file with a `#else` branch above it. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.h` | Added `Set_Associated_Texture(uintptr_t)` / `Get_Associated_Texture()` (bgfx-only) plus four private members (`CpuPixels`, `CpuWidth`, `CpuHeight`, `CpuPitch`, `AssociatedTextureHandle`) under `#ifndef RTS_RENDERER_DX8`. |
| `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp` | Replaced trailing `#endif` with `#else` + full bgfx-mode body (~250 lines). File size grows from 938 → 1225 lines. |

### New files

| File | Purpose |
|---|---|
| `tests/tst_bgfx_surface/{main.cpp, CMakeLists.txt}` | Smoke test: allocate empty 4×4 bgfx texture, allocate SurfaceClass(4, 4, A8R8G8B8), `Set_Associated_Texture`, lock, fill red, unlock (→ auto-upload via `Update_Texture_RGBA8`), render quad sampling the texture into 32×32 RT, capture, assert center pixel is red within ±3 LSB. |

### The `Upload_To_Associated_Texture` routing helper

```cpp
void Upload_To_Associated_Texture(uintptr_t handle, WW3DFormat fmt,
    const unsigned char* src, unsigned w, unsigned h, unsigned pitch)
{
    if (handle == 0 || src == nullptr || w == 0 || h == 0) return;
    IRenderBackend* backend = RenderBackendRuntime::Get_Active();
    if (backend == nullptr) return;

    const bool isBGRA8 = (fmt == WW3D_FORMAT_A8R8G8B8 || fmt == WW3D_FORMAT_X8R8G8B8);
    if (!isBGRA8) return;

    const unsigned byteSize = w * h * 4u;
    unsigned char* rgba = new unsigned char[byteSize];
    for (unsigned y = 0; y < h; ++y) {
        const unsigned char* row = src + y * pitch;
        unsigned char* drow = rgba + y * w * 4u;
        for (unsigned x = 0; x < w; ++x) {
            drow[x*4 + 0] = row[x*4 + 2]; // R <- B
            drow[x*4 + 1] = row[x*4 + 1]; // G
            drow[x*4 + 2] = row[x*4 + 0]; // B <- R
            drow[x*4 + 3] = (fmt == WW3D_FORMAT_A8R8G8B8) ? row[x*4 + 3] : 0xFF;
        }
    }
    backend->Update_Texture_RGBA8(handle, rgba, uint16_t(w), uint16_t(h));
    delete[] rgba;
}
```

BGRA8 (memory order for `A8R8G8B8`) → RGBA8 swizzle is done in a
throwaway buffer so the CPU-side surface layout stays stable for
subsequent reads.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. The bgfx-branch `SurfaceClass` methods now contribute symbols to `surfaceclass.cpp.o` in `libz_ww3d2.a`. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — unchanged. |
| `cmake --build build_bgfx --target tst_bgfx_surface` | OK. |
| `tst_bgfx_surface` | PASSED — center pixel is BGRA 0,0,255,255 (red) within tolerance. |
| All 22 bgfx tests (21 prior + `tst_bgfx_surface`) | PASSED. |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `#ifdef RTS_RENDERER_DX8` / `#else` in `surfaceclass.cpp` | Now matched; 5h.31 added the `#else` branch. |
| `SurfaceClass::` method definitions outside the `#ifdef` | 17 in the bgfx `#else` branch (matches the header's public surface minus a couple of trivial inline accessors). |
| `::Get_Bytes_Per_Pixel` calls inside the bgfx branch | Zero — all routed through local `Surface_BPP()`. |
| `IRenderBackend::Update_Texture_RGBA8` callers in production code | One — `Upload_To_Associated_Texture` in `surfaceclass.cpp`'s bgfx branch. The RT-path doesn't upload, so this is the sole production-side caller (plus `tst_bgfx_update` / `tst_bgfx_surface` in tests). |

## What this phase buys

**SurfaceClass is no longer a phantom type in bgfx builds.** A bgfx-mode
caller can:

1. `NEW_REF(SurfaceClass, (w, h, WW3D_FORMAT_A8R8G8B8))` — get real storage.
2. `Lock() → memset/memcpy/plot → Unlock()` — edit pixels.
3. Optionally `Set_Associated_Texture(handle)` — wire the surface to a
   `BgfxTexture`.
4. Any subsequent mutation (Lock/Unlock, Clear, Copy) auto-uploads into
   the bound texture via `IRenderBackend::Update_Texture_RGBA8`.

That makes the 5h.30 interface method reachable from the game's
existing surface-driven update paths without a rewrite of those paths.
`font3d.cpp` / `render2dsentence.cpp` / `bmp2d.cpp` / `W3DAssetManager
.cpp` all `NEW_REF(SurfaceClass, …)` and then blit into the surface;
under bgfx those blits now survive the compile + link, and the pixel
result is visible to whatever bgfx texture the surface is tied to.

## Deferred

| Item | Stage | Note |
|---|---|---|
| bimg-backed file-ctor (`SurfaceClass(const char*)`) | later | Parallel to `BgfxTextureCache::Get_Or_Load_File`; likely folded into a single "surface-from-file" helper once a bgfx caller needs it |
| Format-aware upload (non-RGBA8 paths) | later | Requires `IRenderBackend::Update_Texture` that takes a format enum |
| CPU-side `Stretch_Copy` (box filter) | later | No bgfx caller today |
| `Hue_Shift` / `Is_Monochrome` with full palette of pixel formats | later | Would require lifting `Convert_Pixel`/`Recolor` out of the DX8 branch |
| `TextureClass::Apply_New_Surface` → wire the surface's handle into the TextureClass's bgfx handle | 5h.32 | Closes the other half of the surface-path integration: a caller that constructs a TextureClass from a SurfaceClass should inherit the surface's CPU buffer as its GPU-side storage. |
| `BgfxTextureCache` refcounting + `Invalidate` integration | 5h.33 | Previously 5h.32 in the 5h.30 doc; bumped by one because the surface→texture wire-up is the more natural immediate follow-on |
| Sampler-state routing from `TextureFilterClass::Apply` | 5h.34 | Unchanged |

## Meta-status on the 5h arc

- **31 sub-phases** complete (5h.1 → 5h.31).
- **22 bgfx tests**, all green; DX8 reconfigures clean after every phase.
- The `IRenderBackend` interface surface stays at 26 virtual methods —
  no new backend methods this phase. What landed is a production caller
  for the 5h.30 method: `SurfaceClass::Unlock()` now reaches
  `IRenderBackend::Update_Texture_RGBA8` through `surfaceclass.cpp`'s
  bgfx branch.

Backend capability: unchanged from 5h.30 (functionally complete). What
5h.31 fills in is adapter-side: **`SurfaceClass` is now a first-class
citizen in bgfx mode** instead of a compile-time ghost.
