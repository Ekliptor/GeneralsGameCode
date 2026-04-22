# Phase 7 — DX8 retirement roadmap (optional later cleanup)

This is a **reference roadmap**, not an execution plan. Every item below is
gated on invariants the current environment cannot verify — principally
**replay bit-identity between 32-bit DX8 and bgfx builds** — so the
retail-compat path has to stay buildable until a real parity run signs
off. Once that signal is available, this doc is the punch list.

Phases 5 and 6 closed feature-complete (5h.36 texture adapter; 6g first
non-Windows CI job). Phase 7's scope per
`docs/CrossPlatformPort-Plan.md` §Phasing item 8:

> **Phase 7 — Retire DX8.** Once bgfx is replay-identical, default to
> bgfx on Windows and remove the DX8 backend, Miles, Bink, DirectInput,
> and the Win32 entry point.

## Preconditions before *any* 7.x item lands

1. **Replay parity signal.** A deterministic replay plays bit-identically
   between the retail 32-bit DX8 build and the cross-platform bgfx build.
   The test harness itself is tracked as Phase 5 deferred work ("golden-
   image CI parity test", plan §Verification) and Phase 6e deferred
   ("replay bit-identity regression harness"). Until that harness is
   green on at least one replay, deleting DX8 / Miles / Bink source risks
   permanent loss of the retail-compat reference.
2. **Windows CI green on bgfx.** Phase 6c declared `RTS_WINDOWS_64BIT`
   but no Windows bgfx CI runs yet. Phase 6g only covers macOS arm64.
   A dedicated `build-windows-bgfx.yml` (6g deferred) needs to exist
   and stay green across a meaningful commit window.
3. **macOS `.app` artifact built end-to-end.** Phase 6f scaffolded the
   bundle but the game link itself is still blocked on unported
   subsystems. Until `Generals.app` is a working launchable artifact,
   "bgfx is the default on Windows" is premature — deleting DX8 without
   a working alternate reference is irreversible.

Sub-phases 7a and 7b are **independent** of those preconditions — they
remove code that is already proven unreferenced. Everything from 7c
onward waits on the preconditions.

## 7a — Dead compat shims (no preconditions)

**Files confirmed unreferenced via `#include` grep and CMake
`target_sources` sweep (2026-04-21):**

| File | LOC | Consumers |
|---|---|---|
| `Dependencies/Utility/Utility/pseh_compat.h` | 52 | Only `atl_compat.h`, which is itself unused. |
| `Dependencies/Utility/Utility/comsupp_compat.h` | 144 | **Zero.** Old consumer `dx8webbrowser.cpp` already deleted. |
| `Dependencies/Utility/Utility/atl_compat.h` | 90 | **Zero.** Already documented as retired in both `PreRTS.h` files. |

**Deliverable:** delete all three files (~286 LOC), update the stale
comment in the two `PreRTS.h` files that still names `atl_compat.h`,
scrub the `cmake/reactos-atl.cmake` comments at lines 35–37 / 56 that
point at the now-deleted `comsupp_compat.h`. The `reactos_atl` target
itself stays — `wolSetup` still needs the ReactOS ATL headers on
MinGW.

**Risk:** essentially zero. Three `rm` + three comment edits. If the
build breaks, one of the files wasn't actually dead and we learn
something.

**Verification:** flagless configure + build of `corei_bgfx` + 23 bgfx
tests green.

## 7b — Embedded browser retirement (no preconditions)

**Current state:**

- `WebBrowser.{h,cpp}` in `Core/GameEngine/{Include,Source}/GameNetwork/
  WOLBrowser/` — ~176 LOC abstract no-op stubs.
- `W3DWebBrowser.h` duplicated in `Generals/Code/` + `GeneralsMD/Code/`
  — 37 LOC each, inline no-op overrides.
- Three live call sites: `WOLLoginMenu.cpp`, `WOLLadderScreen.cpp`,
  `INIWebpageURL.cpp`. All already guarded `if (TheWebBrowser !=
  nullptr) { ... }`, so the global going permanently null produces
  safe no-ops.

**Deliverable:**
1. Delete the four `WebBrowser.*` / `W3DWebBrowser.h` files.
2. Remove three `TheWebBrowser` call sites (or replace with `SDL_OpenURL`
   where the URL is well-defined — `INIWebpageURL.cpp` is the candidate).
3. Remove any `TheWebBrowser` global definition / initialization.
4. Unlink `corei_wolbrowser` (or equivalent library target) from the
   game executables.

**Risk:** low. The interface is already hollow. Call-site rewrites are
mechanical. Only risk is an orphan `#include "WebBrowser.h"` that needs
to go with it.

**Verification:** bgfx build clean; DX8 build (if still supported)
clean; menu path still exits the Login / Ladder screens without
segfaulting on null `TheWebBrowser`.

## 7c — Narrow `reactos_atl` linkage

**Current state:** `Core/GameEngine/CMakeLists.txt:1208` links
`reactos_atl` to `corei_gameengine_public` on MinGW. Game engine does
not use ATL types (grep confirmed — all hits are comments). The real
consumer is `Core/Tools/wolSetup/wolInit.cpp` (`<atlbase.h>`), built
under `RTS_BUILD_CORE_EXTRAS=ON`.

**Deliverable:**
1. Remove the `target_link_libraries(corei_gameengine_public INTERFACE
   reactos_atl)` line at `Core/GameEngine/CMakeLists.txt:1208`.
2. Add `target_link_libraries(core_wolsetup PRIVATE reactos_atl)` with
   a MinGW guard in `Core/Tools/wolSetup/CMakeLists.txt`.

**Precondition:** MinGW host available for verification. Without it,
this change silently breaks MinGW wolSetup builds (nobody else runs the
MinGW-only codepath from macOS CI).

**Risk:** low given a MinGW runner; high without.

## 7d — Delete `cmake/reactos-atl.cmake`

Follows 7b (`wolSetup` might die as part of browser retirement) *or*
follows tooling-scope review. If `wolSetup` is retained, this file
stays. If `wolSetup` is dropped — it talks to Westwood Online which is
defunct — then `reactos-atl.cmake` retires with it.

Scope: delete the file + its `include(...)` site in root `CMakeLists.txt`
+ the FetchContent of `reactos/reactos` at ~6 MB source footprint.

## 7e — Flip Windows renderer default to `bgfx`

One-line change in `cmake/config-build.cmake`:

```cmake
if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    set(_rts_default_renderer "dx8")    # becomes "bgfx"
    set(_rts_default_audio    "miles")  # becomes "openal"
    set(_rts_default_video    "bink")   # becomes "ffmpeg"
    # ...
endif()
```

**Precondition — critical:** replay parity signal. This is the headline
"retail-compat is no longer the default" move. Reversible (single line)
but visible: any Windows contributor running `cmake -S . -B build` with
no flags gets the modern stack immediately. Retail-compat becomes
opt-in via `-DRTS_RENDERER=dx8 -DRTS_AUDIO=miles -DRTS_VIDEO=bink`.

**Risk:** moderate. Breaks every downstream script / doc that assumed
`dx8` was the Windows default, even when it was implicit.

## 7f — Delete the DX8 backend source

**Precondition — critical:** replay parity + 7e default-flip lived on
`main` for at least one release cycle with no regressions reported.

**Scope estimate (from `CrossPlatformPort-Plan.md`):**

- `Core/Libraries/Source/WWVegas/WW3D2/` — ~8.4 kLOC of DX8 wrapper
  (`dx8wrapper.*`, `dx8caps.*`, `dx8texman.*`, `dx8polygonrenderer.*`,
  `formconv.h`, `rddesc.h`, `dx8list.h`, plus whatever else the session
  has accumulated under Phase 5h work).
- `Generals/Code/Libraries/Source/WWVegas/WW3D2/` + `GeneralsMD/...` —
  ~4.7 kLOC per-game DX8 specialization (`dx8renderer.*`, `dx8fvf.*`,
  `dx8vertexbuffer.*`, `dx8indexbuffer.*`).
- `cmake/dx8.cmake` — retail SDK FetchContent shim.
- `RTS_RENDERER_DX8` compile definition and all its `#ifdef`s.
- Every file that had a bgfx path added in Phase 5 that still carries a
  DX8 `#else` branch — those `#else` branches can be deleted, not just
  the `#ifdef RTS_RENDERER_DX8` guard.

**Scope estimate:** ~13+ kLOC source deletion, tens of CMake edits,
dozens of `#ifdef` collapses.

**Risk:** irreversible. This is where retail-compat truly dies.

## 7g — Delete Miles (audio retail backend)

Mirror of 7f for audio: `Core/GameEngineDevice/{Include,Source}/
MilesAudioDevice/`, `cmake/miles.cmake`, `RTS_AUDIO_MILES`
conditional dispatch, and all the `milesstub` plumbing.

**Precondition:** replay parity + 7e default-flip proven stable. Audio
is less visibly tied to replay identity than the renderer, but it
shares the same "retail-compat-only" status and lives behind the same
32-bit gate in root `CMakeLists.txt:59`.

## 7h — Delete Bink (video retail backend)

Same shape: `Core/GameEngineDevice/{Include,Source}/VideoDevice/Bink/`,
`cmake/bink.cmake`, `RTS_VIDEO_BINK` conditional dispatch, `binkstub`.

## 7i — Delete DirectInput wrappers + `Win32Mouse.cpp` / `Win32DI*.cpp`

`Core/GameEngineDevice/Source/Win32Device/GameClient/Win32DI{Keyboard,
Mouse}.cpp` and `Win32Mouse.cpp` — ~1.4 kLOC. SDL3 already covers
every input they provide.

**Precondition:** 7e default-flip. On MSVC the Win32 backend might
still be useful for bugs that are platform-specific to SDL, so a
`RTS_PLATFORM=win32` opt-in might stay one release longer than DX8/
Miles/Bink.

## 7j — Delete `WinMain.cpp`

`Generals/Code/Main/WinMain.cpp` + `GeneralsMD/Code/Main/WinMain.cpp`
— ~200 LOC each. Already decoupled (`#if RTS_PLATFORM_WIN32` guards +
`AppMain.cpp` is selected for `RTS_PLATFORM=sdl`). Deletion is
symbolic; it just removes the dead-code branch once 7i lands.

## 7k — Retire retail-SDK CMake gate

The root `CMakeLists.txt:59` block

```cmake
if((WIN32 OR ...) AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    include(cmake/miles.cmake)
    include(cmake/bink.cmake)
    include(cmake/dx8.cmake)
endif()
```

goes away once the three `.cmake` files have nothing left to include.
Along with it, the mingw-w64-i686 toolchain, 32-bit guards, the whole
LLP64 story. The Phase 6 `RTS_WINDOWS_64BIT` option becomes the
default and the option itself retires.

## 7l — Audit pass on `docs/CrossPlatformPort-Plan.md` §Subsystem
## Alternatives

Every "Option (chosen)" row in the alternatives tables becomes "the
single implementation" after 7f-k. The plan doc's tables can be
collapsed to one-sentence statements of fact (e.g. "Audio: OpenAL Soft"
instead of the four-row comparison).

## Rough LOC budget

| Sub | Rough net deletion | Risk |
|---|---|---|
| 7a | ~300 source | Zero (dead code). |
| 7b | ~500 source | Low (already stubbed). |
| 7c | ~0 (move only) | Low, MinGW-gated. |
| 7d | ~70 cmake + freed FetchContent | Low. |
| 7e | 4 strings in one file | Moderate (visible default flip). |
| 7f | ~13,000 | Irreversible, gated on parity. |
| 7g | ~500 | Gated on parity. |
| 7h | ~500 | Gated on parity. |
| 7i | ~1,400 | Gated on parity. |
| 7j | ~400 | Symbolic. |
| 7k | ~200 cmake | Trivial once 7f-j land. |
| **Total** | **~17,000 LOC** | — |

## Not in scope (ever)

- **MFC tools** (WorldBuilder, W3DView, ImagePacker, ParticleEditor,
  DebugWindow). Stay Windows-only per the locked-decisions table in
  `CrossPlatformPort-Plan.md`. They are independently buildable and
  don't block the game retiring DX8.
- **GameSpy → OpenSpy rewrite** for multiplayer. Orthogonal to DX8
  retirement. Tracked under Phase 4.
- **32-bit Windows build mode** entirely. Phase 6c / 6d work keeps 64-bit
  opt-in; flipping it to mandatory is explicitly Phase 7k territory.

## Tracking / how this doc should be used

When one of the preconditions clears, pick the smallest unblocked item,
copy its section into a fresh `docs/Phase7<letter>-<slug>.md` with a
real scope gate + verification section (matching the Phase 6 template),
and implement. This doc stays as the index.

If a sub-phase lands on `main`, strike it from here and note the doc
that replaced it. This file becomes shorter over time; eventually
(post-7k) it retires into `CrossPlatformPort-Plan.md` itself as a
single line: "Phase 7 complete."