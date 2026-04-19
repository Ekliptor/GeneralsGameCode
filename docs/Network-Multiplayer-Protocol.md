# Network Multiplayer Protocol

Architectural summary of the Command & Conquer Generals multiplayer networking stack as implemented in this repository.

## Transport

- **UDP only.** A single UDP socket per peer, wrapped by a `Transport` class with send/receive queues.
- BSD socket API (`socket()`, `bind()`, `sendto()`, `recvfrom()`).
- Base port: `NETWORK_BASE_PORT_NUMBER = 8088` (`Core/GameEngine/Include/GameNetwork/NetworkDefs.h:227`).
- Source: `Core/GameEngine/Source/GameNetwork/udp.cpp`, `Transport.cpp`.

## Packet Format

Hierarchical, packed (`#pragma pack(push, 1)`), little-endian.

| Layer | Contents | Source |
|------|---------|--------|
| `TransportMessageHeader` | CRC32 (4 B) + Magic `0xF00D` (2 B) = **6-byte header** | `NetworkDefs.h:51-57` |
| `NetPacket` | Multiple commands with compression / delta encoding | `NetPacket.h:48-129` |
| Field tags | Per-field type markers: `T` type, `F` frame, `P` player ID, `C` command ID, `D` data, `Z` repeat | `NetPacketStructs.h:110-170` |

- **Payload caps:** 1100 B safe modern limit vs. 476 B retail-compatible (tied to legacy LAN broadcast).
- **Obfuscation:** Packet-level XOR on 4-byte dwords; rotating mask starts at `0x0000FADE` and increments by `0x00000321` per dword (`Transport.cpp:37-66`).

## Topology: Peer-to-Peer with Elected Packet Router

- Every player opens a direct UDP connection to every other player (`Connection.h:45-101`).
- One elected **Packet Router** peer relays critical synchronization frames — this is not an authoritative server; gameplay state is simulated locally on every peer.
- **No dedicated game server.** GameSpy is used for lobby/matchmaking only; in-game traffic is pure P2P.

## Synchronization: Lockstep Command-Turn RTS

Classic deterministic lockstep model.

- Local input enters `TheCommandList`, is pulled by `Network::GetCommandsFromCommandList()` (`Network.cpp:182`).
- Each order becomes a `NetGameCommandMsg` tagged with an execution frame (`NetCommandMsg.h:49,87`).
- The simulation will not advance to frame N until `allCommandsReady(N)` returns true for all peers (`Connection.h:59`).
- **Run-ahead:** Dynamically computed lookahead (`Network.cpp:115,705`), bounded by `FRAME_DATA_LENGTH` / `MAX_FRAMES_AHEAD` (`NetworkDefs.h:36-41`). Max 256 commands per frame.
- **Batching:** Configurable `setFrameGrouping(time_t)` (`Connection.h:64`).

## Reliability

UDP is made reliable at the application layer:

- Two-stage ACK protocol: `NETCOMMANDTYPE_ACKSTAGE1`, `NETCOMMANDTYPE_ACKSTAGE2`, `NETCOMMANDTYPE_ACKBOTH` (`NetworkDefs.h:145-147`).
- Connection keeps retry queues and tracks per-peer latency (`Connection.h:93-100`).

## NAT / Firewall Traversal

Heavy client-side logic since there is no relay server.

- **`FirewallHelperClass`** (`FirewallHelper.h:110-307`) classifies 8+ firewall/NAT behaviors (simple, dumb mangling, smart mangling, Netgear bug, port allocation patterns).
- **`NAT` class** (`NAT.h:63-155`) probes external "Mangler" servers (port `4321`) to detect port-mapping schemes, then builds a probe/response path between peers before game start (`NAT.h:91`).
- **Keep-alive packets** maintain NAT bindings during gameplay (`NAT.h:151`).

## LAN vs. Internet Paths

Clean split at session establishment; both converge on the same Transport / ConnectionManager / Network core afterwards.

- **LAN (`LANAPI.h:57-100`):**
  - UDP broadcast discovery (`RequestLocations()`, `RequestGameJoin()`).
  - Direct-IP join (`RequestGameJoinDirectConnect()`).
  - Packet size capped at `MAX_LANAPI_PACKET_SIZE = RETAIL_GAME_PACKET_SIZE = 476` B.
- **Internet:**
  - GameSpy lobby (`GSConfig.h`, `PeerThread.cpp`, `GameSpy.h:25-80`).
  - Mandatory NAT / Firewall probe phase before P2P session starts.
  - No WebOL / Westwood Online code present.

## Key File Pointers

- Core stack: `Core/GameEngine/Source/GameNetwork/` (`udp.cpp`, `Transport.cpp`, `Connection.cpp`, `NetPacket.cpp`, `Network.cpp`, `ConnectionManager.cpp`, `FrameDataManager.cpp`)
- NAT / Firewall: `NAT.cpp`, `FirewallHelper.cpp`
- LAN: `LANAPI.cpp`
- Online matchmaking: `Generals/Code/GameEngine/Source/GameNetwork/GameSpy*.cpp`

## Historical Comparison: Westwood Online (WOL)

Generals / Zero Hour **never shipped using the Westwood Online protocol** for gameplay, despite leftover naming in the codebase. The classic WOL stack (used by Red Alert 2, Tiberian Sun, Renegade, Emperor) is a different architecture.

### Vestigial WOL artifacts in this repo

These exist but are not part of the gameplay network path:

- `Generals/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/WOL*Menu.cpp` — UI menus named after WOL but internally include `GameNetwork/GameSpy/*.h` and drive the GameSpy stack exclusively.
- `Core/Tools/wolSetup/WOLAPI/wolapi.h`, `wolapi_i.c` — MIDL-generated COM bindings (dated 2001-11-05) for the legacy `wolapi.dll` chat launcher. Used only by the separate `wolSetup.exe` utility.
- `Core/Tools/PATCHGET/CHATAPI.cpp` — fragments of the Westwood patcher/chat wrapper.
- `Core/GameEngine/Source/GameNetwork/WOLBrowser/WebBrowser.cpp` — embedded IE COM browser control; the "WOL" prefix is historical, GameSpy reuses it for MOTD/ads.

### Protocol differences (WOL vs. Generals GameSpy+P2P)

| Aspect | Generals (this repo) | Westwood Online |
|---|---|---|
| Lobby / chat | GameSpy Peer SDK (proprietary binary over TCP) | **IRC-based** — modified IRCd (`WChat`) on TCP/4000; `JOIN`, `PRIVMSG` plus custom commands (`GAMEOPT`, `STARTG`, `JOINGAME`, `SERIAL`) |
| Auth | GameSpy profiles via GP SDK (TCP/29900) | Custom `APGAR` challenge/response against Westwood ladder/account servers |
| Server discovery | GameSpy Master Server List (UDP/27900) | Westwood "ServServ" directory + regional chat servers |
| In-game transport | UDP P2P, 6-byte `0xF00D` header, XOR mask (this doc) | UDP P2P lockstep, different framing (older Westwood `Connection` / `Combat` classes, no `0xF00D` magic) |
| NAT traversal | `FirewallHelper` + Mangler servers on UDP/4321 | Minimal — relied on manual port forwarding; many NAT setups failed |
| Matchmaking | GameSpy Quickmatch + GP ladder backend | Westwood ladder + `QuickMatch` bot on the chat server |
| Patching | GameSpy-era updater | `PATCHGET.exe` / WDT patch distributor |
| Embedded browser | Shared concept (IE COM control) | Same — used for MOTD/ads/news |

The **gameplay sync model itself is the same family** across both eras: deterministic lockstep, run-ahead frames, peer-to-peer mesh, elected relay. What EA changed for Generals was the lobby, matchmaking, chat, and NAT layers — WOL's IRC-style stack was replaced with the GameSpy SDK in `Core/GameEngine/Source/GameNetwork/GameSpy/`.

> Note: WOL protocol details are from historical/community reverse-engineering knowledge. This repo contains only GameSpy-era gameplay code, so WOL-specific claims cannot be cited to file:line here.