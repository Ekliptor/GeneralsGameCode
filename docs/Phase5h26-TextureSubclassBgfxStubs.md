# Phase 5h.26 — ZTexture / CubeTexture / VolumeTexture bgfx stubs

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-sixth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h25-TextureClassBgfxStubs.md`.

## Scope gate

5h.25 added minimum-viable bgfx-mode stubs for the canonical
`TextureClass`. This phase does the same for the three remaining
texture subclasses:

* `ZTextureClass` (depth-stencil textures; direct subclass of
  `TextureBaseClass`, single ctor).
* `CubeTextureClass` (cubemaps; inherits `TextureClass`, four ctors
  mirroring the parent's public overloads).
* `VolumeTextureClass` (3D textures; inherits `TextureClass`, same
  four-ctor shape plus an extra `depth` parameter on one overload).

All nine ctors + four virtual overrides + `ZTextureClass::
Get_D3D_Surface_Level` land as stub bodies in the `#ifndef
RTS_RENDERER_DX8` block added in 5h.25. No new TU; the bgfx-mode
texture implementations sit in one contiguous block at the top of
`texture.cpp`.

With this phase the entire texture class hierarchy is linkable in
bgfx mode. Every `TextureBaseClass*` / `TextureClass*` /
`CubeTextureClass*` / `VolumeTextureClass*` / `ZTextureClass*`
reference in the game now compiles + links. Textures still don't
render — that's 5h.27's `BgfxTextureCache` wire-up.

## Locked decisions

| Question | Decision |
|---|---|
| Inherit from `TextureClass` vs `TextureBaseClass` | `ZTextureClass` inherits `TextureBaseClass` directly (matches its DX8 declaration). `CubeTextureClass` + `VolumeTextureClass` both inherit `TextureClass` and delegate most init to the parent ctor. Stub ctors forward to the matching `TextureClass` ctor where possible. |
| Cube ctors that take `SurfaceClass*` / `IDirect3DBaseTexture8*` | Delegate to `TextureClass`'s stub ctor of the same shape. Bodies are empty beyond that (the parent handles base-class init). |
| Volume ctor with `depth` parameter | The `(w, h, depth, fmt, ...)` ctor captures `depth` into the protected `Depth` member. Other volume ctors default `Depth = 0`. |
| `ZTextureClass::Apply` | No-op. Depth textures aren't sampled by the uber-shader (bgfx render targets handle their own depth attachments via `Create_Render_Target(width, height, hasDepth=true)`). |
| `ZTextureClass::Get_D3D_Surface_Level` | Returns `nullptr`. Callers in bgfx mode don't touch z-texture surface levels. |
| `Apply_New_Surface` overrides | Minimal: toggle `Initialized` / `InactivationTime` flags (same pattern as `TextureClass::Apply_New_Surface`). No actual D3D texture pointer management. |
| `IsProcedural` / `Initialized` defaults | Match each DX8 ctor's defaults where observable. `ZTextureClass` marks itself procedural (no file-loading path); cube/volume ctors inherit parent behavior. |
| DX8 impact | Zero. All bodies sit inside `#ifndef RTS_RENDERER_DX8`. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Extended the Phase 5h.25 `#ifndef RTS_RENDERER_DX8` block with: `ZTextureClass::{ctor, Apply, Apply_New_Surface, Get_D3D_Surface_Level, Get_Texture_Memory_Usage}`, `CubeTextureClass::{ctor × 4, Apply_New_Surface}`, `VolumeTextureClass::{ctor × 4, Apply_New_Surface}`. All delegate to parent ctors or set minimum state. |

### ZTextureClass stub body

```cpp
ZTextureClass::ZTextureClass
(
    unsigned width, unsigned height,
    WW3DZFormat zformat,
    MipCountType mip_level_count,
    PoolType pool
)
:   TextureBaseClass(width, height, mip_level_count, pool,
                     /*rendertarget=*/true, /*reducible=*/false),
    DepthStencilTextureFormat(zformat)
{
    Initialized  = true;
    IsProcedural = true;
    LastAccessed = WW3D::Get_Sync_Time();
}

void ZTextureClass::Apply(unsigned int)                            {}
void ZTextureClass::Apply_New_Surface(…, bool init, bool)          { if (init) Initialized = true; }
IDirect3DSurface8* ZTextureClass::Get_D3D_Surface_Level(unsigned)  { return nullptr; }
unsigned ZTextureClass::Get_Texture_Memory_Usage() const           { return 0; }
```

### CubeTextureClass pattern

```cpp
CubeTextureClass::CubeTextureClass(unsigned w, unsigned h, WW3DFormat f, …)
:   TextureClass(w, h, mip, pool, rt, f, reduce) {
    Initialized  = true;
    IsProcedural = true;
    LastAccessed = WW3D::Get_Sync_Time();
}

CubeTextureClass::CubeTextureClass(const char* name, const char* path, …)
:   TextureClass(name, path, mip, fmt, compress, reduce) {}

CubeTextureClass::CubeTextureClass(SurfaceClass* s, MipCountType m)
:   TextureClass(s, m) {}

CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d)
:   TextureClass(d3d) {}
```

Volume ctors are structurally identical plus a `Depth` assignment.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. All nine subclass ctors + four virtual overrides + `ZTextureClass::Get_D3D_Surface_Level` link in bgfx mode. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep — **full** texture class hierarchy, linkable in bgfx mode:

| Class | Link status |
|---|---|
| `TextureBaseClass` (22 methods) | Yes (Phases 5h.19 → 5h.24) |
| `TextureClass` (5 ctors + 4 virtual overrides) | Yes (Phase 5h.25) |
| `TextureFilterClass` (ctor) | Yes (Phase 5h.25) |
| `ZTextureClass` (ctor + 4 methods) | Yes (this phase) |
| `CubeTextureClass` (4 ctors + 1 override) | Yes (this phase) |
| `VolumeTextureClass` (4 ctors + 1 override) | Yes (this phase) |
| `TextureLoader` (4 entry points) | Yes (stubs from 5h.22 + 5h.25) |
| `MissingTexture::_Get_Missing_Texture` | Yes (Phase 5h.22) |
| `WW3DAssetManager::TheInstance` (static) | Yes (Phase 5h.23) |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `BgfxTextureCache` wire-up in `TextureClass::Init` + `Apply`: load texture file → upload to bgfx → store handle in `BgfxTexture` field (added in Phase 5h.4) → bind on Apply through `IRenderBackend::Set_Texture` | 5h.27 | All prerequisites in place — this is purely adding lines to the existing 5h.25 stubs |
| First game-loaded DDS renders in bgfx mode | after 5h.27 | End-to-end texture path |

## What this phase buys

Vacuous in isolation — three texture subclasses that nothing in
bgfx-mode code currently constructs become constructible. But every
game-code path that holds a `TextureBaseClass*` pointer (materials,
render objects, scene graph nodes) can now hold any concrete
subclass without pinning the build to DX8-only code.

The full texture hierarchy is now available to the bgfx build. The
texture arc's last remaining milestone is 5h.27's cache wire-up: a
one-phase change to `TextureClass::Init` and `Apply` that routes
through `BgfxTextureCache::Get_Or_Load_File` (the 5h.3 infrastructure
that's been sitting idle). After 5h.27 the game can load a DDS,
bind it to a material, and render textured geometry end-to-end in
bgfx mode.
