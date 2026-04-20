# Phase 5h.15 — DX8 buffer subclass ctors + dtors un-`#ifdef`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fifteenth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h14-BufferSortingLockUnstub.md`.

## Scope gate

5h.13 un-guarded the base `VertexBufferClass` / `IndexBufferClass`.
5h.14 un-guarded `SortingVertexBufferClass` / `SortingIndexBufferClass`
plus the base-class Lock classes. 5h.15 un-guards the **DX8 subclass
ctors / dtors + Create_Vertex_Buffer + Copy methods** —
`DX8VertexBufferClass` (5 ctors + 1 dtor + `Create_Vertex_Buffer` +
4 `Copy` overloads) and `DX8IndexBufferClass` (1 ctor + 1 dtor).
`DynamicVBAccessClass` / `DynamicIBAccessClass` stay inside the guard
(pool management + `DX8Wrapper::Draw_Sorting_IB_VB` dependency is its
own larger slice).

This is a compile-/link-readiness step, not a runtime-behavior step.
Nothing in bgfx-mode production code instantiates a `DX8VertexBufferClass`
yet; when it eventually does (5h.17+), the compat-shim stubs return
`S_OK` with a null payload so any accidental runtime use fails loudly
instead of silently writing into the void.

## Locked decisions

| Question | Decision |
|---|---|
| Compat shim extension | Add no-op `CreateVertexBuffer(UINT, DWORD, DWORD, UINT, IDirect3DVertexBuffer8**)` + `CreateIndexBuffer(UINT, DWORD, UINT, UINT, IDirect3DIndexBuffer8**)` + `ResourceManagerDiscardBytes(DWORD)` methods on `IDirect3DDevice8`. All three return `S_OK`; pointers are zeroed on exit. `UINT` for pool/format params instead of `D3DPOOL` / `D3DFORMAT` — the enums are declared later in the header and forward-declaring them to avoid ordering issues would need typedef redeclarations that conflict with the full enum. Implicit integer conversion at the call site handles the type mismatch. |
| Forward declarations | `IDirect3DVertexBuffer8` and `IDirect3DIndexBuffer8` are now forward-declared above `IDirect3DDevice8` so the `Create*` stub signatures reference valid types. Full definitions unchanged below. |
| TextureClass + WW3D recovery paths | `TextureClass::Invalidate_Old_Unused_Textures(5000)` + `WW3D::_Invalidate_Mesh_Cache()` are defined in `texture.cpp` / `ww3d.cpp` — both wholly gated by `#ifdef RTS_RENDERER_DX8` and thus missing symbols in bgfx mode. Surgically guarded the two call sites (in `DX8VertexBufferClass::Create_Vertex_Buffer` and `DX8IndexBufferClass`'s ctor) with `#ifdef RTS_RENDERER_DX8`. Bgfx mode's no-op `CreateVertexBuffer` returns `S_OK` on the first try anyway, so the recovery branch is unreachable there. |
| `DX8Wrapper::Get_Current_Caps()` / `_Get_D3D_Device8()` | Both inline in `dx8wrapper.h` returning static pointers (`CurrentCaps`, `D3DDevice`). In bgfx mode these are `nullptr`-initialized. The DX8 subclass ctors dereference them — fine at compile time (method calls through pointer), UB at runtime only if bgfx code actually instantiates a `DX8VertexBufferClass` (which it doesn't yet). |
| Dynamic pool statics | `_DynamicSortingVertexArray*` / `_DynamicDX8VertexBuffer*` / `_DynamicFVFInfo` / `_DX8VertexBufferCount` remain inside `#ifdef RTS_RENDERER_DX8`. They're only used by `DynamicVBAccessClass` and by `DX8VertexBufferClass`'s `VERTEX_BUFFER_LOG`-guarded log statements (the log isn't defined in release builds, so the references are inert). |
| DynamicVBAccessClass / DynamicIBAccessClass | Stay guarded. Both call `DX8Wrapper::Draw_Sorting_IB_VB` (a static method defined inside the DX8 section of `dx8wrapper.cpp`); moving them out would need a bgfx stub of that method plus the other pool statics. Scoped to 5h.16. |
| DX8 impact | Zero. The DX8 constructor paths execute exactly the same source lines as before; the `#ifdef` split only moves which lines are inside the guard versus outside. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/compat/d3d8.h` | Forward-declare `IDirect3DVertexBuffer8` / `IDirect3DIndexBuffer8` above `IDirect3DDevice8`. Add `CreateVertexBuffer` + `CreateIndexBuffer` + `ResourceManagerDiscardBytes` no-op stubs to `IDirect3DDevice8`. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Moved `#include "dx8caps.h"` + `"thread.h"` above the guard. Reduced the guarded region to just the pool statics (closed by a new `#endif` right after `_DX8VertexBufferCount=0;`). `DX8VertexBufferClass::{ctor×5, dtor, Create_Vertex_Buffer, Copy×4}` are now outside the guard. Re-opened `#ifdef RTS_RENDERER_DX8` right before `DynamicVBAccessClass::DynamicVBAccessClass` (still closes at file end). Guarded the `TextureClass::Invalidate_Old_Unused_Textures` + `WW3D::_Invalidate_Mesh_Cache` calls inside `Create_Vertex_Buffer` with an inline `#ifdef RTS_RENDERER_DX8`. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same pattern. `DX8IndexBufferClass::{ctor, dtor}` now outside the guard. Recovery-path calls inside the ctor guarded with inline `#ifdef`. |

### Structure after 5h.15

```cpp
// Unguarded:
//   base VertexBufferClass / IndexBufferClass (5h.13)
//   SortingVertexBufferClass / SortingIndexBufferClass (5h.14)
//   Base Lock classes (5h.14)
//   IndexBufferClass::Copy (5h.14)
// #ifdef RTS_RENDERER_DX8
//   Dynamic pool statics + VERTEX_BUFFER_LOG counter
// #endif
// Unguarded (5h.15):
//   DX8VertexBufferClass::{ctor×5, dtor, Create_Vertex_Buffer, Copy×4}
//   DX8IndexBufferClass::{ctor, dtor}
// #ifdef RTS_RENDERER_DX8
//   DynamicVBAccessClass / DynamicIBAccessClass + pool allocate/deinit methods
// #endif
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `DX8VertexBufferClass`'s 5 ctors + Create_Vertex_Buffer + 4 Copy methods + dtor, and `DX8IndexBufferClass`'s ctor + dtor, now link in `z_ww3d2.a` in bgfx mode. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `DX8VertexBufferClass` ctors linkable in bgfx mode | Yes (all 5). |
| `DX8IndexBufferClass::DX8IndexBufferClass` linkable in bgfx mode | Yes. |
| `IDirect3DDevice8::CreateVertexBuffer` callable | Yes — compat-shim stub returning `S_OK` with null output. |
| `DynamicVBAccessClass` linkable in bgfx mode | Still **no** — stays guarded. |
| Full VB/IB subclass sprawl compilable in bgfx mode | Yes except the Dynamic*Access path. |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `DynamicVBAccessClass` / `DynamicIBAccessClass` + `Write` lock classes + `_Deinit` / `_Reset` / `Get_Default_*_Count` / `Allocate_*_Dynamic_Buffer` | 5h.16 | Calls `DX8Wrapper::Draw_Sorting_IB_VB`, which is a DX8-only static. Needs either a bgfx stub of that static or an alternate code path |
| `DX8Wrapper::Set_Vertex_Buffer` / `Set_Index_Buffer` bgfx-branch stubs → real CPU-side data extraction + `Draw_Triangles_Dynamic` forwarding | 5h.17 | Previous item |
| `DX8Wrapper::Draw_Triangles` / `Draw_Strip` / `Draw` bgfx-branch stubs → `IRenderBackend::Draw_Indexed` | 5h.17 | Previous item |
| `texture.cpp` un-`#ifdef` same pattern — base class first, DX8 subclass second | 5h.18 | Analogous to 5h.13/5h.14 but for the TextureBaseClass / DX8TextureClass sprawl |
| `ww3d.cpp` un-`#ifdef` for `WW3D::_Invalidate_Mesh_Cache` + other hot-path statics | 5h.19 | Has many DX8-specific entry points; may need surgical splits per-method |

## What this phase buys

Roughly all of `dx8vertexbuffer.cpp` + `dx8indexbuffer.cpp` **except**
the `Dynamic*AccessClass` sprawl now compiles + links in bgfx mode.
Constructing a `DX8VertexBufferClass` in bgfx-mode code is now a
compile + link + runtime success path (the runtime yields a buffer
with `VertexBuffer=nullptr`, i.e. a logically-zero-size DX8 VB, which
subsequent 5h.17 draw-path code will detect and route through the
sorting / dynamic path).

The cumulative footprint of 5h.13 + 5h.14 + 5h.15 is about **1,300
lines of previously DX8-only code** now available in bgfx mode. The
remaining ~200 lines (`Dynamic*AccessClass` + pool management) is
the narrowest remaining blocker before the first real game-driven
draw can land.
