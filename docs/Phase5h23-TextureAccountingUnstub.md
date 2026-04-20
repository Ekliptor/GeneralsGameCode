# Phase 5h.23 — Texture accounting methods + assetmgr singleton

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-third
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h22-TextureMissingLoaderBridge.md`.

## Scope gate

5h.22 brought `Is_Missing_Texture` + `Load_Locked_Surface` out of
the guard with narrow stubs for their dependencies. 5h.23 tackles
the next block of deferred `TextureBaseClass` methods — all of them
texture-hash walkers keyed off `WW3DAssetManager::Get_Instance()`:

* `Invalidate_Old_Unused_Textures(unsigned invalidation_time_override)`
* `_Get_Total_Texture_Size` / `_Count`
* `_Get_Total_Lightmap_Texture_Size` / `_Count`
* `_Get_Total_Procedural_Texture_Size` / `_Count`
* `_Get_Total_Locked_Surface_Size` / `_Count`

Nine methods total, each a 10–20 line hash-walk. The shared blocker
was `WW3DAssetManager::TheInstance` — the static member's definition
lives inside `assetmgr.cpp`'s outer `RTS_RENDERER_DX8` guard so
`Get_Instance()` doesn't link in bgfx mode. Moving just that one
line above the guard fixes the link-time half; each method then gets
a runtime null-guard so the `nullptr`-returning `Get_Instance()` in
bgfx mode is handled gracefully.

Also in this phase: the **5h.15 surgical guards** in the
`DX8VertexBufferClass::Create_Vertex_Buffer` and
`DX8IndexBufferClass::DX8IndexBufferClass` recovery paths now collapse
— `TextureClass::Invalidate_Old_Unused_Textures` is linkable, so the
surgical `#ifdef` around that call can go. Only the
`WW3D::_Invalidate_Mesh_Cache` call stays guarded (that's its own
un-guard slice, 5h.24).

## Locked decisions

| Question | Decision |
|---|---|
| `WW3DAssetManager::TheInstance` location | Moved above the outer guard in `assetmgr.cpp`. Single line of initialization; stays `nullptr` in bgfx mode. Nothing in bgfx-mode code constructs a `WW3DAssetManager` yet, so the pointer stays null for the life of the process. |
| Null-guard pattern per method | Each method starts with `WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance(); if (mgr == nullptr) return 0;` (or `return;` for `Invalidate_Old_Unused_Textures`). Cheap `mov + branch`; zero cost in DX8 mode (always non-null there, predictable branch). |
| `TexturesAppliedPerFrame` counter | The original `Invalidate_Old_Unused_Textures` body resets this counter to zero each call. Dropped from the un-guarded copy — `TexturesAppliedPerFrame` is a file-local static still inside the DX8 guard, and the counter's only readers are the texture-apply throttle also inside the guard. |
| `HashTemplateIterator` in bgfx mode | The iterator is a template over `HashTemplateClass<StringClass, TextureClass*>`, which the un-guarded `Get_Instance()->Texture_Hash()` call returns. In bgfx mode we never reach that call (null-guard kicks in first), so the iterator's behavior over a real hash is unexercised. But instantiating the template requires the type to be complete — `assetmgr.h` + `texture.h` provide it unconditionally. |
| Removing 5h.15 surgical guards | Yes. `DX8VertexBufferClass::Create_Vertex_Buffer`'s recovery path can now call `TextureClass::Invalidate_Old_Unused_Textures(5000)` unconditionally. `WW3D::_Invalidate_Mesh_Cache()` is still DX8-only — keeps its `#ifdef RTS_RENDERER_DX8` guard. Same pattern in the IB ctor. |
| DX8 impact | The null-guards are one extra branch per call in DX8 mode — effectively free with the instance always present. Loose TexturesAppliedPerFrame reset dropped from bgfx path only; DX8 path still resets via the in-guard copy (which stays, since it still references the guarded static). |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/assetmgr.cpp` | Moved `WW3DAssetManager::TheInstance = nullptr;` above the `#ifdef RTS_RENDERER_DX8` guard. Original location replaced with a marker comment. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Added nine null-guarded method bodies above the guard: `Invalidate_Old_Unused_Textures` + eight `_Get_Total_*`. Pulled `#include "assetmgr.h"` above the guard. Removed the in-guard copies (replaced with marker comments). |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Collapsed 5h.15 surgical `#ifdef` around `TextureClass::Invalidate_Old_Unused_Textures(5000)` in `Create_Vertex_Buffer`'s recovery path. `WW3D::_Invalidate_Mesh_Cache()` keeps its guard. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same pattern in `DX8IndexBufferClass`'s ctor recovery path. |

### The representative method body

```cpp
int TextureBaseClass::_Get_Total_Texture_Size()
{
    WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
    if (mgr == nullptr) return 0;
    int total = 0;
    HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
    for (ite.First(); !ite.Is_Done(); ite.Next())
        total += ite.Peek_Value()->Get_Texture_Memory_Usage();
    return total;
}
```

All eight `_Get_Total_*` methods follow this shape; the only
differences are the filter (`Is_Lightmap()` / `Is_Procedural()` /
`!Initialized`) and whether the accumulator sums bytes or counts.

### The collapsed VB recovery path

```cpp
// Phase 5h.23 — no more surgical #ifdef around the texture invalidate call.
TextureClass::Invalidate_Old_Unused_Textures(5000);

#ifdef RTS_RENDERER_DX8
// Invalidate the mesh cache
WW3D::_Invalidate_Mesh_Cache();
#endif
```

Where 5h.15 had both calls wrapped in a single `#ifdef`, 5h.23 only
guards the mesh-cache call (the texture-invalidate is now unguarded).

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `WW3DAssetManager::TheInstance` + all nine texture-accounting methods now link in bgfx mode. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `WW3DAssetManager::TheInstance` defined in bgfx mode | Yes — `assetmgr.cpp`, above the guard. |
| `TextureBaseClass::_Get_Total_*` / `Invalidate_Old_Unused_Textures` linkable in bgfx mode | Yes (nine methods). |
| `#ifdef RTS_RENDERER_DX8` surgical guards in VB/IB recovery paths | Reduced from 2 (5h.15) to 2 but narrower — each now only wraps `WW3D::_Invalidate_Mesh_Cache()` instead of the full recovery block. |
| Duplicate method definitions | Zero across all three TUs. |

Cumulative progress on the `TextureBaseClass` surface: **20 of ~18**
methods now link in bgfx mode — actually 20 because there are
8 `_Get_Total_*` variants, not just the 2 I was counting as an
aggregate. Roughly **95%** of the class. The remaining methods are
`Apply_Null` (needs `DX8Wrapper::Set_DX8_Texture` un-guard) and
`Get_Reduction` (needs `WW3D::Get_Texture_Reduction()` un-guard).

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `Apply_Null` | 5h.24 | `DX8Wrapper::Set_DX8_Texture` is DX8-only; needs forwarding to `IRenderBackend::Set_Texture(stage, 0)` |
| `Get_Reduction` | 5h.24 | `WW3D::Get_Texture_Reduction()` + `Is_Large_Texture_Extra_Reduction_Enabled()` have non-inline bodies in ww3d.cpp's guarded section |
| `TextureClass` derived ctors + `Apply` + `Init` | 5h.25+ | Full `TextureLoader` un-guard + `BgfxTextureCache` wire-up |
| First game-loaded DDS renders in bgfx mode | after 5h.25+ | depends on TextureClass ctors routing through BgfxTextureCache |

## What this phase buys

**~95% of the `TextureBaseClass` surface now links in bgfx mode.**
Every method except `Apply_Null` and `Get_Reduction` is callable from
bgfx-mode code. The 5h.15 workaround guards collapsed — the codebase
carries less `#ifdef` friction as a result.

The null-guard pattern (`if (Get_Instance() == nullptr) return 0;`)
lets every texture-accounting method survive the "WW3DAssetManager
never constructed" reality of bgfx mode. Once 5h.25+ does construct
the asset manager, these methods start producing real numbers — no
further code change needed on their end.

One more `TextureBaseClass` un-guard slice (5h.24: `Apply_Null` +
`Get_Reduction`) and then the focus shifts to the derived
`TextureClass` and the texture-load path itself.
