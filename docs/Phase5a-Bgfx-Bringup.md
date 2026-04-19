# Phase 5a — Bgfx bring-up (init + clear + swap)

Companion doc to `docs/CrossPlatformPort-Plan.md`. First stage of Phase 5
(Renderer bgfx backend). Follows the structure of
`Phase0-RHI-Seam.md` through `Phase4-Networking.md`.

## Objective

Unblock `RTS_RENDERER=bgfx` so the project can configure and build a
bgfx-backed renderer library on macOS and Windows. Phase 5a does **not** draw
geometry — it proves that bgfx can initialize, clear to a solid color, swap,
and shut down cleanly through the SDL3 native-window-handle path. The main
game executables do **not** build under `bgfx` yet (they depend on `DX8Wrapper`
which is not yet abstracted — that's Phase 5d).

## Locked decisions

| Question | Decision |
|---|---|
| bgfx source | **FetchContent** from `bkaradzic/bgfx.cmake` (tag `v1.143.9216-529`). Consistent with dx8/gamespy FetchContent pattern. Falls back to `find_package` for pre-installed (vcpkg/system). |
| Renderer type | **Auto** (`bgfx::RendererType::Count`). bgfx picks Metal on macOS, D3D11 on Windows. |
| Threading | **Single-threaded** (`bgfx::renderFrame()` called before `bgfx::init`). Multi-threaded deferred to 5j. |
| Platform data | `SDLDevice::getNativeWindowHandle()` — SDL3 is the single source of truth. Windows returns HWND, macOS returns NSWindow*. |
| BgfxBackend location | `Core/GameEngineDevice/{Include,Source}/BGFXDevice/` — parallel to SDLDevice, OpenALAudioDevice, etc. |
| Game targets under bgfx | **Skipped.** GeneralsMD/, Generals/, Core/Tools/ gated out. Only `corei_bgfx`, `tst_bgfx_clear`, and their deps build. |
| bgfx header leakage | **None.** `<bgfx/bgfx.h>` appears only in `BGFXDevice/` and `tst_bgfx_clear/`. `IRenderBackend.h` has zero bgfx awareness. |

## Source-level changes

### New files

- `cmake/bgfx.cmake` — FetchContent with CONFIG fallback. Suppresses bgfx
  example/tool targets. Creates `bgfx::bgfx` alias for downstream use.
- `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` — concrete
  `IRenderBackend` implementation. Init/Shutdown/Reset/Begin_Scene/End_Scene/Clear
  are real; Set_*/Draw_* are stubs.
- `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` — ~140 LOC.
  Single-threaded bgfx init, Metal/D3D11 auto-detect, clear via
  `bgfx::setViewClear`, frame via `bgfx::frame`.
- `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` — `corei_bgfx`
  static library linking `bgfx::bgfx` + `corei_always_no_pch`.
- `tests/CMakeLists.txt` — top-level test directory.
- `tests/tst_bgfx_clear/main.cpp` — 80-line smoke harness: SDL window → bgfx
  init → 2-second clear loop (#306080) → shutdown.
- `tests/tst_bgfx_clear/CMakeLists.txt` — gated on `RTS_RENDERER=bgfx` and
  `RTS_BUILD_CORE_EXTRAS=ON`.

### Edited files

| File | Change |
|---|---|
| `vcpkg.json` | No change (bgfx fetched via FetchContent, not vcpkg). |
| `cmake/config-build.cmake` | Removed `FATAL_ERROR` gate. Added `elseif(bgfx)` arm emitting `RTS_RENDERER_BGFX=1`. |
| `CMakeLists.txt` (root) | Added `include(cmake/bgfx.cmake)` conditional. Gated GeneralsMD/Generals `add_subdirectory` on not-bgfx. Added `tests/` subdirectory. |
| `Core/CMakeLists.txt` | Gated `add_subdirectory(Tools)` on not-bgfx. |
| `Core/GameEngineDevice/CMakeLists.txt` | Gated `d3d8lib` link on `RTS_RENDERER=dx8`. Added `add_subdirectory(Source/BGFXDevice)` when bgfx. |
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Replaced `#include "matrix4.h"` / `#include "vector4.h"` with forward declarations. Avoids pulling the heavyweight WWMath → WWLib → compat.h chain into the bgfx backend. |
| `Core/GameEngineDevice/Include/SDLDevice/Common/SDLGlobals.h` | `getNativeWindowHandle()` is now cross-platform (was `#ifdef _WIN32`-only). |
| `Core/GameEngineDevice/Source/SDLDevice/Common/SDLGlobals.cpp` | Added macOS Cocoa path (`SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`) and Linux stub (Wayland/X11). |
| `Dependencies/Utility/Utility/thread_compat.h` | Fixed `GetCurrentThreadId()` — `pthread_self()` returns a pointer on macOS, not int. Added `reinterpret_cast<uintptr_t>` + `static_cast<int>`. |

## Collateral macOS fix

`thread_compat.h` had a pre-existing macOS compile error (documented in Phase 4
as a Phase 6 deferral). The bgfx backend's include chain transits through
`always.h → compat.h → thread_compat.h`, so the fix was pulled forward into 5a.

## CMake plumbing

- `cmake/bgfx.cmake` uses `FetchContent_Declare` + `FetchContent_MakeAvailable`
  with `GIT_SHALLOW TRUE`. Build suppression: `BGFX_BUILD_TOOLS=OFF`,
  `BGFX_BUILD_EXAMPLES=OFF`, `BGFX_INSTALL=OFF`, `BGFX_CUSTOM_TARGETS=OFF`.
- `corei_bgfx` is a STATIC library (not INTERFACE) so it compiles its own TUs
  independently. Links `bgfx::bgfx`, `corei_always_no_pch` (provides core
  includes without PCH).
- When `RTS_RENDERER=bgfx`:
  - GeneralsMD/, Generals/ subdirectories are skipped with a status message.
  - Core/Tools/ is skipped (DX8/MFC dependent).
  - `cmake/bgfx.cmake` is included; `cmake/dx8.cmake` is not (already gated
    behind the Win32+32-bit check).

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529
(built from source via FetchContent).

| Command | Outcome |
|---|---|
| `cmake .. -DRTS_RENDERER=bgfx -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg -DRTS_BUILD_CORE_EXTRAS=ON` | OK. `RendererBackend: bgfx`. Game targets skipped. |
| `cmake --build . --target corei_bgfx` | OK. `libcorei_bgfx.a` built. |
| `cmake --build . --target tst_bgfx_clear` | OK. Executable built. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | OK. Prints `BgfxBackend::Init: Metal (800x600, windowed)` → 2s clear → `BgfxBackend::Shutdown: complete` → `tst_bgfx_clear: PASSED`. |
| `cmake .. -DRTS_RENDERER=dx8 -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK. No regression — game targets visible, RendererBackend: dx8. |

Static sweep:

| Pattern | Result |
|---|---|
| `#include.*<bgfx` outside `BGFXDevice/` and `tst_bgfx_clear/` | Zero. |
| `RTS_RENDERER_BGFX` outside `cmake/` | Zero in source files (compile define only). |

## Deferred to later Phase 5 stages

| Item | Stage |
|---|---|
| D3D type removal from public RHI (D3DMATRIX → Matrix4x4 in RenderStateStruct) | 5b |
| DX-leaking client TUs (W3DShaderManager, W3DSnow, W3DSmudge, etc.) | 5c |
| DX8Wrapper → IRenderBackend facade (game targets build under bgfx) | 5d |
| First textured triangle in bgfx | 5e |
| Fixed-function uber-shaders + shaderc pipeline | 5f |
| Mesh rendering parity | 5g |
| Terrain + water shader rewrite (bgfx HLSL/GLSL) | 5h |
| DDS loading via bimg | 5i |
| Full scene parity + golden-image CI test | 5j |
| macOS .app bundle + CI jobs | 5k |
