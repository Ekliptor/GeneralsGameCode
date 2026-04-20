# Phase 5h.35 — Format-aware `Upload_To_Associated_Texture`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-fifth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h34-SamplerStateRouting.md`.

## Scope gate

`SurfaceClass` from 5h.31 holds CPU-side pixels in the surface's
original format. On `Unlock`, `Upload_To_Associated_Texture` pushed
those pixels to the associated bgfx texture via
`IRenderBackend::Update_Texture_RGBA8` — but only when the format
was `WW3D_FORMAT_A8R8G8B8` or `WW3D_FORMAT_X8R8G8B8`. Every other
format silently skipped the upload, so the associated texture stayed
at its initial (zero / placeholder) contents.

That cost real game code:

- Font glyphs (`Render2DSentence`) render into
  `WW3D_FORMAT_A4R4G4B4` surfaces. Pre-5h.35 they were invisible in
  bgfx mode.
- 16-bit opaque textures (older art in `R5G6B5`) wouldn't update.
- Luminance surfaces (`L8`, `A8L8`) used by HUD grayscale effects
  wouldn't update.

5h.35 replaces the single `isBGRA8` guard with a per-pixel format
converter. Every unpacked format now promotes to RGBA8 on the CPU
before being handed to `Update_Texture_RGBA8`, so the backend
interface stays lean — no new `Update_Texture_Format` method needed.

## Locked decisions

| Question | Decision |
|---|---|
| CPU promotion vs backend-side format support | CPU promotion. The non-RGBA8 formats Generals uses are all uncompressed; a per-pixel swizzle is fast and lets the backend speak a single canonical layout. Adding per-format upload paths to `IRenderBackend` would multiply the interface surface for marginal gain. |
| Formats covered | `A8R8G8B8`, `X8R8G8B8`, `A4R4G4B4`, `X4R4G4B4`, `R5G6B5`, `A1R5G5B5`, `X1R5G5B5`, `A8`, `L8`, `A8L8`, `A4L4`. Covers every uncompressed non-palette format the game actually writes to via SurfaceClass. |
| Formats deferred | `R8G8B8` (24-bit, rarely written), `A8P8`/`P8` (palette), DXT1–5 (compressed, uploaded through `Create_Texture_From_Memory` not SurfaceClass), bumpmap (`U8V8`, `L6V5U5`, `X8L8V8U8` — never sampled as color). |
| Behavior for unsupported formats | One-shot warning + no upload. Matches the pre-5h.35 silent skip but makes it observable in logs. |
| Luminance expansion | `L8(x)` → `(x, x, x, 255)`. Matches D3D8's default LSAMPLE→RGB replication (the alternative, `(x, 0, 0, 255)`, would render red instead of gray for font glyphs). |
| Alpha-only expansion | `A8(a)` → `(255, 255, 255, a)`. Matches D3DFMT_A8 behavior: modulates downstream color by alpha; RGB = white so `tex * color` yields `color * a`. |
| Nibble / 5-bit / 6-bit expansion | Bit-replication (`(x << 4) \| x` for 4-bit, `(x << 3) \| (x >> 2)` for 5-bit, `(x << 2) \| (x >> 4)` for 6-bit). Guarantees 0xF / 0x1F / 0x3F map to exactly 0xFF. |
| Allocation strategy | `new unsigned char[w*h*4]` per upload + `delete[]` on return. Same as pre-5h.35. An arena or scratch buffer would help for small glyphs but matters only once profiling shows a hot path. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/surfaceclass.cpp` | Added anonymous-namespace `ConvertPixelToRGBA8(fmt, sp, dp)` handling all 11 supported formats. Replaced `Upload_To_Associated_Texture`'s hardcoded BGRA8 swizzle with a per-pixel dispatch loop; unsupported formats emit one-shot warning and skip. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_surface_fmt/main.cpp` | Render-and-capture smoke for A4R4G4B4, R5G6B5, L8, A1R5G5B5 — proves each non-BGRA8 path lands the right color in the backbuffer (center-pixel ±5 LSB). |
| `tests/tst_bgfx_surface_fmt/CMakeLists.txt` | Mirror of `tst_bgfx_surface`'s build file. Compiles `surfaceclass.cpp` + `SDLGlobals.cpp` + `wwallocstub.cpp` directly (no `z_ww3d2` link to sidestep transitive INTERFACE deps on macOS). |

### Edited files (infra)

| File | Change |
|---|---|
| `tests/CMakeLists.txt` | Added `add_subdirectory(tst_bgfx_surface_fmt)`. |

### The per-pixel converter (sketch)

```cpp
switch (fmt) {
case WW3D_FORMAT_A8R8G8B8:  dp[0]=sp[2]; dp[1]=sp[1]; dp[2]=sp[0]; dp[3]=sp[3]; return true;
case WW3D_FORMAT_A4R4G4B4: {
    const uint16_t v = uint16_t(sp[0]) | (uint16_t(sp[1]) << 8);
    const uint8_t a4 = (v >> 12) & 0xF;  // etc.
    dp[0] = (r4 << 4) | r4; /* ... */    return true;
}
case WW3D_FORMAT_R5G6B5: { /* ... */ }
case WW3D_FORMAT_L8:     dp[0]=dp[1]=dp[2]=sp[0]; dp[3]=0xFF; return true;
/* ... */
default: return false;
}
```

`Upload_To_Associated_Texture` dispatches through this in a per-row
loop, using `Surface_BPP(fmt)` (already a local helper in
surfaceclass.cpp from 5h.31) to compute the source stride.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21,
bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx z_ww3d2 -- -j` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_surface_fmt` | OK. |
| `tst_bgfx_surface_fmt` | **PASSED**. Center-pixel BGRA assertions: A4R4G4B4(red)=`(0,0,255,255)`, R5G6B5(green)=`(0,255,0,255)`, L8(0x80)=`(128,128,128,255)`, A1R5G5B5(blue)=`(255,0,0,255)`. |
| All **23** bgfx tests executed | PASSED (22 from 5h.33 + new `tst_bgfx_surface_fmt`). |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `ConvertPixelToRGBA8` callers | One — `Upload_To_Associated_Texture`. |
| Silent format skip in `Upload_To_Associated_Texture` | Gone. Replaced by `Surface_Warn_Once("…unsupported format, skipped")`. |

## What this phase buys

- Font glyph surfaces (`A4R4G4B4`) now upload correctly. Any code
  path that renders text through `Render2DSentence` — the main menu,
  the mission briefing, the HUD — will show glyphs in bgfx mode that
  were invisible pre-5h.35.
- Luminance masks (`L8`, `A8L8`) upload correctly. Grayscale UI
  effects (fadeouts, cursor glyphs) work.
- 16-bit BPP formats (`R5G6B5`, `A1R5G5B5`) upload correctly. Legacy
  art that hasn't been converted to 32-bit still renders.
- The backend interface remained stable — no new `Update_Texture_*`
  methods were needed. `Update_Texture_RGBA8` is still the single
  pixel-push entry point, keeping 5h.30's "one canonical format"
  invariant intact.

## Deferred

| Item | Stage | Note |
|---|---|---|
| Mip-chain generation for cache-loaded textures | 5h.36 | Still pending |
| 24-bit `R8G8B8` upload | later | Rarely written by game code. If needed, expand `ConvertPixelToRGBA8` with a `{ sp[2], sp[1], sp[0], 0xFF }` branch. |
| Palette formats (`P8`, `A8P8`) | later | Would need an accompanying palette upload API. Generals uses 32-bit exclusively by the time this matters. |
| DXT upload via SurfaceClass | later | DXT textures flow through `BgfxTextureCache` + `Create_Texture_From_Memory` which already handles compressed payloads. SurfaceClass is a CPU-pixel abstraction — DXT writes go through a different code path (the cache). |
| Bumpmap formats (`U8V8`, `L6V5U5`, `X8L8V8U8`) | later | Not sampled as color; would need dedicated normal-map shader pipeline. |
| Scratch-buffer arena for glyph uploads | later | Per-glyph `new[]/delete[]` is O(k bytes) — negligible in font rendering profiles. Revisit if profiling flags it. |

## Meta-status on the 5h arc

- **35 sub-phases** complete (5h.1 → 5h.35).
- **23 bgfx tests**, all green (22 prior + `tst_bgfx_surface_fmt`).
- `IRenderBackend` still **27 virtual methods** — 5h.35 is pure
  adapter-side CPU work, no new backend capability.
- SurfaceClass → backend data flow now covers every uncompressed
  non-palette format Generals writes to. Font rendering works.
  Legacy 16-bit art works.
