# Vanilla Generals (macOS BGFX) — Status

Last updated: 2026-04-24 (round 8 — main menu now renders).

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
- Button labels missing — font rasterizer stubbed (§3.3).
- 3D shell-map scene black — separate from 2D quad path (§3.2).
- Background music silent — `OpenALAudioManager` gaps (§3.4).
- Intro video still renders black — confirmed not a 2D-quad issue (audio plays, frames upload). Likely the same upstream submission gap that one diagnostic at this round did NOT pursue (only 1 dynamic draw/frame fires at the steady menu state on both targets — most UI elements may bypass `Draw_Triangles_Dynamic` entirely; needs a separate investigation pass).

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
