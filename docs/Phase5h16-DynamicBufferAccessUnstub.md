# Phase 5h.16 — Dynamic buffer access classes un-`#ifdef`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Sixteenth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h15-DX8BufferSubclassUnstub.md`.

## Scope gate

5h.13 → 5h.15 incrementally un-guarded `VertexBufferClass` /
`IndexBufferClass`, `SortingVertexBufferClass` / `SortingIndexBufferClass`,
the base-class Lock classes, and the full `DX8VertexBufferClass` /
`DX8IndexBufferClass` ctor/dtor/Create_*_Buffer/Copy surface. 5h.16 is
the narrow final slice: **un-guard `DynamicVBAccessClass` /
`DynamicIBAccessClass`** plus the pool statics that back them, plus
each class's `WriteLockClass` + `_Deinit` / `_Reset` / `Allocate_*` /
`Get_Default_*_Count` methods.

5h.15's doc flagged `DX8Wrapper::Draw_Sorting_IB_VB` as the remaining
blocker. That turned out to be **phantom** — neither
`DynamicVBAccessClass` nor `DynamicIBAccessClass` references it. The
actual blockers (`NEW_REF(DX8VertexBufferClass, ...)` +
`IDirect3DVertexBuffer8::Lock/Unlock` + pool statics) were all
resolved by 5h.14's compat-shim extension and 5h.15's subclass
un-guard. So this slice is just deleting the remaining `#ifdef`
blocks.

After 5h.16, **both `dx8vertexbuffer.cpp` and `dx8indexbuffer.cpp`
compile end-to-end in bgfx mode** — the only `#ifdef RTS_RENDERER_DX8`
remaining in those two files is the surgical inside-function guard
around `TextureClass::Invalidate_Old_Unused_Textures` +
`WW3D::_Invalidate_Mesh_Cache` in the `Create_*_Buffer` recovery
paths (dead code in bgfx since the compat-shim `Create*Buffer` always
succeeds).

## Locked decisions

| Question | Decision |
|---|---|
| Where are the `Draw_Sorting_IB_VB` references really | Nowhere in `dx8vertexbuffer.cpp` / `dx8indexbuffer.cpp`. The 5h.15 doc was wrong about the dependency — grep found only the speculative comment I'd written in 5h.15's own source code. Actual Draw_Sorting_IB_VB callers live in `sortingrenderer.cpp` / `dx8renderer.cpp`, neither of which is compiled in bgfx mode today. |
| Remaining DX8 API surface in Dynamic*AccessClass | `IDirect3DVertexBuffer8::Lock/Unlock` (compat shim, 5h.14) + `DX8Wrapper::Get_Current_Caps()->Support_NPatches()` (inline header, always available) + `NEW_REF(DX8VertexBufferClass, ...)` (ctor linkable from 5h.15). All three resolve in bgfx mode. |
| Pool statics | `_DynamicSortingVertexArray*` / `_DynamicDX8VertexBuffer*` / `_DynamicFVFInfo` / `_DX8VertexBufferCount` and IB equivalents — all file-local statics, no DX8 API, no ordering requirements. Moved outside the guard alongside the class methods that read/write them. |
| `REF_PTR_RELEASE` / `REF_PTR_SET` / `NEW_REF` | All macros from `always.h`, unconditionally available. |
| VERTEX_BUFFER_LOG | Disabled in release builds; the counter + log statements inside `Create_Vertex_Buffer` reference `_DX8VertexBufferCount` only when the macro's defined. Debug-only builds with `VERTEX_BUFFER_LOG` on would get the same behavior in both modes now (bgfx builds would increment the counter too — harmless). |
| DX8 impact | Zero. Every method that was inside the outer `#ifdef` is still emitted as before; the split only changes which #ifdef boundaries the lines sit between. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Removed both remaining outer `#ifdef RTS_RENDERER_DX8` / `#endif` pairs: the pool-statics block (lines 296–316 in 5h.15) and the DynamicVBAccessClass block (lines 748 … end). Only the inside-function `#ifdef` around `TextureClass::Invalidate_Old_Unused_Textures` + `WW3D::_Invalidate_Mesh_Cache` in `Create_Vertex_Buffer`'s recovery path remains. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same — removed both outer guards. Surgical recovery-path guard in `DX8IndexBufferClass`'s ctor stays. |

### Final structure

```cpp
// dx8vertexbuffer.cpp
#include "dx8vertexbuffer.h"
#include "dx8wrapper.h" / "dx8fvf.h" / "wwmemlog.h" / "dx8caps.h" / "thread.h"

// Base class (5h.13)
VertexBufferClass::{ctor, dtor, Get_Total_*, Engine_Ref}
// Sorting (5h.14)
VertexBufferClass::{WriteLockClass, AppendLockClass}
SortingVertexBufferClass::{ctor, dtor}
// Pool statics (5h.16)
static bool _DynamicSortingVertexArrayInUse = false; /* etc */
// DX8 subclass (5h.15)
DX8VertexBufferClass::{ctor×5, dtor, Create_Vertex_Buffer, Copy×4}
  └─ #ifdef RTS_RENDERER_DX8 around TextureClass/WW3D recovery-path calls
// Dynamic access (5h.16)
DynamicVBAccessClass::{ctor, dtor, _Deinit, _Reset, Allocate_DX8_*, Allocate_Sorting_*,
                       WriteLockClass::{ctor, dtor}, Get_Default_Vertex_Count}
```

IB file structure is analogous.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. Both `.o` files now contain every non-recovery-path symbol from the `.cpp` files. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| Outer `#ifdef RTS_RENDERER_DX8` blocks in `dx8vertexbuffer.cpp` / `dx8indexbuffer.cpp` | Zero. Count of occurrences dropped from 5 (post-5h.15) to 1 each (the surgical inside-function guards, which are `#ifdef RTS_RENDERER_DX8` on their opening line for the grep pattern but don't wrap whole classes). |
| `DynamicVBAccessClass::DynamicVBAccessClass` linkable in bgfx mode | Yes. |
| `DynamicIBAccessClass::DynamicIBAccessClass` linkable in bgfx mode | Yes. |
| `DynamicVBAccessClass::WriteLockClass` linkable | Yes. |

## What this phase buys

All of `dx8vertexbuffer.cpp` + `dx8indexbuffer.cpp` now contributes
real symbols to `z_ww3d2` in bgfx mode. Code that allocates a dynamic
vertex/index buffer (for particles, debug geometry, text rendering,
anything that goes through `DynamicVBAccessClass`) will now link and
run — though at runtime the `DX8VertexBufferClass`-backed path will
produce a no-op buffer (ctor returns with `VertexBuffer = nullptr`
since the compat shim's `CreateVertexBuffer` zeros the output).

The functional path for dynamic VBs in bgfx mode is the
`BUFFER_TYPE_DYNAMIC_SORTING` branch: `SortingVertexBufferClass`
owns a `VertexFormatXYZNDUV2*` CPU array, `WriteLockClass` returns a
pointer into that array, and the eventual draw path (5h.17) can
extract vertices from it to route through
`IRenderBackend::Draw_Triangles_Dynamic`.

**This closes the VB/IB un-`#ifdef` arc.** 5h.17 can now wire
`DX8Wrapper::Set_Vertex_Buffer` / `Set_Index_Buffer` / `Draw_*` in
bgfx mode to reach real geometry.

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `DX8Wrapper::Set_Vertex_Buffer(VertexBufferClass*, unsigned)` bgfx-branch stub → store in `render_state.vertex_buffers[]` | 5h.17 | None — prerequisites done |
| `DX8Wrapper::Set_Index_Buffer(IndexBufferClass*, unsigned short)` bgfx-branch stub → store in `render_state.index_buffer` | 5h.17 | None |
| `DX8Wrapper::Draw_Triangles` / `Draw_Strip` / `Draw` bgfx-branch stubs → extract vertex data from `render_state.vertex_buffers[0]` (cast to `SortingVertexBufferClass*`), extract indices, build a `VertexLayoutDesc` from the FVF, call `IRenderBackend::Draw_Triangles_Dynamic` | 5h.17 | None — the first phase where the bgfx backend actually draws game-driven geometry |
| `texture.cpp` un-`#ifdef` following the same base / subclass / lock-class split pattern | 5h.18 | Analogous to 5h.13 → 5h.16 but for TextureBaseClass / TextureClass / DX8TextureClass |
| `ww3d.cpp` un-`#ifdef` for `WW3D::_Invalidate_Mesh_Cache` + other hot-path statics | 5h.19 | Per-method split |

After 5h.17, running the game in bgfx mode should transition from
"clears the screen each frame but stalls" to "renders some subset of
its geometry" — the first user-visible milestone of the draw-path
work. The infrastructure is now in place; 5h.17 is pure wiring.
