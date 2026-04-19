# Phase 5d — Portable D3D8 type shim + game target gate removed

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fourth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5c-D3DXRemoval.md`.

## Objective

Remove the compile-time gate that prevented game targets (GeneralsMD, Generals)
from building when `RTS_RENDERER=bgfx`. Provide a portable D3D8 type shim so
that all WW3D2 headers/sources compile on non-Windows platforms. Guard DX8-
specific implementation code with `#ifdef RTS_RENDERER_DX8`. The bgfx smoke test
continues to pass. Game targets don't fully compile yet due to broader Windows
API dependencies (Phase 2–4 scope), but all D3D8-specific compilation barriers
are resolved.

## Strategy

**Portable D3D8 type shim via `compat/` directory** on the include path, combined
with `#ifdef RTS_RENDERER_DX8` guards around D3D COM calls, and `DX8CALL` macros
that become no-ops under bgfx.

## Changes

### New files — compat shim headers

| File | Purpose |
|---|---|
| `WW3D2/compat/d3d8.h` | Main portable D3D8 shim: Win32 type polyfills, D3D COM interface stubs (AddRef/Release), all D3D8 enums/constants/structs (~750 lines) |
| `WW3D2/compat/d3d8types.h` | Redirect → `d3d8.h` |
| `WW3D2/compat/d3d8caps.h` | Redirect → `d3d8.h` |
| `WW3D2/compat/d3dx8.h` | Stub → `d3d8.h` |
| `WW3D2/compat/d3dx8core.h` | Stub with inline `D3DXFilterTexture` no-op |
| `WW3D2/compat/d3dx8math.h` | Stub with `D3DXMATRIX` struct definition |
| `WW3D2/compat/d3dx8tex.h` | Redirect → `d3dx8core.h` |

### New files — Unix OS-dependency stubs

| File | Purpose |
|---|---|
| `Dependencies/Utility/Utility/osdep.h` | Unix compat: `__int64`, `__forceinline`, `_stricmp`, `HANDLE`, `HMODULE`, Win32 string aliases, `SUCCEEDED`/`FAILED` macros |
| `Dependencies/Utility/Utility/osdep/osdep.h` | Redirect → parent `osdep.h` |

### Guarded DX8Wrapper — macros and implementation

| File | Change |
|---|---|
| `dx8wrapper.h` | `DX8CALL`/`DX8CALL_HRES`/`DX8CALL_D3D` become `((void)0)` when `!RTS_RENDERER_DX8`. `DX8_ErrorCode` body guarded. |
| `dx8wrapper.cpp` | Entire implementation wrapped in `#ifdef RTS_RENDERER_DX8`. `#else` section provides stub implementations for all non-inline public/protected methods. |
| `dx8caps.cpp` | Entire implementation wrapped in `#ifdef RTS_RENDERER_DX8` with empty stubs. |

### Core WW3D2 files guarded with `#ifdef RTS_RENDERER_DX8`

Files with D3D COM calls or Windows-only includes, wrapped after first `#include`:

`missingtexture.cpp`, `surfaceclass.cpp`, `texture.cpp`, `textureloader.cpp`,
`texturefilter.cpp`, `texturethumbnail.cpp`, `render2dsentence.cpp`,
`agg_def.cpp`, `FramGrab.cpp`, `font3d.cpp`, `rendobj.cpp`, `w3d_dep.cpp`

### Per-game WW3D2 files guarded (GeneralsMD)

`dx8indexbuffer.cpp`, `dx8vertexbuffer.cpp`, `ddsfile.cpp`, `assetmgr.cpp`,
`shader.cpp`, `dx8renderer.cpp`, `dx8rendererdebugger.cpp`, `sortingrenderer.cpp`,
`render2d.cpp`, `hlod.cpp`, `motchan.cpp`, `part_emt.cpp`, `part_ldr.cpp`,
`ww3d.cpp`

### Windows header guards

| File | Change |
|---|---|
| `framgrab.h` | `#ifdef _WIN32` around VFW includes and AVI member types |
| `render2dsentence.h` | `#ifdef _WIN32` around `HFONT`/`HBITMAP` members |
| `registry.h` | `#ifdef _WIN32` around `HKEY` methods |
| `WWAudio.h` | `#ifdef _WIN32` around `mss.h`; `#else` stub Miles types |
| `AudibleSound.h`, `SoundBuffer.h`, `Utils.h` | `#ifdef _WIN32` around `mss.h` |
| `DbgHelpLoader.h` | `#ifdef _WIN32` around Windows debug API members |
| `DbgHelpLoader.cpp` | `#ifdef _WIN32` around full implementation |
| `PreRTS.h` (GeneralsMD) | `#ifdef _WIN32` around Windows system headers |

### Debug/profile library guards

All `.cpp` files in `Core/Libraries/Source/debug/` and `Core/Libraries/Source/profile/`
wrapped with `#ifdef _WIN32` (13 files total).

### Cross-platform fixes

| File | Change |
|---|---|
| `FastAllocator.h` | `#include <malloc.h>` → conditional `<cstdlib>` |
| `SystemAllocator.h` | `GlobalAlloc`/`GlobalFree` → `std::malloc`/`std::free` on non-Windows |
| `ini.cpp` | Same `malloc.h` fix |
| `TARGA.cpp` | Same + `memory.h` → `cstring` |
| `GameMemory.h` | `<new.h>` → `<new>` on non-MSVC |
| `wwprofile.h` | Replaced inline `__int64` typedef with `#include "osdep.h"` |
| `profile_funclevel.h` | `#include "osdep.h"` + pointer-to-int cast fix for 64-bit |

### CMake changes

| File | Change |
|---|---|
| Root `CMakeLists.txt` | Removed bgfx game-target gate |
| `Core/.../WW3D2/CMakeLists.txt` | Added `compat/` include dir for bgfx |
| `Core/.../WWMath/CMakeLists.txt` | Added `compat/` include dir for bgfx |
| Core/per-game `wwcommon` (3 files) | Conditional `d3d8lib` link |
| Game exe CMakeLists (2 files) | Conditional `d3d8`/`d3dx8`/`dinput8`/Windows libs; link `corei_bgfx` for bgfx |
| All PCH CMakeLists (7 files) | Conditional `<windows.h>` |
| Tools CMakeLists (2 files) | Gated on `WIN32` |
| `Dependencies/Utility/CMakeLists.txt` | Added `Utility/` to include path on non-Windows |
| `debug/CMakeLists.txt`, `profile/CMakeLists.txt` | Added `core_utility` link |

## Verification

| Test | Result |
|---|---|
| `cmake .. -DRTS_RENDERER=bgfx -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK — game targets included |
| `cmake --build . --target corei_bgfx` | OK |
| `cmake --build . --target tst_bgfx_clear` | OK |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `BgfxBackend::Init: Metal (800x600, windowed)` → `PASSED` |
| D3D-related compile errors in `z_generals` | **Zero** |
| Remaining compile errors | 501 — all Windows API / 32-bit porting (Phase 2–4 scope) |

### Static sweep — D3D8 shim coverage

| Pattern | Result |
|---|---|
| `#include.*d3d8` resolving outside WW3D2 | Zero — compat shim intercepts all |
| `D3DXMATRIX` or `D3DXVECTOR4` in compiled TUs | Zero |
| `DX8CALL` expanding to real D3D calls under bgfx | Zero — all no-ops |
| `IDirect3DDevice8` method calls in compiled code (bgfx) | Zero — guarded |

## Remaining work (not Phase 5d)

The 501 compile errors in game engine code are Windows API dependencies, not D3D:

| Category | Count | Examples | Phase |
|---|---|---|---|
| Win32 types (`SYSTEMTIME`, `WIN32_FIND_DATA`) | ~160 | `GameState.h`, `Recorder.h` | 2–3 |
| DirectInput (`dinput.h`) | ~121 | `KeyDefs.h` | 2 (SDL input) |
| Win32 APIs (`GetModuleFileName`, `CreateDirectory`) | ~100 | `GlobalData.cpp`, `GameStateMap.cpp` | 2–3 |
| 64-bit pointer casts | ~20 | `ThingTemplate.cpp`, `PartitionManager.cpp` | 6 |
| Network/socket compat | ~15 | `Transport.cpp`, `udp.cpp` | 4 |
| MSVC intrinsics (`__max`, `itoa`, `_fpreset`) | ~15 | `Player.cpp`, `OpenContain.cpp` | 2–3 |
| GameSpy linkage | ~13 | `gsplatform.h` | 4 |
| Missing SDL3 | 1 | `RegistryStore.cpp` | 2 |

## Deferred to later phases

| Item | Stage |
|---|---|
| First textured triangle in bgfx | 5e |
| Fixed-function uber-shaders + shaderc pipeline | 5f |
| Full scene parity + golden-image CI | 5j |
| Window / Input / Timing (SDL3) — resolves dinput.h, SYSTEMTIME, etc. | 2 |
| Config / Threading / Browser cleanup — resolves Win32 API calls | 3 |
| Networking — resolves socket compat | 4 |
| 64-bit pointer/size audit | 6 |
