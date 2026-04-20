# Phase 6e — Save/replay format portability audit

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fifth sub-phase of Phase 6.
Follows `Phase6d-64BitAudit.md`.

## Scope gate

Phase 6 hinges on "the 32-bit Windows retail save/replay format keeps working
on a 64-bit build". This phase **audits** the `Xfer` serialization family and
reports findings — it deliberately does not patch anything, because the one
real risk flagged (`XferFilePos = long`) needs an end-to-end bit-identity
harness to patch safely, and that harness lives in Phase 6g CI, not here.

## Methodology

Surveyed the save/replay pipeline by reading:

- `Core/GameEngine/Include/Common/Xfer.h`
- `Core/GameEngine/Include/Common/XferSave.h`
- `Core/GameEngine/Include/Common/XferLoad.h`
- `Core/GameEngine/Include/Common/XferCRC.h`
- `Core/GameEngine/Source/Common/System/Xfer.cpp`
- `Core/GameEngine/Source/Common/System/XferSave.cpp`
- `Core/GameEngine/Source/Common/System/XferLoad.cpp`
- `Core/GameEngine/Source/Common/System/XferCRC.cpp`

Grep-sweep for pointer-to-integer casts, `sizeof(void*)`, `#pragma pack`,
`long`/`DWORD` uses in serialization paths, and CRC digest inputs.

## Findings

### 1. On-wire primitives — portable

`Xfer` writes fixed-width types:

| Primitive | Underlying type | Bytes on wire |
|---|---|---|
| `xferByte` | `Byte` (`uint8_t`) | 1 |
| `xferShort` / `xferUnsignedShort` | `int16_t` / `uint16_t` | 2 |
| `xferInt` / `xferUnsignedInt` | `int32_t` / `uint32_t` | 4 |
| `xferInt64` / `xferUnsignedInt64` | `int64_t` / `uint64_t` | 8 |
| `xferReal` | `float` (IEEE 754 binary32) | 4 |

Base typedefs in `Core/Libraries/Include/Lib/BaseTypeCore.h` pin these to
fixed-width `int32_t` / `uint32_t` / `int64_t` etc. **No `long` in the wire
format.** The xfer API's on-disk layout is 32/64 portable.

### 2. CRC digest — portable, no pointer-folding

`XferCRC::addCRC(UnsignedInt val)` folds only serialized data. Nothing in
the Xfer CRC path casts a `this` pointer or any other pointer to a CRC
input. Verdict: CRC digests are bit-identical across 32/64-bit for the same
wire payload.

### 3. No `#pragma pack` in serialization — portable

`#pragma pack(push, 1)` appears only in `Core/GameEngine/Include/GameNetwork/
NetPacketStructs.h` and adjacent network code, which is out of save/replay
scope. Xfer does no `memcpy`-style struct serialization.

### 4. **Risk flagged: `XferFilePos = long` (internal-only typedef)**

**File:** `Core/GameEngine/Include/Common/XferSave.h:40`

```cpp
typedef long XferFilePos;
```

**Usage:**

- `XferBlockData::filePos` (line 43 of the same header) is `XferFilePos`.
- `XferSave::closeBlock()` / `openBlock()` (`XferSave.cpp:173–238`) do
  `currentFilePos - top->filePos - sizeof(XferBlockSize)` to compute the
  block size that is *then* written as `Int` (`int32_t`) on the wire.

**Why `long` is a trap:**

| Platform | `long` size |
|---|---|
| Windows 32-bit MSVC | 4 bytes |
| Windows 64-bit MSVC | 4 bytes (LLP64) |
| macOS arm64 / x86_64 | 8 bytes (LP64) |
| Linux 64-bit | 8 bytes (LP64) |

**The wire is safe either way** because the subtraction result is truncated
into `Int` at write time. The risk is **arithmetic behaviour** if a save
file exceeds 2 GB — on LLP64 (Windows 64-bit) `long` would wrap, on LP64
(macOS) it wouldn't, so the *subtraction* would differ. For Generals-scale
replays (typically <100 MB) this is theoretical; for exceptionally long
multiplayer games it is real. Also: `ftell` returns `long`, so switching
the typedef means also switching to `ftello` / `_ftelli64` for consistency.

### 5. No pointer-to-integer casts in save/replay paths

Greps for `reinterpret_cast<.*>\(.*\*\)`, `(DWORD)this`, `(int)this`, etc.
in `Core/GameEngine/Source/Common/Xfer/` and callers showed zero matches
that affect the wire format. The only pointer-to-integer casts in the
broader codebase live in Win32Device / DX8 / Miles / Bink paths that are
gated off for the cross-platform stack.

## Verdict

**Save/replay wire format is 64-bit portable for the bgfx stack.** No wire-
level changes needed. One internal typedef is a latent arithmetic trap at
the extreme tail (files >2 GB), not blocking.

## Recommended follow-up (not in this phase)

1. Change `typedef long XferFilePos;` → `typedef int64_t XferFilePos;`
   (`Core/GameEngine/Include/Common/XferSave.h:40`).
2. Replace `ftell(fp)` with `_ftelli64(fp)` on MSVC / `ftello(fp)` on
   POSIX in `XferSave.cpp` (two call sites around line 173 / 227).
3. Verify via an end-to-end replay bit-identity test (Phase 6g) that
   replays from 32-bit Windows play identically on 64-bit macOS.

These three steps are Phase 6e.1; packaged as a deferred item because the
verification step genuinely requires CI infrastructure that 6g is adding.

## Verification

This is an audit phase; verification is the audit itself.

| Check | Result |
|---|---|
| Greps run for `typedef.*long.*` in `Core/GameEngine/Include/Common/Xfer*` | One hit — `XferFilePos` (documented above). |
| Greps for `(DWORD)this` / `reinterpret_cast<.*int.*>(.*\*)` in Xfer paths | Zero. |
| `#pragma pack` in Xfer files | Zero. |
| Wire-format types fixed-width? | Confirmed via `BaseTypeCore.h`. |
| CRC digest independent of platform? | Confirmed — CRC walks serialized bytes, not object layout. |

## Deferred

| Item | Why | Stage |
|---|---|---|
| Change `XferFilePos` typedef + `ftello`/`_ftelli64` adoption | Needs bit-identity regression harness. | 6e.1 |
| Replay bit-identity test harness | Distribution concern; requires real game assets. | 6g / 7 |
| Network protocol audit | Different code path (`NetPacket*`); same methodology but separate scope. | 6e.2 |
| Save files produced on 64-bit macOS loading on 32-bit Windows 1.04 retail | Needs both platforms side-by-side. | 7 |

## Meta-status

- Phase 6: 6a–6e land; 6f–6g still this session.
- No code change in 6e itself — pure audit.
- bgfx tests: **23/23 green** (unchanged — no code touched).
- Save/replay wire format: **portable**. One latent arithmetic risk documented.
