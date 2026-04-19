# Phase 4 — Networking (POSIX sockets, libcurl, OpenSpy)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Mirrors the structure of
`Phase0-RHI-Seam.md`, `Phase1-Audio-Video.md`, `Phase2-Window-Input.md`,
`Phase3-Config-Threading-Browser.md`.

## Objective

Remove the three remaining Win32-only networking surfaces from the main game
binary and replace them with portable equivalents. Together with the Phase 0-3
work, the game's networking TUs now compile without `<winsock.h>` anywhere in
the source except one init-only site (`NetworkInit.cpp`), and the 1846-line
hand-rolled FTP client has been collapsed onto libcurl.

Phase 4 does not introduce a new CMake selector; networking is not a
user-selectable backend. It adds one new hard dependency (`libcurl`) via
`vcpkg.json` and one new CMake include (`cmake/curl.cmake`).

## Locked decisions

| Question | Decision |
|---|---|
| DNS API | `getaddrinfo` everywhere. New helper `resolveHostIPv4(const char *, UnsignedInt &)` in `NetworkUtil.{h,cpp}`. All 9 live `gethostbyname` game-code callers route through it. |
| Winsock init | Single `NetworkInit::ensureStarted()` / `shutdownOnce()` namespace. `WSAStartup(MAKEWORD(2,2))` under `std::once_flag` on Windows, true/no-op on POSIX. Replaces 4 duplicated init blocks in IPEnumeration, Transport, PingThread, GameResultsThread. |
| `udp.h` platform switch | Thin `#include <Utility/socket_compat.h>`. On POSIX that header maps `SOCKET_ERROR`, `INVALID_SOCKET`, `WSAEWOULDBLOCK`, `closesocket`, `ioctlsocket`, `WSAGetLastError` onto their BSD equivalents. `udp.cpp` is now platform-agnostic apart from the `GetWSAErrorString` debug dump (Windows-specific error-name lookup; POSIX falls through to `strerror`). |
| IPEnumeration | Two compile-time paths. Windows: `getaddrinfo(hostname, …)` walks the DNS-resolved IPv4 set. POSIX: `getifaddrs()` walks local interfaces, filters `AF_INET`, skips loopback and down interfaces. The `rts::ClientInstance::isMultiInstance()` synthetic `127.x.y.z` block is unchanged. |
| SNMP NAT detection | Deleted. The 330-line `GetLocalChatConnectionAddress` that walked the MIB-II TCP connection table via `inetmib1.dll`/`snmpapi.dll` `LoadLibrary` calls is gone. Replaced by a ~30-line `connect()` + `getsockname()` UDP-probe in new `GameSpy/LocalChatAddress.{h,cpp}`. Network-byte-order return matches the old API contract. |
| Async DNS thread | `CreateThread(…)` in `MainMenuUtils.cpp:742` swapped for `std::thread(…).detach()` with `std::atomic<Bool>` flags. `StopAsyncDNSCheck` can no longer force-kill the worker (detached threads have no handle); it clears `s_asyncDNSLookupInProgress` and lets the worker self-exit. |
| WWDownload | Hand-rolled Winsock FTP client (`FTP.cpp`, 1846 LOC; `ftp.h`'s Winsock internals: `m_iCommandSocket`, `m_iDataSocket`, FTPREPLY_* constants, `SendData/RecvData/OpenDataConnection/SendNewPort/CloseSockets`) **rewritten as a libcurl multi-handle wrapper** (~350 LOC). `CDownload` public API (`DownloadFile`, `PumpMessages`, `Abort`, `GetLastLocalFile`, `IDownload` listener) unchanged. Resume via `CURLOPT_RESUME_FROM_LARGE`. `Core/Tools/PATCHGET/` keeps working on Windows through the preserved API. |
| WWDownload registry helper | `Core/Libraries/Source/WWVegas/WWDownload/registry.cpp` gated `#ifdef _WIN32` with POSIX stub returning false. On macOS the download-resume feature degrades to "always start fresh" (acceptable until Phase 6 lets macOS actually run the game). The main game's registry migrated to `RegistryStore` in Phase 3; this helper is scoped to WWDownload only. |
| libcurl dependency | `curl` added to `vcpkg.json`. New `cmake/curl.cmake`: `find_package(CURL CONFIG QUIET)` with a `find_package(CURL REQUIRED)` MODULE-mode fallback for Homebrew hosts that don't ship `CURLConfig.cmake`. Always-on (networking is not selectable). |
| GameSpy SDK | **No source change.** `cmake/gamespy.cmake` already fetches the `TheAssemblyArmada/GamespySDK` fork which is cross-platform. Already pointed at `server.cnc-online.net` for OpenSpy. |
| `Core/Tools/PATCHGET/`, matchbot, mangler | Out of scope. Windows-only tools. Continue to consume `CDownload` (preserved API) and the raw-Winsock `wnet/*.cpp` files — Phase 6/7 territory. |

## Source-level changes

### New compat / init files

- `Dependencies/Utility/Utility/socket_compat.h` — platform shim.
- `Dependencies/Utility/Utility/hresult_compat.h` — minimal `HRESULT` /
  `S_OK` / `E_FAIL` / `MAKE_HRESULT` for `WWDownload` on non-Windows
  (Windows includes real `<windows.h>` / `<winerror.h>`).
- `Core/GameEngine/Include/GameNetwork/NetworkInit.h` +
  `Core/GameEngine/Source/GameNetwork/NetworkInit.cpp` — `NetworkInit::ensureStarted/shutdownOnce`.
- `Core/GameEngine/Include/GameNetwork/GameSpy/LocalChatAddress.h` +
  `Core/GameEngine/Source/GameNetwork/GameSpy/LocalChatAddress.cpp` —
  cross-platform UDP-probe replacement for SNMP.

### Edited game sources

| File | Phase 4 change |
|---|---|
| `Core/GameEngine/Include/GameNetwork/udp.h` | Replaced the 25-line platform `#ifdef` block with `#include <Utility/socket_compat.h>`. |
| `Core/GameEngine/Source/GameNetwork/udp.cpp` | `gethostbyname` → `resolveHostIPv4`. Unified Write/Read error path (dropped `#ifdef _WIN32` guards; `WSAGetLastError`/`WSAEWOULDBLOCK`/`SOCKET_ERROR` now resolve on both platforms via `socket_compat.h`). `GetWSAErrorString` Windows-gated; POSIX branch uses `strerror`. `socklen_t` used for `recvfrom`/`getsockname` length arg. |
| `Core/GameEngine/Include/GameNetwork/networkutil.h` | `resolveHostIPv4` declaration. |
| `Core/GameEngine/Source/GameNetwork/NetworkUtil.cpp` | `resolveHostIPv4` definition + `ResolveIP` delegates. |
| `Core/GameEngine/Source/GameNetwork/IPEnumeration.cpp` | Rewritten — dual Windows/POSIX paths. No more private `WSAStartup`/`WSACleanup`. |
| `Core/GameEngine/Include/GameNetwork/Transport.h` | Dropped `m_winsockInit`. |
| `Core/GameEngine/Source/GameNetwork/Transport.cpp` | Init inlined → `NetworkInit::ensureStarted()`. `reset()` no longer calls `WSACleanup`. |
| `Core/GameEngine/Source/GameNetwork/NAT.cpp` | `gethostbyname(manglerName)` → `resolveHostIPv4(manglerName, m_manglerAddress)`. |
| `Core/GameEngine/Source/GameNetwork/FirewallHelper.cpp` | Same pattern — `gethostbyname` → `resolveHostIPv4`. |
| `Core/GameEngine/Source/GameNetwork/DownloadManager.cpp` + `DownloadManager.h` | Dropped private `m_winsockInit`; ctor calls `NetworkInit::ensureStarted()`. |
| `Core/GameEngine/Source/GameNetwork/GameSpy/Thread/PingThread.cpp` | `WSAStartup`/`WSACleanup` → `NetworkInit::ensureStarted()`. `gethostbyname` → `resolveHostIPv4`. `<winsock.h>` include swapped for `<Utility/socket_compat.h>`. |
| `Core/GameEngine/Source/GameNetwork/GameSpy/Thread/GameResultsThread.cpp` | Same pattern as PingThread. |
| `Core/GameEngine/Source/GameNetwork/GameSpy/MainMenuUtils.cpp` | `CreateThread` → `std::thread(…).detach()`. Atomic flags. `asyncGethostbynameThreadFunc` body rewritten to call `resolveHostIPv4`. `StopAsyncDNSCheck` becomes a flag clear. |
| `Core/GameEngine/Source/GameNetwork/GameSpy/StagingRoomGameInfo.cpp` | Deleted lines 70-423 (SNMP function pointers, `ConnInfoStruct`, 330-line `GetLocalChatConnectionAddress`). Now forwards to `LocalChatAddress.cpp` via the include. |
| `Generals/Code/GameEngine/Source/GameNetwork/GameSpyGameInfo.cpp` | Same SNMP-body deletion. Includes `LocalChatAddress.h`. |
| `GeneralsMD/Code/GameEngine/Source/GameNetwork/GameSpyGameInfo.cpp` | Same. |
| `Generals/Code/GameEngine/Source/GameNetwork/GameSpy.cpp` | Includes `LocalChatAddress.h` for the `JoinRoomCallback` + `createRoomCallback` sites. |

### WWDownload rewrite

- `Core/Libraries/Source/WWVegas/WWDownload/ftp.h` — stripped all Winsock
  internals; Cftp class is now a libcurl-opaque wrapper. Progress fields
  (`m_iFilePos`, `m_iBytesRead`, `m_iFileSize`) promoted to public for the
  progress callback's benefit.
- `Core/Libraries/Source/WWVegas/WWDownload/FTP.cpp` — complete rewrite.
  `curl_multi_perform`-driven state machine. `ConnectToServer`/`LoginToServer`
  stash credentials; `FindFile` uses a short-lived easy handle with
  `CURLOPT_NOBODY` and reads `CURLINFO_CONTENT_LENGTH_DOWNLOAD_T`;
  `GetNextFileBlock` advances a multi-attached easy handle until
  `curl_multi_info_read` yields `CURLMSG_DONE`. Resume via
  `CURLOPT_RESUME_FROM_LARGE`.
- `Core/Libraries/Source/WWVegas/WWDownload/registry.cpp` — `#ifdef _WIN32`
  around the HKEY body; POSIX stubs return `false`/`true` appropriately.
- `Core/Libraries/Source/WWVegas/WWDownload/Download.cpp` — POSIX branches
  for `_mkdir` → `mkdir(0755)`, `struct _stat`/`_stat()` → `struct stat`/`stat()`,
  `_strnicmp` → `strncasecmp`, `MulDiv` → int64 multiply-divide. `<mmsystem.h>`
  gated to Windows; POSIX pulls `<Utility/time_compat.h>` for `timeGetTime`.

### Miscellaneous macOS enablement (collateral)

- `Core/Libraries/Source/WWVegas/WWLib/stringex.h` — added `<wctype.h>`
  for `towlower` (previously leaned on Win32 pulling wctype transitively).
- `Dependencies/Utility/Utility/time_compat.h` — `timeGetTime` clock
  selection now probes `CLOCK_BOOTTIME` → `CLOCK_UPTIME_RAW` → `CLOCK_MONOTONIC`
  so macOS (no BOOTTIME) picks the right clock.

## CMake plumbing

- `vcpkg.json` — added `"curl"`.
- `cmake/curl.cmake` — `find_package(CURL CONFIG QUIET)` + MODULE fallback.
- `CMakeLists.txt` (root) — `include(cmake/curl.cmake)` alongside
  `sdl3.cmake`/`openal.cmake`/`ffmpeg.cmake`.
- `Core/Libraries/Source/WWVegas/WWDownload/CMakeLists.txt` — added
  `CURL::libcurl` to `corei_wwdownload` link libs.
- `Core/GameEngine/CMakeLists.txt` — added
  `Source/GameNetwork/NetworkInit.cpp`,
  `Source/GameNetwork/GameSpy/LocalChatAddress.cpp`,
  and the two corresponding headers.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, libcurl 8.19
(Homebrew). Clean configure + WWDownload build:

| Command | Outcome |
|---|---|
| `cmake .. -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg` | OK. `PlatformBackend: sdl`, all four backends listed. libcurl picked up from the macOS SDK (`libcurl.tbd`). |
| `cmake --build . --target z_wwdownload` | OK. `libwwdownload.a` linked. |
| `cmake --build . --target g_wwdownload` | OK. Both per-game variants build. |

The full `z_gameengine` / `g_gameengine` macOS build still breaks on
pre-existing macOS incompatibilities (PreRTS.h unconditionally includes
`<windows.h>`, `thread_compat.h` returns `pthread_t` as `int`, `<malloc.h>`
path). Those are Phase 6 territory — Phase 4 does not regress Windows and
makes all networking TUs source-level clean.

Static-sweep grep results after Phase 4:

| Pattern | Result |
|---|---|
| `\bgethostbyname\b` in `Core/GameEngine`, `Generals/Code/GameEngine`, `GeneralsMD/Code/GameEngine` | 1 hit — a `DEBUG_LOG` string in `FirewallHelper.cpp` (text only, no call). Zero active calls. |
| `WSAStartup` in the above game-engine paths | 1 hit — the consolidated call inside `NetworkInit.cpp`. |
| `CreateThread` in `Source/GameNetwork/` | Zero. |
| `snmpapi\|inetmib1\|SnmpExtensionQuery` in game code | 1 hit — a comment in `StagingRoomGameInfo.cpp` explaining the removal. Zero active code. |
| `#include *<winsock` in game code (excluding `Tools/`) | 1 hit — `NetworkInit.cpp`. Zero in leaf TUs. |

Windows smoke tests (LAN game, GameSpy internet game, patch download,
replay determinism) run on the Windows CI matrix once this lands — this
host has no Windows toolchain.

## Deferred to later phases

| Item | Phase |
|---|---|
| 64-bit socket FD (`SOCKET` vs `int` width on Win64) | Phase 6 (64-bit Windows build) |
| Raw Win32 `CRITICAL_SECTION` in `WWAudio`, `WWDebug`, `WWLib`, `simpleplayer.cpp`, `matchbot`, `mangler` | Phase 6/7 |
| `InterlockedIncrement` in WWAudio/WWDebug | Phase 6/7 |
| `pseh_compat.h` retirement | Phase 6/7 |
| `tchar_compat.h` retirement | Phase 6 |
| Main-game macOS build (blocked on `<windows.h>` in PreRTS.h, thread_compat, malloc.h) | Phase 6 |
| `ApplicationHWnd` → backend-neutral rename | Phase 7 |
| `Win32Mouse` → `BufferedMouse` rename | Phase 7 |
| IPv6 | never — retail clients are IPv4-only |
| GameSpy wire-protocol audit vs. OpenSpy | out-of-scope |
| `ShellExecute` → `SDL_OpenURL` | opportunistic — no callers today |
| WWDownload resume on non-Windows | Phase 6 (once RegistryStore reaches WWDownload) |
| `tests/tst_TcpConnect` | optional — not required to land Phase 4 |
