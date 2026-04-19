# Phase 2 — Window / Input / Timing (SDL3)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Mirrors `docs/Phase0-RHI-Seam.md`
and `docs/Phase1-Audio-Video.md` in structure: disposition table, what shipped,
what's deferred.

## Objective

Land a **selectable cross-platform window/input/timing backend (SDL3)**
alongside the existing Win32 `WinMain` + DirectInput path, without changing
legacy behavior on the `win32-msvc-dx8` retail-compat build. When
`RTS_PLATFORM=sdl`, SDL3 owns the window and the event pump; the DX8 renderer
still draws into the SDL-owned HWND on Windows, so the cross-platform branch
is demonstrable end-to-end on Windows ahead of the eventual macOS runtime
(which lands in Phase 5/6 once bgfx exists).

Phase 2 does **not** ship a runnable macOS binary. DX8, MFC, COM, and the
legacy Win32 file/thread/registry plumbing still gate macOS execution.

## Locked decisions

| Question | Decision |
|---|---|
| Windows default platform | `RTS_PLATFORM=win32` (preserve retail WndProc behavior) |
| macOS / non-Windows default | forces `RTS_PLATFORM=sdl` (guard rejects `win32` on non-Windows hosts) |
| WinMain.cpp on SDL path | compiled-out via `#if RTS_PLATFORM_WIN32`; the cross-platform `AppMain.cpp` takes over |
| Mouse refactor | **skipped** — SDL events translate to synthetic `WM_*` records and feed `Win32Mouse::addWin32Event()` directly, so `Win32Mouse` stays unchanged and `W3DMouse` keeps its parent |
| Key codes | game internal codes stay DIK-valued (as the engine already assumes); SDL3 translates `SDL_Scancode` → DIK in a static helper |
| Gamepad / IME / clipboard | out of scope — not exercised by the game |
| `ApplicationHWnd` rename | deferred to Phase 3 cleanup |
| Miles/Bink removal under SDL3 | **still deferred** (Phase 5/7) — SDL3 coexists with them on Windows |

## CMake surface

One new cache variable in `cmake/config-build.cmake`, mirroring Phase 0/1:

| Variable | Values | Default | Compile define |
|---|---|---|---|
| `RTS_PLATFORM` | `win32` \| `sdl` | `win32` | `RTS_PLATFORM_WIN32=1` / `RTS_PLATFORM_SDL=1` |

Non-Windows hosts that request `RTS_PLATFORM=win32` hit an immediate
`FATAL_ERROR`; unknown values hit the same error legibly.

New cross-platform module:
- `cmake/sdl3.cmake` — `find_package(SDL3 CONFIG REQUIRED)` gated on
  `RTS_PLATFORM=sdl`. Works against vcpkg's `SDL3Config.cmake` and Homebrew's
  upstream-shipped `SDL3Config.cmake` equally.

`vcpkg.json` gained `"sdl3"`.

`cmake/openal.cmake` was widened to also load for `RTS_AUDIO=null` (the
`OpenALAudioManagerNull` translation unit still references OpenAL symbols
because it shares `.cpp` files with `OpenALAudioManager`; a Phase 1 oversight
that only surfaced on macOS with `null+ffmpeg`).

## Source-level changes

### New SDL3 input + event pump

New files under `Core/GameEngineDevice/{Include,Source}/SDLDevice/`:

| File | Role |
|---|---|
| `GameClient/SDLKeyboard.h` / `.cpp` | `Keyboard` subclass. Static `SDL_Scancode→DIK` translator (stable PS/2 Set 1 values, no `dinput.h` dependency). Internal 256-slot ring buffer fed by `pushEvent(SDL_Event&)` and drained by `getKey(KeyboardIO*)`. Repeats dropped at the feeder (base class does its own repeat handling). |
| `Common/SDLGlobals.h` / `.cpp` | Holds `TheSDLWindow`. On Windows, `getNativeWindowHandle()` returns the underlying HWND via `SDL_GetWindowProperty(SDL_PROP_WINDOW_WIN32_HWND_POINTER)` so `ApplicationHWnd` stays meaningful for DX8 / cursor-clip / title-set. |
| `Common/SDLGameEngine.h` / `.cpp` | Subclass of `Win32GameEngine`. Overrides `serviceWindowsOS()` with `SDL_PollEvent` loop. Translates `SDL_EVENT_MOUSE_*` into synthetic `WM_*`+wParam+lParam and feeds `TheWin32Mouse->addWin32Event()` — so the existing `Win32Mouse::translateEvent` consumes them unchanged. `SDL_EVENT_KEY_DOWN/UP` go to `TheSDLKeyboard->pushEvent()`. Focus gain/loss routes through the same `AudioManager::muteAudio(MuteAudioReason_WindowFocus)` and `Mouse::refreshCursorCapture()` calls as the legacy WndProc. |

No changes to `Win32Mouse`, `W3DMouse`, `Keyboard`, or the game-side call
sites. The `W3DMouse : public Win32Mouse` hierarchy is preserved on both
paths; the mouse event pipe is unchanged, only its upstream source flips.

### Cross-platform entry point

New files (duplicated per game because each defines its own
`CreateGameEngine()` and the `BuildVersion.h` constants diverge):

- `Generals/Code/Main/AppMain.cpp`
- `GeneralsMD/Code/Main/AppMain.cpp`

Each replicates the retail `WinMain` startup sequence — critical-section
globals, `initMemoryManager`, debug-heap flags, `CommandLine::parseCommandLineForStartup`,
`TheVersion` setup, `rts::ClientInstance::initialize()` single-instance check,
`GameMain()` — and adds SDL init + window creation + HWND extraction in place
of `RegisterClass/CreateWindow`. On Windows, `ApplicationHWnd` and
`ApplicationHInstance` are populated from the SDL-owned window so DX8
rendering into that HWND keeps working.

`CreateGameEngine()` in each `AppMain.cpp` returns a `SDLGameEngine` instead
of the `Win32GameEngine` the legacy WinMain.cpp returns.

### Legacy entry points gated

Both `Generals/Code/Main/WinMain.cpp` and `GeneralsMD/Code/Main/WinMain.cpp`
are now wrapped in `#if RTS_PLATFORM_WIN32`. They compile byte-identically
when `RTS_PLATFORM=win32` (the default on Windows) and become empty
translation units when `RTS_PLATFORM=sdl`, so `AppMain.cpp` can provide the
singular `WinMain`/`main` + `CreateGameEngine` definitions.

### Factory wiring

`W3DGameClient::createKeyboard()` (Generals and GeneralsMD copies) now
dispatches on the platform selector:

```cpp
#if RTS_PLATFORM_SDL
    SDLKeyboard *kb = NEW SDLKeyboard;
    TheSDLKeyboard = kb;  // global for the SDL event pump
    return kb;
#else
    return NEW DirectInputKeyboard;
#endif
```

`createMouse()` still returns `W3DMouse` in both paths; the SDL pump reuses
the Win32Mouse event feeder via synthetic `WM_*` records.

### GameEngineDevice sources list

`Core/GameEngineDevice/CMakeLists.txt` got a new conditional block mirroring
the Phase 1 audio/video pattern:

- `RTS_PLATFORM=win32` adds `Win32DIKeyboard.{h,cpp}` (legacy DirectInput).
- `RTS_PLATFORM=sdl` adds `SDLDevice/**` and links `SDL3::SDL3`.
- `Win32Mouse.{h,cpp}` compiles unconditionally on Windows (needed by both
  paths via `W3DMouse`).

Each per-game `Main/CMakeLists.txt` (`Generals/Code/Main/`,
`GeneralsMD/Code/Main/`) picks `AppMain.cpp` vs `WinMain.cpp`/`WinMain.h`
based on `RTS_PLATFORM_LOWER` and links `SDL3::SDL3` on the SDL path.

## Verification

Ran on macOS 15 (Apple Silicon) with Homebrew `ffmpeg` 8.x, Homebrew `sdl3`
3.4.4, system OpenAL framework, CMake 4.3, AppleClang 21. Matrix sweep from
a clean build directory:

| Command | Outcome |
|---|---|
| `cmake ..` (defaults on macOS) | FATAL_ERROR: `RTS_PLATFORM=win32 requires a Windows host` |
| `cmake .. -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK — `PlatformBackend: sdl` |
| `cmake .. -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=bink` | OK — Bink fetch auto-skipped (Windows-gated) |
| `cmake .. -DRTS_PLATFORM=sdl -DRTS_AUDIO=null -DRTS_VIDEO=ffmpeg` | OK after widening `openal.cmake` to cover `null` (Phase 1 oversight fix) |
| `cmake .. -DRTS_PLATFORM=win32` on macOS | FATAL_ERROR with legible message |
| `cmake .. -DRTS_PLATFORM=bogus` | FATAL_ERROR with legible message |

Full compilation + link was **not** exercised — the project is Windows-native
(dx8/Win32Device/COM) and this host is macOS without a Windows toolchain. The
CMake configure sweep validates the selector plumbing. Windows smoke tests
(legacy `RTS_PLATFORM=win32` retail parity + SDL `RTS_PLATFORM=sdl` with DX8
rendering into the SDL-owned HWND) run on the Windows CI matrix once the PR
lands.

Static-sweep grep results after Phase 2:

- `RTS_PLATFORM`: 34 occurrences across 11 files — all in the new selector
  plumbing, the two `AppMain.cpp` files, the two `WinMain.cpp` guards, the
  two `W3DGameClient.h` factory branches, and the three CMakeLists.txt
  conditional blocks.
- `RTS_PLATFORM_WIN32` guards `WinMain.cpp` entirely in both games.

## What actually runs under `RTS_PLATFORM=sdl`

- **Windows path (tested via configure sweep; compile/link/run pending
  Windows CI)**: SDL3 creates the HWND, AppMain.cpp populates
  `ApplicationHWnd`, DX8 initializes into that HWND, `SDLGameEngine`'s event
  pump replaces `Win32GameEngine::serviceWindowsOS()`, the game logic
  consumes input via unchanged `Keyboard`/`Mouse` interfaces.
- **macOS path**: configure only; DX8/Miles/Bink still block execution until
  Phase 5/6 lands bgfx and the app bundle.

## Deferred to later phases

| Item | Phase |
|---|---|
| Runnable macOS binary | Phase 5/6 (requires bgfx + app bundle + notarization) |
| Gamepad support via SDL_GameController | opportunistic follow-up |
| IME / text-input events (chat, player names) | deferred until UI needs surface |
| Clipboard routing | game doesn't use it outside the MFC `GUIEdit` tool |
| `SDL_SetWindowFullscreen` toggle polish, DPI scaling parity | Phase 6 |
| Rename `ApplicationHWnd`, `Win32Mouse`→platform-neutral names | Phase 3 cleanup |
| Delete `DirectInputKeyboard` / `Win32DIMouse` | Phase 7 (after DX8 retirement) |
| Hoist `Win32GameEngine` platform-neutral bits into a true base so `SDLGameEngine` doesn't inherit Win32 plumbing | Phase 6 (macOS trigger) |
| Extract `BufferedMouse` from `Win32Mouse` so `SDLMouse` can stand alone without synthetic `WM_*` | Phase 6/7 (when Win32 path retires) |
