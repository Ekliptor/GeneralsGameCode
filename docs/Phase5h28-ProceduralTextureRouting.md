# Phase 5h.28 — Procedural texture routing

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-eighth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h27-TextureCacheWireup.md`.

## Scope gate

5h.27 closed the file-loaded texture path: game code that constructs
`TextureClass("path/to/texture.dds", ...)` now reaches the bgfx
backend with a real bgfx texture handle bound to the sampler. The
deferred table flagged **procedural textures** — those built through
the `TextureClass(width, height, format, ...)` ctor rather than from
a file — as 5h.28's target.

Procedural textures cover:
- UI paint buffers (radar icons, minimap sprites).
- Dynamically-generated content (damage decals, scrolling effects).
- Render-target textures (post-process buffers — though full
  render-target handling needs more work, documented below).

Pre-5h.28, the bgfx-mode stub of this ctor (from 5h.25) just stored
metadata. No backend allocation happened; `Apply(stage)` bound the
backend's 2×2 white placeholder.

5h.28 allocates an empty bgfx texture of the right dimensions so
`Apply` binds something storage-sized. Two small changes:

1. **Backend accepts null pixels.** `BgfxBackend::Create_Texture_RGBA8`
   grew a `pixels == nullptr` code path that allocates an uninitialized
   bgfx texture (via `bgfx::createTexture2D` with `mem == nullptr`).
2. **Procedural ctor wires the allocation.** The `(width, height, format, …)`
   `TextureClass` ctor now calls `Create_Texture_RGBA8(nullptr, …)` and
   stores the resulting handle via `Set_Bgfx_Handle`.

Render-target-flagged textures (`rendertarget == true`) stay as a
placeholder — those need `IRenderBackend::Create_Render_Target` which
returns a frame-buffer handle, not a texture handle directly. That's a
separate slice (5h.29+).

## Locked decisions

| Question | Decision |
|---|---|
| Null-pixel behavior in `Create_Texture_RGBA8` | Allocate an uninitialized bgfx texture; return the handle. Earlier the function returned 0 on null input — that was strictly safer (never produce a texture the caller can't immediately sample), but it blocked the procedural path entirely. Contents of the uninitialized texture are driver-dependent until a later write; callers must fill before sampling. |
| Format | Stays RGBA8 in the bgfx backend regardless of the `WW3DFormat format` argument the game passes. Game-side formats (WW3D_FORMAT_A8R8G8B8, _DXT1, etc.) are a DX8 concern; the bgfx texture stores pixels in its native RGBA8 and shaders receive linearized data. Re-format translation can happen when an Update_Texture path lands and the caller provides real pixel data. |
| Render-target flag | Not wired in this phase. Textures created with `rendertarget = true` still end up with a zero handle, so `Apply` binds the placeholder. Proper render-target handling needs the backend to expose `Get_Render_Target_Texture(rtHandle)` → `uintptr_t` as a sampler handle (already exists, but not integrated with the texture-class path yet). Deferred to 5h.29+. |
| Mip map selection | Uses `mip_level_count != MIP_LEVELS_1` as the hint — if the caller asked for a single level, no mips. Matches what the DX8 ctor does when it creates the D3D texture. |
| Width/height zero guard | Yes: `if (!rendertarget && width > 0 && height > 0)`. The `(SurfaceClass*, mip)` and `(IDirect3DBaseTexture8*)` ctors pass width/height 0 and rely on later introspection; those still skip the allocation. |
| Update path | Deferred. `bgfx::updateTexture2D` is the corresponding API, and `IRenderBackend` doesn't expose it yet. When it does (future slice), callers can fill the empty procedural textures with real pixels. |
| DX8 impact | Zero. Both edits are in bgfx-only code paths. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `Create_Texture_RGBA8`: dropped the `pixels == nullptr` early-return. Now null pixels create an uninitialized texture via `bgfx::createTexture2D(…, nullptr)`. Non-null pixels still go through `bgfx::copy(pixels, byteSize)` as before. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Extended `TextureClass(w, h, format, …)` bgfx-mode ctor to call `Create_Texture_RGBA8(nullptr, w, h, mipmap)` when `!rendertarget && w > 0 && h > 0` and store the handle via `Set_Bgfx_Handle`. |

### Key code

```cpp
// BgfxBackend.cpp
const bgfx::Memory* mem = nullptr;
if (pixels != nullptr) {
    const uint32_t byteSize = uint32_t(width) * uint32_t(height) * 4u;
    mem = bgfx::copy(pixels, byteSize);
}
bgfx::TextureHandle handle = bgfx::createTexture2D(
    width, height, hasMips, 1, bgfx::TextureFormat::RGBA8, flags, mem);

// texture.cpp (bgfx-mode ctor)
if (!rendertarget && width > 0 && height > 0) {
    if (IRenderBackend* b = RenderBackendRuntime::Get_Active()) {
        const bool mipmap = (mip_level_count != MIP_LEVELS_1);
        const uintptr_t h = b->Create_Texture_RGBA8(
            nullptr,
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            mipmap);
        Set_Bgfx_Handle(h);
    }
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `corei_bgfx` recompiled with the extended `Create_Texture_RGBA8`; `z_ww3d2` picked up the new procedural ctor path. |
| All 20 bgfx tests | PASSED. None exercise the procedural ctor directly (tests build textures through `backend.Create_Texture_RGBA8(...)` with real pixels); behavior on the existing paths is unchanged. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `Create_Texture_RGBA8(nullptr, …)` call sites | One production caller — the procedural `TextureClass` ctor. Harness tests all pass non-null pixels. |
| Null-pixel path at runtime (for existing tests) | Unreachable; tests always supply pixel data. Null-pixel only fires when production game code constructs a procedural `TextureClass`. |
| Procedural `TextureClass` now yields a non-zero `BgfxTexture` | Yes, provided a backend is active and dimensions are non-zero. |

## Deferred — remaining 5h arc work

| Item | Stage | Note |
|---|---|---|
| Render-target flag (`rendertarget == true`) wires to `Create_Render_Target` + `Get_Render_Target_Texture` | 5h.29 | Needs a design decision: does the texture handle hold the RT's color attachment or the RT itself? |
| Update path: `IRenderBackend::Update_Texture(handle, pixels, ...)` so procedural textures can be filled after allocation | 5h.30 | Wraps `bgfx::updateTexture2D`. One-line addition to the interface + backend impl. |
| `BgfxTextureCache::Release` integration in `TextureBaseClass::Invalidate` | 5h.31 | Needs refcounting on the cache first — multiple `TextureClass` instances can share a path, invalidating one mustn't drop the handle the others are using. |
| Sampler state: `TextureFilterClass::Apply` forwarding to bgfx sampler flags instead of DX8 stage state | 5h.32 | Today the backend hardcodes trilinear for mips / point for no-mips. Real sampler routing is a backend + shader slice. |
| In-game verification on Metal + Windows DX11/12 | Windows CI | Still the biggest unknown |

## What this phase buys

One concrete new capability: **procedural textures allocate backend
storage**. The game's UI system, scrolling decals, and any code path
that constructs a `TextureClass` via `(w, h, format, ...)` now gets a
bgfx texture handle of the right dimensions (though empty contents).
When the update path lands in 5h.30, those handles can hold real
pixel data.

The backend's `Create_Texture_RGBA8` is now a general-purpose
allocator — any future bgfx-mode caller that needs an empty texture
allocates through the same entry point. That's the kind of primitive
the next phases (render targets, texture updates) build on top of.

## Meta-status on 5h arc

- Total phases in 5h arc: **28** (5h.1 → 5h.28).
- Total production code lines changed: roughly **2,500** (state-surface adapters + un-`#ifdef` work + backend wiring).
- Total test count: **20** bgfx tests, all green; DX8 reconfigure-clean after every phase.
- End-to-end data-plane coverage in bgfx mode: transforms, lights (4-slot), materials, shaders, viewport, vertex/index buffers, draw calls, textures (file-loaded + procedural).

The cross-platform-port adapter is no longer the blocker for the
bgfx backend to render real game scenes. From this point forward,
the work shifts from "make the code compile + link" to "run the game
and fix what's actually wrong."
