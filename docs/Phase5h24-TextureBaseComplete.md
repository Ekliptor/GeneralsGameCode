# Phase 5h.24 — TextureBaseClass complete + WW3D texture-reduction statics

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-fourth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h23-TextureAccountingUnstub.md`.

## Scope gate

Six phases of the texture arc (5h.19 – 5h.23) brought 20-of-22
`TextureBaseClass` methods out of the guard. 5h.24 closes the
base-class surface: the remaining two methods `Apply_Null` and
`Get_Reduction`, each with a small cross-TU prerequisite in `ww3d.cpp`.

`Apply_Null(stage)` forwards to `DX8Wrapper::Set_DX8_Texture(stage,
nullptr)`. That function is already inline in `dx8wrapper.h`, uses
`DX8CALL` which is a no-op macro in bgfx mode, and reads/writes the
`Textures[]` static array that's been defined outside the guard
since Phase 5h.13 era. So `Apply_Null` is **already linkable** — it
just needed to physically move above `#ifdef RTS_RENDERER_DX8` in
`texture.cpp`.

`Get_Reduction()` reads two WW3D getters (`Get_Texture_Reduction` +
`Is_Large_Texture_Extra_Reduction_Enabled`) whose method bodies sit
inside `ww3d.cpp`'s DX8 guard, reading file-local statics also
inside the guard. The prerequisite step moves both statics + both
getters above the guard. Once done, `Get_Reduction` itself moves out
too.

After this phase, **the entire `TextureBaseClass` public surface
links in bgfx mode** — 22-of-22 methods. The texture arc's focus
shifts to the derived `TextureClass` and its constructors /
`Apply` / `Init` (5h.25+).

## Locked decisions

| Question | Decision |
|---|---|
| `_TextureReduction` + `_LargeTextureExtraReductionEnabled` statics | Moved above the `ww3d.cpp` guard. Both stay at defaults (0 / false) in bgfx mode — textures render at full resolution, which is the desirable default. |
| `WW3D::Get_Texture_Reduction()` + `Is_Large_Texture_Extra_Reduction_Enabled()` bodies | Moved above the guard right after their statics. Each is a one-line return. |
| Setters stay guarded | `Enable_Large_Texture_Extra_Reduction()` and the reduction-setter in `WW3D::Set_Texture_Reduction()` stay inside the DX8 guard — they reference `_Invalidate_Textures()` and other still-guarded machinery. In bgfx mode nothing calls them, so the statics are effectively read-only. |
| `Apply_Null` routing to backend | Kept as-is (forwards to the inline `Set_DX8_Texture`). That function reads/writes `Textures[]` + fires a `DX8CALL(SetTexture)` (no-op in bgfx). Future 5h.25+ work will also route `backend->Set_Texture(stage, 0)` explicitly from the adapter's per-frame texture-bind path. |
| DX8 impact | Zero. Same source lines, different `#ifdef` region on each file. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp` | Moved `static int _TextureReduction = 0;` + `static bool _LargeTextureExtraReductionEnabled = false;` + `int WW3D::Get_Texture_Reduction()` + `bool WW3D::Is_Large_Texture_Extra_Reduction_Enabled()` above the `#ifdef RTS_RENDERER_DX8` guard. Marker-commented the originals. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Moved `TextureBaseClass::Apply_Null` + `TextureBaseClass::Get_Reduction` above the guard. Pulled `#include "dx8wrapper.h"` above the guard for Apply_Null's call site. Marker-commented the originals. |

### Method bodies (unchanged from DX8)

```cpp
void TextureBaseClass::Apply_Null(unsigned int stage)
{
    DX8Wrapper::Set_DX8_Texture(stage, nullptr);
}

unsigned TextureBaseClass::Get_Reduction() const
{
    if (MipLevelCount==MIP_LEVELS_1) return 0;
    if (Width <= 32 || Height <= 32) return 0;
    int reduction = WW3D::Get_Texture_Reduction();
    if (WW3D::Is_Large_Texture_Extra_Reduction_Enabled() && (Width > 256 || Height > 256))
        reduction++;
    if (MipLevelCount && reduction > MipLevelCount) reduction = MipLevelCount;
    return reduction;
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `Apply_Null` + `Get_Reduction` link in bgfx mode; the two WW3D statics + getters that `Get_Reduction` needs resolve cleanly. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `TextureBaseClass::Apply_Null` linkable in bgfx | Yes. |
| `TextureBaseClass::Get_Reduction` linkable in bgfx | Yes. |
| `WW3D::Get_Texture_Reduction` + `Is_Large_Texture_Extra_Reduction_Enabled` linkable in bgfx | Yes. |
| Duplicate definitions | Zero across both TUs. |

### The full `TextureBaseClass` scorecard, post-5h.24

All 22 public + protected methods now link in bgfx mode:

| Phase | Method |
|---|---|
| 5h.19 | `TextureBaseClass` ctor |
| 5h.19 | `~TextureBaseClass` dtor |
| 5h.20 | `Set_Texture_Name` |
| 5h.20 | `Set_HSV_Shift` |
| 5h.20 | `Invalidate` |
| 5h.21 | `Peek_D3D_Base_Texture` |
| 5h.21 | `Set_D3D_Base_Texture` |
| 5h.21 | `Get_Priority` |
| 5h.21 | `Set_Priority` |
| 5h.22 | `Load_Locked_Surface` |
| 5h.22 | `Is_Missing_Texture` |
| 5h.23 | `Invalidate_Old_Unused_Textures` |
| 5h.23 | `_Get_Total_Texture_Size` |
| 5h.23 | `_Get_Total_Lightmap_Texture_Size` |
| 5h.23 | `_Get_Total_Procedural_Texture_Size` |
| 5h.23 | `_Get_Total_Locked_Surface_Size` |
| 5h.23 | `_Get_Total_Texture_Count` |
| 5h.23 | `_Get_Total_Lightmap_Texture_Count` |
| 5h.23 | `_Get_Total_Procedural_Texture_Count` |
| 5h.23 | `_Get_Total_Locked_Surface_Count` |
| 5h.24 | `Apply_Null` |
| 5h.24 | `Get_Reduction` |

The pure-virtual methods (`Init`, `Apply`, `Apply_New_Surface`,
`Get_Texture_Memory_Usage`, `Get_Asset_Type`) aren't `TextureBaseClass`
methods per se — they're interface declarations that derived
`TextureClass` / `ZTextureClass` / `CubeTextureClass` /
`VolumeTextureClass` must implement. Those derived classes are the
target of the next phase.

## Deferred — `TextureClass` and beyond

| Item | Stage | Blocker |
|---|---|---|
| `TextureClass` five ctor overloads + `Init` + `Apply` + `Apply_New_Surface` + `Get_Texture_Memory_Usage` | 5h.25 | Large. `Init` routes through `TextureLoader` (mostly guarded); `Apply` reads `DX8Wrapper::Set_DX8_Texture` + `SetTextureStageState` (partially guarded); `Get_Texture_Memory_Usage` touches `IDirect3DTexture8::GetLevelDesc` + level count (compat-shim extension needed) |
| `ZTextureClass` / `CubeTextureClass` / `VolumeTextureClass` ctors | 5h.26 | Analogous |
| `BgfxTextureCache` wire-up in `TextureClass::Init` | 5h.27 | Requires the above |
| First game-loaded DDS renders in bgfx mode | after 5h.27 | End-to-end texture path |

## What this phase buys

**`TextureBaseClass` is done.** Any code in bgfx mode that holds a
`TextureBaseClass*` and calls any of its 22 methods now links and
runs — the methods either produce correct results (when no state
depends on DX8 machinery) or no-op gracefully (when they would
iterate a null `WW3DAssetManager`). The texture arc's remaining
surface is the derived `TextureClass` family — heavier work because
the ctors touch `IDirect3DTexture8` creation via `DX8Wrapper::
_Create_DX8_Texture`, which itself dispatches to `CreateTexture` +
surface-loading logic.

From the CrossPlatformPort-Plan vantage: texture binding is the
**last** major data-plane surface that's still stubbed in the bgfx
path. Once `TextureClass::Init` routes through `BgfxTextureCache`
(landing in 5h.27-ish), the game can load a DDS, bind it to a
material, and render a textured mesh end-to-end — which is the
"first visible frame" milestone for bgfx mode.
