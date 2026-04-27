# HiDPI Font Readability (macOS BGFX build)

Last updated: 2026-04-27.

Tracks the font/text-rendering fixes applied to the `render2dsentence_bgfx`
pipeline so small UI text stays crisp and legible on Retina displays. Related
to the ZH main-menu backdrop fix landed in the same iteration — see
[ZH-MainMenu-Bugs.md](ZH-MainMenu-Bugs.md) for that side.

## Problem

The engine's UI is authored against an 800×600 logical design space. On a
macOS Retina fullscreen window (e.g. 6016×3384 physical / 1440×900 logical),
that design space is stretched ~7.5× horizontally. Two symptoms fell out of
that:

1. **Pixelation** — glyphs were rasterized by `stb_truetype` at the
   authored point size, then scaled up by the 2D pipe. At 8–10pt, the
   atlas bitmap only held ~12–16 device pixels of glyph edge, which
   magnified into a blurry 60+ physical pixels on screen.
2. **Tiny type at comfortable viewing distance** — even crisp glyphs are
   unreadable when an 8pt label only occupies ~5 mm on a 27" 4K display.
   The FPS counter and dialog body text ("Are you sure you want to
   exit?") were the worst offenders.

Both are consequences of running a 2003 800×600 design against a
modern high-density display without DPI-aware asset or layout
decisions.

## Fix summary

All changes are in
`Core/Libraries/Source/WWVegas/WW3D2/render2dsentence_bgfx.cpp`.

### 1. Two-scale rasterization (crispness)

Glyphs are now rasterized at the physical-pixel density while layout
metrics stay in logical-pixel space.

- `FontImpl` gained `rasterScale` (logical → physical ratio),
  `rasterStbScale` (stb scale to dense atlas pixels), `rasterScaleFinalized`,
  and `atlasW` / `atlasH`.
- `currentFontRasterScale()` reads the bgfx pixel/logical ratio via
  `IRenderBackend::Get_Back_Buffer_Size()` and
  `Get_Logical_Resolution()`.
- `atlasEdgeForScale(rasterScale)` sizes the atlas to fit the denser
  glyphs (256 → 512 → 1024 as scale grows).
- `rasterizeGlyph` calls `stbtt_MakeGlyphBitmap` with
  `f->rasterStbScale` and stores the dense bitmap into the atlas; the
  quad UV / draw metrics use `f->scale` (logical).

### 2. Deferred atlas binding (decouple metrics from draw)

Earlier, `Initialize_GDI_Font` allocated the atlas and finalized the
raster scale immediately. That locked the font to a 1.0× scale if the
font was initialized *before* the bgfx backend came up — which is
exactly what `W3DDisplayString::computeExtents` →
`Get_Formatted_Text_Extents` does during early INI load.

- `ensureFontDrawReady(f)` is now called at the top of
  `Build_Sentence`. By then a draw is imminent, so the backend is up
  and the raster scale can be sampled correctly.
- `Initialize_GDI_Font` only sets `rasterScale = 1.0`,
  `rasterStbScale = scale`, `rasterScaleFinalized = false`, and
  defers atlas allocation.
- `glyphAdvanceLogical()` / `glyphLogicalWidth()` return metrics
  without touching the atlas, so extents queries can run before the
  backend exists.

### 3. HiDPI point-size boost (readability)

Small fonts are scaled up at `Initialize_GDI_Font` entry when the
backend reports a HiDPI ratio. The current curve (replacing an earlier
static 1.5× for ≤10pt):

```cpp
const float hidpiScale = currentFontRasterScale();
if (hidpiScale >= 1.5f && point_size > 0 && point_size < 14) {
    const float density = std::min((hidpiScale - 1.0f) * 0.7f, 0.8f);
    const float sizeWeight = 1.0f - std::min((point_size - 6) / 12.0f, 0.7f);
    float boost = 1.0f + density * (0.6f + 0.6f * sizeWeight);
    const float cap = (point_size >= 11) ? 1.3f : 1.8f;
    boost = std::min(boost, cap);
    point_size = static_cast<int>(point_size * boost + 0.5f);
}
PointSize = point_size;
```

Key properties:

- **Fires only at HiDPI** (`hidpiScale >= 1.5`). 1× displays are left
  alone, so non-Retina users see no regression.
- **Only affects small fonts** (`point_size < 14`). Menu buttons and
  headlines (14pt+ authored) are untouched, so menu chrome does not
  overflow. Earlier the gate was `<= 14`, which caught 14pt menu
  button fonts and made boosted glyphs spill outside the button
  image. Bumped to strict-less-than on 2026-04-27 as a layout-safety
  measure. (Note: the user-visible "only the middle of the button is
  clickable" regression that prompted this audit turned out to be a
  separate mouse-coord-scaling bug in SDLGameEngine.cpp — see
  comment block above `refreshMouseScale()` there. The font gate
  change is independently defensible but was not the click fix.)
- **Smaller fonts get a bigger boost.** `sizeWeight` pushes 6–8pt
  harder than 12–14pt, so the FPS counter (8pt) gets more growth than
  dropdown labels (12pt).
- **Two-tier cap: 1.8× for ≤10pt, 1.3× for 11–13pt.** The 11–13pt
  band is dominated by widget labels (push-button captions, dropdown
  titles) authored to fit specific .wnd boxes at 800×600; over-
  boosting them past ~1.3× re-creates the click-area regression even
  inside the gate. ≤10pt fonts (FPS counter, dialog body) keep the
  original 1.8× ceiling so the readability win is preserved.

Only `point_size` is boosted — all downstream stb math
(`stbtt_ScaleForPixelHeight`, vertical metrics, atlas pixels) then
sees the larger size automatically.

### 4. Combo-box title clipping (containment)

The boost is permanent on the font once `Initialize_GDI_Font` runs, so
boosted line height inflates the rendered glyph extent for every
`DisplayString` that uses that font. Authored .wnd layouts in
800×600 logical space did not budget for this, so glyph descenders
can overflow a gadget's authored rect into the gadget below.

The skirmish "Players" column was the worst case: 8 player-slot combo
boxes stacked vertically with no gap. `W3DGadgetComboBoxDraw` /
`W3DGadgetComboBoxImageDraw` drew the closed-state title text without
ever calling `setClipRegion`, so descenders from one slot's title
visually merged with the next slot's title (e.g. "Easy Army"
overlapping "Open"). Fixed by setting the title's clip region to the
combo box's authored screen rect just before each `title->draw`:

```cpp
IRegion2D titleClip;
titleClip.lo.x = x;
titleClip.lo.y = y;
titleClip.hi.x = x + size.x;
titleClip.hi.y = y + size.y;
title->setClipRegion( &titleClip );
```

`W3DDisplayString::setClipRegion` forwards through to
`Render2DSentenceClass::Set_Clipping_Rect`, so the BGFX text path
honours it. Same pattern is already in use by `W3DStaticText`
(`drawStaticTextText`) and per-cell in `W3DListBox`
(`drawListBoxText`); combo boxes were the gap.

`W3DListBox::drawListBoxText` did NOT need a matching change — its
per-cell `setClipRegion(&columnRegion)` (at the text-draw branch)
already scissors each row's text to that row's `listData[i].height`,
so within a single open dropdown list the boosted glyphs are
contained.

### 5. Static text — skip wrap when the unwrapped line fits

`W3DStaticText::drawStaticTextText` always called
`text->setWordWrap(size.x - 10)` before measuring. With a HiDPI-boosted
font, even labels authored to fit on one line at 800×600 (e.g.
`Starting Cash`) overflow `size.x - 10` and wrap mid-word
(`Starting Ca\nsh`).

Fixed by probing the unwrapped width first and only enabling wrap
when the text genuinely overflows:

```cpp
text->setWordWrap(0);
text->getSize(&textWidth, &textHeight);
if (wordWrap > 0 && textWidth > wordWrap)
    text->setWordWrap(wordWrap);
```

Multi-line / intentionally-wrapped labels (exit-dialog body text,
long localized strings) still wrap because `textWidth > wordWrap`
holds for them. Short labels stop being mangled.

### 6. Push-button title clipping (containment)

Defense in depth, not the click-bug fix. The authored-rect-vs-
boosted-glyph mismatch that motivated §4 (combo box title clipping)
applied symmetrically to `W3DGadgetPushButtonDraw` /
`…ImageDraw` / `…ImageDrawOne` / `…ImageDrawThree` — all of which
delegate to the shared `drawButtonText` helper, and none of which
called `setClipRegion` before `text->draw`. Even with the boost
contained by §3's tightened gate, an authored .wnd label whose
unwrapped width exceeds the button rect would still leak glyphs
outside.

`drawButtonText` now sets the title's clip region to the button's
authored rect just before `text->draw`:

```cpp
IRegion2D buttonClip;
buttonClip.lo.x = origin.x;
buttonClip.lo.y = origin.y;
buttonClip.hi.x = origin.x + size.x;
buttonClip.hi.y = origin.y + size.y;
text->setClipRegion( &buttonClip );
```

This mirrors the W3DStaticText (§5) and W3DComboBox (§4) pattern, so
any future change to font sizing or to .wnd authoring cannot
re-create the click-mismatch class of bug — visible text and
hit-test rect are guaranteed coincident.

## Files touched

| File | Role |
|------|------|
| `Core/Libraries/Source/WWVegas/WW3D2/render2dsentence_bgfx.cpp` | All font pipeline changes above |
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Adds virtual `Get_Back_Buffer_Size` / `Get_Logical_Resolution` with `{0,0}` defaults |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Overrides the two accessors to expose `m_width`/`m_height` + `m_logicalW`/`m_logicalH` |
| `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DComboBox.cpp` | `setClipRegion` on closed-state title in both Draw + ImageDraw functions |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DComboBox.cpp` | mirror of above |
| `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DStaticText.cpp` | probe unwrapped extent before enabling wrap, so single-line labels stop wrapping mid-word under HiDPI boost |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DStaticText.cpp` | mirror of above |
| `Generals/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DPushButton.cpp` | `setClipRegion` on title in `drawButtonText` so boosted glyphs cannot spill outside the button hit-test rect |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/GUI/Gadget/W3DPushButton.cpp` | mirror of above |

Related (same iteration, not font-specific — kept here for context):

| File | Role |
|------|------|
| `GeneralsMD/Code/GameEngine/Source/GameClient/GameClient.cpp` | Post-INI redirect of `MainMenuBackdrop` MappedImage to `ChallengeBackgroundMinSpec.dds` so ZH stops rendering vanilla's tank-battle backdrop |
| `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` | Port of vanilla's UV-zero substitution so a degenerate `(0,0,0,0)` UV rect maps to `(0,0,1,1)` — covers the redirected backdrop Image |
| `Generals/Code/GameEngine/Source/Common/GameLOD.cpp`, `GeneralsMD/.../GameLOD.cpp` | `#ifndef RTS_RENDERER_DX8` force `m_shellMapOn = false` — routes both targets to the static backdrop path because the 3D shellmap scene isn't ported to bgfx yet |

## Verification

1. **Build both targets:**
   ```
   cd build_bgfx && make -j8 g_generals z_generals
   ```

2. **Fullscreen screenshot each target:**
   ```
   ./build_bgfx/Generals/generalsv.app/Contents/MacOS/generalsv     -screenshot /tmp/v.tga
   ./build_bgfx/GeneralsMD/generalszh.app/Contents/MacOS/generalszh -screenshot /tmp/zh.tga
   ```

3. **Readable FPS counter.** Crop `(0,0,600,100)` from each TGA — the
   `30(30) HH:MM:SS` string should be visibly larger than before and
   have clean glyph edges (no blur, no aliasing on diagonals).

4. **Exit dialog body text.** Click `EXIT GAME` → `Are you sure you
   want to exit?` body text should be comfortably readable at typical
   viewing distance.

5. **No menu regression.** The six right-side main menu buttons (14pt+
   labels) must not visibly grow — they sit above the `point_size <= 14`
   gate. Confirm labels still fit their button boxes.

6. **Windowed non-HiDPI.** With `-win` on a 1× external display, the
   boost gate should fail (`hidpiScale < 1.5`) and fonts should render
   at their authored sizes. Regression-tested via `-win -screenshot`.

## Known limitations / out of scope

- Fonts > 14pt stay at authored size. Menu buttons and headlines
  already read comfortably on Retina; boosting them risks layout
  breakage for little benefit.
- Boost is capped at 1.8× per-font. Very dense displays (> ~3× pixel
  ratio) will still read somewhat small at 8pt, but going past 1.8×
  starts clipping labels into their widget borders.
- Localized fonts (JP/KR/CN) not yet exercised on this path. The stb
  code path supports them; the HiDPI boost is purely point-size based
  so it will apply uniformly.
- Full W3D→bgfx port of the animated 3D shellmap (ZH's authored main
  menu background) is deferred — tracked under
  [ZH-MainMenu-Bugs.md](ZH-MainMenu-Bugs.md) §3.2. Until then the
  static ChallengeBackgroundMinSpec fallback is what ZH users see.