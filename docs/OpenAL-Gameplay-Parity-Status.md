# OpenAL Audio Backend — Gameplay Parity Status (macOS / bgfx)

Companion to `docs/Phase1-Audio-Video.md`. That doc covers the initial Phase-1
scaffolding (ALC device lifecycle, `OpenALAudioStream` for FFmpeg-decoded video
audio). This doc tracks the later work to bring the OpenAL backend up to
Miles parity for gameplay audio on macOS.

## Problem chain that prompted this work

1. **No audio with `-win` on macOS.** Intro videos worked in fullscreen, were
   silent in windowed mode.
2. **No main-menu music / SFX in any mode** — even after (1) was fixed, because
   `OpenALAudioManager` was Phase-1 scaffolding with every playback method
   stubbed to `{}`.
3. **No in-game audio** — unit voices (`AT_Streaming`), weapon SFX, positional
   audio, ambient loops, mission briefings, and scripted music changes all
   depend on pipeline features the Phase-1 scaffolding never implemented.

## Fix 1 — Windowed audio race (shipped)

`SDL_ShowWindow` on a hidden window uses `orderFront:` on macOS, not
`makeKeyAndOrderFront:`, so the app wasn't frontmost when
`OpenALAudioManager::openDevice` ran inside `GameMain()`. CoreAudio's default
device selection bound the ALC context against a backgrounded-app route that
never produced audible output. Focus events after the fact didn't rebind it.

**Change:** after `SDL_ShowWindow` in both `Generals/Code/Main/AppMain.cpp` and
`GeneralsMD/Code/Main/AppMain.cpp`, raise + drain stale window events so the
main loop enters with the SDL window as the key window:

```cpp
SDL_ShowWindow(SDLDevice::TheSDLWindow);
SDL_RaiseWindow(SDLDevice::TheSDLWindow);
SDL_PumpEvents();
SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
```

Intro audio confirmed working in both modes after this.

## Fix 2 — Menu music + SFX (shipped)

Implemented the 2D-sample + music pipeline that routes `AudioEventRTS` →
`MusicManager`/`SoundManager` → `AudioRequest` → backend:

- **New:** `Core/GameEngineDevice/Include/OpenALAudioDevice/OpenALAudioFileCache.h`
  and its `.cpp`. Ref-counted cache keyed by VFS path. Decodes arbitrary audio
  formats via `FFmpegFile` → PCM16 → `alBufferData`. Reuses the swr pattern
  from `FFmpegVideoPlayer.cpp:356-405`.
- **Modified:** `OpenALAudioManager.h` / `.cpp` — pre-allocates 32 2D sources +
  one dedicated music source in `openDevice`; `update()` drains the
  `AudioRequest` queue and reclaims stopped sources; `playAudioEvent` binds
  buffer → source with loop / pitch / gain; `stopAudio` / `pauseAudio` /
  `resumeAudio` / `killAudioEventImmediately` walk the playing lists;
  overridden `setVolume` pushes `effectiveVolume` to live `AL_GAIN` so the
  existing focus-mute path (`muteAudio(WindowFocus)`) now actually silences
  output.
- **Modified:** `Core/GameEngineDevice/CMakeLists.txt` — registered the two new
  files under the `openal|null` branch.

## Fix 3 — Gameplay parity (shipped)

Builds on Fix 2 to cover everything the engine actually invokes during
gameplay. No new files; all in `OpenALAudioManager.h/.cpp`.

### Implemented

| Feature | Mechanism |
|---|---|
| 3D positional audio | `alSourcei(AL_SOURCE_RELATIVE, FALSE)` + `alSource3f(AL_POSITION, …)`; `AL_ROLLOFF_FACTOR = 0` so OpenAL doesn't double-attenuate. |
| Distance attenuation | Miles parity in `effectiveVolume`: clamp to 0 past `m_maxDistance`, `pow((d-min)/(max-min), exponent)` fade past `m_minDistance`; `ST_GLOBAL` events use `AudioSettings::m_globalMinRange/MaxRange`. |
| AT_Streaming (speech / unit voices / VO) | Same one-shot buffer path as SFX, tracked on `m_playingSpeech`. `getUninterruptible` toggles `setDisallowSpeech` so the base-class cull works. |
| Attack / Sound / Decay sequencing | `playAudioEvent` respects `getNextPlayPortion()`. On `AL_STOPPED`, `handleSourceStopped` advances the portion and swaps the AL buffer without releasing the source. |
| Finite loops (`AC_LOOP` + `loopCount > 0`) | `AL_LOOPING = FALSE`; on `AL_STOPPED`, `decreaseLoopCount` + `alSourceRewind` + `alSourcePlay`. Permanent loops (`loopCount == 0`, no portions) use hardware `AL_LOOPING`. |
| Frame-based delay | `shouldProcessRequestThisFrame` / `adjustRequest` mirror `MilesAudioManager::…` — delayed requests stay queued and `decrementDelay(MSEC_PER_LOGICFRAME_REAL)` each frame. |
| `isPlayingAlready(event)` | Count of matching event-name across all three playing lists + pending requests. |
| `isObjectPlayingVoice(objID)` | Scan lists for matching `ObjectID` + `ST_VOICE` type flag. |
| `doesViolateLimit(event)` | Count vs `info->m_limit`; `AC_INTERRUPT` events bypass the cap (caller is expected to kill the oldest). |
| `isPlayingLowerPriority(event)` + `killLowestPrioritySoundImmediately` | Priority comparison across SFX + speech lists; music is not evicted. |
| `friend_forcePlayAudioEventRTS` | Synchronous path used by `LoadScreen.cpp` mission briefings: resolves info, copies the event, runs `generateFilename/PlayInfo`, calls `playAudioEvent` directly (bypasses the request queue). |
| `hasMusicTrackCompleted(name, N)` | Per-track counter in `m_musicCompletions`, bumped in `processPlayingList` when a non-looping music source hits `AL_STOPPED`. |
| Per-frame listener push | Overridden `setListenerPosition` calls `setDeviceListenerPosition()` immediately so new positional sources track the tactical camera. |
| Per-frame positional refresh | `processPlayingList` pushes fresh `AL_POSITION` + recomputed `AL_GAIN` every frame for entries with `pa->positional`; dead-owner events (`event->isDead()`) finalize. |
| `m_volumeHasChanged` | Honoured for non-positional entries; cleared at end of `processPlayingList`. |

### Deliberately out of scope

- **EFX reverb / occlusion** (`m_lowPassFreq`, Miles `initFilters3D`). Apple's
  OpenAL framework does not expose EFX; would require switching to OpenAL-Soft
  or adding a DSP layer. Low priority — game still plays.
- **Speaker surround / explicit downmix.** CoreAudio handles downmix to the
  user's default device configuration; OpenAL sees stereo/mono buffers and
  macOS does the rest.
- **Hardware-acceleration provider toggling.** N/A on CoreAudio; kept as a
  stored preference.
- **Miles DSP delay filters** (`m_delayFilter`, `DP_FILTER` sample processors).
  Frame-based delay in `processRequestList` matches what the game's scripting
  expects; sub-frame DSP delay isn't exercised.
- **Dynamic provider switching** (`selectProvider` rebinding the ALC device).
  Still a stored preference that takes effect on next `openDevice`.

## Files changed summary

| File | Status |
|---|---|
| `Generals/Code/Main/AppMain.cpp` | Modified — `SDL_RaiseWindow` + flush after `SDL_ShowWindow`. |
| `GeneralsMD/Code/Main/AppMain.cpp` | Modified — same. |
| `Core/GameEngineDevice/Include/OpenALAudioDevice/OpenALAudioFileCache.h` | New. |
| `Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioFileCache.cpp` | New. |
| `Core/GameEngineDevice/Include/OpenALAudioDevice/OpenALAudioManager.h` | Modified — un-stubbed interface, added `PlayingAudio` + helper signatures + `m_musicCompletions`. |
| `Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioManager.cpp` | Modified — full playback pipeline (roughly Miles parity minus the deferred items above). |
| `Core/GameEngineDevice/CMakeLists.txt` | Modified — registered the file-cache pair in the `openal` branch. |

## Verification status

Confirmed working (by user testing):
- Intro video audio in windowed and fullscreen.
- Main-menu music + GUI click/hover SFX in both modes.

Not yet verified in play (implemented, needs ear check):
- In-game unit voice acks (select / move / attack).
- Positional weapon / explosion / death SFX with distance falloff.
- Ambient drawable loops (factory hum, terrain ambience).
- Mission-briefing VO on campaign load screens.
- Scripted music changes triggered by `MusicHasCompletedSomeAmount` conditions.
- Polyphony under heavy combat (source-pool culling under pressure).
- Options-panel volume sliders affecting live sources, including mute.

Build commands (both targets must compile clean):

```
cd build_bgfx && make -j8 g_generals z_generals
```

Run commands (manual ear-check needed for each):

```
./build_bgfx/Generals/generalsv.app/Contents/MacOS/generalsv -win
./build_bgfx/Generals/generalsv.app/Contents/MacOS/generalsv
./build_bgfx/GeneralsMD/generalszh.app/Contents/MacOS/generalszh -win
./build_bgfx/GeneralsMD/generalszh.app/Contents/MacOS/generalszh
```

## Open follow-ups

- Once OpenAL-Soft replaces Apple's framework (or an EFX shim is added),
  re-enable reverb / low-pass occlusion by implementing `initFilters3D` parity.
- Consider streaming (not one-shot buffering) for music tracks longer than a
  few minutes — `OpenALAudioStream` already has the buffer-queue pattern; the
  file cache would need a producer thread / per-frame pump.
- `m_zoomVolume` (camera-zoom volume boost from `AudioManager::update`) is
  populated by the base class but not consumed by our `effectiveVolume`. Fold
  it into the `AT_SoundEffect` branch when zoom-aware loudness becomes
  desirable.
