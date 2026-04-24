# Porting 32-bit Win32 C++ to 64-bit macOS/Linux — Blueprint & Pitfalls

A distilled field guide from porting *Command & Conquer: Generals / Zero Hour*
(SAGE engine, 2003, MSVC 6-era Win32/DirectX 8, x86-32) to native macOS
(arm64/x86_64) and Linux. Written to be reusable for any similar old
Windows-only codebase. Cross-references the phase docs in this folder and the
memory notes in `~/.claude/projects/.../memory/`.

Scope: concrete bugs that only surfaced after building 64-bit, platform
boundaries that had to be re-drawn, and the order that minimized breakage.

---

## 1. Type-size pitfalls (the silent wire-format bugs)

The most dangerous class of bugs — code compiles, runs, and *looks* correct
but reads or writes the wrong number of bytes on the new target. Prefer
fixed-width types (`<cstdint>`) at every serialization, wire, and vertex-layout
boundary.

| Type | Win32 x86 | macOS/Linux x86_64/arm64 | Why it bites |
|---|---|---|---|
| `long` | 4 | 8 (LP64) / 4 (Windows LLP64) | Any `long` in save files or struct layout; `ftell` returns `long` → switch to `ftello`/`_ftelli64`. |
| `DWORD` (compat shim typed as `unsigned long`) | 4 | **8** on LP64 | `sizeof(DWORD)` silently doubles field sizes. See `memory/dword_is_8_bytes_on_macos.md`. |
| `wchar_t` / `WideChar` | 2 (UTF-16) | **4** (UCS-4) | EA binary formats (CSF, W3D strings) are UCS-2 on disk; `read(buf, len*sizeof(WideChar))` consumes 2× too many bytes. See `memory/widechar_is_4_bytes_on_macos.md`. |
| `size_t` / pointer | 4 | 8 | `int` ↔ `size_t` narrowing in containers (`DynamicVectorClass` etc.); pointer-to-int casts in hash tables. |
| `time_t` | 4 on VC6 | 8 almost everywhere | Save-file timestamps. |

**Real examples from this port:**

- `Generals/.../dx8fvf.cpp` used `sizeof(DWORD)` for FVF diffuse/specular fields. On macOS that put UVs at the wrong stride offset, so bgfx sampled texel (0,0) for every menu quad and the whole UI rendered as gradient triangles. Fix: `sizeof(uint32_t)`. (Phase 5 round 8 in `Vanilla-Generals-Status.md`.)
- `GameTextManager::parseCSF` read `len*sizeof(WideChar)` into a `wchar_t*` — worked on Windows, desynced on the 2nd label on macOS, every UI label came back empty. Fix: read into `uint16_t` staging buffer, then widen. (ZH-MainMenu §3.2.)
- `XferFilePos = long` in save files: on-wire format is safe (result is truncated to `int32_t`) but the arithmetic wraps differently on LLP64 vs LP64 for files >2 GB. Flagged, not yet fixed. (`Phase6e-SaveReplayAudit.md`.)

**Rule of thumb:** treat every `sizeof(DWORD)`, `sizeof(LONG)`, `sizeof(WORD)`,
`sizeof(wchar_t)` in target-duplicated Win32 code as a candidate bug. Replace
with `sizeof(uint32_t)` / `sizeof(uint16_t)` / explicit widths.

**CMake opt-in for the audit:** `-DRTS_ENABLE_64BIT_WARNINGS=ON` turns on
`-Wshorten-64-to-32 -Wint-to-pointer-cast -Wpointer-to-int-cast` (Clang/GCC)
or `/we4244 /we4267 /we4311 /we4312` (MSVC). Default OFF because legacy code
generates thousands of hits; meant for focused audit passes. See
`Phase6d-64BitAudit.md`.

---

## 2. Binary-file-format assumptions

Assume every file-format reader written in 2003 hardcoded a Windows ABI.

- **Endianness:** most x86-era Win32 code never checked. arm64 is
  little-endian so this didn't bite here, but any PowerPC/big-endian
  deployment requires an audit.
- **Struct packing:** Win32 code often relies on default 8-byte alignment
  matching the disk format. Greps for `#pragma pack(push, 1)` reveal the
  network/protocol structs; the absence of `#pragma pack` in save-format
  structs is usually a hint that the wire is serialized field-by-field
  (portable) rather than via `memcpy` (fragile).
- **String width:** see WideChar above. Always read into a staging buffer
  sized to the *disk* width, then widen.
- **XOR / CRC transforms on the raw bytes:** apply before widening, not
  after. The CSF format `*ptr = ~*ptr` — if done on the widened 32-bit
  `wchar_t`, the high 16 bits invert from 0 to `0xFFFF` and garble the
  character.

---

## 3. Filesystem differences

- **Case sensitivity.** APFS defaults to case-insensitive but can be
  case-sensitive; Linux almost always is. Win32 is case-insensitive.
  Audit:
  - Loose files referenced from data — `Art/Textures/foo.tga` vs
    `art/textures/foo.tga` — will fail on case-sensitive volumes.
  - Asset archives (BIG/PAK/ZIP) usually store exact names; lookups into
    them need a normalization layer.
  - Path separators: Win32 code littered with `\\` string literals. Keep
    them in archive-internal paths (the archive format uses backslash), but
    split on both `'/'` and `'\\'` in any OS-facing path code. See
    `Win32BIGFileSystem.cpp`.
- **Path case on disk:** add a case-normalization shim in the StdDevice
  file backend for loose files. Don't try to fix it at every call site.
- **Localized subpaths must be probed before bare paths** — the ZH build
  keeps UI textures under `Data\<Language>\Art\Textures\` inside a
  localized archive. Any new reader bypassing the existing probe order
  (e.g., `readViaFS` in the BGFX path) has to replicate the probe or the
  non-localized asset wins. See `memory/zh_localized_asset_paths.md`.
- **Recursion bugs hidden by case-sensitivity:**
  `StdLocalFileSystem::getFileListInDirectory` passed only the leaf name
  to its recursive call, not the full relative path. Worked on Win32 by
  accident; broke map enumeration on macOS. (ZH-MainMenu resolution 1.)
- **App directory:** Win32 reads install paths from the registry; on
  macOS use `SDL_GetBasePath()` + the `.app` bundle's
  `Contents/Resources/`; on Linux use XDG base-dir spec
  (`$XDG_DATA_HOME`, `$XDG_CONFIG_HOME`) or the equivalent via
  `SDL_GetPrefPath`.

---

## 4. Config storage: the Windows Registry

Rewrite `HKLM\SOFTWARE\Vendor\...` reads to a flat INI under a platform-aware
config dir:

- `SDL_GetPrefPath(org, app)` returns `%APPDATA%`, `~/Library/Application
  Support/...`, or `$XDG_CONFIG_HOME/...` as appropriate.
- Keep a one-time migration that reads the old registry keys on Windows
  first-run and writes them into the INI, so users don't lose their config.
- Watch for anything that used `SHGetKnownFolderPath(FOLDERID_Documents)` —
  save games, screenshots. Map to `SDL_GetPrefPath` or platform-native
  documents folder.

---

## 5. Threading and synchronization

Replace the Win32 primitives wholesale with C++20 equivalents:

| Win32 | Replacement |
|---|---|
| `CRITICAL_SECTION`, `InitializeCriticalSection` | `std::mutex` |
| `InterlockedIncrement` / `InterlockedCompareExchange` | `std::atomic<T>` |
| `CreateThread` / `_beginthreadex` | `std::thread` / `std::jthread` |
| `WaitForSingleObject` on events | `std::condition_variable` |
| `SRWLOCK` | `std::shared_mutex` |
| `TLS_OUT_OF_INDEXES` / `TlsAlloc` | `thread_local` |

SEH (`__try`/`__except`, `pseh_compat.h`) has no portable analogue —
replace with error codes or C++ exceptions; retire the compat shim from
non-Windows targets. MSVC 64-bit uses native SEH anyway.

---

## 6. Window / input / timing / audio / video

The rewrite pattern that held up well: keep the engine's abstract interfaces
(`Keyboard`, `Mouse`, `AudioManager`, `VideoPlayer`, `File`), subclass them
with cross-platform backends, gate selection in CMake.

| Subsystem | Win32-era lib | Replacement |
|---|---|---|
| Window/input/clipboard/timer/gamepad | `WinMain`, DirectInput 8, `GetTickCount`, `SetCursor`, `OpenClipboard` | **SDL3** (one library for all of it) |
| Audio | Miles Sound System, DirectSound | **OpenAL Soft** (3D positional, HRTF, EFX — closest semantic match to Miles) |
| Video | Bink | **FFmpeg** (often already in the tree via vcpkg) |
| HTTP/FTP | Hand-rolled Winsock | **libcurl** |
| Matchmaking | GameSpy SDK | **OpenSpy** (if retail wire-compat matters) or Steam GNS / ENet if not |
| Embedded browser | IE ActiveX / COM / ATL | **Delete.** Use `SDL_OpenURL` to open links externally. |
| Font rasterization | GDI (`CreateFont`, `CreateDIBSection`) | FreeType or SDL_ttf + a backend-agnostic glyph atlas |
| Registry | `RegOpenKeyEx` etc. | INI + `SDL_GetPrefPath` |
| COM/ATL/MFC | everywhere | Delete from the game. Tools (WorldBuilder etc.) stay Windows-only. |

**Do not skip:** font rasterization. GDI is invisible in the codebase — the
DX8 path in `render2dsentence.cpp` silently owned it — but every piece of UI
text depends on it. On the BGFX build it is still stubbed and all button
labels render blank. Budget a dedicated subphase. See
`memory/bgfx_build_subsystem_stubs.md`.

**Win32 message pump:** the new SDL main loop still needs to fan events into
the engine's internal message queue — keep the bridge layer, replace only the
producer. Same pattern worked for keyboard, mouse, and gamepad.

---

## 7. Graphics — DirectX 8 fixed-function → modern

By far the largest subsystem. Decisions that held up:

- **Pick a modern RHI (bgfx, sokol, Diligent, WebGPU).** bgfx matches DX8's
  state-based coarseness most closely and vendors `shaderc`. It supports
  Metal on macOS, D3D11/12 on Windows, Vulkan on Linux.
- **DX8 fixed-function → uber-shaders.** No shader assets existed; every
  lighting/texture-stage combination gets a shader permutation. The small
  core set (untextured, single-texture, two-stage modulate/add, alpha-blended
  2D quad) covers 90% of draws.
- **Preserve the public API.** `TextureClass`, `VertexBufferClass`,
  `IndexBufferClass`, `ShaderClass`, `VertexMaterialClass`, `Render2DClass`
  stay. Only their internals get rewritten against the new RHI.
- **Math:** homegrown (`WWMath`) or project-local math beats
  `D3DXVECTOR`/`D3DXMATRIX`. If the codebase is on `d3dx8math.h` callers,
  port those first — they're usually a mechanical refactor.
- **DDS:** `bimg`/`bx` decode for DXT1/3/5. stb_image for loose TGA/PNG.
- **Backend activation timing.** If the new RHI brings up the device lazily
  (e.g., on first `Set_Render_Device`), runtime `if (backend == bgfx)`
  checks can fire *before* the backend is active — during `WW3D::Init`'s
  post-init configuration block, for example. Use **compile-time** gates
  (`#ifdef RTS_RENDERER_DX8`) for anything in that window. See
  `memory/bgfx_backend_activation_timing.md`.
- **Target-duplicated files are a landmine.** If the repo has
  `Generals/.../WW3D2/*.cpp` AND `GeneralsMD/.../WW3D2/*.cpp` as parallel
  trees, fixes made in one almost never get backported to the other. The
  ZH tree had already fixed `sizeof(DWORD)→sizeof(uint32_t)` *years*
  before vanilla did. Always `diff -rq` the trees early.
- **DX8 compat header.** A shim like `compat/d3d8.h` that redefines
  `DWORD = unsigned long` is the root of half the type-size bugs in §1.
  Retire it per-subsystem as the RHI wrapper gets fully ported.

---

## 8. CPU / memory / platform introspection

The fingerprinting code built into every 2003-era game breaks on ARM and
POSIX:

- **`Init_Memory` / `GlobalMemoryStatus`:** stub on non-Win32 → code thinks
  the machine has 0 bytes of RAM → LOD degrades to "LOW" → shell map gets
  disabled → main menu renders a fallback and the user thinks the whole
  graphics backend is broken. Implement via `sysctlbyname("hw.memsize", …)`
  on macOS/BSD and `sysconf(_SC_PHYS_PAGES)*sysconf(_SC_PAGE_SIZE)` on Linux.
  (ZH-MainMenu resolution 1.)
- **`Init_Processor_Speed` / RDTSC:** `Has_RDTSC_Instruction()` returns
  FALSE on ARM → `m_cpuFreq=0` → `isReallyLowMHz()` returns TRUE → shell
  map disabled again. Fix: treat 0 as "unknown, do not gate" at the LOD
  check. Proper fix: `sysctl hw.perflevel0.freq_hz` on macOS, `/proc/cpuinfo`
  on Linux.
- **`GetSystemInfo` / `GetLogicalProcessorInformation`:** replace with
  `std::thread::hardware_concurrency()` or platform-specific sysctls.

Pattern: any Win32 hardware-detection code is almost certainly a
stubbed-to-zero pit on non-Windows builds. Audit *all* of them early — they
produce silent functional degradation, not crashes.

---

## 9. Networking

- `Winsock2` → `<sys/socket.h>`, `<netdb.h>`, `<arpa/inet.h>`. Most APIs
  are 1:1; `WSAGetLastError` → `errno`, `closesocket` → `close`,
  `WSAStartup`/`WSACleanup` → no-op.
- `IPEnumeration` via `GetAdaptersInfo` → `getifaddrs`.
- `getaddrinfo` is cross-platform; `gethostbyname` is not — use the former.
- Byte-order macros differ (`ntohl` exists everywhere; `_byteswap_ulong` is
  MSVC-only). Use `<endian.h>` / `<libkern/OSByteOrder.h>` or your own
  portable wrapper.
- Any `#pragma pack(push, 1)` network structs must compile identically on
  both toolchains — spot-check with `static_assert(sizeof(Foo)==N)`.

---

## 10. Build / CMake / dependencies

- **Default-picker pattern.** At the top of `cmake/config-build.cmake`, one
  `if(WIN32) … else() …` block picks sensible defaults for every backend
  selector (`RTS_RENDERER`, `RTS_AUDIO`, `RTS_VIDEO`, `RTS_PLATFORM`). Fall
  through is **fatal error**, not silent skip — so a non-Windows user who
  asks for the retail middleware fails at configure, not link. See
  `Phase6a-NonWindowsDefaults.md`.
- **macOS universal build.** `CMAKE_OSX_ARCHITECTURES=arm64;x86_64` plus
  `MACOSX_DEPLOYMENT_TARGET=11.0`. The project's own static libs go
  universal cleanly; Homebrew-sourced dylibs do not. Either FetchContent
  every dep from source or accept single-arch for now (current state).
  See `Phase6b-MacOSUniversal.md`.
- **macOS `.app` bundle.** Minimal `Info.plist.in` + `MACOSX_BUNDLE ON`
  per executable target. Required keys: `CFBundleExecutable`,
  `CFBundleIdentifier`, `CFBundlePackageType=APPL`,
  `CFBundleShortVersionString`, `CFBundleVersion`,
  `NSHighResolutionCapable=true`, `NSPrincipalClass=NSApplication`,
  `LSMinimumSystemVersion=11.0`, `NSRequiresAquaSystemAppearance=false`.
  Code signing + notarization require external credentials — add in CI,
  not local. See `Phase6f-MacOSAppBundle.md`.
- **Windows 64-bit.** Dropping the 32-bit gate for retail middleware
  (Miles/Bink/DX8 SDK) is safe once you've got the modern backend working.
  `/MACHINE:X64` or `-A x64`; no source-level changes beyond the type-size
  audit. See `Phase6c-Windows64.md`.
- **Resource files.** Keep `.rc`/`.manifest` for Windows; on macOS embed the
  icon via `.icns` in the bundle and `SDL_SetWindowIcon` from a PNG.

---

## 11. Recommended phasing

Order matters — doing this bottom-up prevents 3-way merge hell.

0. **RHI seam.** Make the DX wrapper a true abstract interface. Retain
   DX as the only backend. No behavior change. Port the `d3dx*math.h` leak
   sites to your engine's own math. *Single-platform commit.*
1. **Audio + video.** Clean abstractions usually already exist; subclass
   them with OpenAL + FFmpeg. Keep the Windows backends selectable.
2. **Window / input / timing.** SDL backends; new cross-platform main(). Keep
   `WinMain` selectable for Windows.
3. **Config / threading / browser.** Registry → INI, `CriticalSection` →
   `std::mutex`, delete the embedded browser.
4. **Networking.** POSIX sockets, libcurl, `getaddrinfo`.
5. **New RHI backend.** The big one. Build parity incrementally: clear →
   quad → textured quad → mesh → full scene. Gate behind a CMake flag; keep
   DX shipping until parity.
6. **64-bit.** Flip the `CMAKE_SIZEOF_VOID_P` gate. Run the 64-bit
   warnings audit. Fix save/replay format types. Universal builds on macOS.
   App bundle scaffolding.
7. **Retire the legacy stack.** Once parity and replay bit-identity
   are green, delete DX + Miles + Bink + DirectInput + WinMain.

Per-phase smoke test: main menu renders, 30-second skirmish runs, exit clean.
Keep a replay-bit-identity regression test from the start — every subsystem
edit must preserve it.

---

## 12. Quick checklist for the next port

Before starting, run these greps and turn each hit into a ticket:

```
git grep -n 'sizeof([A-Z]*)'       # catches sizeof(DWORD), sizeof(LONG)
git grep -n 'sizeof(wchar_t)'      # UCS-2 format hazard
git grep -n 'sizeof(long)\|sizeof(size_t)'
git grep -n '#pragma pack'         # struct-layout wire formats
git grep -n 'HKEY_\|RegOpenKey'    # registry
git grep -n 'CRITICAL_SECTION\|Interlocked\|CreateThread\|TlsAlloc'
git grep -n 'WSAStartup\|closesocket\|gethostbyname'
git grep -n 'CreateFont\|CreateDIBSection'  # GDI rasterizer
git grep -n 'd3dx\?8\|AIL_\|BinkOpen\|IDirectInput'
git grep -n '__try\|__except\|_SEH'
git grep -n '_wcsicmp\|_stricmp'    # case-insensitive comparators
git grep -n 'SHGetKnownFolderPath\|SHGetFolderPath'
git grep -n 'GetTickCount\|QueryPerformance'
git grep -n 'Get.*MemoryStatus\|cpuid\|RDTSC'
git grep -rn 'Art\\Textures\|[Dd]ata\\'  # hardcoded Win32-style paths
```

Also:

- Diff every target-duplicated tree pair (`diff -rq`) — your "two executables
  share the same engine" assumption is probably wrong.
- Turn on `-Wshorten-64-to-32` and `-Werror=implicit-function-declaration`
  early, even if you don't fix every hit; the list IS the backlog.
- Build a headless-audio + headless-video path before you start cutting the
  real ones; saves hours of "is this silent because the backend is broken
  or because the audio pipeline is stubbed?".
- Write a replay bit-identity test harness *before* touching save/replay
  code. Without it you will ship a desync and not notice.

---

## Cross-references

- `CrossPlatformPort-Plan.md` — the phased plan this blueprint summarizes.
- `Phase6a-NonWindowsDefaults.md` through `Phase6f-MacOSAppBundle.md` — each
  64-bit sub-phase with decisions, edits, verification.
- `Phase6d-64BitAudit.md` — the warnings-audit opt-in, known-clean /
  known-dirty subsystem list.
- `Phase6e-SaveReplayAudit.md` — save/replay format portability audit.
- `ZH-MainMenu-Bugs.md` — resolutions 1–4 are a case study in cpudetect /
  CSF / localized-asset-path / UV-stride bugs chained together.
- `Vanilla-Generals-Status.md` — round 8 is the `sizeof(DWORD)` smoking gun.
- Memory notes: `dword_is_8_bytes_on_macos.md`,
  `widechar_is_4_bytes_on_macos.md`, `bgfx_backend_activation_timing.md`,
  `bgfx_build_subsystem_stubs.md`, `zh_localized_asset_paths.md`.