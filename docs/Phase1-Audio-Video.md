# Phase 1 — Audio (OpenAL Soft) + Video (FFmpeg)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Mirrors `docs/Phase0-RHI-Seam.md`
in structure: disposition table, what shipped, what's deferred.

## Objective

Land **selectable cross-platform audio (OpenAL Soft) and video (FFmpeg)** backends
alongside the existing Windows-retail Miles/Bink implementations, without changing
the legacy behavior on the `win32-msvc-dx8` retail-compat build.

Phase 1 does **not** delete Miles or Bink — those are the legacy reference until
Phase 5 proves bgfx renderer parity. Windows defaults stay `RTS_AUDIO=miles` +
`RTS_VIDEO=bink`.

## Locked decisions

| Question | Decision |
|---|---|
| Windows default audio | `RTS_AUDIO=miles` |
| Windows default video | `RTS_VIDEO=bink` |
| Bink removal | Deferred to Phase 5/7 (after FFmpeg parity is verified in CI) |
| Miles removal | Deferred to Phase 7 |
| `RTS_BUILD_OPTION_FFMPEG` | Deprecated; forwards to `RTS_VIDEO=ffmpeg` with `DEPRECATION` warning |
| Sample decoding on OpenAL | FFmpeg libavformat/libavcodec (mandatory dep when `RTS_AUDIO=openal`) |

## CMake surface

Two new cache variables in `cmake/config-build.cmake`, mirroring Phase 0's
`RTS_RENDERER` pattern:

| Variable | Values | Default | Compile define |
|---|---|---|---|
| `RTS_AUDIO` | `miles` \| `openal` \| `null` | `miles` | `RTS_AUDIO_MILES=1` / `RTS_AUDIO_OPENAL=1` / `RTS_AUDIO_NULL=1` |
| `RTS_VIDEO` | `bink` \| `ffmpeg` | `bink` | `RTS_VIDEO_BINK=1` / `RTS_VIDEO_FFMPEG=1` (+`RTS_HAS_FFMPEG=1` for back-compat) |

Each legacy-SDK stub self-gates on the selector:
- `cmake/miles.cmake` early-returns unless `RTS_AUDIO=miles`.
- `cmake/bink.cmake` early-returns unless `RTS_VIDEO=bink`.

New cross-platform modules:
- `cmake/openal.cmake` — prefers vcpkg's `OpenALConfig.cmake`, falls back to
  CMake's built-in `FindOpenAL.cmake` (Homebrew/system/macOS framework).
  Sets the cross-module variable `RTS_AUDIO_NEEDS_FFMPEG=TRUE`.
- `cmake/ffmpeg.cmake` — prefers vcpkg's `FFMPEGConfig.cmake`, falls back to
  `pkg-config` (Homebrew `ffmpeg`, Linux system packages). Runs when
  `RTS_VIDEO=ffmpeg` OR `RTS_AUDIO_NEEDS_FFMPEG` is set.

`include(cmake/config.cmake)` was moved above the legacy-SDK fetch block in
the top-level `CMakeLists.txt` so the selectors are available when the stubs
decide whether to fetch.

`vcpkg.json` gained `"openal-soft"`.

## Source-level changes

### Audio — new backend

New files under `Core/GameEngineDevice/{Include,Source}/OpenALAudioDevice/`:

| File | Role |
|---|---|
| `OpenALAudioStream.h` / `.cpp` | Rolling-buffer streaming source. API: `bufferData`, `play`, `update`, `reset`, `isPlaying`. Used by `FFmpegVideoPlayer` for movie audio. |
| `OpenALAudioManager.h` / `.cpp` | Subclass of `AudioManager`. ALC device + context lifecycle, ALC device enumeration surfaced as providers, listener transform, Bink/video-audio interop via `getHandleForBink`, static `getALFormat` PCM helper, FFmpeg-backed `getFileLengthMS`. |
| `OpenALAudioManagerNull` (declared in `OpenALAudioManager.h`) | Headless dummy that keeps `getFileLengthMS` working (for script CRC) but opens no ALC device. Mirrors `MilesAudioManagerDummy`. |

### Video — FFmpeg path promoted

`FFmpegVideoPlayer.cpp` cleaned:
- `RTS_USE_OPENAL` / `RTS_HAS_OPENAL` → **`RTS_AUDIO_OPENAL`** (single define coming from the CMake selector).
- Dead `//BinkDoFrame`, `//BinkWait`, `//BinkSetVolume`, `//BinkSetSoundTrack`, `//BinkSoundUseDirectSound` comments removed. The volume-side-effect lines that called `TheAudio->getVolume(AudioAffect_Speech)` solely to compute an unused `volume` int were dropped.
- `initializeBinkWithMiles()` shrunk to a single `getHandleForBink()` prime call; noted in a `ToDo (Phase 3 cleanup)` comment for renaming to a neutral identifier.

### Factory wiring

`Win32GameEngine::createAudioManager(Bool dummy)` (Generals and GeneralsMD
copies) now dispatches on the CMake selector:

```cpp
#if RTS_AUDIO_OPENAL
    return NEW OpenALAudioManager[Null];
#elif RTS_AUDIO_NULL
    return NEW OpenALAudioManagerNull;
#else // RTS_AUDIO_MILES
    return NEW MilesAudioManager[Dummy];
#endif
```

`W3DGameClient::createVideoPlayer()` (Generals and GeneralsMD copies) now
dispatches on `RTS_VIDEO_FFMPEG`. The Generals (non-MD) copy previously
hardcoded Bink — it now matches GeneralsMD's pattern.

### GameEngineDevice sources list

`Core/GameEngineDevice/CMakeLists.txt` was restructured so the audio and
video source files + link libs come in by backend selection, not
unconditionally. The retail-default link set for `miles` + `bink` is
byte-equivalent to the pre-Phase-1 state (same `milesstub` + `binkstub`
interfaces). The old `if(RTS_BUILD_OPTION_FFMPEG)` block is gone — its work
is now owned by the `RTS_VIDEO_LOWER STREQUAL "ffmpeg"` arm plus
`cmake/ffmpeg.cmake`.

## What actually plays audio under `RTS_AUDIO=openal`

- **Intro / briefing / victory videos**: decoded by FFmpeg, audio track
  funneled through `OpenALAudioStream` via the existing `getHandleForBink`
  seam. Already wired in `FFmpegVideoPlayer::onFrame`.
- **Main-menu music, game SFX, 3D positional sounds, dialog/VO**: not yet
  implemented in Phase 1. The `OpenALAudioManager` playback-control virtuals
  are deliberate stubs (`stopAudio`, `addAudioEvent` via base, etc. — see
  the `ToDo` header comment). The build matrix stays green because the
  `AudioManager` base class provides default behavior for `muteAudio`,
  `setVolume`, and the like.

This is a deliberate scope cut: full `MilesAudioManager` parity (3,379 LOC of
sample/stream pools, priority culling, EFX reverb, speaker downmix) is
follow-up work. Phase 1 lands the **scaffolding + the video-audio path** so
the cross-platform branch is demonstrable and the CMake matrix is exercised.

## Verification

Ran on macOS 15 (Apple Silicon) with Homebrew `ffmpeg`, system OpenAL framework,
CMake 4.3, AppleClang 21. Five configure runs from a clean build directory:

| Command | Outcome |
|---|---|
| `cmake ..` (defaults) | OK — `AudioBackend: miles`, `VideoBackend: bink` |
| `cmake .. -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK — pulls OpenAL framework + Homebrew libav* via pkg-config |
| `cmake .. -DRTS_AUDIO=openal -DRTS_VIDEO=bink` | OK — FFmpeg still pulled via `RTS_AUDIO_NEEDS_FFMPEG` |
| `cmake .. -DRTS_AUDIO=null -DRTS_VIDEO=bink` | OK — `AudioBackend: null` |
| `cmake .. -DRTS_BUILD_OPTION_FFMPEG=ON` | OK with `CMake Deprecation Warning`, forwards to `VideoBackend: ffmpeg` |
| `cmake .. -DRTS_AUDIO=bogus` | FATAL_ERROR with legible message |
| `cmake .. -DRTS_VIDEO=mpeg` | FATAL_ERROR with legible message |

Full compilation + link was **not** exercised — the project is Windows-native
(dx8/Win32Device) and this host is macOS without a Windows toolchain. The
CMake configure sweep validates the selector plumbing. Actual binary validation
(Windows smoke test, replay bit-identity across `miles` and `openal`, intro
video A/B) runs on the Windows CI matrix once the PR lands.

Static-sweep grep results after Phase 1:

- `RTS_USE_OPENAL`: 0 occurrences (was 4)
- `RTS_HAS_OPENAL`: 0 occurrences (was 1)
- `RTS_BUILD_OPTION_FFMPEG`: 3 occurrences in `cmake/config-build.cmake` (the
  deprecation shim — required for backward compat with existing scripts/CI)

## Deferred to later phases

| Item | Phase |
|---|---|
| Full OpenAL game SFX/music/3D parity with Miles | follow-up inside Phase 1 (or Phase 1.5) |
| Rename `getHandleForBink` / `initializeBinkWithMiles` to backend-neutral names | Phase 3 cleanup |
| Delete Bink code path | Phase 5 or 7 (after bgfx parity) |
| Delete Miles code path | Phase 7 |
| macOS app-bundle layout + build | Phase 6 |
| EAX/EFX reverb parity | opportunistic — only if asset audit surfaces a need |
| Audio-parity golden-image / listener-distance test in CI | Phase 7 gating |
