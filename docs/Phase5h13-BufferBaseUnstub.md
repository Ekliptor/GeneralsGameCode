# Phase 5h.13 — VertexBuffer / IndexBuffer base-class un-`#ifdef`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirteenth sub-phase
of the DX8Wrapper adapter. Follows `Phase5h12-ShaderMaterialDispatch.md`.

## Scope gate

5h.12 closed the last state-surface translator gap in the adapter
(shader + material). Everything else blocking the draw path —
`Draw_Triangles` / `Draw_Strip` / `Draw` / `TextureClass::Init` — is
gated on un-`#ifdef`'ing compile units that today are wholly wrapped
in `#ifdef RTS_RENDERER_DX8` and contribute **zero symbols** in bgfx
mode.

5h.13 takes the first concrete bite out of that problem: the **base
classes** `VertexBufferClass` and `IndexBufferClass` in
`dx8vertexbuffer.cpp` / `dx8indexbuffer.cpp` have no direct DX8 API
dependency — their constructors / destructors, engine-ref counters,
and static `Get_Total_*` accessors are plain C++ that uses
`FVFInfoClass` and memory counters. This slice moves those methods
**outside** the `#ifdef RTS_RENDERER_DX8` guard so the symbols link in
bgfx mode. DX8 subclasses (`DX8VertexBufferClass`,
`DX8IndexBufferClass`, `SortingVertexBufferClass`, the Lock classes,
the Dynamic access classes, and the DX8-API-bound `Copy` methods) all
stay inside the guard.

This is intentionally narrow. The deliverable isn't "the draw path
works" — it's "code that holds a `VertexBufferClass*` or calls
`Add_Engine_Ref`, `Get_Vertex_Count`, `Type`, etc. now compiles and
links in bgfx mode." That's the prerequisite for the later slices
(5h.14+) that will actually un-guard the subclass bodies and make
them do real work.

## Locked decisions

| Question | Decision |
|---|---|
| Scope size | Just the base-class methods. Subclasses stay guarded. Rationale: base-class uses no DX8 API and is ~50 lines per file. Subclasses total ~1,450 lines and pull in `DX8Wrapper::_Get_D3D_Device8` / `DX8CALL` / `Lock` / `Unlock` / `CreateVertexBuffer` — each its own chunk. |
| Where to move `#ifdef` open | Below the base-class method bodies, right before the DX8 helper includes (`dx8wrapper.h`, `dx8caps.h`, `thread.h`) and the DX8 static variables. The `#endif` at the file's end stays where it was. |
| Includes outside guard | `dx8fvf.h` (for `FVFInfoClass`) and `wwmemlog.h` (for `WWMEMLOG`). Already safe in both builds. Added `dx8wrapper.h` to `dx8indexbuffer.cpp`'s outer section for `BUFFER_TYPE_DX8` / `BUFFER_TYPE_SORTING` (the constructor's `WWASSERT` uses them). |
| Generals vs GeneralsMD | Only touched GeneralsMD's files. The plain "Generals" (non-MD) variant's `dx8vertexbuffer.cpp` / `dx8indexbuffer.cpp` have **no** `#ifdef` at all — they compile unconditionally in the `g_ww3d2` target, which isn't used by the bgfx build today. Updating them is a no-op for bgfx; can be revisited if/when `g_ww3d2` gains a bgfx path. |
| No test added | Nothing in the bgfx build today constructs a `VertexBufferClass` / `IndexBufferClass`. The value is pure link-readiness for future slices. Build + 20-test regression + DX8 reconfigure-clean is the right bar. |
| DX8 impact | Zero. The static counters (`_VertexBufferCount`, etc.) live in the same TU; moving them out of the guard puts them in file scope where the DX8 constructor path can still see them. The DX8 subclass bodies that read/write them are unchanged. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Moved `#include "dx8fvf.h"` + `"wwmemlog.h"` above the guard. Moved the three static counters (`_VertexBufferCount`, `_VertexBufferTotalVertices`, `_VertexBufferTotalSize`) above the guard. Moved `VertexBufferClass::{ctor, dtor, Get_Total_*, Add_Engine_Ref, Release_Engine_Ref}` above the guard. Opened a new `#ifdef RTS_RENDERER_DX8` right after, containing the DX8-only includes, DX8-only static vars, subclasses, Lock classes, and Dynamic access classes. The closing `#endif` at the file's end still matches. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same pattern: moved `#include "dx8wrapper.h"` + `"wwmemlog.h"` above the guard, counters above the guard, `IndexBufferClass::{ctor, dtor, Get_Total_*, Add_Engine_Ref, Release_Engine_Ref}` above the guard. Opened a new `#ifdef RTS_RENDERER_DX8` before the DX8-only helpers + `Copy` methods + Lock classes + Dynamic access classes. |

### The boundary

```cpp
// File top
#include "dx8vertexbuffer.h"
#include "dx8fvf.h"
#include "wwmemlog.h"

// Base class — always compiled.
static int _VertexBufferCount;
static int _VertexBufferTotalVertices;
static int _VertexBufferTotalSize;

VertexBufferClass::VertexBufferClass(...) { ... }
VertexBufferClass::~VertexBufferClass() { ... }
unsigned VertexBufferClass::Get_Total_Buffer_Count() { ... }
// (etc.)

#ifdef RTS_RENDERER_DX8
#include "dx8wrapper.h"
#include "dx8caps.h"
#include "thread.h"

// DX8-specific statics, subclasses, Lock classes, Dynamic access...

#endif // RTS_RENDERER_DX8
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `VertexBufferClass` + `IndexBufferClass` symbols now present in `libz_ww3d2.a`'s `dx8vertexbuffer.cpp.o` / `dx8indexbuffer.cpp.o` in bgfx mode (previously empty object files — hence the `ranlib: warning: 'libww3d2.a(dx8vertexbuffer.cpp.o)' has no symbols` warning is now gone for these two files). |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `VertexBufferClass::VertexBufferClass` definitions linked in bgfx mode | One — from `dx8vertexbuffer.cpp`. |
| `IndexBufferClass::IndexBufferClass` definitions linked in bgfx mode | One — from `dx8indexbuffer.cpp`. |
| DX8 subclass definitions in bgfx mode | Zero — `DX8VertexBufferClass`, `DX8IndexBufferClass`, `SortingVertexBufferClass`, `SortingIndexBufferClass`, `DynamicVBAccessClass`, `DynamicIBAccessClass` are still fully `#ifdef`'d out. Instantiating any of them in bgfx mode would still fail to link. |
| `ranlib` no-symbol warnings for `dx8vertexbuffer.cpp.o` / `dx8indexbuffer.cpp.o` | Gone. (Verified against the build log.) |

## Deferred — remaining work to reach the draw path

| Item | Stage | Blocker |
|---|---|---|
| `SortingVertexBufferClass` / `SortingIndexBufferClass` — CPU-only classes that own a `VertexFormatXYZNDUV2*` / `unsigned short*` array. Un-`#ifdef`'ing these is essentially free (no DX8 API), but they construct through `W3DNEW` + `delete` which needs no-op hooks in bgfx mode | 5h.14 | None — straightforward |
| `VertexBufferClass::WriteLockClass` / `AppendLockClass` — dispatch on `BUFFER_TYPE_DX8` vs `BUFFER_TYPE_SORTING`. The SORTING path is pure pointer arithmetic; the DX8 path calls `IDirect3DVertexBuffer8::Lock`/`Unlock`. Needs compat shim extension OR two-branched implementation | 5h.14 | Compat shim Lock/Unlock stubs |
| `DX8VertexBufferClass` / `DX8IndexBufferClass` — wrap `IDirect3DVertexBuffer8*`. Constructors call `DX8Wrapper::_Get_D3D_Device8()->CreateVertexBuffer`. In bgfx mode these would need a no-op or a wrap around a `bgfx::VertexBufferHandle`. Substantial | 5h.15 | DX8Wrapper::_Get_D3D_Device8 stub + compat CreateVertexBuffer |
| `DynamicVBAccessClass` / `DynamicIBAccessClass` — used for streaming geometry. Maps onto `IRenderBackend::Draw_Triangles_Dynamic` naturally | 5h.15 | same |
| `Set_Vertex_Buffer` / `Set_Index_Buffer` bgfx stubs → real routing | 5h.16 | above |
| `Draw_Triangles` / `Draw_Strip` / `Draw` bgfx stubs → `IRenderBackend::Draw_Indexed` | 5h.17 | above |
| `texture.cpp` un-`#ifdef` following the same base / subclass split pattern | 5h.18 | analogous |

**Ordered build-up to the first real draw:**

1. (This phase, 5h.13) Base classes link.
2. (5h.14) SortingVertexBufferClass + Lock classes for sorting path.
3. (5h.15) Extend compat shim with `Lock/Unlock/CreateVertexBuffer/CreateIndexBuffer` stubs. Add `DX8Wrapper::_Get_D3D_Device8` bgfx stub returning a dummy `IDirect3DDevice8*`. Un-guard DX8 subclasses — their methods become no-ops in bgfx, but linkable.
4. (5h.16) `DX8Wrapper::Set_Vertex_Buffer` / `Set_Index_Buffer` bgfx branch stores the `VertexBufferClass*` / `IndexBufferClass*` in `render_state` (already happens — it's the inline path).
5. (5h.17) Parallel bgfx-path in `Draw_Triangles` that extracts vertex data from the stored `VertexBufferClass*` via a new CPU-side payload the subclass maintains, builds a `VertexLayoutDesc`, and calls `IRenderBackend::Draw_Triangles_Dynamic`. This is the "first real draw" milestone.

Each of these is a discrete slice; none individually exceeds the complexity of 5h.11's specular work. The sum is substantial but each step is shippable on its own.

## What this phase buys

In isolation, 5h.13 doesn't do anything visible — no test changes,
no new backend capabilities, no new game rendering. What it changes
is the **link surface**: before 5h.13, anything in bgfx-mode code that
mentioned `VertexBufferClass::Add_Engine_Ref` or
`IndexBufferClass::Get_Vertex_Count` would fail to link. After 5h.13,
those references are resolvable. That removes a class of compile
errors the next few slices will trigger as they un-guard the subclass
bodies one at a time.

The value is clearing the path, not traveling it.
