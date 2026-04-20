# Phase 6b — macOS universal (arm64 + x86_64) build opt-in

Companion doc to `docs/CrossPlatformPort-Plan.md`. Second sub-phase of Phase 6.
Follows `Phase6a-NonWindowsDefaults.md`.

## Scope gate

Phase 6a made the non-Windows build pick the cross-platform defaults. Phase 6b
adds the flag that turns a host-native macOS build into a universal
`arm64;x86_64` binary — **to the extent the leaf executables can link at all**.
The phase ships the opt-in and verifies that the project's own code compiles
clean for both slices; distribution-level universal (a universal `.app` that
contains fat binaries for every dependency) stays deferred because the system
dependencies shipped by Homebrew are single-arch.

## Locked decisions

| Question | Decision |
|---|---|
| Default arch | Native (Apple Silicon → arm64, Intel Mac → x86_64). The flagless `cmake -S . -B build` on a macOS host must still produce a working binary; a universal default that fails at link time because `/opt/homebrew/lib/libSDL3.dylib` is arm64-only would be worse than the status quo. |
| Opt-in mechanism | `-DRTS_MACOS_UNIVERSAL=ON`. Sets `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when the user hasn't already pinned architectures. |
| Do we override user-supplied `CMAKE_OSX_ARCHITECTURES`? | No — `if(... AND NOT CMAKE_OSX_ARCHITECTURES)`. Explicit `-DCMAKE_OSX_ARCHITECTURES=arm64` wins. |
| Where does the option live? | `cmake/config-build.cmake`, next to the other Phase 6 options. |
| Why not just set `CMAKE_OSX_ARCHITECTURES` unconditionally on Apple? | The FetchContent'd bgfx + stock Homebrew SDL3/OpenAL/FFmpeg are all single-arch. The static libs this project builds (corei_bgfx, z_ww3d2, etc.) compile universal cleanly — verified empirically — but leaf executables that link against `/opt/homebrew/lib/libSDL3.dylib` fail with `ld: warning: ignoring file '/opt/homebrew/lib/libSDL3.0.dylib': found architecture 'arm64', required architecture 'x86_64'`. The opt-in is honest about that. |
| How to actually produce a universal binary? | Outside the scope of this phase. A follow-up (6b.1 or 6f sibling) needs to either FetchContent SDL3/OpenAL/ffmpeg and build them universal, or install universal builds via a Homebrew bottle-free path. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `cmake/config-build.cmake` | Added `option(RTS_MACOS_UNIVERSAL ...)` plus the guarded `set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING ... FORCE)` block. |

### The opt-in

```cmake
option(RTS_MACOS_UNIVERSAL "macOS: build universal arm64+x86_64 (requires universal deps)." OFF)
if(APPLE AND RTS_MACOS_UNIVERSAL AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS target architectures" FORCE)
endif()
```

`CACHE … FORCE` is deliberate: we want the opt-in to win over an older cache
entry from a previous native-only configure. The `NOT CMAKE_OSX_ARCHITECTURES`
guard still preserves the escape hatch for developers who pass the arch list
explicitly.

## Verification

Ran on macOS 15 (Apple Silicon).

| Command | Outcome |
|---|---|
| `cmake -S . -B /tmp/univ "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DRTS_BUILD_CORE_EXTRAS=ON` | Configure OK. |
| `cmake --build /tmp/univ --target corei_bgfx` | **OK.** |
| `lipo -info /tmp/univ/Core/GameEngineDevice/Source/BGFXDevice/libcorei_bgfx.a` | `Architectures in the fat file: … are: x86_64 arm64`. The project's own code is arch-clean. |
| `cmake --build /tmp/univ --target tst_bgfx_clear` | **Fails at link time** with `ld: warning: ignoring file '/opt/homebrew/lib/libSDL3.0.dylib': found architecture 'arm64', required architecture 'x86_64'`. Exactly the dependency-sourcing problem this phase flags. |
| Native flagless configure still works | Verified — `cmake -S . -B build_bgfx` picks `arm64`-only, builds corei_bgfx, 23/23 bgfx tests PASSED. |

## What this phase buys

- A single line (`-DRTS_MACOS_UNIVERSAL=ON`) to request universal.
- Empirical proof that the project's own static libs (corei_bgfx) go
  universal cleanly — so when universal deps become available, the leaf
  executables will link without additional source fixes.
- A clear error boundary for contributors: if you opt in and link fails,
  you know the fix is in the dependency pipeline, not in game code.

## Deferred

| Item | Why | Stage |
|---|---|---|
| Universal SDL3 / OpenAL / FFmpeg sourcing | Requires either building from source via FetchContent (big diff) or a universal Homebrew tap. Distribution-level concern; not a build option. | 6b.1 / 6f |
| Universal bgfx (FetchContent already exists — just needs to honor `CMAKE_OSX_ARCHITECTURES`) | Verified works — universal static lib contains both slices. No action. | — (done) |
| Verifying a universal test executable end-to-end | Blocked on the dep sourcing above. | 6b.1 |
| Testing a universal `generalszh` binary | Blocked on 6f (.app bundle) + real game assets. | 6f+ |

## Meta-status

- Phase 5: complete.
- Phase 6: 6a + 6b land; 6c–6g still ahead in this session.
- Flagless build unchanged: native arch.
- bgfx tests: **23/23 green** after the opt-in addition.
