# Phase 5h.2 — Render-device lifetime: DX8Wrapper → BgfxBootstrap

Companion doc to `docs/CrossPlatformPort-Plan.md`. Second sub-phase of the
DX8Wrapper adapter. Follows `Phase5h1-BackendRuntime.md`.

## Scope gate

5h splits into ~7 incremental slices. 5h.1 landed the runtime-selection
seam (header-only singleton). 5h.2 is the first slice that **wires
production DX8Wrapper code** to the bgfx backend — specifically the
device-lifetime half: creation on `Set_Render_Device`, reconfiguration on
`Set_Device_Resolution`, teardown on `Shutdown`. No draw calls yet; the
DX8Wrapper draw-path stubs stay inert. Those are 5h.4 / 5h.5.

## Objective

Put an `IRenderBackend` singleton behind the DX8Wrapper bgfx stubs so
that, whenever the existing game boot sequence runs
`DX8Wrapper::Init(hwnd) → Set_Render_Device(dev, w, h, ...)`, a live
`BgfxBackend` comes up transparently. `RenderBackendRuntime::Get_Active`
(installed in 5h.1) starts returning a pointer that subsequent slices
can route draws through.

The DX8Wrapper bgfx stub branch was already in place from prior phases —
`Init / Shutdown / Set_Render_Device / Set_Device_Resolution` were all
`return true;` no-ops. 5h.2 swaps the no-ops for calls into a new
`BgfxBootstrap` helper that owns the backend's construction, Reset, and
destruction.

## Locked decisions

| Question | Decision |
|---|---|
| Where does the backend instance live | A file-local static `BgfxBackend*` inside `BgfxBootstrap.cpp`. One process, one backend, one lifetime tied to DX8Wrapper::Shutdown. Matches the DX8 path's "one D3DDevice per app" model. No need for per-scene or per-window backends — the game is single-window. |
| Why a separate `BgfxBootstrap` namespace instead of static methods on `BgfxBackend` | Ownership responsibility is separate from the class's own Init / Shutdown contract. `BgfxBackend` remains allocator-friendly (tests stack-construct it); `BgfxBootstrap` adds the singleton-lifetime layer. DX8Wrapper includes only `BgfxBootstrap.h` — no reason for WW3D2 to touch the backend class directly. |
| Idempotence | `Ensure_Init(hwnd, w, h, windowed)` can be called repeatedly: same args → no-op, changed dimensions → `Reset` on the existing instance, changed hwnd → tear down + recreate. Matches DX8's `Reset_Device` + mode-switch semantics. |
| What if `Set_Render_Device` fires before `Init` | Wired defensively — `s_pendingHwnd` defaults to nullptr; `BgfxBackend::Init(nullptr, …)` will fail closed and `Ensure_Init` returns false. `Init` must be called first (which is what the game does anyway). |
| What hwnd does the hidden-window test path pass | `SDLDevice::getNativeWindowHandle()`, same as every other bgfx test. Real game code will pass the Windows HWND from `WinMain` or the SDL window handle depending on `RTS_PLATFORM`. |
| `Set_Any_Render_Device` fallback resolution | 800×600 windowed when `ResolutionWidth` / `Height` are zero. Matches the DX8 path's default-if-never-configured behavior. Real game startup always writes the registry's preferred mode into these before `Set_Any_Render_Device` fires. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBootstrap.h` | `BgfxBootstrap::{Ensure_Init, Shutdown, Is_Initialized}` — thin lifetime wrapper around a singleton `BgfxBackend`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBootstrap.cpp` | Implementation. Tracks `s_instance`, `s_hwnd`, `s_width`, `s_height`, `s_windowed`. Idempotent; handles Reset-in-place and hwnd-change-means-recreate. |
| `tests/tst_bgfx_bootstrap/main.cpp` | Six lifetime invariants: virgin state, first init, idempotent-on-same-args, Reset-on-new-dims, Shutdown-clears, re-init-after-shutdown. |
| `tests/tst_bgfx_bootstrap/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5h2-DeviceLifetime.md` | This doc. |

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Add `BgfxBootstrap.cpp` + its header to `corei_bgfx`. |
| `Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt` | In bgfx builds, put `Core/GameEngineDevice/Include` on `corei_ww3d2`'s interface include path (so dx8wrapper.cpp finds `BgfxBootstrap.h`) and link `corei_bgfx` (so the singleton's symbols resolve in the per-game `z_ww3d2` target). |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | The five stubs in the `#else` branch of `RTS_RENDERER_DX8` now route: `Init(hwnd, _)` stashes `hwnd`; `Shutdown()` calls `BgfxBootstrap::Shutdown()`; `Set_Render_Device` (both 6-arg and 8-arg overloads) + `Set_Any_Render_Device` + `Set_Device_Resolution` all call `BgfxBootstrap::Ensure_Init(stashed_hwnd, w, h, windowed)` after updating the static resolution fields. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_bootstrap)`. |

### Flow: DX8Wrapper → BgfxBackend

```
DX8Wrapper::Init(hwnd, lite)
    │
    └─► s_pendingHwnd = hwnd         // stash, no bgfx yet

DX8Wrapper::Set_Render_Device(dev, w, h, bits, windowed, …)
    │
    ├─► ResolutionWidth/Height/IsWindowed = … // sync DX8-style statics
    └─► BgfxBootstrap::Ensure_Init(s_pendingHwnd, w, h, windowed)
            │
            ├─► (new BgfxBackend)->Init(hwnd, w, h, windowed)
            │       └─► RenderBackendRuntime::Set_Active(this)   // from 5h.1
            └─► s_instance = backend

… later frames / mode switches call Set_Device_Resolution(w', h', …) …

BgfxBootstrap::Ensure_Init(s_pendingHwnd, w', h', windowed)
    │
    └─► s_instance->Reset(w', h', windowed)                   // in-place

DX8Wrapper::Shutdown()
    │
    └─► BgfxBootstrap::Shutdown()
            │
            ├─► s_instance->Shutdown()
            │       └─► RenderBackendRuntime::Set_Active(nullptr)
            ├─► delete s_instance
            └─► s_instance = nullptr
```

### Why `Ensure_Init` instead of `Init` / `Reset` / `Recreate` as separate methods

Call-site simplicity. DX8Wrapper has at least five entry points that can
trigger a backend-state change, and they all want the same
post-condition: "the backend is up, with these dimensions, for this
hwnd". Folding the dispatch (construct vs. Reset vs. tear-down-and-
recreate) into one method removes that branching from every caller.
Cost: `BgfxBootstrap` grew a tiny state machine. Net LOC is less.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — `BgfxBootstrap.cpp` compiles. |
| `cmake --build build_bgfx --target z_ww3d2` | OK — the modified dx8wrapper.cpp resolves `BgfxBootstrap::*` symbols through corei_bgfx (picked up via the new link edge from `corei_ww3d2`). |
| `cmake --build build_bgfx --target tst_bgfx_bootstrap` | OK. |
| All 14 prior bgfx tests + `tst_bgfx_bootstrap` = 15 total | PASSED. |
| `./tests/tst_bgfx_bootstrap/…` | All six lifetime invariants; `tst_bgfx_bootstrap: PASSED`. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — `#else` branch is gated out so zero impact. |

Pitfall: the first version of the test asserted that a second instance
had a different pointer address than the first. libc++ reuses freshly-
freed slots in practice; invariant was over-specified and the test was
relaxed to check liveness (`Get_Active != nullptr`) without claiming
address independence. Noted in the test comments.

Static sweep:

| Pattern | Result |
|---|---|
| `BgfxBootstrap::` callers outside the test + dx8wrapper.cpp bgfx branch | Zero (one-way dependency). |
| DX8 stub branch that still says `return true;` without calling `BgfxBootstrap` | Init's `lite` path — left as-is because it corresponds to the DX8 "no-device" code path the game uses for resource tools (no render device needed). Revisit if a bgfx tool wants a headless backend. |
| `corei_bgfx` linked from non-bgfx builds | Zero — the link edge lives inside `if(RTS_RENDERER_LOWER STREQUAL "bgfx")`. |

## Deferred

| Item | Stage |
|---|---|
| `Reset_Device(bool)` → `Ensure_Init` with current dims — the "D3D9-style lost-device recovery" path. Trivially wireable; gated on whether any caller actually exercises it in the bgfx config | 5h.3 if observed |
| `Find_Color_And_Z_Mode` and friends — DX8 enumerates display modes; bgfx doesn't need this (platform handles it). Leave stubs as-is unless a caller complains | later |
| `Create_Render_Target` / `Set_Render_Target` / `Create_Additional_Swap_Chain` — the *per-frame* RT set is backend-side (Phase 5p). The DX8Wrapper wrappers still return nullptr here; 5h.3+ threads them through once TextureBaseClass is bridged | 5h.3 |
| Backend instance as a `std::unique_ptr` — cosmetically nicer, but the file-local `new` / `delete` pattern is transparent for one instance and minimizes includes (no `<memory>` in BgfxBootstrap.cpp) | later |
| DX8Wrapper::Init's `lite` path getting a headless backend option — low priority | later |
| Actually routing draws through the backend (Begin_Scene / Clear / Draw_* all still stubs) | 5h.3 + 5h.4 + 5h.5 |

The bgfx backend now **boots with the game**. Production game startup
bringing up a bgfx backend is zero-touch from a caller perspective —
`DX8Wrapper::Init` and `Set_Render_Device` are the same API the game has
always called; behind the stub façade the wires are now live.
