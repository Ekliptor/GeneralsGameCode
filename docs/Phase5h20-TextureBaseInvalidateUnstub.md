# Phase 5h.20 — TextureBaseClass::Invalidate + WW3D POD statics

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twentieth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h19-TextureBaseUnstub.md`.

## Scope gate

5h.19 un-guarded `TextureBaseClass::TextureBaseClass` (ctor) + `~TextureBaseClass`
(dtor). 5h.20 is the next bite: un-guard three more methods
(`Set_Texture_Name`, `Set_HSV_Shift`, `Invalidate`) — each CPU-only
except `Invalidate` which reads `WW3D::Get_Sync_Time()` (inline accessor
that reads the `WW3D::SyncTime` static).

The `SyncTime` static — and five others commonly called by inline WW3D
accessors — are defined in `ww3d.cpp`'s DX8-only section. 5h.20 pulls
just those six POD statics above the guard as a **cross-TU
prerequisite** for the texture methods. Everything else in
`ww3d.cpp`'s DX8 block (heavy classes like `VertexMaterialClass *
WW3D::DefaultDebugMaterial`, `StaticSortListClass *
WW3D::DefaultStaticSortLists`, `FrameGrabClass * WW3D::Movie`) stays
guarded — those involve compile-unit dependencies (VertexMaterialClass
static init, framegrab, sort-list machinery) that would drag in more
sprawl.

## Locked decisions

| Question | Decision |
|---|---|
| Which WW3D statics to move | Six POD scalars: `SyncTime`, `PreviousSyncTime`, `IsInitted`, `IsRendering`, `FrameCount`, `ThumbnailEnabled`. All `unsigned int` / `bool` / `int` with simple zero/default initializers. |
| Why these six specifically | The inline accessors that read them (`WW3D::Get_Sync_Time`, `Get_Thumbnail_Enabled`, `Is_Initted`, etc.) are called from `TextureBaseClass::Invalidate` + future 5h.21+ methods. Moving these six means every texture-adjacent inline accessor in ww3d.h now has a linkable definition in bgfx mode. |
| Why not move all WW3D statics | The class-typed ones (`FrameGrabClass * Movie`, `StaticSortListClass *`, etc.) either require linking additional classes or are behind DX8-only constructor calls (`ShaderClass WW3D::DefaultDebugShader(DEFAULT_DEBUG_SHADER_BITS)` — constructs a `ShaderClass` with a shader_bits integer, which is actually fine, but let's be conservative). Bundling them would expand the blast radius. |
| Behavior in bgfx mode with the moved statics | In bgfx mode the full WW3D machinery (that would mutate `SyncTime` per frame, toggle `IsInitted` on init, etc.) isn't wired up. The statics sit at their initial values: `SyncTime = 0`, `ThumbnailEnabled = true`, etc. `TextureBaseClass::Invalidate` calling `Get_Sync_Time()` gets `0` and stamps `LastAccessed = 0` — benign, since no later bgfx-mode code reads `LastAccessed` (Invalidate_Old_Unused_Textures stays guarded). |
| Texture methods moved | `Set_Texture_Name` (trivial string copy), `Set_HSV_Shift` (calls `Invalidate` + stores vector3), `Invalidate` (ref-releases `D3DTexture`, flips `Initialized = false`, reads `Get_Sync_Time`). |
| The `/* was battlefield version */` comment block in Invalidate | Original Invalidate had a 40-line commented-out block at the end ("was battlefield version"). Dropped from the moved copy — dead code has no reason to carry into the un-guarded version. DX8 path no longer has it either (it's removed from the file entirely, not just from the un-guarded copy). |
| `Peek_D3D_Base_Texture` / `Set_D3D_Base_Texture` / `Load_Locked_Surface` | Stay guarded. They use DX8 API (`AddRef`) or DX8-bound classes (`TextureLoader::Request_Thumbnail`). Deferred to 5h.21. |
| DX8 impact | Near-zero. POD statics and three method bodies moved above the guard; the DX8 build sees the same definitions in the same TU, just 50 lines earlier in the translation unit. Recompile triggers but emitted code is identical. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp` | Moved `WW3D::SyncTime`, `PreviousSyncTime`, `IsInitted`, `IsRendering`, `FrameCount`, `ThumbnailEnabled` static member definitions above the `#ifdef RTS_RENDERER_DX8` guard at line 85. Commented out the original in-guard definitions (replaced with `// Phase 5h.20 — … moved above the guard.` markers so future readers know where they went). |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Added `TextureBaseClass::Set_Texture_Name`, `Set_HSV_Shift`, `Invalidate` above the `#ifdef RTS_RENDERER_DX8` guard (after the ctor/dtor landed in 5h.19). Replaced the original in-guard definitions with marker comments. |

### The moved WW3D block

```cpp
#include "ww3d.h"

// Phase 5h.20 — POD statics moved above the RTS_RENDERER_DX8 guard so they
// link in bgfx mode. …
unsigned int    WW3D::SyncTime          = 0;
unsigned int    WW3D::PreviousSyncTime  = 0;
bool            WW3D::IsInitted         = false;
bool            WW3D::IsRendering       = false;
int             WW3D::FrameCount        = 0;
bool            WW3D::ThumbnailEnabled  = true;

#ifdef RTS_RENDERER_DX8
#include "rinfo.h"
// … rest of the file unchanged …
```

### TextureBaseClass::Invalidate (unchanged from DX8 behavior)

```cpp
void TextureBaseClass::Invalidate()
{
    if (TextureLoadTask)   return;
    if (ThumbnailLoadTask) return;
    if (IsProcedural)      return;
    if (D3DTexture) {
        D3DTexture->Release();
        D3DTexture = nullptr;
    }
    Initialized = false;
    LastAccessed = WW3D::Get_Sync_Time();
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `TextureBaseClass::Invalidate` + `Set_Texture_Name` + `Set_HSV_Shift` now link in bgfx mode; `WW3D::SyncTime` + 5 other statics are now resolvable. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. (Full DX8 compile needs Windows SDK; the reconfigure + `bgfx` build both passing is the usual sanity signal.) |

Static sweep:

| Pattern | Result |
|---|---|
| `WW3D::SyncTime` defined in bgfx mode | Yes (ww3d.cpp, line 95). |
| `TextureBaseClass::Invalidate` linkable in bgfx mode | Yes. |
| Duplicate definitions of moved statics / methods | Zero. Each symbol defined exactly once in the TU. |
| `Invalidate` recipe path in bgfx | `TextureLoadTask` / `ThumbnailLoadTask` stay nullptr (loader is guarded); `IsProcedural` starts false; `D3DTexture` starts null → Release skipped; `Initialized = false` stamped; `LastAccessed = 0` stamped. |

## Deferred — remaining texture arc

| Item | Stage | Blocker |
|---|---|---|
| `Peek_D3D_Base_Texture` + `Set_D3D_Base_Texture` + `Load_Locked_Surface` + `Is_Missing_Texture` + `Get_Priority` / `Set_Priority` + `Get_Reduction` | 5h.21 | `TextureLoader` + `MissingTexture` un-guard; compat-shim `GetPriority/SetPriority` on `IDirect3DBaseTexture8` |
| `_Get_Total_*_Size/Count` statics | 5h.21 | `WW3DAssetManager::Get_Instance()` — assetmgr.cpp has its own guards |
| `Apply_Null` | 5h.21 | `DX8Wrapper::Set_DX8_Texture` is DX8-only; needs bgfx forwarding |
| `Invalidate_Old_Unused_Textures` | 5h.21 | `WW3DAssetManager::Get_Instance()->Texture_Hash()` + this method is called from `DX8VertexBufferClass::Create_Vertex_Buffer` recovery path — un-ifdef'ing removes the need for the 5h.15 surgical `#ifdef` around the recovery call |
| `TextureClass` ctors + `Apply` + `Init` — full file-loading + per-frame texture bind | 5h.22+ | TextureLoader un-ifdef, substantial |
| First game-loaded DDS renders in bgfx mode | after 5h.22+ | texture bind path lands |

## What this phase buys

Three more `TextureBaseClass` methods link in bgfx mode. The WW3D
static un-guard isn't useful in isolation — but it *was* the blocker
for every texture method that reads `Get_Sync_Time`. With that unblocked,
5h.21 can un-guard the remaining `TextureBaseClass` surface (the
`Peek_` / `Set_` methods, `_Get_Total_*` accessors, `Apply_Null`,
`Invalidate_Old_Unused_Textures`) in a single slice rather than
needing a prerequisite chain.

Cumulative progress on the texture arc: 5 base-class methods now
linkable (5h.19 ctor + dtor, 5h.20 three more). Remaining base-class
surface is ~13 methods, most gated by one additional un-ifdef each.
After 5h.21 the whole of `TextureBaseClass` should be linkable;
5h.22+ moves on to `TextureClass` (the derived class that holds the
D3DTexture + owns the lifecycle).
