# OpenAL crash on Apple Silicon (Apple framework bug)

Last updated: 2026-04-27.

> **Status (2026-04-27):** Option 1 (OpenAL Soft via Homebrew) implemented.
> `cmake/openal.cmake` probes `/opt/homebrew/opt/openal-soft` on Apple
> and prepends it to `CMAKE_PREFIX_PATH`; `CMAKE_FIND_FRAMEWORK LAST`
> keeps the MODULE-mode fallback off the broken framework. Source
> headers (`OpenALAudioManager.h`, `OpenALAudioFileCache.h`,
> `OpenALAudioStream.h`) drop their `#ifdef __APPLE__` framework
> include branch and use `<AL/al.h>` / `<AL/alc.h>` everywhere.
> `otool -L` on both `generalsv` and `generalszh` confirms
> `libopenal.1.dylib` (1.25.1) instead of `OpenAL.framework`.
> Bundle relocation (copying the dylib into `Contents/Frameworks/`)
> remains deferred to Phase 6f.4 alongside FFmpeg / SDL3.

Tracks the recurring `EXC_BAD_ACCESS / SIGBUS / EXC_ARM_DA_ALIGN`
crash that hits the BGFX/macOS build of GeneralsZH (and Generals)
during `OpenALAudioManager::playAudioEvent`. The earlier swresample-
to-44100 fix in
[`OpenALAudioFileCache.cpp`](../Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioFileCache.cpp)
removed one trigger but the underlying problem is in Apple's OpenAL
framework itself, so the crash still surfaces. This doc is a punch
list for the proper fix.

## Symptom

Interactive launch — usually within ~15–30 s of reaching the main
menu (often as soon as a UI hover/click sound or shell music starts).
Crash report top frames:

```
0   caulk            <deduplicated>                         + 76
1   caulk            <deduplicated>                         + 188
2   caulk            tree_allocator()::lambda()::__invoke   + 24
3   caulk            tiered_allocator::allocate             + 1136
4   caulk            exported_resource::do_allocate         + 24
5   AudioToolboxCore EABLImpl::create                       + 220
6   AudioToolboxCore acv2::AudioConverterChain::PostBuild   + 848
7   AudioToolboxCore newAudioConverter                      + 1456
8   AudioToolboxCore AudioConverterNewInternal              + 128
9   AudioToolboxCore AudioConverterNewWithOptions           + 512
10  OpenAL           OALSource::AppendBufferToQueue         + 256
11  OpenAL           OALSource::SetBuffer                   + 356
12  OpenAL           alSourcei                              + 548
13  generalszh       OpenALAudioManager::playAudioEvent     + 248
14  generalszh       OpenALAudioManager::processRequestList + 220
15  generalszh       OpenALAudioManager::update             + 36
```

Exception subtype: `EXC_ARM_DA_ALIGN` ("byte read Alignment fault")
at addresses ending in `…048` / `…050` — i.e. 8-byte aligned but not
16-byte aligned, while the caulk audio allocator wants the latter.

## Root cause

`alSourcei(source, AL_BUFFER, buffer)` (line 551 of
`OpenALAudioManager.cpp`) makes Apple's OpenAL framework attach the
PCM buffer to the source. Internally OpenAL must build an
`AudioConverter` chain that bridges the buffer's format / sample
rate / channel layout to whatever the audio device's mix bus
expects. Building that converter calls into `AudioToolboxCore`,
which calls into the Apple-private **`caulk`** audio allocator. On
Apple Silicon (M1 / M2 / M3 / M4) `caulk`'s lazy-init `tiered_allocator`
path alignment-faults under several reproducible conditions:

- The buffer's sample rate doesn't match the device's preferred mix
  rate (Generals content is largely 22050 Hz; modern Apple hardware
  mixes at 44100 / 48000 Hz). **Already mitigated** by resampling to
  44100 in `OpenALAudioFileCache::openBuffer` — this fix is in the
  tree.
- Repeated allocator pressure from many short converter rebuilds (one
  per `alSourcei` call per source). Not mitigated.
- Race between the main-thread `alSourcei` and the audio I/O thread
  (`com.apple.audio.IOThread.client`) using the same `caulk`
  resource. Not mitigated.

The framework is no longer maintained — Apple soft-deprecated
`OpenAL.framework` long ago (`Wdeprecated-declarations` warnings on
every `al*` call confirm) and the residual bugs on Apple Silicon are
not getting fixed upstream. Every modern macOS game I know of either
ships its own OpenAL Soft or skips OpenAL entirely.

## What's been tried

| Fix | Where | Effect |
|-----|-------|--------|
| Resample all decoded audio to 44100 Hz S16 in swr before `alBufferData` | `OpenALAudioFileCache.cpp:46-46` (`kTargetSampleRate`) | Removes the rate-mismatch trigger; reduces but does not eliminate the crash |
| `swr_get_out_samples`-sized output blob + `drainSwr` for trailing samples | same file | Correct sizing — needed regardless of which audio backend we use |

## Recommended fix (priority order)

### Option 1 — Swap to OpenAL Soft (recommended)

[OpenAL Soft](https://openal-soft.org/) is the de-facto cross-platform
OpenAL implementation. It's API-compatible with Apple's framework,
actively maintained, and known-good on Apple Silicon. Vendored or
linked from Homebrew it Just Works.

**Steps:**

1. Install via Homebrew:
   ```sh
   brew install openal-soft
   ```
   Lands at `/opt/homebrew/opt/openal-soft/{lib,include}` on Apple
   Silicon (already where `cmake/find_package(OpenAL …)` will look
   if `OPENAL_ROOT` is set).
2. Update the CMake link target. Today the BGFX target links the
   system framework — search for `OpenAL` in the cmake/ tree and the
   per-target `target_link_libraries(... OpenAL::OpenAL)` /
   `find_package(OpenAL REQUIRED)` lines. Pin the package via
   `OPENAL_ROOT=/opt/homebrew/opt/openal-soft` so CMake picks the
   Homebrew version over the framework. Verify with
   `otool -L generalszh.app/Contents/MacOS/generalszh | grep -i openal` —
   should show `libopenal.1.dylib`, not
   `/System/Library/Frameworks/OpenAL.framework/...`.
3. App-bundle relocate. The Homebrew dylib lives at
   `/opt/homebrew/opt/openal-soft/lib/libopenal.1.dylib` — for a
   redistributable .app, copy it into
   `generalszh.app/Contents/Frameworks/` and rewrite the install
   name with `install_name_tool -change … @rpath/libopenal.1.dylib`,
   or set `LC_RPATH` to `@executable_path/../Frameworks`. Already a
   pattern with `libavcodec.62.28.100.dylib` etc. in this build.
4. No source changes needed. Our `OpenALAudioManager` /
   `OpenALAudioFileCache` use only the `al*` / `alc*` API surface,
   which OpenAL Soft implements.
5. Drop `Wdeprecated-declarations` warnings — OpenAL Soft's headers
   don't carry the framework deprecation pragmas.

**Estimated effort:** 30–60 minutes including bundle relocation and
`otool -L` verification on both Generals and GeneralsMD targets.

**Risk:** very low. Worst case is `dlopen` fails at startup with a
clear error and we fall back to system OpenAL by un-pinning
`OPENAL_ROOT`.

### Option 2 — Port to SDL3 audio

SDL3's audio API
([SDL_OpenAudioStream](https://wiki.libsdl.org/SDL3/SDL_OpenAudioStream)
+ `SDL_PutAudioStreamData`) is already a transitive dependency
(`libSDL3.0.dylib`) and works cleanly on Apple Silicon. It's a
push-style stream API rather than OpenAL's source/buffer model, so
it's a real refactor — but it eliminates the OpenAL surface area
entirely and would fix any future OpenAL framework regressions for
free.

**Estimated effort:** 1–2 days. The `OpenALAudioManager` and
`OpenALAudioFileCache` would be replaced by `SDL3AudioManager` /
`SDL3AudioFileCache`. Buffer caching, source pool, music
voice, positional/2D distinction, fades, and pitch shift all need
re-implementation against SDL3 streams.

**Risk:** moderate. More code surface than option 1, but no platform
regressions to chase — SDL3 already handles macOS's HiDPI / mix-rate
quirks for us in the audio path the same way it does in the input
path (which is why the click-area fix landed in
`SDLGameEngine.cpp::refreshMouseScale` and not in OpenAL).

### Option 3 — miniaudio

[miniaudio](https://miniaud.io/) is a single-header audio library
with a simple device + ring-buffer API. Smaller than SDL3 audio but
also a real refactor. Worth considering only if we want to drop SDL
dependency entirely (we don't — input/window already need it).

**Not recommended** — adds a third library where SDL3 already covers
the same ground.

### Non-options

- **"Disable audio temporarily."** Tempting (one-line `playAudioEvent`
  early-return guard) but it ships a regressed user experience and
  the crash will resurface the moment the guard is removed. Only
  acceptable as a debug-build escape hatch, not the fix.
- **"Catch the SIGBUS."** We can't — it's a hardware fault, not a
  C++ exception. The faulting allocator state is corrupt on return,
  so even if we longjmp'd out of the signal handler the next audio
  call would re-fault.

## Out of scope (for the fix; document anyway)

- **AL_EXT_FLOAT32 / matching device rate exactly.** On paper, if the
  buffer format already matches the device's mix bus exactly (same
  rate, same channel layout, same sample format), Apple OpenAL would
  build a no-op converter and skip `caulk` allocation entirely. In
  practice this is fragile — the device's preferred format isn't
  user-queryable through OpenAL ALC, and Apple OpenAL still
  allocates spatialization / 3D-position state through `caulk` for
  every `AL_SOURCE_RELATIVE = AL_FALSE` source. So matching the
  format only narrows the crash window, doesn't close it.
- **Pre-warming the caulk allocator at startup.** Tempting (force
  the first `alSourcei` to happen against a known-good silent
  buffer in a controlled context) but the lazy-init failure isn't
  deterministic — it surfaces on different threads and against
  different allocator tiers. Pre-warming would improve reliability
  but not eliminate the crash.

## When to implement

Whenever the next focused audio session lands. Audio is currently
"works mostly, crashes occasionally" on Apple Silicon — playable
demo, not shippable. Option 1 (OpenAL Soft) is the smallest and most
contained fix and clears the path for the broader OpenAL pipeline
gaps (event/source/buffer port) tracked in
[`ZH-MainMenu-Bugs.md`](ZH-MainMenu-Bugs.md).

## Verification (after implementing option 1)

1. `brew install openal-soft && cd build_bgfx && cmake -DOPENAL_ROOT=/opt/homebrew/opt/openal-soft .. && make -j8 g_generals z_generals`
2. `otool -L build_bgfx/GeneralsMD/generalszh.app/Contents/MacOS/generalszh | grep -i openal` — must show `libopenal.1.dylib`, not the system framework.
3. Launch ZH interactively and let it sit on the main menu for ≥ 5
   minutes. Click and hover repeatedly across all six right-side
   buttons. The original crash reproduced within 15–30 s; 5 minutes
   without `EXC_BAD_ACCESS` is a strong fix signal.
4. Open Skirmish, place units, fire a few weapon barks — exercises
   positional 2D + pitch-shift + rapid source acquisition, the
   stress patterns most likely to expose any residual issues.
5. Both targets (`generalsv` and `generalszh`) — they share the
   `Core/GameEngineDevice/Source/OpenALAudioDevice/` tree, so the
   linker swap covers both.