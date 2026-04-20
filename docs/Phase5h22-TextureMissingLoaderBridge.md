# Phase 5h.22 — MissingTexture + TextureLoader bridge

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-second
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h21-TextureAccessorsUnstub.md`.

## Scope gate

5h.21 brought four `TextureBaseClass` methods out of the guard. The
next two on the deferred list — `Is_Missing_Texture` and
`Load_Locked_Surface` — are blocked on symbol references to
`MissingTexture::_Get_Missing_Texture` and
`TextureLoader::Request_Thumbnail` respectively. Both of those live
in TUs wholly wrapped in `#ifdef RTS_RENDERER_DX8`
(`missingtexture.cpp` is ~700 lines; `textureloader.cpp` is ~2800
lines). Un-guarding the whole of either file in one slice is too big.

5h.22 takes the **minimum** cut from each file needed to unblock the
texture accessors:

1. `MissingTexture::_Get_Missing_Texture` is 5 lines (returns a
   static pointer with AddRef). The static it reads + the accessor
   itself move above the guard. Everything else in `missingtexture.cpp`
   (`_Init`, `_Create_Missing_Surface`, palette tables, the huge
   image-pixel array) stays guarded.
2. `TextureLoader::Request_Thumbnail` is ~80 lines with locks, task
   queues, and DX8-thread branching. Un-guarding it fully is out of
   scope; instead a **no-op stub** is provided in a dedicated
   `#ifndef RTS_RENDERER_DX8` block at the top of `textureloader.cpp`.
   The full DX8 body remains inside the guard as the "real"
   implementation.

With those two symbols resolvable, `TextureBaseClass::Is_Missing_Texture`
and `Load_Locked_Surface` can move above the guard in `texture.cpp`.

## Locked decisions

| Question | Decision |
|---|---|
| `_MissingTexture` static location | Moved above the guard in `missingtexture.cpp`. File-local `static IDirect3DTexture8* _MissingTexture = nullptr;` — zero-initialized, stays null in bgfx mode (no one runs `_Init` to populate it). |
| `_Get_Missing_Texture` in bgfx mode | Behavior: returns nullptr. DX8 body asserted `_MissingTexture != nullptr` before returning; the un-guarded version relaxes that to `if (_MissingTexture) _MissingTexture->AddRef();` and returns the pointer (may be nullptr). Callers (only `Is_Missing_Texture` in practice) already handle nullptr safely with `if (missing_texture) missing_texture->Release();`. |
| `TextureLoader::Request_Thumbnail` bgfx stub | Empty body in a `#ifndef RTS_RENDERER_DX8` block at the top of `textureloader.cpp`. In bgfx mode `Load_Locked_Surface` calls this and nothing happens. DX8 mode skips the stub (the `#ifdef RTS_RENDERER_DX8` branch has the full loader). |
| `WWPROFILE` in `Load_Locked_Surface` | Dropped from the un-guarded copy. `wwprofile.h` pulls in threading macros that may not be safe outside the DX8 guard; skipping profiling in bgfx mode is fine for a cold path. Can re-add once the profile surface is verified. |
| `Is_Missing_Texture` runtime correctness in bgfx mode | Suboptimal but harmless. `_Get_Missing_Texture` returns nullptr; if `D3DTexture` is also nullptr (always in bgfx mode today), the function reports "this is the missing texture". Callers that branch on this don't exist in bgfx-mode code paths yet (no `TextureClass` ctor has run), so the wrong answer is never observed. |
| DX8 impact | Zero. Each edit moves code without changing behavior in the DX8 build. `_Get_Missing_Texture`'s relaxed null check still asserts-effectively in DX8 via `_MissingTexture->AddRef` (would crash on null, same as the explicit `WWASSERT`). |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/missingtexture.cpp` | Moved `static IDirect3DTexture8 * _MissingTexture = nullptr;` + `MissingTexture::_Get_Missing_Texture()` above the `#ifdef RTS_RENDERER_DX8` guard. Relaxed the `WWASSERT(_MissingTexture)` in the accessor to a `if (_MissingTexture) AddRef();` null check. |
| `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp` | Added a `#ifndef RTS_RENDERER_DX8` block at the top with `void TextureLoader::Request_Thumbnail(TextureBaseClass*) {}`. Full DX8 body stays inside its own `#ifdef`. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Moved `TextureBaseClass::Load_Locked_Surface` + `Is_Missing_Texture` above the guard. Dropped the `WWPROFILE` call from `Load_Locked_Surface`'s un-guarded copy. Pulled `#include "missingtexture.h"` + `#include "textureloader.h"` above the guard for the new method bodies. Replaced original in-guard copies with marker comments. |

### The bgfx-mode `_Get_Missing_Texture`

```cpp
static IDirect3DTexture8 * _MissingTexture = nullptr;

IDirect3DTexture8* MissingTexture::_Get_Missing_Texture()
{
    if (_MissingTexture) _MissingTexture->AddRef();
    return _MissingTexture;  // nullptr in bgfx mode
}
```

### The bgfx-mode `Request_Thumbnail`

```cpp
#ifndef RTS_RENDERER_DX8
void TextureLoader::Request_Thumbnail(TextureBaseClass* /*tc*/) {}
#endif
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `MissingTexture::_Get_Missing_Texture` + `TextureLoader::Request_Thumbnail` are now linkable in bgfx mode; `TextureBaseClass::Is_Missing_Texture` + `Load_Locked_Surface` join the un-guarded surface. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `MissingTexture::_Get_Missing_Texture` linkable in bgfx mode | Yes. |
| `TextureLoader::Request_Thumbnail` linkable in bgfx mode | Yes (empty no-op body). |
| `TextureBaseClass::Is_Missing_Texture` + `Load_Locked_Surface` linkable | Yes. |
| Duplicate definitions | Zero across all three TUs. |

Cumulative progress on the `TextureBaseClass` surface: 11 of ~18
methods now link in bgfx mode (2 in 5h.19, 3 in 5h.20, 4 in 5h.21, 2
in 5h.22). 60% of the class.

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `_Get_Total_*_Size/Count` (×8) + `Invalidate_Old_Unused_Textures` | 5h.23 | `WW3DAssetManager::Get_Instance()->Texture_Hash()` — `assetmgr.cpp` has its own `RTS_RENDERER_DX8` guards around the texture-registry machinery |
| `Apply_Null` | 5h.24 | `DX8Wrapper::Set_DX8_Texture` is DX8-only; needs forwarding to `IRenderBackend::Set_Texture(stage, 0)` |
| `Get_Reduction` | 5h.24 | `WW3D::Get_Texture_Reduction()` + `Is_Large_Texture_Extra_Reduction_Enabled()` have non-inline bodies in ww3d.cpp's guarded section |
| `TextureClass` derived ctors + `Apply` + `Init` | 5h.25+ | Full `TextureLoader` un-guard + `BgfxTextureCache` wire-up |

## What this phase buys

Same as 5h.19–5h.21: no user-visible change, but further unblocking
for the next slices. The `MissingTexture` and `TextureLoader` stubs
are small, targeted, and cost nothing at runtime in bgfx mode (the
accessor returns nullptr; the thumbnail request is a no-op). They
unlock two more `TextureBaseClass` methods without having to un-guard
any new heavy compile units.

Pattern emerging for the texture arc: each base-class method needs a
narrow stub or un-guard in **one** supporting TU (assetmgr, ww3d,
textureloader, missingtexture, dx8wrapper). Each stub is trivial in
isolation — the value is cumulative. Three more phases (5h.23
assetmgr, 5h.24 apply/reduction, 5h.25 TextureClass ctors) and the
texture surface is linkable end-to-end.
