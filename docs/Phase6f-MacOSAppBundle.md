# Phase 6f — macOS `.app` bundle scaffolding

Companion doc to `docs/CrossPlatformPort-Plan.md`. Sixth sub-phase of Phase 6.
Follows `Phase6e-SaveReplayAudit.md`.

## Scope gate

macOS ships GUI apps as `Foo.app/Contents/MacOS/Foo` bundles. CMake can produce
this with `add_executable(<target> MACOSX_BUNDLE)` given a minimally populated
`Info.plist`. This phase wires the two main executables (`g_generals`,
`z_generals`) through a shared helper that sets the required bundle
properties. **Icons, code signing, and notarization are deferred** — all
three need artifacts this repo doesn't have (an `.icns`, an Apple Developer
ID certificate, and `notarytool` credentials).

## Locked decisions

| Question | Decision |
|---|---|
| Minimum `Info.plist` contents | `CFBundleExecutable`, `CFBundleIdentifier`, `CFBundleName`, `CFBundlePackageType=APPL`, `CFBundleShortVersionString`, `CFBundleVersion`, `NSHighResolutionCapable=true`, `NSPrincipalClass=NSApplication`, `LSMinimumSystemVersion=11.0`, `NSRequiresAquaSystemAppearance=false`. That's the bare minimum SDL3 + Metal need. |
| Template file location | `cmake/macos/Info.plist.in`. One template used by both `g_generals` and `z_generals` via `MACOSX_BUNDLE_*` CMake variables for per-target substitution. |
| Version numbers | `CFBundleVersion` / `CFBundleShortVersionString` = `"1.04"` (matches the retail SAGE version string). Not wired to `GeneratedVersion.h` yet — that would couple doc scope to a revision-history refactor. |
| Bundle identifier | `com.community.generals` / `com.community.generalszh`. Reverse-DNS, rooted under `com.community.*` to make it clear these are not EA-distributed artifacts. |
| Minimum OS version | `11.0` (Big Sur) — matches the arm64 cutover. Could be bumped later. |
| Wired at which target? | The two game executables only (`g_generals`, `z_generals`). Build-test binaries stay plain Mach-O (they are headless SDL hidden-window tests; no bundle needed). |
| Helper location | `cmake/macos/bundle.cmake`, exposes `rts_configure_macos_bundle(target display_name bundle_id)`. One-line callsite per target. |
| Do we build the bundle by default on macOS? | **Yes** — `add_executable(... MACOSX_BUNDLE)` is only invoked when `APPLE`. Keeping it the default means `cmake --build . --target g_generals` produces `Generals.app` automatically. |

## Source-level changes

### New files

| File | Purpose |
|---|---|
| `cmake/macos/Info.plist.in` | CFBundle template. Uses `${MACOSX_BUNDLE_*}` substitutions filled in by CMake per-target properties. |
| `cmake/macos/bundle.cmake` | One-function helper: `rts_configure_macos_bundle(target, display_name, bundle_id)`. Guards with `if(NOT APPLE) return()` so it's a no-op elsewhere. |

### Edited files

| File | Change |
|---|---|
| `Generals/Code/Main/CMakeLists.txt` | Added an `elseif(APPLE)` branch: `add_executable(g_generals MACOSX_BUNDLE)` + `rts_configure_macos_bundle(g_generals "Generals" "com.community.generals")`. Windows and other-Unix paths unchanged. |
| `GeneralsMD/Code/Main/CMakeLists.txt` | Same pattern for `z_generals` with bundle name "Generals Zero Hour" and id `com.community.generalszh`. |

### Helper body

```cmake
function(rts_configure_macos_bundle target display_name bundle_id)
    if(NOT APPLE)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "${display_name}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.04"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.04"
        MACOSX_BUNDLE_COPYRIGHT "EA Pacific / community port"
    )
endfunction()
```

## Verification

Verified on macOS 15 (Apple Silicon), CMake 4.3.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | Configure OK (no `MACOSX_BUNDLE_INFO_PLIST` errors; template path resolves). |
| `cmake --build build_bgfx --target corei_bgfx z_ww3d2` | OK. |
| bgfx test suite (23 tests) | All PASSED — bundle scaffolding is inert for test targets. |

Build-of-`g_generals` itself is not part of this phase's verification — the
game target currently fails to link for reasons unrelated to this phase
(unported game-engine code paths still reference Win32 symbols). Once those
are addressed (Phase 7-adjacent work), the bundle metadata will be picked up
automatically. The CMake scaffolding is in place; when the link succeeds, the
output will be a properly formed `Generals.app`.

## Deferred

| Item | Why | Stage |
|---|---|---|
| `.icns` icon | No icon asset in the repo yet; needs design work. | 6f.1 |
| Code signing (`codesign`) | Needs Apple Developer ID certificate — external credential. | 6f.2 |
| Notarization (`notarytool`) | Needs Apple ID / team ID / app-specific password. | 6f.3 |
| Bundling dylibs into `Contents/Frameworks/` | Needs `install(CODE ...)` with `otool -L` + `install_name_tool`. | 6f.4 |
| Auto-detect asset path via `SDL_GetBasePath` | Already works — SDL3 handles the `Contents/Resources/` directory lookup internally. Validated by reading the SDL3 docs. | — |
| Wiring `CFBundleVersion` to `GeneratedVersion.h` | Touches the version pipeline; out of scope for bundle scaffolding. | 6f.5 |
| Universal binary integration | Blocked on 6b.1 universal deps. | After 6b.1 |

## Meta-status

- Phase 6: 6a–6f land; 6g remains.
- Bundle scaffolding wired for `g_generals` + `z_generals`; inert until those
  targets link, then auto-produces `.app` wrappers.
- bgfx tests: **23/23 green** (unchanged — test executables are not bundles).
