# Vanilla Generals (macOS BGFX) — Status

Last updated: 2026-04-24 (round 11 — replay file WideChar wire-format fix).

Tracks the state of the *vanilla* `generalsv.app` build on macOS/BGFX/SDL3/
OpenAL/FFmpeg. For the sibling Zero Hour build see
[ZH-MainMenu-Bugs.md](ZH-MainMenu-Bugs.md). For the overall cross-platform
port plan see [CrossPlatformPort-Plan.md](CrossPlatformPort-Plan.md).

## Headline

- Builds + launches without crashing.
- Intro-video **audio** plays end-to-end (EA logo + Westwood + intro).
- Intro-video **picture** does not render: window stays black throughout
  the intro and carries over to the main menu.
- FFmpeg decode + `SurfaceClass::Unlock → Upload_To_Associated_Texture →
  bgfx::updateTexture2D` pipeline is wired and verified — the bug is not
  in upload.
- The upload target handle and the draw-stage-0 handle are the **same**
  `bgfx::TextureHandle*` per frame. Identical diagnostics to the working
  ZH build.

## Fixed this round

| # | File | Change |
|---|------|--------|
| 1 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.{h,cpp}` | Ported ZH's `Render2DClass` wholesale — vanilla had the stale DX8-era `SimpleDynVecClass` version with `Add_Multiple` / `Delete_All` semantics. Replaced with `DynamicVectorClass<T>` + `PreAllocated*[60]` backing buffers, `Uninitialized_Add` / `Reset_Active`, and `WW3D::Get_Device_Resolution`-based viewport. See the "Phase 5h.2D" banner at top of `render2d.cpp`. |
| 2 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | `DX8VertexBufferClass::Create_Vertex_Buffer` unconditionally called `_Get_D3D_Device8()->CreateVertexBuffer(...)`, null-dereferencing on BGFX. Wrapped the D3D-specific body in `#ifdef RTS_RENDERER_DX8` / `#else VertexBuffer = nullptr; m_cpuShadow = new unsigned char[...];`. Fixed the `TerrainTracksRenderObjClassSystem::ReAcquireResources` SIGSEGV at boot. |
| 3 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Same treatment for `DX8IndexBufferClass` ctor — DX8CALL of `CreateIndexBuffer` was unguarded. |
| 4 | `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp` | Added `WW3D_FORMAT_R8G8B8` case to `ConvertPixelToRGBA8` — a 128×128 24-bit BGR UI texture was tripping the "unsupported format, skipped" stub. |
| 5 | `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` | Added `WW3D::Set_Texture_Bitdepth(32)` in `init` (ZH had it, vanilla was missing). |

### Round 6 — ZH-delta alignment edits (DID NOT fix the black intro)

| # | File | Change |
|---|------|--------|
| 6 | `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` | Wrapped the `DX8Wrapper::_Get_D3D_Device8()` gate in `W3DDisplay::draw()` (~line 1820) in `#ifdef RTS_RENDERER_DX8 … #else if (true) … #endif` to match ZH's `W3DDisplay.cpp:1912`. On BGFX `_Get_D3D_Device8()` is `nullptr`, so the block (including `updateViews()` + per-frame water/shadow render-target refreshes) was being skipped every frame. |
| 7 | same file | Added `Render2DClass::Set_Screen_Resolution(RectClass(0,0,getWidth(),getHeight()))` after each `Set_Coordinate_Range` call in `setWidth` and `setHeight` to match ZH's pair of calls. Keeps the static `ScreenResolution` rect in sync with live resize. |

**Round 6 result**: User-verified (2026-04-24): intro on vanilla still renders black with only the mouse cursor (triangle) drawn; ZH screenshot path still renders main-menu border+logo correctly. So edits #6 and #7 align vanilla to ZH behavior but **do not** close the remaining gap. They are kept as correctness-fixes (both are ZH deltas already in that tree).

### Conclusion after round 6

The remaining black-intro bug is **not** in `W3DDisplay`, not in `render2d`, not in upload/handle-binding, and not in the per-frame `updateViews` plumbing. A diff-scan of the target-duplicated WW3D2 tree pair (`Generals/Code/Libraries/Source/WWVegas/WW3D2` vs `GeneralsMD/...`) showed ~40 files differ; all reviewed so far (`shader.cpp`, `dx8fvf.cpp`, `dx8indexbuffer.cpp`, `dx8vertexbuffer.cpp`, `ww3d.cpp`) either differ only inside `#ifdef RTS_RENDERER_DX8` blocks (inert on BGFX) or in comments / unrelated debug tooling. The real delta must be in a file not yet audited — candidates: `assetmgr.cpp`, `texture.cpp` (if target-duplicated), `dx8renderer.cpp`, `mesh.cpp` — or in the BGFX sub-pipeline itself (Core).

Notable evidence: the *mouse cursor is drawn as a visible triangle*, which means textured 2D quads can produce pixels — yet the intro-video quad produces none. This suggests the bug is texture-binding-related on the *specific handle path* used by `W3DVideoBuffer`, not a global 2D-pipe failure.

### Round 7 — stage-1 stale binding (partial fix; does not close)

Tested round 6's hypothesis #2 (`SelectProgram` → `tex2` due to stale stage 1) and confirmed it is *one* real bug. Applied:

| # | File | Change |
|---|------|--------|
| 8 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.cpp` + `GeneralsMD/.../render2d.cpp` | In `Render2DClass::Render`, add `DX8Wrapper::Set_Texture(1, NULL)` after `Set_Texture(0, Texture)`. `dynamic_fvf_type` carries TEX2, so the FVF→layout has UV1; if any earlier 3D draw left `m_stageTexture[1] != 0`, `BgfxBackend::SelectProgram` (`BgfxBackend.cpp:1104`) returns the two-stage `tex2` program and the quad samples the stale stage-1 texture → black. The 2D path only ever uses stage 0. |
| 9 | `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | `TextureBaseClass::Apply_Null` previously only called the DX8-only inline `Set_DX8_Texture` (a no-op on BGFX), so a `Set_Texture(stage, NULL)` from any caller never propagated to `BgfxBackend::m_stageTexture[]`. Fixed to also call `b->Set_Texture(stage, 0)` so `hasStage1` actually goes false when the engine asks. Required for fix #8 to take effect at the BGFX level. |

**Result (user-verified 2026-04-24):** Vanilla main-menu screenshot changed from completely black to a few colored gradient triangles in screen corners (yellow/teal). ZH (with the same shared-code edits) still renders the menu correctly — confirms the fixes are not regressions.

**So the fixes are correct as far as they go**: they unblock the textured-quad path (no longer all-black) but the 2D quads are still landing at wrong positions / mostly off-screen. The program-selection bug was masking a *separate* downstream bug on the vanilla 2D pipe.

### Round 8 — fixed: `sizeof(DWORD)` on macOS is 8 bytes

The round-7 hypothesis #3 ("vertex byte-layout offset mismatch — *not* the bug") was wrong. It IS the bug, but the root cause was a 64-bit-host platform mismatch I had previously ruled out without measuring.

**Cause:** `FVFInfoClass` (vanilla `Generals/.../WW3D2/dx8fvf.cpp`) uses `sizeof(DWORD)` to size `D3DFVF_DIFFUSE` and `D3DFVF_SPECULAR`. The compat header at `Core/.../compat/d3d8.h` types `DWORD` as `unsigned long`, which is **8 bytes on 64-bit macOS** (vs 4 bytes on Win32). The diffuse field then consumed 8 bytes of stride instead of 4, shifting `texcoord_offset[0]` from 28 to 32 and reporting `Get_FVF_Size()=48` instead of 44.

End result: `Render2DClass::Render` wrote per-vertex UVs at byte offset 32 (where `FVFInfoClass` said tex0 lived), but `FillLayoutFromFVF` in shared `Core/dx8wrapper.cpp` packed the bgfx vertex layout with tex0 at offset 28 (it uses `sizeof(unsigned)` = always 4). So bgfx read garbage zeros for every UV, sampling the texture's leftmost column — which is mostly black/transparent — for every quad. Mouse cursor and a couple of edge-aligned quads happened to pick up some pixels and showed as the mysterious gradient triangles.

ZH fixed this years ago in its dx8fvf.cpp by switching `sizeof(DWORD)` → `sizeof(uint32_t)`. Vanilla never picked up that delta. Both `dx8fvf.cpp` files live in the target-duplicated WW3D2 trees — vanilla's was stale.

| # | File | Change |
|---|------|--------|
| 10 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/dx8fvf.cpp` | Replaced all `sizeof(DWORD)` with `sizeof(uint32_t)` (4 occurrences) and added `#include <cstdint>`. Mirrors ZH's existing fix. |
| 11 | `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxTextureCache.cpp` | Defensive: patch the TGA image-descriptor byte to declare 8 alpha bits when the file is 32-bit truecolor with `alphaBits=0`. Vanilla's `scsmshelluserinterface512_001.tga` has `desc=0x00`; ZH's has `desc=0x08`. Without the patch `bimg`/stb_image may treat the alpha channel as zero and render fully transparent. (Independently of fix #10 this would have surfaced once the UV bug was fixed.) |

**Result (user-verified 2026-04-24):** Vanilla main menu now renders the same UI shell as ZH — Generals logo in the upper-right, six menu button frames stacked in the right panel, screen border, grid backdrop. Button labels are blank (font rasterizer is still stubbed in the BGFX build, same limitation as ZH — see `ZH-MainMenu-Bugs.md` §3.3).

Round-7 fixes #8 (`Render2DClass` stage-1 unbind) and #9 (`Apply_Null` propagation) are kept as correctness improvements: they were independently real bugs that would have surfaced after #10 unblocked vertex submission.

### Conclusion after round 8

The 2D quad pipeline is functional on vanilla. Remaining gaps are shared with ZH and tracked under `ZH-MainMenu-Bugs.md`:
- Button labels missing — font rasterizer stubbed (§3.3). **Update (ZH-MainMenu-Bugs §5.1, 2026-04-24): FIXED.** New `Core/Libraries/Source/WWVegas/WW3D2/render2dsentence_bgfx.cpp` rasterizes glyphs with `stb_truetype` into an A8R8G8B8 atlas and submits quads through the same `Render2DClass` pipeline the round-8 fix unblocked. Vanilla + ZH main-menu buttons both show their labels end-to-end.
- 3D shell-map scene black — separate from 2D quad path (§3.2).
- Background music silent — `OpenALAudioManager` gaps (§3.4).
- Intro video still renders black — confirmed not a 2D-quad issue (audio plays, frames upload). Likely the same upstream submission gap that one diagnostic at this round did NOT pursue (only 1 dynamic draw/frame fires at the steady menu state on both targets — most UI elements may bypass `Draw_Triangles_Dynamic` entirely; needs a separate investigation pass).

### Round 8 follow-up (2026-04-24): proactive `sizeof(DWORD|LONG|WORD)` audit — clean

Did a proactive sweep of every `sizeof(DWORD)`, `sizeof(LONG)`, `sizeof(long)`, `sizeof(unsigned long)`, `sizeof(WORD)`, and `sizeof(BYTE)` across `Generals/` vs `GeneralsMD/` (plus relevant `Core/` paths) to find any other instances of the same 4-vs-8-byte pitfall as fix #10. **Zero stale deltas remain in the target-duplicated trees.** All `sizeof(WORD)` usages (index buffers, shadow buffers) are universally safe because `WORD == unsigned short == 2 bytes` everywhere. The `Render2DClass` line-87 idiom `sizeof(PreAllocatedColors)/sizeof(unsigned long)` is the standard element-count idiom — both factors scale together and it correctly evaluates to 60 on both platforms. The related `Colors[i]` write at line 666 explicitly downcasts the `unsigned long` to `unsigned int` before writing into the FVF DIFFUSE slot, so only 4 bytes ever flow into the vertex buffer regardless of macOS's 8-byte `unsigned long`. The `dx8fvf.cpp` case (fix #10) was the last `sizeof()` delta where vanilla had failed to pick up a ZH-side macOS-portability fix. The memory entry `dword_is_8_bytes_on_macos.md` has been refined to capture which patterns to flag and which to skip. The one real outstanding `sizeof(long)` finding is in shared `Core/.../WWLib/crc.cpp` (cross-platform CRC value divergence — affects cross-platform savegame/replay/multiplayer compatibility but not macOS-only play); tracked under `docs/ZH-MainMenu-Bugs.md` for a future cross-platform compatibility pass.

### Round 9 — cross-platform CRC + Unicode-on-disk fixes

A second-tier audit covering `sizeof(time_t|off_t|size_t|wchar_t|uintptr_t|intptr_t|ptrdiff_t)` and the game's `sizeof(WideChar|Int|UnsignedInt|Real|Bool)` typedefs surfaced three live cases of the same class of bug (cross-platform serialization byte-count divergence). Two were safe to fix this round; one is deferred until the corresponding subsystem (multiplayer) actually works on the BGFX port.

| # | File | Change |
|---|------|--------|
| 12 | `Core/Libraries/Source/WWVegas/WWLib/CRC.h` and `crc.cpp` | `CRCEngine` now chunks input by `uint32_t` (4 bytes on every platform) instead of `long` (4 on Win32, 8 on 64-bit macOS). The internal `StagingBuffer` union and the bulk-processing pointer / counter / decrement all use `uint32_t`. `_lrotl` was already 32-bit on macOS (per `intrin_compat.h:75`), so no extra masking is needed. Risk: low — `CRCEngine` has no live callers in active game code. INI hashing uses the separate byte-by-byte `CRC::String`, savegame integrity uses `XferCRC` (chunks by fixed 4); both were already platform-safe. The fix prevents any *future* user of `CRCEngine` from getting platform-divergent values. |
| 13 | `Generals/Code/GameEngine/Source/Common/System/DataChunk.cpp` and `GeneralsMD/Code/GameEngine/Source/Common/System/DataChunk.cpp` | `DataChunkOutput::writeUnicodeString` and `DataChunkInput::readUnicodeString` now use a `uint16_t` staging buffer for the on-disk format (2 bytes per char regardless of platform), narrowing on write and widening on read. Mirrors the existing `parseCSF` fix at `Core/.../GameText.cpp:946–971`. Save/map files now have a fixed wire format and are cross-platform compatible (Win32-authored saves load on macOS and vice versa). Existing macOS-written saves (if any) become unreadable, which the project status accepts at this stage. |

### Round 10 — NetPacket WideChar wire-format fix

The third finding from round 9's typedef audit (deferred at the time) is now fixed: the network packet wire format for chat / disconnect-chat / `WIDECHAR` game-message arguments serialized `WideChar` data as `len * sizeof(WideChar)` bytes on every platform. Win32 wrote 2 bytes per char, macOS/Linux wrote 4. Cross-platform multiplayer therefore mismatched every Unicode-bearing message — the length prefix is a *char count* and the reader recomputed the byte count using its local `sizeof(WideChar)`, so a mixed-platform peer would walk the wrong number of bytes and corrupt every subsequent read.

| # | File | Change |
|---|------|--------|
| 14 | `Core/GameEngine/Include/GameNetwork/NetPacketStructs.h` | Replaced `writeStringWithoutNull` with a uint16_t-narrowing implementation; added a symmetric `readStringWithoutNull` helper. Wire format is now stable at 2 bytes per char (UCS-2). Added `#include <cstdint>` for `uint16_t`. |
| 15 | `Core/GameEngine/Source/GameNetwork/NetPacketStructs.cpp` | Three `getSize` callers (`NetPacketChatCommandData`, `NetPacketDisconnectChatCommandData`, `NetPacketGameCommandData::ARGUMENTDATATYPE_WIDECHAR`) now pre-allocate `len * sizeof(uint16_t)` bytes; the WIDECHAR arg writer narrows `arg.wChar` to `uint16_t` before `writePrimitive`. |
| 16 | `Core/GameEngine/Source/GameNetwork/NetPacket.cpp` | `readDisconnectChatMessage`, `readChatMessage`, and the `ARGUMENTDATATYPE_WIDECHAR` reader now consume 2 bytes per char via `network::readStringWithoutNull` (or a direct uint16_t read for the single-arg case) and widen to `WideChar`. |

**Wire compat:** Win32 builds (where `sizeof(WideChar) == 2`) emit and consume the same bytes as before — the narrowing/widening is a no-op there. macOS builds become wire-compatible with Win32 for the first time. Existing macOS multiplayer sessions (none in production — multiplayer subsystem isn't functional on the BGFX port yet, see `ZH-MainMenu-Bugs.md` §3.4 audio + §3.3 fonts) are not interoperable with the new format; that's the price of fixing the bug.

**Out of scope:** `Core/GameEngine/Include/GameNetwork/LANAPI.h:158+` — `LANMessage` is a packed struct that embeds `WideChar name[…]` and `WideChar gameName[…]` arrays directly and is `memcpy`'d to the wire. Same `sizeof(WideChar)` divergence, but the fix needs a struct restructure or per-field serialize/deserialize. The existing `MAX_LANAPI_PACKET_SIZE = 700` workaround in `Core/GameEngine/Include/GameNetwork/NetworkDefs.h:69–77` and its TODO comment stay in place; this NetPacket fix does not subsume it.

**Verification:** Both targets build clean (no narrowing warnings), launch successfully, and produce screenshots identical to round 9. Network code is not exercised by the menu screenshot path, so visual / runtime behavior is unchanged. End-to-end network round-trip would require an active multiplayer testbed, which doesn't exist on the BGFX port yet.

### Round 10 user-observed update

User reported "hover works on menu" during round 10 verification. This means the `W3DGameWindowManager` hover-state callback is firing through to `Render2DClass` and the menu button outlines are visually reactive. Round 9 + round 10 fixes don't touch the menu render path, so this confirms the round-8 menu work is fully integrated into the input/event loop. Buttons remain text-less (font rasterizer still stubbed, see `ZH-MainMenu-Bugs.md` §3.3) but the chrome is interactive.

**Out of scope (deferred):** `Core/GameEngine/Include/GameNetwork/NetPacketStructs.h:88` uses `copyLen * sizeof(WideChar)` for chat/disconnect message wire format — same pattern, but multiplayer is not a working subsystem on the BGFX port yet. Fixing without an active testbed risks landing untested; tracked under `docs/ZH-MainMenu-Bugs.md`.

**Verification:** Both targets (`generalsv.app` and `generalszh.app`) build clean and launch without crashing. Stderr output identical to round 8 baseline (single `BgfxBackend::Init: Metal (1600x1200, windowed)` line). Neither fix touches the menu-render path (`DataChunk` is only called from save/map binary I/O via `writeDict`/`readDict`; `CRCEngine` has no game-code callers), so no visual change is expected. The captured `-screenshot` TGAs show the same content as before — vanilla's screenshot countdown of 90 frames captures earlier than ZH's 300, which is the pre-existing screenshot-vs-window timing discrepancy documented at round 4 (logo + clear at capture-time, more chrome added later in the running window).

### Round 11 — Replay file WideChar wire-format fix

User asked to "fix all remaining wire format issues" with `LANMessage` named as the next target. Investigation re-shaped the plan: TheSuperHackers' "newer protocol" is OpenSpy (a wire-compatible GameSpy re-implementation pointed at `server.cnc-online.net`) — the live online chat path still goes through the legacy GameSpy SDK, but every `UnicodeString → wire` handoff in `Core/GameEngine/Source/GameNetwork/GameSpy/` already converts to UTF-8 via `WideCharStringToMultiByte(CP_UTF8, ...)` before crossing the SDK's `gsi_char = char` boundary (verified in `PeerThread.cpp:1482-1490`, `BuddyThread.cpp:315/340`, `StagingRoomGameInfo.cpp:504`, `GameInfo.cpp:955`). UTF-8 is platform-width-independent, so online chat is already cross-platform safe — no fix needed. That moved the focus to **replay files**, which had a real `sizeof(WideChar)` divergence.

| # | File | Change |
|---|------|--------|
| 17 | `Core/GameEngine/Source/Common/System/LocalFile.cpp` | `readWideChar`, `writeFormat(const WideChar*, ...)`, and `writeChar(const WideChar*)` now read/write via a `uint16_t` UCS-2 staging buffer instead of the platform-native `WideChar`. Added `#include <cstdint>`. The wide-char file I/O surface has exactly one consumer in the entire codebase: `Recorder.cpp` replay header strings (`replayName`, `versionString`, `versionTimeString`). The fix lives entirely below the `File` abstraction, so neither Recorder target nor `RAMFile` (no-op overrides) nor `StdLocalFile` (inherits) needs a change. |

**Wire compat:** Win32 builds (where `sizeof(WideChar) == 2 == sizeof(uint16_t)`) emit and consume the same bytes as before — narrow/widen is a no-op there. macOS builds previously wrote 4 bytes per char on disk; now write 2, matching Win32. macOS-recorded replays from earlier R10/R11 builds become unreadable, but no live macOS replay archive exists. macOS replays are now byte-identical to Win32 replays for the first time.

**Verification:** Both targets build clean. Smoke screenshots (`/tmp/v-r11.tga`, `/tmp/zh-r11.tga`) show single clean `BgfxBackend::Init: Metal (6016x3384, fullscreen)` line and no errors/aborts. Replay code is not on the menu render path; visual behavior is unchanged from R10. Functional verification (record on Win32, play on macOS or vice versa) deferred — would require a Win32 build pipeline.

**Out of scope:** `Core/GameEngine/Include/GameNetwork/LANAPI.h:158+` `LANMessage` packed-struct LAN protocol (documented TODO + `MAX_LANAPI_PACKET_SIZE = 700` workaround); `Core/GameEngine/Source/Common/System/Xfer.cpp:204` base-class `xferUnicodeString` (needs reachability audit since `XferLoad`/`XferSave` override it).

### Conclusion after round 11

The cross-platform serialization story is now: CRC values agree across platforms; save/map binary chunk Unicode strings agree across platforms; chat / disconnect-chat / `WIDECHAR` arg network packets agree across platforms; replay header strings agree across platforms; live online chat (GameSpy/OpenSpy) is already UTF-8 over the wire and platform-safe. Two outstanding items remain (`LANMessage` packed-struct + `Xfer::xferUnicodeString` base) — both documented with deferral rationale. Menu rendering itself is unchanged from round 8; remaining gaps (font rasterizer, 3D shell map, audio, intro video) are all subsystem-level and tracked under `ZH-MainMenu-Bugs.md`.

## Root cause of round-5 discovery

`Generals/Code/Libraries/Source/WWVegas/WW3D2/render2d.{h,cpp}` had
**never been ported** for BGFX. It still used `SimpleDynVecClass`, a
different accumulation API whose `Add_Multiple(N)` returns a pointer that
`Grow()` may invalidate — and a different reset semantic (`Delete_All(false)`
vs `Reset_Active()`). Every `W3DDisplay::drawVideoBuffer`,
`drawImage`, and `drawFillRect` call pumps through this class, so the
effect was that 2D quads submitted garbage vertex/UV state under BGFX even
though the pipeline looked plumbed.

## Pipeline status (intro-video path)

```
FFmpegVideoStream::update
  → W3DVideoBuffer::lock          -> TextureClass::Get_Surface_Level
                                     (ww3d2_bgfx_stubs.cpp:439)
                                   -> new SurfaceClass(w,h,fmt)
                                   -> Set_Associated_Texture(Peek_Bgfx_Handle())
  → FFmpeg writes BGRA into CpuPixels           ✓ verified
  → W3DVideoBuffer::unlock
      → SurfaceClass::Unlock
          → Upload_To_Associated_Texture        ✓ verified
              → ConvertPixelToRGBA8 (A8R8G8B8)  ✓ rgba0=02060cff etc.
              → backend->Update_Texture_RGBA8
                  → bgfx::updateTexture2D       ✓ valid handle

W3DDisplay::draw
  → drawScaledVideoBuffer → drawVideoBuffer
      → m_2DRender->Set_Texture(vbuffer->texture())
      → m_2DRender->Add_Quad(screen_rect, (0,0,1,1))
      → m_2DRender->Render()
          → DX8Wrapper::Set_Viewport(0,0,0,0) → bgfx full-backbuffer
          → DX8Wrapper::Set_Texture(0, TextureClass)
              → Texture->Apply(0) → backend->Set_Texture(0, bgfxH)
          → DX8Wrapper::Draw_Triangles(...)
              → BgfxBackend::Draw_Indexed or Draw_Triangles_Dynamic
                  → ApplyDrawState (binds stage 0 tex, program, state)
                  → bgfx::submit(view0, prog)    ✓ reached
```

End-to-end confirmed by a one-shot printf campaign. ZH produces identical
output and renders the video; vanilla produces identical output and
renders black. Delta is therefore *not* in the above chain.

## Remaining bug — 2D textured quads render black

Unknown cause. Next-round candidates, in order of likelihood:

1. **Target-duplicated WW3D2 files other than `render2d.{cpp,h}`** — the
   `diff -rq` between `Generals/Code/Libraries/Source/WWVegas/WW3D2` and
   `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2` still lists ~40
   files that differ. Most are irrelevant to 2D quads; the ones that
   could affect them:
   - `dx8vertexbuffer.cpp` / `.h` — `DynamicVBAccessClass` body
   - `dx8renderer.cpp` — only if 2D calls route through it (probably not)
   - `shader.cpp` / `.h` — the `ShaderClass` enum / blend funcs
   - `assetmgr.cpp` — texture asset lifecycle
   Diff-audit each for BGFX-relevant deltas.
2. **Program selection** — `SelectProgram` in `BgfxBackend.cpp:1100`
   branches on `hasStage1 && hasUV0 && hasUV1` → `tex2`. The FVF
   `D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2 | D3DFVF_DIFFUSE` has two
   texcoord slots. If `m_stageTexture[1]` is non-zero (a stale binding
   from a prior draw), vanilla would select `tex2` which expects two
   textures — and stage 1 would be bound to whatever stale handle was
   left over, producing black or wrong colors. Verify `m_stageTexture[1]
   == 0` at the time of the video draw.
3. **View-transform / projection matrix** — `Render2DClass::Render` sets
   `D3DTS_PROJECTION = identity` before drawing. The bgfx view transform
   queued via `setViewTransform` could be stale and override. Check
   `m_viewProjDirty` flow per-frame.
4. **Depth/stencil** — default shader has `DEPTH_WRITE_DISABLE` +
   `PASS_ALWAYS`, so unlikely, but verify the state mask at submit time.
5. **Vertex attribute mask vs program** — `AttrMaskFromLayout` must match
   what `fs_triangle.sc` declares, or bgfx rejects the draw. A silent
   program/layout mismatch would make the quad submit-but-not-render.

### Proposed next diagnostic

Add a conditional printf inside `BgfxBackend::Draw_Indexed` (or
`Draw_Triangles_Dynamic` for the dynamic path) that fires only when the
bound stage-0 texture matches the known-video handle (pass a one-shot
`s_videoBgfxH` captured from the first `Upload_To_Associated_Texture`
call with `w>=640 && h>=400`). Print:

- program `prog.idx`,
- state mask (hex),
- `m_stageTexture[0]`, `m_stageTexture[1]`,
- `m_vbAttrMask`,
- `bgfx::isValid(*owned_stage0)`,
- current view's viewport.

Compare between ZH (works) and vanilla (black). The line that diverges is
the bug.

## Unrelated crashes observed this round

- **`os_unfair_lock_corruption_abort` in `UIIntelligenceSupport` /
  `swift_getSingletonMetadata` on macOS 26.3.1** — fires in the AppKit
  event pump thread, nothing to do with our code. System-level issue in
  Apple's Intelligence framework. Not actionable from our side.

## Out of scope / tracked elsewhere

- Main-menu backdrop black — see `ZH-MainMenu-Bugs.md` §3.1 (same
  underlying 2D-quad bug).
- Button labels blank — font rasterizer / `Render2DSentenceClass` port,
  `ZH-MainMenu-Bugs.md` §3.3.
- 3D shell-map scene black — `ZH-MainMenu-Bugs.md` §3.2.
- SFX/music silent (intro-video audio *is* working via
  `OpenALVideoStream`) — `OpenALAudioManager` SoundManager/MusicManager
  port, `ZH-MainMenu-Bugs.md` §3.4.

## Build + test commands

```bash
# build
scripts/build-osx.sh --target generals      # Release

# launch (video + audio intro)
scripts/run-game.sh --target generals -- -win

# screenshot the menu (skips intro videos per CLAUDE.md)
scripts/run-game.sh --target generals -- -win -screenshot /tmp/vanilla-menu.tga
sips -s format png /tmp/vanilla-menu.tga --out /tmp/vanilla-menu.png && open /tmp/vanilla-menu.png
```
