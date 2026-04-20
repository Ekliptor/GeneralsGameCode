# Phase 6c — 64-bit Windows opt-in (bgfx stack)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Third sub-phase of Phase 6.
Follows `Phase6b-MacOSUniversal.md`.

## Scope gate

The retail 32-bit Windows build is `DX8 + Miles + Bink` by design — it has to
stay save/replay-compatible with 1.04 clients. But the bgfx/OpenAL/FFmpeg stack
has no 32-bit dependencies, so there is no principled reason to block 64-bit
on Windows once the retail middleware is out of the way. This phase adds the
opt-in flag that lifts the implicit 32-bit gate for the modern stack on a
Windows host. **The phase does not validate 64-bit on Windows** — that
requires a Windows builder, which this environment is not.

## Locked decisions

| Question | Decision |
|---|---|
| Where is the 32-bit gate? | `CMakeLists.txt:59`: `if((WIN32 OR ...) AND CMAKE_SIZEOF_VOID_P EQUAL 4) include(cmake/miles.cmake) / bink.cmake / dx8.cmake`. The three retail SDKs self-gate further on their `RTS_*_LOWER` selectors. |
| Does `RTS_WINDOWS_64BIT=ON` need to modify that gate? | **No.** The gate already produces the correct behaviour: on 64-bit Windows, the three retail `.cmake` stubs are simply not included, and the bgfx/openal/ffmpeg `.cmake` files (which are always included) do the right thing. The opt-in is documentary — it tells the contributor "yes, this is supported". The actual 32/64-bit dispatch is driven by the caller's toolchain (`-A x64` / `-G "Visual Studio 17 2022" -A x64`). |
| Why gate it behind an option at all? | A flag named `RTS_WINDOWS_64BIT` gives CI a single switch and makes it discoverable via `cmake-gui` / `ccmake`. Also gives future phases a hook to add 64-bit-only compile options. |
| What about `pseh_compat.h` / SEH? | `pseh_compat.h` was already retired from the cross-platform stack per earlier phases. MSVC 64-bit uses native SEH anyway — no diff required. |
| Save/replay compat on 64-bit Windows? | See `Phase6e-SaveReplayAudit.md`. Short version: the on-wire format uses fixed-width types (`Int` = `int32_t`, `UnsignedInt` = `uint32_t`, `Int64` = `int64_t`), so the wire is portable. **One internal typedef** (`XferFilePos = long`) is flagged; doesn't affect wire format on its own. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `cmake/config-build.cmake` | Added `option(RTS_WINDOWS_64BIT ...)`. Pure documentation/CI hook — no `if(RTS_WINDOWS_64BIT)` branch needed anywhere in this phase. |

## Verification

**Not verified from this environment** — the session runs on macOS 15 Apple
Silicon with no Windows toolchain available.

The intended verification path (to be executed once a Windows runner is
available in Phase 6g CI):

| Command | Expected |
|---|---|
| `cmake -S . -B build64 -G "Visual Studio 17 2022" -A x64 -DRTS_RENDERER=bgfx -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg -DRTS_PLATFORM=sdl -DRTS_WINDOWS_64BIT=ON` | Configure succeeds. `RendererBackend: bgfx`, no `miles.cmake`/`bink.cmake`/`dx8.cmake` inclusion. |
| `cmake --build build64 --target corei_bgfx z_ww3d2 --config Release` | OK. |
| Windows 64-bit bgfx test suite | All 23 bgfx tests PASSED, matching the macOS baseline. |
| Static sweep | `grep -n '#pragma pack' CMakeFiles/` — no surprises; `grep -n 'sizeof(long)' Core/` — no portability regressions. |

Static-only checks I *can* run from macOS:

| Check | Result |
|---|---|
| `grep -rn 'CMAKE_SIZEOF_VOID_P' cmake/ CMakeLists.txt` | Only the retail-SDK gate at root `CMakeLists.txt:59` + MinGW `mingw.cmake:8` + the toolchain file `mingw-w64-i686.cmake:28`. None of these block 64-bit bgfx. |
| `grep -rn '/MACHINE:X86\|-m32\|_M_IX86' cmake/ CMakeLists.txt` | No arch-specific flags anywhere. |
| `grep -rn 'RTS_WINDOWS_64BIT' .` | Option declared in `config-build.cmake`; referenced from this doc. No code branches. Intended. |

## What this phase buys

- A named, discoverable flag for 64-bit Windows builders.
- Documentation that explains why the existing 32-bit gate is *correct* as
  written and needs no code change.
- A hook for Phase 6g CI: a Windows matrix entry can set
  `-DRTS_WINDOWS_64BIT=ON` and know it's intentional.
- Zero risk to the retail 32-bit Windows build: no conditional was changed.

## Deferred

| Item | Why | Stage |
|---|---|---|
| Running the build on Windows 64-bit | No Windows host in this session. | 6g CI |
| Save/replay format audit | Has its own doc. | 6e (same session) |
| `int`/`size_t` narrowing audit | Has its own doc. | 6d (same session) |
| Actual 64-bit cleanup of the few flagged internal typedefs | Requires Windows test coverage first. | 6c.1 follow-up |
| Dropping the 32-bit gate entirely | Can happen after Phase 7 retires DX8/Miles/Bink. | Phase 7 |

## Meta-status

- Phase 5: complete.
- Phase 6: 6a–6c land; 6d–6g remain in this session.
- `RTS_WINDOWS_64BIT` option declared but inert until a Windows builder exists.
- bgfx tests: **23/23 green** (macOS only — no regression from this phase).
