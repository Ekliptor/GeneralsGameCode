# Phase 5h.1 — Backend-selection runtime seam

Companion doc to `docs/CrossPlatformPort-Plan.md`. First sub-phase of the
long-deferred **Phase 5h** — the DX8Wrapper → `IRenderBackend` adapter.
Follows `Phase5q-FrameCapture.md`. Previous numerical siblings (5a–5g, 5i–5q)
built out the bgfx backend's capability envelope; 5h is where production
game code starts routing through it.

## Scope gate

5h as a whole routes ~8 kLOC of `DX8Wrapper` statics through
`IRenderBackend`. It breaks naturally into several independently-shippable
sub-phases:

| # | Slice | Status |
|---|---|---|
| 5h.1 | Backend-selection runtime seam | **this doc** |
| 5h.2 | Render-device lifetime (Create_Render_Device / Close_Render_Device) | next |
| 5h.3 | `TextureBaseClass` → bgfx texture bridge via 5j | later |
| 5h.4 | `ShaderClass` / `VertexMaterialClass` → `ShaderStateDesc` / `MaterialDesc` | later |
| 5h.5 | `VertexBufferClass` / `IndexBufferClass` → `Set_Vertex_Buffer` / `Set_Index_Buffer` | later |
| 5h.6 | Light + transform call sites | later |
| 5h.7 | Remove the `#ifdef RTS_RENDERER_bgfx` forks once all call sites are routed | last |

5h.1 is the smallest shippable slice — **the seam**. It doesn't route
anything yet. It just makes the accessor that subsequent slices will
call exist, be callable from both DX8 and bgfx builds, and correctly
reflect backend lifecycle.

## Objective

Give production DX8 call sites a zero-risk way to ask "is a bgfx
`IRenderBackend` currently active, and if so, what is it?" — without
introducing any new link-time dependency on bgfx for DX8-only builds.

Answer shape:

```cpp
#include "WW3D2/RenderBackendRuntime.h"
// …in production code…
if (IRenderBackend* backend = RenderBackendRuntime::Get_Active())
{
    // bgfx fast path
}
else
{
    // existing DX8 path — unchanged
}
```

A DX8-only build: nothing calls `Set_Active`, so `Get_Active` always
returns `nullptr`; the `if` branch is dead; the existing fast path runs
byte-identical to pre-5h.1. A bgfx build: `BgfxBackend::Init` calls
`Set_Active(this)` once bgfx is up; `Shutdown` clears it. Production
code that opts into the seam gets the bgfx pointer and can route.

## Locked decisions

| Question | Decision |
|---|---|
| Where does the seam live | `Core/Libraries/Source/WWVegas/WW3D2/RenderBackendRuntime.h` — same directory as `IRenderBackend.h` and `BackendDescriptors.h`. Both per-game WW3D2 libraries (`Generals/…/WW3D2`, `GeneralsMD/…/WW3D2`) already include from here; DX8 and bgfx builds both compile out of the same header tree. |
| Header-only vs. .cpp | Header-only, inline singleton. Rationale: avoids a new static-library target, avoids having to decide whether the symbol lives in `corei_bgfx` (missing in DX8 builds) or `corei_ww3d2` (which is an INTERFACE target). C++17+ inline functions with function-local statics are guaranteed single-definition across TUs, so both the DX8 and bgfx codepaths end up reading the same slot. |
| Who owns registration | `BgfxBackend::Init` calls `Set_Active(this)` on success; `Shutdown` calls `Set_Active(nullptr)` — but guarded by `Get_Active() == this` so a stale shutdown (after a caller has already reassigned the slot to a different backend) doesn't clobber state. |
| Thread safety | None. Bgfx init/shutdown are main-thread only; the seam is read by the render thread but written only during well-defined lifecycle transitions. If that ever changes, add a `std::atomic<IRenderBackend*>` — cost is one relaxed load per query. |
| DX8 behavior | Compile-time: the header is identical code in DX8 builds; it just never gets called because no bgfx backend exists. Link-time: the header has no non-inline symbols so no link impact. Runtime: `Get_Active()` returns nullptr forever; the seam's consumer branches fall through to the DX8 path. |
| Why not a weak symbol or link-time selection | Those techniques pick one implementation at link time and can't flip at runtime. For golden-image regression testing (and future CI where one binary tests both paths) a runtime seam is strictly more flexible. The cost — one pointer load + branch — is in the noise for anything that happens at draw-call granularity. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/RenderBackendRuntime.h` | The seam. Inline `Get_Active()` / `Set_Active(IRenderBackend*)` over a function-local static. |
| `tests/tst_bgfx_runtime/main.cpp` | Lifecycle assertion — four invariants (null-initially / Init-registers / Shutdown-clears / second cycle round-trips). |
| `tests/tst_bgfx_runtime/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5h1-BackendRuntime.md` | This doc. |

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Include `WW3D2/RenderBackendRuntime.h`. On `Init` success, `RenderBackendRuntime::Set_Active(this)`. In `Shutdown`, guard-clear via `if (Get_Active() == this) Set_Active(nullptr)`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_runtime)`. |

### The whole seam, end-to-end

```cpp
// Core/Libraries/Source/WWVegas/WW3D2/RenderBackendRuntime.h
#pragma once
class IRenderBackend;

namespace RenderBackendRuntime
{
    inline IRenderBackend*& Instance()
    {
        static IRenderBackend* s_backend = nullptr;
        return s_backend;
    }
    inline IRenderBackend* Get_Active() { return Instance(); }
    inline void Set_Active(IRenderBackend* b) { Instance() = b; }
}
```

```cpp
// BgfxBackend.cpp — after bgfx::init, at the end of Init():
RenderBackendRuntime::Set_Active(this);

// BgfxBackend.cpp — after bgfx::shutdown, at the end of Shutdown():
if (RenderBackendRuntime::Get_Active() == this)
    RenderBackendRuntime::Set_Active(nullptr);
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_runtime` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic,alphatest,fog,rendertarget,capture,runtime}` (all 14) | OK. |
| `./tests/tst_bgfx_runtime/…` | All 4 lifecycle invariants pass; `tst_bgfx_runtime: PASSED`. |
| All 13 prior bgfx tests | PASSED — the Init/Shutdown register/unregister is the only behavior change, and no existing test queries `Get_Active`. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — the header is harmless in a DX8 build (nothing `#include`s it yet). |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions + the new runtime header's forward decl). |
| `RenderBackendRuntime::` callers | Two — `BgfxBackend::Init` and `BgfxBackend::Shutdown`. Plus the test harness. |
| `#include.*RenderBackendRuntime\.h` | Three — `BgfxBackend.cpp`, `tst_bgfx_runtime/main.cpp`, and this doc's code samples. DX8 production code has not yet started including it. |
| Per-game WW3D2 CMake lists touched | Zero — the header is already on the include path via `corei_ww3d2`'s existing setup. |

## Deferred

| Item | Stage |
|---|---|
| Render-device lifetime plumbing — the first *actual* call site. `DX8Wrapper::Create_Render_Device` starts `if (auto* b = Get_Active()) b->Reset(w, h, windowed);` after its existing DX8 init, and `Close_Render_Device` drains the active backend before the DX8 device goes away | 5h.2 |
| Texture bridge — `TextureBaseClass::Get_Bgfx_Handle()` lazily calls `Get_Active()->Create_Texture_From_Memory(dds_bytes, size)` when the DX8 load completes. Uses Phase 5j's path | 5h.3 |
| ShaderClass / VertexMaterialClass translator — populates `ShaderStateDesc` / `MaterialDesc` PODs from the per-game classes when bgfx is active | 5h.4 |
| Thread-safety upgrade to `std::atomic<IRenderBackend*>` if/when multi-threaded rendering lands | later |
| Retire the seam entirely once DX8 is removed (Phase 7) — `Get_Active()` becomes `GetTheBackend()` returning a reference, no-null | Phase 7 |

The wire is live. Subsequent 5h slices plug call sites into it.
