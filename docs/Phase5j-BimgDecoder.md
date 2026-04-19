# Phase 5j — bimg DDS / KTX / PNG decoder integration

Companion doc to `docs/CrossPlatformPort-Plan.md`. Ninth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5i-RealTextures.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5j slots in
before it so 5h can rely on a working DDS decode path when bridging
`TextureBaseClass` to bgfx.)

## Objective

Let the bgfx backend ingest the native texture formats the game actually
ships — `.dds` files containing A8R8G8B8 / DXT1 / DXT3 / DXT5 mip chains —
plus anything else bimg decodes (KTX, KTX2, PNG, TGA, EXR, HDR). The
existing Phase 5i `Create_Texture_RGBA8` path stays for cases where the
caller already has raw pixels.

A new `tst_bgfx_bimg` harness builds a minimal uncompressed A8R8G8B8 DDS
**in memory** (128-byte header + pixel data written by hand) and renders
the Phase 5g cube with the decoded result. Generating the DDS in-process
sidesteps the test harness's working-directory assumption — no asset files
on disk.

## Locked decisions

| Question | Decision |
|---|---|
| Decoder library | `bimg::imageParse` — unified entry point that dispatches on magic bytes to `imageParseDds` / `imageParseKtx` / `imageParsePvr3` / `imageParseGnf` / stb_image for PNG/TGA/etc. Already bundled with bgfx.cmake; no extra FetchContent. |
| Linkage | `bimg_decode` target (alongside `bimg`). bimg-core only contains the pixel-format tables and the `ImageContainer` struct; the parsers live in `bimg_decode` along with miniz and lodepng. The split isn't obvious from bimg's public headers, so the CMake add has a comment. |
| Allocator | A single file-scope `bx::DefaultAllocator` inside `Create_Texture_From_Memory`. `ImageContainer` allocations live only between `imageParse` and bgfx upload (then `imageFree` inside the `makeRef` release callback), so even a shared allocator sees short-lived traffic. |
| Lifetime bridge to bgfx | `bgfx::makeRef(image->m_data, image->m_size, releaseCB, imagePtr)` — bgfx keeps the pixel bytes alive until the GPU finishes reading them, then the release callback fires `bimg::imageFree` which frees the `ImageContainer` and its `m_data`. No intermediate copy, no extra memory peak. |
| Format handling | `static_cast<bgfx::TextureFormat::Enum>(image->m_format)` — bimg and bgfx agree on the `TextureFormat::Enum` numbering (both are in bgfx's ecosystem and literally share the same enum order). A 1:1 cast is safe and avoids a switch-of-doom. |
| Mipmap inference | `hasMips = image->m_numMips > 1`. bgfx auto-uses the mip-chain embedded in the memory blob; no separate upload-per-level loop. Sampler filtering flips to trilinear when mips exist, point-sample when not (keeps 16×16 test patterns crisp). |
| API shape | `Create_Texture_From_Memory(data, size)` → `uintptr_t`. Same ownership model as Phase 5i: caller `Destroy_Texture`s, `Shutdown` sweeps the rest. No `mipmap` flag — bimg-provided images either have mips or they don't; the caller can't override that. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Add `Create_Texture_From_Memory(const void* data, uint32_t size) -> uintptr_t` alongside the existing `Create_Texture_RGBA8`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare the override. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `#include <bimg/decode.h>` + `<bx/allocator.h>`. Implement the method: `bimg::imageParse` → `bgfx::makeRef` with a release callback that calls `bimg::imageFree` → `bgfx::createTexture2D(w, h, hasMips, 1, format, flags, mem)` → wrap the resulting handle as a heap-allocated `bgfx::TextureHandle*` and track in `m_ownedTextures` (same bookkeeping as 5i). |
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | `target_link_libraries(corei_bgfx PUBLIC … bimg bimg_decode …)`. The split into two targets upstream is non-obvious — a missing `bimg_decode` surfaces as a linker error on `bimg::imageParse`, not a compile error. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_bimg)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_bimg/main.cpp` | Clones `tst_bgfx_texture` but replaces the raw-RGBA upload with a hand-rolled 128-byte DDS header + 16×16 A8R8G8B8 pattern (deep blue background, orange diagonal stripes every 4 pixels, green corner tiles). Decodes via `Create_Texture_From_Memory`. |
| `tests/tst_bgfx_bimg/CMakeLists.txt` | Same gating as the other 5x tests. |
| `docs/Phase5j-BimgDecoder.md` | This doc. |

### DDS layout written by the test

Standard legacy DDS — no DX10 extension, no compression:

```
0..3    "DDS " magic
4..127  DDS_HEADER (124 bytes)
          +0   dwSize = 124
          +4   dwFlags = CAPS|HEIGHT|WIDTH|PIXELFORMAT = 0x1007
          +8   dwHeight = 16
          +12  dwWidth  = 16
          +16  dwPitchOrLinearSize = 64  (= width × 4)
          +20  dwDepth = 0
          +24  dwMipMapCount = 0         (single level)
          +28  dwReserved1[11] = zeros
          +72  DDS_PIXELFORMAT (32 bytes)
                 dwSize = 32
                 dwFlags = ALPHAPIXELS|RGB = 0x41
                 dwFourCC = 0
                 dwRGBBitCount = 32
                 R/G/B/A masks = 0x00FF0000 / 0x0000FF00 / 0x000000FF / 0xFF000000
          +104 dwCaps = DDSCAPS_TEXTURE = 0x1000
          +108 dwCaps2..dwReserved2 = zeros
128..    pixel bytes in BGRA order (little-endian dword 0xAARRGGBB)
```

Total bytes: 128 + 16 × 16 × 4 = 1152.

### Ownership flow

```
imageParse(bx allocator, dds bytes, dds size)    // owns: ImageContainer + m_data
 ↓
makeRef(m_data, m_size, releaseCB, image)        // bgfx holds refcount
 ↓
createTexture2D(… mem)                            // GPU upload scheduled
 ↓
… frames elapse; bgfx releases `mem`
 ↓
releaseCB(image) → bimg::imageFree(image)         // ImageContainer + m_data freed
```

`bgfx::destroy(handle)` is issued by `BgfxBackend::Destroy_Texture` or
`DestroyPipelineResources`, same as all other owned textures.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK. `libcorei_bgfx.a` now transitively pulls `libbimg.a` + `libbimg_decode.a`. |
| `cmake --build build_bgfx --target tst_bgfx_clear tst_bgfx_triangle tst_bgfx_uber tst_bgfx_mesh tst_bgfx_texture tst_bgfx_bimg` | OK — all six smoke tests link. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `tst_bgfx_clear: PASSED` (regression). |
| `./tests/tst_bgfx_triangle/tst_bgfx_triangle` | `tst_bgfx_triangle: PASSED` (regression). |
| `./tests/tst_bgfx_uber/tst_bgfx_uber` | `tst_bgfx_uber: PASSED` (regression). |
| `./tests/tst_bgfx_mesh/tst_bgfx_mesh` | Placeholder-cube regression — `tst_bgfx_mesh: PASSED`. |
| `./tests/tst_bgfx_texture/tst_bgfx_texture` | Raw-RGBA path regression — `tst_bgfx_texture: PASSED`. |
| `./tests/tst_bgfx_bimg/tst_bgfx_bimg` | Cube visible with DDS-decoded texture (deep blue + orange stripes + green corners) for 3s → `tst_bgfx_bimg: PASSED`. |
| `cmake-build-release` reconfigure with `RTS_RENDERER=dx8` | OK. DX8 game targets still configure — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` / `#include.*<bimg` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `bimg::` calls in `Core/Libraries/` | Zero (only in `BGFXDevice/`). |
| Asset files on disk required by tests | Zero (DDS is generated in-memory). |

## Deferred

| Item | Stage |
|---|---|
| DX8Wrapper → IRenderBackend adapter (TextureBaseClass → bgfx handle via `Create_Texture_From_Memory` with the same DDS bytes DX8 already loads) | 5h |
| Hooking DXT1/3/5 in a real DDS — trivially works once the game's `TextureLoader` is wired up in 5h (bimg handles the FourCC path); no further backend changes needed | 5h |
| Compressed fallback when a Metal / GL device reports `BGFX_CAPS_TEXTURE_COMPARE_LEQUAL == 0` for a format | 5h if observed |
| Async / staged texture upload for big DDS mipchains | later |
| Texture streaming (partial upload, residency tiers) | later |
| Cube maps (`image->m_cubeMap`) and 3D volumes (`image->m_depth > 1`) — bimg parses them, backend currently discards | later, with a real use case |
