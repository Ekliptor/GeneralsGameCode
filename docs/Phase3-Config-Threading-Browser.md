# Phase 3 — Config / Threading / Browser cleanup

Companion doc to `docs/CrossPlatformPort-Plan.md`. Mirrors the structure of
`Phase0-RHI-Seam.md`, `Phase1-Audio-Video.md`, and `Phase2-Window-Input.md`.

## Objective

Replace three Windows-only subsystems in the main game binary with
cross-platform equivalents (INI file, `std::mutex`) or delete them outright
(embedded IE browser). Together with the Phase 0-2 selectors, the main game
code is now free of `CComObject`, `atlbase.h`, `atl_compat.h`,
`comsupp_compat.h`, `CRITICAL_SECTION`, and direct `HKLM`/`HKCU` registry
calls — that COM/registry surface retreats to the MFC tools (WorldBuilder,
ParticleEditor, etc.) which stay Windows-only.

Phase 3 does **not** add new CMake selectors; it removes Windows-only
dependencies from the main game.

## Locked decisions

| Question | Decision |
|---|---|
| Registry API | Preserved. `registry.cpp`'s 8 public functions stay; they delegate to `RegistryStore`. |
| Config backing | Flat INI file `RegistrySettings.ini` under `SDL_GetPrefPath("EA", "Generals")` on the SDL build, or `%USERPROFILE%\Documents\Command and Conquer Generals{,ZH} Data\` on the Win32 build. |
| Legacy Windows registry | First-read fallback only. When the INI lacks a key, read HKLM then HKCU, seed the INI, continue. One-way migration; the INI becomes authoritative. |
| `CriticalSection` | `std::mutex` wrapper, same `enter()`/`exit()` public surface, zero call-site churn. |
| `ScopedCriticalSection` | Unchanged — still the RAII wrapper over `enter()`/`exit()`. |
| Embedded browser | Abstract `WebBrowser` interface rewritten without ATL/COM. `W3DWebBrowser` stubbed (no-op `create/closeBrowserWindow`). `DX8WebBrowser`, `FEBDispatch`, `EABrowserDispatch`, `EABrowserEngine` deleted. |
| `atl_compat.h` / `comsupp_compat.h` | Retired from the game precompiled header. Files remain under `Dependencies/Utility/Utility/` for the MFC tools that still reach for them. |
| `getHandleForBink` / `releaseHandleForBink` | Renamed to `getVideoAudioStreamHandle` / `releaseVideoAudioStreamHandle` (Phase 1 deferral). |
| `initializeBinkWithMiles` | Renamed to `primeVideoAudio` (Phase 1 deferral). |
| `InterlockedIncrement` / raw Win32 `CRITICAL_SECTION` in WWAudio/WWDebug/tools | **Not in scope.** Kept for Phase 6/7. |

## Source-level changes

### Threading

**Files**:
- `Generals/Code/GameEngine/Include/Common/CriticalSection.h`
- `GeneralsMD/Code/GameEngine/Include/Common/CriticalSection.h`
- `Generals/Code/GameEngine/Source/Common/System/CriticalSection.cpp`
- `GeneralsMD/Code/GameEngine/Source/Common/System/CriticalSection.cpp`

`CriticalSection` now holds a `std::mutex` and implements `enter()`/`exit()`
as `lock()`/`unlock()`. The `PERF_TIMERS` `AutoPerfGather` instrumentation
that measured Win32 critical-section overhead is gone, along with the
corresponding `TheCritSecPerfGather` global in the `.cpp`.

`ScopedCriticalSection` (in the same header) is untouched — its body only
dereferences `enter()`/`exit()` through a pointer.

The 20 `ScopedCriticalSection` call sites across `AsciiString.cpp`,
`UnicodeString.cpp`, `GameMemory.cpp`, and `Debug.cpp` need zero edits. The
5 `critSec{1..5}` locals in `WinMain.cpp`/`AppMain.cpp` (both games) remain
default-constructible stack objects — unchanged.

### Registry → INI

**New files**:
- `Core/GameEngine/Include/Common/RegistryStore.h`
- `Core/GameEngine/Source/Common/System/RegistryStore.cpp`

`RegistryStore` inherits from `UserPreferences` (so it reuses the existing
INI parse/write machinery) but overrides `load()` so it can compute its own
config directory via `SDL_GetPrefPath` (SDL build) or `SHGetKnownFolderPath(FOLDERID_Documents)`
(Win32 build). The singleton accessor `RegistryStore::get()` constructs on
first call — important because `GetRegistryLanguage()` runs during
`CommandLine::parseCommandLineForStartup()`, before `TheGlobalData` exists.

**Rewritten files**:
- `Generals/Code/GameEngine/Source/Common/System/registry.cpp`
- `GeneralsMD/Code/GameEngine/Source/Common/System/registry.cpp`

All 8 public functions (`GetStringFromRegistry`, `GetUnsignedIntFromRegistry`,
`GetRegistryLanguage`, etc.) retain their signatures and delegate to
`RegistryStore::get()->getString/getUnsignedInt`. The old private
`getStringFromRegistry(HKEY, …)` helpers are gone.

On Windows, `RegistryStore::getString/getUnsignedInt` falls back to
`RegOpenKeyEx(HKLM)` then `RegOpenKeyEx(HKCU)` when the INI lacks a key,
seeds the value into the INI, and writes. Subsequent reads hit the INI only.

### Browser removal (Option B)

**Rewritten** as a plain abstract interface (ATL/COM stripped):
- `Core/GameEngine/Include/GameNetwork/WOLBrowser/WebBrowser.h`
- `Core/GameEngine/Source/GameNetwork/WOLBrowser/WebBrowser.cpp`

`WebBrowser` now inherits only from `SubsystemInterface`. No
`QueryInterface/AddRef/Release`, no `TestMethod`, no `IBrowserDispatch`,
no `OLEInitializer`, no `CComModule`. The `WebBrowserURL` list machinery +
`findURL/makeNewURL` is preserved verbatim. `TheWebBrowser` changes type
from `CComObject<WebBrowser>*` to `WebBrowser*`.

**Stubbed**:
- `Generals/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWebBrowser.h`
- `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWebBrowser.h`

`W3DWebBrowser::createBrowserWindow` returns `TRUE`; `closeBrowserWindow` is
a no-op. The two legacy UI call sites (`WOLLoginMenu.cpp` for Terms of
Service, `WOLLadderScreen.cpp` for the Message Board) compile and link
unchanged; their panels render blank.

**Deleted**:
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DWebBrowser.cpp`
- `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DWebBrowser.cpp`
- `Core/GameEngine/Include/GameNetwork/WOLBrowser/FEBDispatch.h`
- `Core/Libraries/Source/WWVegas/WW3D2/dx8webbrowser.{h,cpp}`
- `Core/Libraries/Source/EABrowserDispatch/` (entire directory, incl. IDL + generated TLB)
- `Core/Libraries/Source/EABrowserEngine/` (entire directory, incl. IDL)

**Factory updates** (both games):
- `Win32GameEngine::createWebBrowser()` changed from `NEW CComObject<W3DWebBrowser>` to plain `NEW W3DWebBrowser`.

**Render-loop hooks dropped**:
- `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` — deleted 2×
  `DX8WebBrowser::Update()` calls in `Begin_Scene()` and 1×
  `DX8WebBrowser::Render(0)` in `End_Scene()`. Also removed
  `#include "dx8webbrowser.h"`.
- `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`
  and `GeneralsMD/.../W3DDisplay.cpp` — deleted `DX8WebBrowser::Initialize()`
  / `DX8WebBrowser::Shutdown()` calls and removed the include.

**ATL/COM retreat**:
- `Generals/Code/GameEngine/Include/Precompiled/PreRTS.h` and
  `GeneralsMD/.../PreRTS.h` — removed the `<atlbase.h>` and `<Utility/atl_compat.h>`
  includes. The only consumer of ATL in the game was the embedded browser,
  which no longer exists.
- `Dependencies/Utility/Utility/atl_compat.h` and `comsupp_compat.h` — files
  stay on disk. Tool targets that still consume them (WorldBuilder etc.)
  are unaffected.

### Rename sweep

`AudioManager`'s video-audio handshake methods are now backend-neutral:

| Before | After |
|---|---|
| `getHandleForBink()` | `getVideoAudioStreamHandle()` |
| `releaseHandleForBink()` | `releaseVideoAudioStreamHandle()` |

`BinkVideoPlayer::initializeBinkWithMiles()` and
`FFmpegVideoPlayer::initializeBinkWithMiles()` became
`primeVideoAudio()`. 9 files touched, 34 occurrences of the new names,
zero of the old names.

### CMake plumbing

- `Core/GameEngine/CMakeLists.txt` — added `Source/Common/System/RegistryStore.cpp`
  to the source list; removed `FEBDispatch.h` from the header list; dropped
  `core_browserdispatch` from `corei_gameengine_public` link libs.
- `Core/Libraries/CMakeLists.txt` — removed `add_subdirectory(Source/EABrowserDispatch)`
  and `add_subdirectory(Source/EABrowserEngine)`.
- `Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt` — removed
  `dx8webbrowser.cpp`/`.h` from the source list.
- `Generals/Code/GameEngineDevice/CMakeLists.txt` and `GeneralsMD/...` —
  removed `W3DWebBrowser.cpp` from the source list. `W3DWebBrowser.h` stays
  (header-only stub).
- `Generals/Code/Tools/WorldBuilder/CMakeLists.txt` and `GeneralsMD/...` —
  removed `core_browserdispatch` from the link libs.
- Per-game `GameEngine/CMakeLists.txt` — removed the commented-out
  `FEBDispatch.h` entries.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21. Clean configure:

| Command | Outcome |
|---|---|
| `cmake .. -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK — `PlatformBackend: sdl`, all four backends listed |
| `cmake -LH build` feature summary | `RendererBackend: dx8 / AudioBackend: openal / VideoBackend: ffmpeg / PlatformBackend: sdl` |

Full build + link + runtime smoke tests (Windows main menu, WOL TOS screen,
WOL Ladder screen, 30-second skirmish with SDL event pump, Miles, DX8) run
on the Windows CI matrix once the PR lands — this host has no Windows
toolchain.

Static-sweep grep results after Phase 3 (excluding docs):

- `DX8WebBrowser|FEBDispatch|IBrowserDispatch|CComObject<W3DWebBrowser`: only 4 historical hits in comment text (PreRTS.h, WebBrowser.h, comsupp_compat.h self-doc). Zero in executable code.
- `InitializeCriticalSection|EnterCriticalSection|LeaveCriticalSection`: 6 hits — all in `WWAudio`, `WWDebug`, `WWLib`, dedicated-server tools (`matchbot`, `mangler`), and `simpleplayer.cpp`. These are raw Win32 critical-section usages outside the `CriticalSection` abstraction and are **explicitly deferred** to Phase 6/7.
- `getHandleForBink|releaseHandleForBink|initializeBinkWithMiles`: zero occurrences (renames complete).
- `getVideoAudioStreamHandle|releaseVideoAudioStreamHandle|primeVideoAudio`: 34 occurrences across 9 files — all at expected rename sites.
- `atl_compat|comsupp_compat` outside the files themselves and cmake helpers: zero in `Core/`, `Generals/Code/`, `GeneralsMD/Code/`.

## Deferred to later phases

| Item | Phase |
|---|---|
| `InterlockedIncrement` → `std::atomic` | Phase 4 (networking) or Phase 7 (DX8 retirement) |
| Raw Win32 `CRITICAL_SECTION` in `WWAudio`, `WWDebug`, `WWLib`, `simpleplayer.cpp`, dedicated-server tools | Phase 6/7 |
| `pseh_compat.h` retirement (SEH call sites) | Phase 4/7 |
| `tchar_compat.h` retirement | Phase 6 |
| `ApplicationHWnd` rename to backend-neutral | Phase 7 (after DX8 retires) |
| `Win32Mouse` rename to `BufferedMouse` | Phase 7 |
| Writing back to Windows registry | never — INI is authoritative |
| Runnable macOS binary (still blocked on DX8/bgfx) | Phase 5/6 |
| `ShellExecute` → `SDL_OpenURL` routing | opportunistic — no callers today |
| Splitting `OpenALAudioManager.cpp` so `Null` variant doesn't pull OpenAL | Phase 6 |
