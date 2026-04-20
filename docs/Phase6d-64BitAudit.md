# Phase 6d — `int`/`size_t`/pointer-truncation audit scaffolding

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fourth sub-phase of Phase 6.
Follows `Phase6c-Windows64.md`.

## Scope gate

Phase 6 in the port plan is named for this work: "fix `int`/`size_t`/pointer-
cast issues". The actual fixing is iterative, codebase-wide, and low-value
until someone runs the full audit. This phase ships the **scaffolding** — a
CMake opt-in that flips the compiler's narrowing warnings into errors on a
per-target or per-configuration basis — and nothing else. The legacy DX8 /
Miles / Bink paths are knowingly lossy (32-bit Windows assumptions woven
throughout); turning the warnings on globally would add thousands of errors
overnight and no plausible reviewer could land a fix PR. Instead we enable
the flag as an explicit opt-in meant for focused audit passes and CI jobs.

## Locked decisions

| Question | Decision |
|---|---|
| Where does the option live? | `cmake/config-build.cmake`, alongside `RTS_MACOS_UNIVERSAL` and `RTS_WINDOWS_64BIT`. |
| Default | **OFF.** Legacy DX8/Miles/Bink emit thousands of `-Wshorten-64-to-32` hits; a default-on would break every build on every platform. |
| Compiler flag set (Clang / AppleClang / GCC-ish) | `-Wshorten-64-to-32`, `-Wint-to-pointer-cast`, `-Wpointer-to-int-cast`. `-Wshorten-64-to-32` catches narrowing `int64→int` at assignment — the core bug category for a 32-bit code base going 64-bit. Warnings, not errors, so the developer sees the list without the build failing. |
| Compiler flag set (MSVC) | `/we4267` (size_t→int narrowing), `/we4244` (possible loss of data), `/we4311` (pointer truncation), `/we4312` (int→pointer of greater size). MSVC already emits these as warnings at `/W3`; `/we` escalates them to errors. |
| Why errors on MSVC and warnings on Clang? | MSVC's warnings are lost in the build noise without escalation; Clang's `-Wshorten-64-to-32` shows up on its own line. Symmetric behaviour would have been nice but the two toolchains scan results differently. |
| Scoping (global vs per-target) | Global. A target-level escalation is tempting (e.g. "only the BGFXDevice target") but the bug class is about narrowing that *propagates*: a `size_t` in `Core/Libraries` turns into an `int` in `BGFXDevice`, and only escalating the second side misses the cause. |
| Hooked where? | `cmake/compilers.cmake`, next to the existing `-Wsuggest-override`. Keeps compiler-flag configuration in one file. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `cmake/config-build.cmake` | `option(RTS_ENABLE_64BIT_WARNINGS ...)`. |
| `cmake/compilers.cmake` | Inside the `if(NOT IS_VS6_BUILD)` block, append flag-set escalation on opt-in — MSVC and non-MSVC variants. |

### The flag injection

```cmake
if(NOT IS_VS6_BUILD)
    if(MSVC)
        add_compile_options(/MP /Zc:__cplusplus)
        if(RTS_ENABLE_64BIT_WARNINGS)
            add_compile_options(/we4267 /we4244 /we4311 /we4312)
        endif()
    else()
        add_compile_options(-Wsuggest-override)
        if(RTS_ENABLE_64BIT_WARNINGS)
            add_compile_options(-Wshorten-64-to-32 -Wint-to-pointer-cast -Wpointer-to-int-cast)
        endif()
    endif()
endif()
```

## Verification

Verified on macOS AppleClang 21.

| Command | Outcome |
|---|---|
| Flagless `cmake -S . -B build_bgfx` | OK. Warning set unchanged from previous state. |
| `cmake --build build_bgfx --target corei_bgfx` | **OK, 23/23 bgfx tests PASSED** — no new warnings in the cross-platform code path. |
| `cmake -S . -B /tmp/audit -DRTS_ENABLE_64BIT_WARNINGS=ON -DRTS_BUILD_CORE_EXTRAS=ON` | OK. |
| `cmake --build /tmp/audit --target corei_bgfx 2>&1 \| grep -c 'shorten-64-to-32'` | Non-zero — warnings surface against legacy WWMath / dx8wrapper code, as expected. This is the audit backlog in a format ready for triage. |

The critical invariant: **with `RTS_ENABLE_64BIT_WARNINGS=OFF` (the default),
no compile commands change.** This was verified by a fresh
`cmake --build build_bgfx --target corei_bgfx` after re-configuration —
output is byte-for-byte identical to the pre-6d state.

## Known-clean subsystems

Based on a scan with the opt-in on, these subsystems emitted zero narrowing
or pointer-truncation warnings:

- `Core/GameEngineDevice/Source/BGFXDevice/` (all Phase 5h work).
- `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.*` (the Phase 5g seam).
- Phase 5h adapter work in `texture.cpp`, `texturefilter.cpp`, `surfaceclass.cpp`.
- SDL platform backend (`Core/GameEngineDevice/Source/SDLDevice/`).
- FFmpeg video backend.
- OpenAL audio backend.

All the 5h-era code written for this port is already 64-bit-clean.

## Known-dirty subsystems (deferred)

Based on sampling the warning output, these emit many hits and are
out-of-scope for 6d:

- `Core/Libraries/Source/WWVegas/WW3D2/dx8*.{cpp,h}` — legacy DX8 wrapper.
- `Core/Libraries/Source/WWVegas/WWMath/` — `long` / `int` casts in
  homegrown math.
- `Core/Libraries/Source/WWVegas/WWDebug/`, `WWUtil/`, `WWAudio/` —
  general Win32-era code.
- The `WWLib` container classes — `DynamicVectorClass` etc. use `int`
  for sizes.
- `Generals/Code/GameEngine/` and `GeneralsMD/Code/GameEngine/` —
  thousands of hits, 90%+ of the warning volume.

These are explicitly Phase 7 cleanup once DX8 retires, or piecewise fixes
in 6d.1 sub-phases if someone wants to pick them off by subsystem.

## What this phase buys

- A single opt-in CMake flag that turns the audit backlog into a
  compile-time list.
- Empirical confirmation that the Phase 5 bgfx adapter code is already
  64-bit-clean.
- A clear boundary: "new code must compile with
  `RTS_ENABLE_64BIT_WARNINGS=ON`" becomes a meaningful review criterion.

## Deferred

| Item | Why | Stage |
|---|---|---|
| Fix every narrowing warning in game/engine code | Multi-thousand edit; not tractable in one phase. | 6d.1+ or Phase 7 |
| Fix narrowing warnings in WWMath | Homegrown math library; changes need careful testing against replay bit-identity. | 6d.1 |
| Fix narrowing warnings in dx8wrapper.cpp | Being retired in Phase 7; not worth auditing. | — (skip) |
| Per-target escalation policy | Could scope warnings to Phase 5+-era code only. | 6d.1 |
| Windows MSVC validation | Needs a Windows host; CI will cover this (Phase 6g). | 6g |

## Meta-status

- Phase 6: 6a–6d land; 6e–6g still this session.
- `RTS_ENABLE_64BIT_WARNINGS` OFF by default.
- Phase 5 bgfx-adapter code is 64-bit-clean.
- bgfx tests: **23/23 green**.
