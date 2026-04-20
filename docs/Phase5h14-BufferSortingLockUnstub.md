# Phase 5h.14 — Sorting buffer classes + Lock classes un-`#ifdef`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fourteenth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h13-BufferBaseUnstub.md`.

## Scope gate

5h.13 made `VertexBufferClass` / `IndexBufferClass` base-class symbols
link in bgfx mode. 5h.14 takes the next bite: `SortingVertexBufferClass`
/ `SortingIndexBufferClass` (CPU-only), the base-class `Copy` methods,
and the base-class `WriteLockClass` / `AppendLockClass` inner classes.

The Lock classes switch on `BUFFER_TYPE_DX8` vs `BUFFER_TYPE_SORTING`
and only the DX8 arm touches `IDirect3DVertexBuffer8::Lock/Unlock`. To
make that arm compile in bgfx mode, the compat shim (`compat/d3d8.h`)
grows stub `Lock/Unlock` methods on both buffer interface types. The
methods return `S_OK` with a null payload so any accidental runtime
call fails loudly rather than silently writing into the void — but
the functional path is the `BUFFER_TYPE_SORTING` arm, which is pure
pointer arithmetic into the `SortingVertexBufferClass`'s
`VertexFormatXYZNDUV2*` array (now linkable).

Still inside the `#ifdef`: `DX8VertexBufferClass` / `DX8IndexBufferClass`
(their ctors call `CreateVertexBuffer` / `CreateIndexBuffer` —
compat shim still lacks those), `DynamicVBAccessClass` /
`DynamicIBAccessClass`, and every `_DynamicDX8*` / `_DynamicSorting*`
pool static.

## Locked decisions

| Question | Decision |
|---|---|
| Compat shim stub shape | `IDirect3DVertexBuffer8::Lock(UINT, UINT, BYTE**, DWORD)` and `Unlock()` both return `S_OK` and zero the output pointer. Same shape for `IDirect3DIndexBuffer8`. Zeroing `*ppbData` means a runtime call crashes on first deref — loud failure, not silent corruption. |
| Why keep the DX8 arm in the Lock class body | The dispatch is `switch (VertexBuffer->Type())`. Removing the `BUFFER_TYPE_DX8` case would mean `#ifdef`-gating the switch body, which is messier than stub-compiling it. Since bgfx-mode code doesn't construct DX8 subclass instances yet, the DX8 arm is dead at runtime. |
| Source-order reshuffle in `dx8indexbuffer.cpp` | Moved `SortingIndexBufferClass::{ctor, dtor}` above `DX8IndexBufferClass::{ctor, dtor}`. The `#ifdef RTS_RENDERER_DX8` opens right before the DX8 class, so the CPU-only Sorting class stays unguarded. No similar reorder needed in `dx8vertexbuffer.cpp` — Sorting class was already between the Lock classes and DX8 class. |
| `dx8wrapper.h` include | Moved to the unguarded prologue of both files. `BUFFER_TYPE_DX8` / `BUFFER_TYPE_SORTING` live there, plus `DX8_THREAD_ASSERT` / `DX8_Assert` / `DX8_ErrorCode` which the Lock classes need. These are all already stubbed to no-ops in bgfx mode (see `dx8wrapper.h:164-169` for the macros, `dx8wrapper.cpp:4516-4517` for the function stubs). |
| DX8 impact | Zero. The DX8 constructor paths, ctor-time `CreateIndexBuffer` / `CreateVertexBuffer` calls, and `IDirect3DVertexBuffer8::Lock/Unlock` calls are compiled from exactly the same source lines — the `#ifdef` split only moves which lines are inside versus outside, not whether they exist. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/compat/d3d8.h` | Add no-op `HRESULT Lock(UINT, UINT, BYTE**, DWORD) { *ppbData = nullptr; return S_OK; }` + `HRESULT Unlock() { return S_OK; }` to `IDirect3DVertexBuffer8` and `IDirect3DIndexBuffer8`. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Moved `#include "dx8wrapper.h"` above the guard. Pulled the DX8-static variable block (`_DynamicSortingVertexArrayInUse`, etc.) inside the guard. The Lock classes + `SortingVertexBufferClass::{ctor, dtor}` are now outside the guard; the `#ifdef RTS_RENDERER_DX8` opens right before `DX8VertexBufferClass::DX8VertexBufferClass`. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same pattern. Moved `SortingIndexBufferClass::{ctor, dtor}` above `DX8IndexBufferClass` in source order so the unguarded section ends where the DX8-API-bound code begins. |

### The new guard boundary (both files)

```cpp
// ---- Unguarded ---------------------------------------------------
//  #include "dx8wrapper.h" (BUFFER_TYPE_*, DX8_*, compat shim)
//  #include "dx8fvf.h"     (FVFInfoClass, VertexFormatXYZNDUV2)
//  #include "wwmemlog.h"
//
//  static int _VertexBufferCount / _Total*;      // base-class counters
//  VertexBufferClass::{ctor, dtor, Get_Total_*, Engine_Ref methods}
//  VertexBufferClass::WriteLockClass::{ctor, dtor}
//  VertexBufferClass::AppendLockClass::{ctor, dtor}
//  SortingVertexBufferClass::{ctor, dtor}
//  (same for IB file: IndexBufferClass, IndexBufferClass::Copy,
//   Lock classes, SortingIndexBufferClass)
//
// #ifdef RTS_RENDERER_DX8
//  DX8 subclass ctor/dtor, Create_*_Buffer, DynamicVBAccessClass, etc.
// #endif
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `dx8vertexbuffer.cpp.o` / `dx8indexbuffer.cpp.o` now contain the sorting classes + Lock classes in addition to the base classes from 5h.13. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `SortingVertexBufferClass::SortingVertexBufferClass` + `SortingIndexBufferClass::SortingIndexBufferClass` defined in bgfx mode | Yes (one TU each). |
| `VertexBufferClass::WriteLockClass` / `AppendLockClass` + IB equivalents defined in bgfx mode | Yes. |
| `IDirect3DVertexBuffer8::Lock/Unlock` callable in bgfx mode | Yes — compat-shim stub. |
| DX8-subclass ctors linkable in bgfx mode | Still **no** — `DX8VertexBufferClass::DX8VertexBufferClass` and `DX8IndexBufferClass::DX8IndexBufferClass` remain inside the guard. Nothing in bgfx production code instantiates them yet; the Lock classes' DX8 arm is dead-code at runtime. |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `DX8VertexBufferClass` / `DX8IndexBufferClass` ctor/dtor. Ctors call `DX8Wrapper::_Get_D3D_Device8()->CreateVertexBuffer/CreateIndexBuffer` | 5h.15 | Compat shim lacks these two device-level creation methods; also needs a bgfx stub of `DX8Wrapper::_Get_D3D_Device8` returning a dummy pointer |
| `DynamicVBAccessClass` / `DynamicIBAccessClass` + their Write lock classes | 5h.15 | Allocate via `DX8VertexBufferClass` (above), and reference `_DynamicDX8*` statics that stay guarded until the subclass ctor is buildable |
| `_DynamicSortingVertexArray` / `_DynamicSortingIndexArray` pool statics. These are CPU-only but the functions that use them also construct DX8 buffers, so they're bundled with the DX8 subclass slice | 5h.15 | same |
| `DX8Wrapper::Set_Vertex_Buffer` / `Set_Index_Buffer` bgfx-branch stubs → real routing | 5h.16 | above |
| `DX8Wrapper::Draw_Triangles` / `Draw_Strip` / `Draw` bgfx-branch stubs → `IRenderBackend::Draw_Indexed` or `Draw_Triangles_Dynamic` | 5h.17 | above |
| `texture.cpp` un-`#ifdef` same pattern | 5h.18 | analogous; easier because TextureClass has no per-instance DX8 API in its base-class methods |

## What this phase buys

Same as 5h.13: no user-visible change; unblocks the next slice. Before
5h.14, `new SortingVertexBufferClass(...)` in bgfx-mode code would
fail to link. After 5h.14 it succeeds. That's the class that the
dynamic-draw path's sorting fallback needs, and it's the one that
most of the engine's CPU-side vertex operations route through.

The cumulative footprint of the last two phases: about 250 lines of
`#ifdef`-guarded code in `dx8vertexbuffer.cpp` + `dx8indexbuffer.cpp`
moved outside the guard. Roughly half the non-DX8-subclass code in
those two files is now available in bgfx mode. The remaining half
(DX8 subclasses + Dynamic access + `Copy` from `Vector3*` helpers) is
tightly coupled to DX8 device-level creation calls and needs the
compat shim's next extension before it can join.
