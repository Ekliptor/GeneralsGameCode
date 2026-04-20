# Phase 5h.19 — TextureBaseClass ctor + dtor un-`#ifdef`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Nineteenth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h18-DX8BufferShadowRouting.md`.

## Scope gate

5h.18 closed the draw-call arc. Every VB/IB path the game drives
through `DX8Wrapper::Draw_Triangles` now reaches the bgfx backend.
The remaining milestone for "first visible game frame in bgfx mode"
is **texture binding** — the uber-shader samples `s_texture` in its
fragment stage, and today bgfx mode binds only the built-in 2×2
white placeholder (since `TextureClass` is still entirely
`#ifdef RTS_RENDERER_DX8`'d out).

5h.19 is the **opening slice** of the texture-unstub arc, matching
the 5h.13 → 5h.16 pattern for VB/IB. Scope: just the
`TextureBaseClass` ctor + dtor. That's 40 lines of CPU-only code
(pointer-member init in the ctor; compat-shim `Release()` + a call
to `DX8TextureManagerClass::Remove` in the dtor). Everything else —
`Invalidate`, `_Get_Total_*` statics, `Peek_D3D_Base_Texture`, the
derived `TextureClass` / `ZTextureClass` / `CubeTextureClass` /
`VolumeTextureClass` — stays inside the guard.

This is explicitly a prerequisite-setting phase: nothing in bgfx-mode
code constructs a `TextureBaseClass` subclass yet (every derived
ctor still lives inside the guard), so this phase doesn't change
runtime behavior. It clears the way for 5h.20+ to move derived-class
ctors outside, at which point `TextureClass` instances can start
landing in bgfx mode.

## Locked decisions

| Question | Decision |
|---|---|
| Scope size | Just the two methods. 40 lines total, no control-flow branching; trivially verifiable via build. Matches the conservative 5h.13 pattern (base-class methods only). |
| `DX8TextureManagerClass::Remove` availability | `dx8texman.cpp` / `dx8texman.h` have **zero** `#ifdef RTS_RENDERER_DX8` guards — both files compile in bgfx mode already. `Remove` is a static method that walks a flat array, no DX8 API usage. |
| `D3DTexture->Release()` | Compat-shim stub returns 0 without side effects (`compat/d3d8.h:109`). Safe in bgfx mode since `D3DTexture` is always nullptr there today; the guard `if (D3DTexture)` skips the call anyway. |
| `delete TextureLoadTask` / `ThumbnailLoadTask` | Forward-declared members; `delete` on a forward-declared type is a `-Wdelete-incomplete` warning the existing codebase already accepts. These pointers are nullptr-initialized in the ctor and stay that way unless the texture loader (gated by DX8) populates them. |
| `unused_texture_id` static | Moved out of the guard alongside the ctor that uses it. Other statics (`DEFAULT_INACTIVATION_TIME`, `TexturesAppliedPerFrame`) stay inside the guard — unused by the base ctor/dtor. |
| Include block | `#include "dx8texman.h"` moved above the guard for the `Remove` call. The rest (d3d8.h, dx8wrapper.h, TARGA.h, textureloader.h, etc.) stays inside — they're only needed by the still-guarded derived-class methods. |
| DX8 impact | Zero. The ctor + dtor bodies are byte-identical; the `#ifdef` open point just moves 40 lines down the file. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Moved `#include "dx8texman.h"` + `static unsigned unused_texture_id;` + `TextureBaseClass::TextureBaseClass(…)` + `TextureBaseClass::~TextureBaseClass()` above the `#ifdef RTS_RENDERER_DX8` guard. Reopened the guard right after the dtor for all remaining DX8-bound includes + methods. |

### The boundary

```cpp
// Unguarded:
//   #include "texture.h"
//   #include "dx8texman.h"
//   static unsigned unused_texture_id;
//   TextureBaseClass::TextureBaseClass(...)
//   TextureBaseClass::~TextureBaseClass()
//
// #ifdef RTS_RENDERER_DX8
//   d3d8/d3dx8/dx8wrapper/TARGA/nstrdup/w3d_file/assetmgr/formconv/
//   textureloader/missingtexture/ffactory/dx8caps/meshmatdesc/
//   texturethumbnail/wwprofile includes
//
//   TexturesAppliedPerFrame + MAX_TEXTURES_APPLIED_PER_FRAME statics
//   Invalidate_Old_Unused_Textures
//   Invalidate
//   Peek_D3D_Base_Texture
//   ... (TextureClass, ZTextureClass, CubeTextureClass, VolumeTextureClass)
// #endif
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `TextureBaseClass::TextureBaseClass` and `~TextureBaseClass` now link in bgfx mode (they contributed **zero** symbols from `texture.cpp.o` prior to this phase — the ranlib-no-symbols warning for that TU is gone). |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `TextureBaseClass` ctor/dtor symbols in `texture.cpp.o` in bgfx mode | Present. |
| `TextureClass::Init` / `Apply` / ctors in bgfx mode | Still not linkable (stays inside guard). |
| Count of `ranlib: warning: 'libww3d2.a(texture.cpp.o)' has no symbols` lines in build log | Dropped from 1 (prior) to 0. |

## Deferred — texture arc roadmap

Ordered path from "`TextureBaseClass` ctor links" (this phase) to
"first game-loaded DDS renders in bgfx mode":

| Item | Stage | Blocker |
|---|---|---|
| `TextureBaseClass` statics: `_Get_Total_*_Size/Count` accessors, `Apply_Null`, `Set_HSV_Shift`, `Get_Priority`/`Set_Priority`, `Set_Texture_Name`. Mostly counter-walking statics + name string management | 5h.20 | `Apply_Null` calls `DX8Wrapper::Set_DX8_Texture_Stage_State` which is a DX8-only static; surgical guard needed |
| `TextureBaseClass::Peek_D3D_Base_Texture` + `Set_D3D_Base_Texture` + `Load_Locked_Surface` + `Invalidate`. All touch `WW3D::Get_Sync_Time` which is `static inline` reading a static that's defined in `ww3d.cpp`'s DX8-guarded section | 5h.20 or 5h.21 | `ww3d.cpp`'s static member definitions need an un-ifdef slice of their own |
| `TextureClass::TextureClass(filename, ...)` — the ctor the asset manager calls to load a file. Routes through `TextureLoader` which is entirely DX8-bound | 5h.22 | `textureloader.cpp` un-ifdef, substantial |
| `TextureClass::Apply(stage)` — the per-frame texture bind. Calls `DX8Wrapper::Set_DX8_Texture` which maps to `SetTexture` in the real DX8 path; in bgfx mode this is where we'd call `BgfxTextureCache::Get_Or_Load_File(path)` → `IRenderBackend::Set_Texture` | 5h.23 | `TextureClass` ctor + `TextureLoader` above |
| `TextureClass::Init` — the lazy-init that loads the file on first `Apply` | 5h.23 | same |

Three sub-phases away from a real game-loaded texture. Each slice
isolates a compile-unit chunk and verifies build cleanliness, same
pattern as the VB/IB arc.

## What this phase buys

Vacuous in isolation — nothing in bgfx-mode code references
`TextureBaseClass::TextureBaseClass` today (no derived ctor is
linkable yet). But the whole texture arc bottoms out on the base
ctor + dtor being linkable; every derived class that 5h.20+ un-ifdef
calls up the inheritance chain to these. Moving them out first means
the next four sub-phases have a clean boundary to expand against —
same as how 5h.13 unblocked 5h.14 → 5h.16.

The `ranlib: no symbols` warning for `texture.cpp.o` disappearing
from the build log is the concrete observable signal that the TU is
now contributing. Prior to 5h.19 it was a completely-empty object
file in bgfx mode.
