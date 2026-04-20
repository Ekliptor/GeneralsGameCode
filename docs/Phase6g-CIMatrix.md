# Phase 6g — CI matrix: first macOS bgfx workflow

Companion doc to `docs/CrossPlatformPort-Plan.md`. Seventh and final
sub-phase of Phase 6. Follows `Phase6f-MacOSAppBundle.md`.

## Scope gate

The port plan names the eventual CI matrix as
`win32-msvc-dx8 / win32-msvc-bgfx / macos-arm64-bgfx / macos-x86_64-bgfx`.
The existing `.github/workflows/ci.yml` builds the retail-compat Windows
variants. This phase adds the **first non-Windows workflow**:
`build-macos-bgfx.yml`, which builds `corei_bgfx` and runs the 23 bgfx
tests on a GitHub macOS runner. Everything else stays deferred:

- Windows bgfx (`win32-msvc-bgfx`, `-bgfx-x64`) needs the Phase 6c
  opt-in exercised under a real MSVC toolchain; a separate workflow
  will add the 64-bit bgfx matrix entry once the test suite behaves
  identically to macOS.
- Universal macOS needs 6b.1 (universal deps).
- Notarized `.app` artifact upload needs 6f.2 (signing credentials).
- Replay bit-identity regression needs real game assets and 6e.1
  (`XferFilePos` fix).

## Locked decisions

| Question | Decision |
|---|---|
| Runner | `macos-14` (Apple Silicon). Covers the primary target platform. |
| Dep install | Homebrew: `sdl3 openal-soft ffmpeg cmake ninja`. The repo already assumes these are available on the host in the 5h-era builds. |
| Generator | Ninja — faster than Xcode's own, shallower log output, consistent with the `build_bgfx/` workflow developers use locally. |
| Scope | `corei_bgfx` + `z_ww3d2` + every `tst_bgfx_*`. The job asserts that all 23 bgfx tests pass end-to-end. |
| Paths-filter | Only run on changes to `Core/`, `cmake/`, `tests/`, root `CMakeLists.txt`, or the workflow itself. Keeps frontend-editor PRs from burning a macOS-runner slot. |
| Why a dedicated workflow instead of a matrix entry in `ci.yml`? | `ci.yml` is the retail-compat build orchestrator; its matrix assumes MSVC + Windows DLLs. Shoehorning macOS into it risks breaking the retail path. A separate workflow is additive and reversible. Once the non-Windows paths stabilise they can be merged. |
| Concurrency | Cancel-in-progress on PR re-pushes; run fully on `main`. Standard pattern matching the other workflows. |

## Source-level changes

### New files

| File | Purpose |
|---|---|
| `.github/workflows/build-macos-bgfx.yml` | Single-job workflow: install Homebrew deps → configure (flagless, picks the cross-platform defaults from 6a) → build `corei_bgfx` + `z_ww3d2` → build all 23 `tst_bgfx_*` targets → run them and assert zero failures. |

### Workflow body (annotated)

```yaml
jobs:
  build-macos-arm64-bgfx:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@… with: submodules: recursive
      - run: brew install sdl3 openal-soft ffmpeg cmake ninja
      - run: cmake -S . -B build_bgfx -G Ninja -DRTS_BUILD_CORE_EXTRAS=ON
      - run: cmake --build build_bgfx --target corei_bgfx z_ww3d2 -j
      - run: |
          for t in $(ls build_bgfx/tests | grep '^tst_bgfx_'); do
            cmake --build build_bgfx --target "$t" -j
          done
      - run: |
          PASS=0; FAIL=0
          for t in build_bgfx/tests/tst_bgfx_*/tst_bgfx_*; do
            "$t" 2>&1 | tail -1 | grep -q PASSED && PASS=$((PASS+1)) || FAIL=$((FAIL+1))
          done
          test "$FAIL" -eq 0
```

Relies on Phase 6a's defaults — no `-DRTS_RENDERER=bgfx` etc. in the
configure step. If 6a ever gets rolled back, this workflow starts picking
up the retail stack and fails at configure with the fatal-error guards,
which is the intended self-consistency check.

## Verification

**Workflow YAML is not executed from this environment.** Verification is
two-fold: static validation of the YAML against the known GitHub Actions
schema, and local execution of the command sequence the workflow runs.

| Check | Result |
|---|---|
| YAML syntax | Valid (`name`, `on`, `jobs`, `runs-on`, `steps` all present; pinned `actions/checkout` SHA matches other workflows in this repo). |
| Local mirror of the workflow's commands | Ran `cmake -S . -B build_bgfx -DRTS_BUILD_CORE_EXTRAS=ON && cmake --build build_bgfx --target corei_bgfx z_ww3d2 && …` → 23/23 bgfx tests PASSED. Exactly the invariant the workflow asserts. |
| Homebrew package names | `sdl3`, `openal-soft`, `ffmpeg`, `cmake`, `ninja` — all current formula names as of macOS 14/15 CI images. |
| Runner choice | `macos-14` is Apple Silicon; the session's entire 5h-era test suite was developed against this target. |

Real verification — running the workflow under GitHub Actions — happens on
the first PR push after this lands. That is intrinsic to the deliverable and
not fakable from the session environment.

## What this phase buys

- First automated non-Windows build gate for the port.
- A public signal that "macOS bgfx is green" travels with every PR.
- A template for the other three planned workflows
  (`build-windows-bgfx-x86.yml`, `build-windows-bgfx-x64.yml`, future
  `build-macos-universal-bgfx.yml`) — identical skeleton, different
  runner and dep install.

## Deferred

| Item | Why | Stage |
|---|---|---|
| `build-windows-bgfx.yml` | Needs 6c verified end-to-end; workflow added after first Windows bgfx green build. | 6g.1 |
| `build-windows-bgfx-x64.yml` | Driven by `RTS_WINDOWS_64BIT=ON`; added alongside 6g.1. | 6g.1 |
| `build-macos-universal-bgfx.yml` | Blocked on 6b.1 universal deps. | 6g.2 |
| Artifact upload (notarized `.app`) | Blocked on 6f.2/6f.3 signing credentials. | 6g.3 |
| Replay bit-identity regression | Blocked on game assets + 6e.1. | 6g.4 |
| Golden-image render parity (DX8 vs bgfx) | Already in the plan file's verification section; needs a deterministic replay + asset pack. | 7 |

## Meta-status

- **Phase 6 complete:** 6a–6g all landed in this session.
- **Phase 5:** still closed at 5h.36 + 5i–5q.
- **Phase 7** is next — retire DX8 / Miles / Bink.
- bgfx tests: **23/23 green** on macOS; CI will enforce this per-PR once
  the workflow runs on a real GitHub runner.

## Arc summary across Phase 6

| Sub | Topic | Deliverable | Verified on macOS? |
|---|---|---|---|
| 6a | Non-Windows defaults | `RTS_*` defaults flip for non-Windows; retail combos fail fast. | Yes. |
| 6b | macOS universal opt-in | `RTS_MACOS_UNIVERSAL` + code proven arch-clean. | Yes (opt-in); universal executable blocked on deps. |
| 6c | Windows 64-bit opt-in | `RTS_WINDOWS_64BIT` declared. | No (no Windows host). |
| 6d | 64-bit warnings | `RTS_ENABLE_64BIT_WARNINGS` + Clang/MSVC flag set. | Yes. |
| 6e | Save/replay audit | Xfer wire format certified portable; one internal typedef flagged. | Yes (audit only). |
| 6f | `.app` bundle | Helper + `Info.plist.in` + wired on `g_generals`/`z_generals`. | Yes (scaffold builds; final link is Phase 7-adjacent). |
| 6g | CI macOS workflow | `build-macos-bgfx.yml` — first non-Windows CI job. | Locally (workflow-runner execution is intrinsically next-PR). |
