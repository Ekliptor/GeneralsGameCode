# Phase 6a — Platform-appropriate defaults for non-Windows hosts

Companion doc to `docs/CrossPlatformPort-Plan.md`. First sub-phase of Phase 6
(64-bit Windows + macOS builds). Follows `Phase5h36-MipChainAwareCacheLoads.md`.

## Scope gate

Phase 5 closed with the texture-adapter surface complete, 23 bgfx tests green,
and the meta-status note "the remaining 5h-era work is integration verification
rather than new code." Phase 5h's deferred items (`Texture_Dimensions`
accessor, 24-bit `R8G8B8`, palette formats, per-sampler anisotropy level,
thread-safe mip-count map) are all marked *later* and none block bringup.

Phase 6 in `CrossPlatformPort-Plan.md` is "64-bit Windows + macOS builds — fix
`int`/`size_t`/pointer-cast issues + save/replay format audit" — a multi-week
initiative. This sub-phase pulls out the smallest high-value slice: **make a
flagless `cmake -S . -B build` on a non-Windows host pick the cross-platform
stack instead of silently collapsing the retail SDK defaults.**

Before 6a:

```
$ cmake -S . -B build           # on macOS
RendererBackend: dx8            # can't actually build
AudioBackend:    miles          # can't actually build
VideoBackend:    bink           # can't actually build
RTS_PLATFORM=win32 requires a Windows host.   ← the only one that fails fast
```

The `dx8/miles/bink` libraries are gated at the root `CMakeLists.txt:59`
behind `WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4`, so configure silently skipped
them and the build would have exploded hundreds of compile units later. Only
`RTS_PLATFORM=win32` had a host-check. 6a extends the same host-check pattern
to the other three selectors and flips the defaults symmetrically.

## Locked decisions

| Question | Decision |
|---|---|
| Where do the defaults live? | `cmake/config-build.cmake`, in a single `if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows") … else() …` block right above the four `set(RTS_* …)` declarations. One block picks all four defaults at once. |
| Variable naming | `_rts_default_renderer`, `_rts_default_audio`, `_rts_default_video`, `_rts_default_platform`. Leading underscore marks them as file-private scaffolding; they're only read a few lines later. |
| Should Windows defaults change? | **No.** Windows keeps `dx8/miles/bink/win32` — that is still the retail-compat reference until Phase 7. This phase is strictly additive for non-Windows hosts. |
| Why guard `dx8/miles/bink` with fatal errors when the root CMakeLists already gates them? | The existing gate silently skips `miles.cmake`/`bink.cmake`/`dx8.cmake`. A user who passes `-DRTS_RENDERER=dx8` on macOS gets a configure that "succeeds" and then fails at `ld` with cryptic missing-symbol errors. A fatal error at the selector makes the misconfiguration a five-second fix instead of a ten-minute dig through link logs. |
| Error phrasing | Mirror the exact shape of the existing `RTS_PLATFORM=win32` guard: `"RTS_X=y requires a Windows host (retail reference). Use -DRTS_X=z on non-Windows."` A reader grepping for `requires a Windows host` finds all four selectors together. |
| Should `RTS_PLATFORM` also get a platform-aware default? | **Yes.** The plan approved as 6a named renderer/audio/video only, but leaving `RTS_PLATFORM` at `win32` means a flagless non-Windows configure still hits the pre-existing fatal error. Flipping its default to `sdl` on non-Windows is the one-line extension that makes 6a actually deliver on "a flagless configure works" — would have been silly to ship without it. |
| Changed behavior for Windows users | None. Every default, every host-check branch, every compile definition is identical on Windows. Verified by `grep` — the `if(WIN32)` branch re-asserts the previous literal strings. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `cmake/config-build.cmake` | Added `if(WIN32) … else() …` default-picker block above the four `RTS_*` selector blocks. Changed `set(RTS_RENDERER "dx8" …)` → `set(RTS_RENDERER "${_rts_default_renderer}" …)` and same for `RTS_AUDIO` / `RTS_VIDEO` / `RTS_PLATFORM`. Added three fatal-error guards (inside the `dx8` / `miles` / `bink` dispatch branches) matching the existing `win32` host-check. |

### The default-picker

```cmake
if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    set(_rts_default_renderer "dx8")
    set(_rts_default_audio    "miles")
    set(_rts_default_video    "bink")
    set(_rts_default_platform "win32")
else()
    set(_rts_default_renderer "bgfx")
    set(_rts_default_audio    "openal")
    set(_rts_default_video    "ffmpeg")
    set(_rts_default_platform "sdl")
endif()
```

Runs once. Drives four downstream `set(… CACHE STRING …)` without `FORCE`, so
any explicit `-D` override still wins — this is strictly a default change.

### The retail host-checks

```cmake
if(RTS_RENDERER_LOWER STREQUAL "dx8")
    if(NOT (WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows"))
        message(FATAL_ERROR "RTS_RENDERER=dx8 requires a Windows host (retail reference). Use -DRTS_RENDERER=bgfx on non-Windows.")
    endif()
    target_compile_definitions(core_config INTERFACE RTS_RENDERER_DX8=1)
```

Same shape for `RTS_AUDIO=miles` and `RTS_VIDEO=bink`.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21.

| Command | Outcome |
|---|---|
| `rm -rf /tmp/flagless && cmake -S . -B /tmp/flagless` (no `-D` flags) | Configure succeeds. Feature summary shows `RendererBackend: bgfx`, `AudioBackend: openal`, `VideoBackend: ffmpeg`, `PlatformBackend: sdl`. |
| `cmake -S . -B /tmp/bad -DRTS_RENDERER=dx8` | **Fatal error:** `RTS_RENDERER=dx8 requires a Windows host (retail reference). Use -DRTS_RENDERER=bgfx on non-Windows.` (`config-build.cmake:42`) |
| `cmake -S . -B /tmp/bad -DRTS_AUDIO=miles` | **Fatal error** with the same phrasing shape (`config-build.cmake:61`). |
| `cmake -S . -B /tmp/bad -DRTS_VIDEO=bink` | **Fatal error** with the same phrasing shape (`config-build.cmake:81`). |
| `cmake --build build_bgfx --target corei_bgfx z_ww3d2 -- -j` | OK. Matches pre-6a state. |
| All 23 bgfx tests executed | **23 PASSED / 0 FAILED.** No regression from 5h.36. |

Windows regression guard (read-only; cannot cross-build from here):

| Static check | Result |
|---|---|
| `grep -n '"dx8"\|"miles"\|"bink"\|"win32"' cmake/config-build.cmake` | Each literal appears inside the `if(WIN32) …` default-picker plus its respective `set_property(CACHE … STRINGS …)` whitelist. No other occurrences. |
| `add_feature_info(RendererBackend …)`, `AudioBackend`, `VideoBackend`, `PlatformBackend` | All four present, identical phrasing to pre-6a. |
| `target_compile_definitions(core_config INTERFACE RTS_RENDERER_DX8=1)` / `RTS_AUDIO_MILES=1` / `RTS_VIDEO_BINK=1` / `RTS_PLATFORM_WIN32=1` | All four present, unchanged relative order, unchanged macro names. |

## What this phase buys

- A new macOS contributor cloning the repo can run `cmake -S . -B build`
  with zero flags and get a working bgfx/openal/ffmpeg/sdl configuration.
- Any attempt to select the retail stack on a non-Windows host fails at
  configure time with an actionable error instead of at link time with a
  wall of missing-symbol errors.
- The `RTS_PLATFORM=win32` host-check precedent is now consistently applied
  to all four backend selectors — easier to reason about and easier to
  grep.

## Deferred

| Item | Why | Stage |
|---|---|---|
| `CMAKE_OSX_ARCHITECTURES=arm64;x86_64` universal build | Needs vcpkg cross-arch triplet + Rosetta toolchain plumbing. Not a defaults change. | 6b |
| Windows 64-bit build | Still gated 32-bit for retail save/replay compat. Needs the `int`/`size_t`/pointer-cast audit. | 6c |
| `int`/`size_t`/pointer-cast audit | The work Phase 6 is actually named for — the largest remaining item under 6. | 6d |
| Save/replay format size audit | Prerequisite for flipping Windows to 64-bit. | 6e |
| macOS `.app` bundle + `notarytool` | Distribution concern, not a build concern. | 6f |
| CI matrix (`macos-arm64-bgfx`, `win32-msvc-bgfx`, `win32-msvc-dx8`) | Requires GitHub Actions credentials. External action. | 6g |
| Retiring DX8 / Miles / Bink code paths | Phase 7. | 7 |

## Meta-status

- **Phase 5:** complete (5a → 5h.36, plus 5i–5q; 23 bgfx tests green).
- **Phase 6:** 6a lands the smallest slice (platform-appropriate defaults);
  6b–6g still to land before Phase 7.
- **Phase 7:** not started.
- **`IRenderBackend`:** unchanged at 28 virtual methods.
- **bgfx tests:** 23/23 green, no new tests added (CMake-only change).
