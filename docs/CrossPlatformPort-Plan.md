# Cross-Platform Port: Replace DirectX 8 & Windows-Only Middleware

## Context

This repository is a community source port of **Command & Conquer: Generals / Zero Hour** (SAGE engine, Westwood W3D renderer). It is currently **Windows-only, 32-bit (x86)**, built around DirectX 8 fixed-function, Miles Sound System, Bink Video, GameSpy SDK, DirectInput 8, Win32 window/registry/COM, and an embedded IE ActiveX control. Non-Windows builds exist only via Docker + Wine cross-compilation.

The goal is to **replace every Windows-only subsystem with a platform-independent alternative** so the game builds and runs natively on **Windows and macOS**. The exploration phase confirmed this is feasible but non-trivial: the engine has a decent device-abstraction pattern (`Win32Device` / `StdDevice`, abstract `AudioManager`, `VideoPlayer`, `Keyboard`, `Mouse`, `File`), but the renderer and many support layers leak Win32 types.

### Findings that shape the plan

- **Renderer is fixed-function DX8 only** (no HLSL/shader assets). ~8.4 kLOC of DX8 wrapper code in `Core/Libraries/Source/WWVegas/WW3D2/` plus ~4.7 kLOC per-game. DX8 is mostly isolated behind `DX8Wrapper` / `TextureClass` / `VertexBufferClass` / `IndexBufferClass` / `ShaderClass`; 37 files transitively include `<d3d8.h>`, but game logic uses abstract classes. Math is homegrown (`WWMath`), not `D3DXVECTOR` / `D3DXMATRIX`. Textures are DDS (DXT1/3/5).
- **FFmpeg video backend already exists** alongside Bink — `Core/GameEngineDevice/{Include,Source}/VideoDevice/FFmpeg/*` (in-progress replacement for `BinkVideoPlayer`). `ffmpeg` is already in `vcpkg.json`.
- **Audio** has a clean abstract `AudioManager` (`Core/GameEngine/Include/Common/GameAudio.h`) with `MilesAudioManager` + a dummy implementation for headless tests. A new backend is a straight subclass.
- **Win32Device vs StdDevice**: file I/O and BIG-archive access already has a `StdDevice` stdio implementation (`Core/GameEngineDevice/Source/StdDevice/`) alongside Win32. Input (`Win32DIKeyboard`, `Win32DIMouse`, `Win32Mouse`; ~1.4 kLOC) is Win32-only.
- **Registry** (`Generals/Code/GameEngine/Source/Common/System/registry.cpp`) is hardcoded to `HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\...`.
- **Threading** `CriticalSection` wraps `CRITICAL_SECTION` directly, no abstraction.
- **Networking**: `IPEnumeration.cpp` uses Winsock; GameSpy SDK is 13 files in `Core/GameEngine/Source/GameNetwork/GameSpy/`; `WWDownload` is a custom socket-based FTP/HTTP client.
- **Embedded IE browser** (`W3DWebBrowser`, `FEBDispatch`, `dx8webbrowser.cpp`) uses COM/ATL + IDispatch for in-game community news / EA login.
- **Compat shims** in `Dependencies/Utility/Utility/*_compat.h` already exist (thread, time, endian, string, stdio, stdint) — the groundwork for non-Windows is partially laid.
- **Tools** (WorldBuilder, ParticleEditor, W3DView, ImagePacker, DebugWindow) are MFC dialogs. Out of scope for this port.

---

## Locked Design Decisions

| Question | Decision |
|---|---|
| Target platforms | **Windows x86** (retail-compat reference) + **macOS Intel + Apple Silicon** (universal). No Linux build. |
| Renderer | **bgfx** (Metal on macOS, D3D11 on Windows; OpenGL/Vulkan as fallbacks) |
| Retail 1.04 compat | Preserve only **until bgfx hits parity**, then drop — single modern stack is the long-term target |
| GameSpy | Keep wire-compat via **OpenSpy** pointed at `server.cnc-online.net` |
| Embedded browser | **Remove**, redirect external links via `SDL_OpenURL` |
| Tools (WorldBuilder/W3DView/etc.) | **Stay Windows-only** (MFC). Re-evaluate later. |
| Audio | **OpenAL Soft** |
| Window / input / timer / clipboard | **SDL3** |
| Video | **FFmpeg** (already in progress) |
| Build matrix | `win32-msvc-dx8` (legacy reference) → `win32-msvc-bgfx` → `macos-universal-bgfx`. After parity: drop the dx8 column. |

### Implications of the macOS-only non-Windows target

- **Metal is the only new graphics path required** (bgfx handles it). No separate Vulkan QA pass needed.
- **macOS is 64-bit only.** The 32-bit Windows retail-compat build keeps `CMAKE_SIZEOF_VOID_P EQUAL 4`; the macOS build is `arm64;x86_64` universal. A save/replay-format size audit (Phase 6) is still required since the eventual goal is to drop 32-bit entirely.
- **Code signing & notarization**: macOS distribution requires Apple Developer ID + `notarytool`. Add to CI in Phase 5 / 7; not blocking for local builds.
- **App bundle layout**: `Generals.app/Contents/{MacOS,Resources,Frameworks}` — needed for icon, `Info.plist`, bundling `SDL3`/`OpenAL`/`FFmpeg` dylibs. Use CMake `BUNDLE` target type; place game assets under `Resources/Data` mirroring the Windows install layout. Detect via `SDL_GetBasePath`.
- **Case-sensitive filesystem**: APFS volumes can be case-sensitive. Audit asset references that rely on Windows case-insensitivity (likely many — `.BIG` archives use exact names but loose files do not). Add a case-normalization layer to the file system backend.

---

## Subsystem Alternatives Considered

### 1. Graphics / Renderer (DirectX 8 fixed-function)

| Option | Pros | Cons |
|---|---|---|
| **bgfx** *(chosen)* | Cross-platform RHI (GL / Vulkan / Metal / D3D11 / D3D12), active, vendors `shaderc` | Larger dep, no fixed-function — must synthesize shaders |
| sokol_gfx | Tiny single-header (GL3 / Metal / D3D11 / WebGPU) | Smaller community, fewer features |
| Diligent Engine | Modern AAA RHI (Vulkan / Metal / D3D12 / WebGPU) | Heavyweight, steeper learning curve |
| The-Forge | AAA-grade, multi-platform | Heavier than needed |
| Raw GL 3.3 + MoltenVK | Minimal dep | macOS deprecated GL; eventually have to write Metal/Vulkan paths |
| Raw Vulkan + MoltenVK + D3D11 fallback | Future-proof | Excessive complexity for a 2003 fixed-function game |
| WebGPU (Dawn / wgpu-native) | Single API everywhere incl. browser | Young; C API still settling |

**Why bgfx**: matches DX8's fixed-function coarseness closely (state-based), vendors its own shader toolchain, and `DX8Wrapper` is already a thin RHI-shaped facade. The small set of "uber-shaders" needed to emulate DX8 fixed-function + texture stages is well-trodden territory (bgfx examples cover it).

**Work**: replace `DX8Wrapper` internals and the 12 files that include `<d3d8.h>` with a bgfx backend. Translate DX8 render / texture-stage state to precompiled shader permutations. Keep `TextureClass`, `VertexBufferClass`, `IndexBufferClass`, `ShaderClass`, `VertexMaterialClass` as the public API. Re-implement DDS load via `bimg` / `bx` (bgfx's texture lib) or a standalone DDS loader.

### 2. Window / Input / Timing / Clipboard

| Option | Pros | Cons |
|---|---|---|
| **SDL3** *(chosen)* | Modern; window + input + clipboard + gamepad + haptics in one; great macOS support | Newer, API churn possible |
| SDL2 | Battle-tested | In maintenance mode |
| GLFW + custom audio glue | Minimal | Need separate solutions for audio/gamepad |

Replaces the WinMain loop, DirectInput keyboard/mouse, DPI handling, fullscreen toggle, gamepad support, clipboard, and timers. Subclass `Keyboard` and `Mouse` with `SDLKeyboard` / `SDLMouse`; rewrite `WinMain.cpp` as a `main()` that creates an SDL window and pumps events into the existing message bridge.

### 3. Audio (Miles Sound System)

| Option | Pros | Cons |
|---|---|---|
| **OpenAL Soft** *(chosen)* | 3D positional, HRTF, EFX effects, LGPL, cross-platform | Separate shared lib |
| miniaudio | Single-header, MIT, has spatializer | Fewer advanced effects |
| SDL3 audio + dr_wav/stb_vorbis | Zero extra dep | Manual 3D mixing |
| FMOD Core | Pro-grade, free for indie | Closed-source, license friction |

OpenAL Soft is the closest semantic match to Miles (3D sources, listener, streaming, speaker configs). Replace `MilesAudioManager` with `OpenALAudioManager`. The `WWAudio` layer is engine-agnostic and stays intact.

### 4. Video

**FFmpeg** — already in progress in `Core/GameEngineDevice/{Include,Source}/VideoDevice/FFmpeg/`. Finish it, make it the default, delete the Bink code path.

### 5. Multiplayer / Matchmaking (GameSpy)

| Option | Pros | Cons |
|---|---|---|
| **OpenSpy / GameSpy re-implementation** *(chosen)* | Drop-in wire compat; keeps retail 1.04 clients working | Requires self-hosting master server |
| Replace GameSpy SDK with a new lobby (custom WS/TCP) | Modern, clean | Breaks retail-client compatibility |
| Steam GameNetworkingSockets (GNS) | Open Valve stack, NAT traversal | Steamworks affinity |
| ENet | Simple reliable UDP | Lobby/matchmaking still to build |

OpenSpy is pointed at `server.cnc-online.net` (already configured in `cmake/gamespy.cmake`). Long-term, evaluate GNS once retail compat is dropped.

### 6. HTTP / FTP download (`WWDownload`)

**libcurl.** Replaces the hand-rolled Winsock FTP client. Cross-platform, battle-tested, handles HTTPS properly.

### 7. Embedded Browser (`W3DWebBrowser` / EABrowser / IE ActiveX)

| Option | Pros | Cons |
|---|---|---|
| **Remove it** *(chosen)* | Massive simplification; UI was for legacy EA login/news that no longer exists | Loses in-game browser feature |
| CEF (Chromium Embedded) | Full modern web | >100 MB binary |
| Ultralight / webview.h | Smaller | Feature-limited |
| Native OS webview (WKWebView / WebView2) | OS-native, small | Different impl per platform |

Stub the `WebBrowser` interface; redirect any remaining clicks to `SDL_OpenURL` in the system browser.

### 8. Config storage (Windows Registry)

INI/JSON file under an XDG-style config dir via `SDL_GetPrefPath` (cross-platform: `%APPDATA%`, `~/Library/Application Support`). Rewrite `registry.cpp` to read/write a flat config file; keep an optional Windows-registry-read fallback for first-run migration from legacy installs.

### 9. Threading / Sync

C++20 `std::thread`, `std::mutex`, `std::atomic`. Project is already on C++20. Rewrite `CriticalSection.h` as a thin wrapper around `std::mutex`. Drop `InterlockedIncrement` for `std::atomic`. Remove `pseh_compat.h` (SEH) — port the affected call sites to standard C++ exceptions or error codes.

### 10. COM / ATL / MFC

Delete from the main game. The main game uses COM only for the IE browser (removed in §7) and `_bstr_t` conversions in its vicinity. MFC is tools-only. The `atl_compat.h` / `comsupp_compat.h` shims can be retired from game targets once the browser dies. Tools stay Windows-only.

### 11. Resource compiler / `.rc` / `app.manifest`

Keep `.rc` for Windows builds (with `windres` on MinGW, `rc.exe` on MSVC). On macOS, embed the icon via `SDL_SetWindowIcon` from a PNG bundled in the app bundle and provide a proper `.icns` for the bundle itself. The Windows manifest stays Windows-only.

### 12. Architecture: 32-bit vs 64-bit

The project currently pins 32-bit (`CMAKE_SIZEOF_VOID_P EQUAL 4`) for retail save/replay/multiplayer compat. macOS requires 64-bit. The 32-bit Windows target remains the retail-compat reference; the macOS build is universal `arm64;x86_64`. Audit `int` / `long` / `size_t` / pointer truncation and any save/replay serialization for size assumptions before flipping Windows to 64-bit (Phase 7).

---

## Architecture & Phasing

### Device-abstraction directories to add

- `Core/GameEngineDevice/{Include,Source}/SDLDevice/` — window, input, clipboard, timer (mirrors `Win32Device`)
- `Core/GameEngineDevice/{Include,Source}/OpenALAudioDevice/` — alongside `MilesAudioDevice`
- `Core/Libraries/Source/WWVegas/WW3D2_bgfx/` — sibling to `WW3D2`, selected via CMake (`RTS_RENDERER=bgfx|dx8`)
- Finish `VideoDevice/FFmpeg/`; delete `VideoDevice/Bink/` once parity is reached
- For file system on macOS, use the existing `Core/GameEngineDevice/Source/StdDevice/` stdio implementation; add case-normalization there

### CMake changes

- New top-level options: `RTS_RENDERER` (`bgfx`|`dx8`), `RTS_AUDIO` (`openal`|`miles`|`null`), `RTS_PLATFORM` (`sdl`|`win32`), `RTS_NETWORK` (`openspy`)
- Drop the `CMAKE_SIZEOF_VOID_P EQUAL 4` gate around `miles.cmake` / `dx8.cmake` / `bink.cmake` — only include them when the corresponding `RTS_*` option picks the legacy backend
- Add presets: `macos-universal`, alongside `win32`
- Extend `vcpkg.json`: add `bgfx`, `sdl3`, `openal-soft`, `libcurl`. Keep `ffmpeg` and `zlib`. Pin via `vcpkg-lock.json`.

### Critical files to modify

| Area | Path |
|---|---|
| Renderer wrappers | `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.{h,cpp}`, `dx8caps.*`, `dx8texman.*`, `dx8polygonrenderer.*`, `formconv.h`, `rddesc.h`, `dx8list.h`, `dx8webbrowser.*` (delete) |
| Per-game renderer | `Generals/Code/Libraries/Source/WWVegas/WW3D2/dx8{renderer,fvf,vertexbuffer,indexbuffer}.*` + `GeneralsMD/...` equivalents |
| DX-leaking clients | `Core/GameEngineDevice/Source/W3DDevice/GameClient/{Water,TreeBuffer,TerrainTex,ShaderManager,Snow}.cpp`, shadow code (17 files including `d3dx8math.h`) — migrate to `WWMath` |
| Entry point / window | `Generals/Code/Main/WinMain.cpp`, `GeneralsMD/Code/Main/WinMain.cpp` → new cross-platform `AppMain.cpp` |
| Input | `Core/GameEngineDevice/Source/Win32Device/GameClient/Win32DI{Keyboard,Mouse}.cpp`, `Win32Mouse.cpp` → new `SDLDevice/GameClient/SDL{Keyboard,Mouse}.cpp` |
| Audio | `Core/GameEngineDevice/{Include,Source}/MilesAudioDevice/MilesAudioManager.*` → new `OpenALAudioDevice/OpenALAudioManager.*` |
| Video | finish `Core/GameEngineDevice/{Include,Source}/VideoDevice/FFmpeg/*`; delete `VideoDevice/Bink/*` |
| Networking | `Core/GameEngine/Source/GameNetwork/GameSpy/*` (13 files) — point at OpenSpy; `IPEnumeration.cpp` → `getaddrinfo` |
| Download | `Core/Libraries/Source/WWVegas/WWDownload/*` (11 files) → libcurl |
| Registry | `Generals/Code/GameEngine/Source/Common/System/registry.cpp` + `cmake/registry.cmake` → INI via `SDL_GetPrefPath` |
| Threading | `Generals/Code/GameEngine/Include/Common/CriticalSection.h` → `std::mutex` wrapper |
| Browser | delete `W3DWebBrowser.*`, `FEBDispatch.*`, `dx8webbrowser.*`; stub `WebBrowser` interface |
| Compat shims | retire `Dependencies/Utility/Utility/{atl,comsupp,pseh}_compat.h` from game targets |

### Phasing (order minimizes breakage)

1. **Phase 0 — Renderer RHI seam.** Make `DX8Wrapper` a true abstract interface; keep DX8 impl as the only backend. No behavior change. Port the 17 `d3dx8math.h` leak sites to `WWMath`.
2. **Phase 1 — Audio & Video.** New `OpenALAudioManager` subclass. Finish FFmpeg video; delete Bink. Both have clean existing abstractions.
3. **Phase 2 — Window / Input / Timing.** SDL3 window + input backends; new cross-platform `AppMain`. Keep Win32 backend selectable.
4. **Phase 3 — Config / Threading / Browser cleanup.** Registry → INI, `CriticalSection` → `std::mutex`, delete `W3DWebBrowser`.
5. **Phase 4 — Networking.** `IPEnumeration` via POSIX; `WWDownload` via libcurl; GameSpy pointed at OpenSpy.
6. **Phase 5 — Renderer bgfx backend.** The big one. Build parity incrementally (clear → quad → textured quad → mesh → full scene). Ship behind `RTS_RENDERER=bgfx`. Add macOS bundle/notarization to CI.
7. **Phase 6 — 64-bit Windows + macOS builds.** Fix any `int` / `size_t` / pointer-cast issues that surface; serialize/replay format audit.
8. **Phase 7 — Retire DX8.** Once bgfx is replay-identical, default to bgfx on Windows and remove the DX8 backend, Miles, Bink, DirectInput, and the Win32 entry point.

---

## Verification

- **Per-phase smoke test**: `generalszh.exe` (Windows) and `Generals.app` (macOS) launch, main menu renders, play a 30-second skirmish without crash, exit clean.
- **Renderer parity**: golden-image test — render a deterministic replay (fixed camera, fixed seed) frame-by-frame on DX8 and bgfx; assert pixel delta below threshold. Add to CI.
- **Audio parity**: `MilesAudioManager` vs `OpenALAudioManager` sample coverage test — play one of every `.wav` / stream, verify 3D attenuation matches at known listener distances.
- **Video**: play every in-game `.bik` start-to-finish with the FFmpeg backend; A/B against Bink on Windows.
- **Multiplayer**: two headless clients on localhost desync-test using the `GeneralsReplays/` regression suite; replays from 1.04 retail must still play identically after each refactor.
- **Save/replay bit-identical**: the existing replay harness (the dummy audio manager work in commit `f61c98b57` is the same idea) — must still pass after each phase.
- **CI matrix**: GitHub Actions jobs for `win32-msvc-dx8`, `win32-msvc-bgfx`, `macos-arm64-bgfx`, `macos-x86_64-bgfx`. The vcpkg binary cache is already wired up per `README.md`.
- **Static sweep**: grep for remaining `d3d8`, `windows.h` (outside platform backends), `HKEY`, `WSAStartup`, `CreateThread`, `InterlockedIncrement`, `AIL_`, `BinkOpen`, `IDirectInput` after each phase — zero in core code; allowed only under `Win32Device/` or `#ifdef _WIN32` blocks.