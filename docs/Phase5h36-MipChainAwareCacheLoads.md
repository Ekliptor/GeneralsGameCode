# Phase 5h.36 — Mip-chain aware cache loads (`Texture_Mip_Count`)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-sixth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h35-FormatAwareUpdateTexture.md`.

## Scope gate

Post-5h.35 the cache path already propagates pre-baked DDS/KTX mip
chains end-to-end: `BgfxTextureCache::Get_Or_Load_File` calls
`Create_Texture_From_Memory`, which does
`bgfx::createTexture2D(w, h, /*hasMips=*/image->m_numMips > 1, ...)`
and hands the full multi-mip blob through `bgfx::makeRef`. That part
has been working since 5h.3.

The gap 5h.36 fills is *knowing* on the adapter side whether a
loaded file actually carried mips, so `TextureClass::Filter` can be
demoted for files that didn't. Without this:

- A `TextureClass` constructed with `MIP_LEVELS_ALL` (the default)
  leaves `Filter.MipMapFilter = FILTER_TYPE_DEFAULT`.
- 5h.34's `TextureFilterClass::Apply` then pushes
  `SamplerStateDesc { mipFilter=LINEAR, hasMips=true }`.
- If the DDS on disk has only mip level 0, the sampler tells bgfx
  "use linear mip filter" on a 1-level texture → undefined /
  driver-dependent behavior at higher LODs.

5h.36 closes the loop: the backend tracks mip counts per owned
handle and exposes `IRenderBackend::Texture_Mip_Count(handle)`.
`TextureClass::Init` queries it post-load and calls
`Filter.Set_Mip_Mapping(FILTER_TYPE_NONE)` when the loaded texture
has <=1 mips — ensuring the next `Filter.Apply(stage)` emits
`hasMips=false` to the backend.

## Locked decisions

| Question | Decision |
|---|---|
| Where is mip count tracked? | Backend-side, in `std::unordered_map<uintptr_t, uint8_t> m_textureMipCounts`. Populated on creation (`Create_Texture_RGBA8`, `Create_Texture_From_Memory`), erased in `Destroy_Texture` and `DestroyPipelineResources`. |
| Why a map instead of a field on the existing `m_ownedTextures` entries? | `m_ownedTextures` stores `bgfx::TextureHandle*`; widening to a struct would propagate through every callsite. A parallel map is additive and removable. |
| `Create_Texture_RGBA8` with `mipmap=true` mip count | Computed at creation: `ceil_log2(max(w,h)) + 1`. bgfx runtime-generates the chain when we pass `hasMips=true` + RGBA8. The count is what bgfx will end up with after `bgfx::updateTexture2D`/first sample. |
| `Create_Texture_From_Memory` mip count | `image->m_numMips` directly — bimg already computed it during parse. |
| Render target textures | Not tracked — `Create_Render_Target` doesn't pre-create owned textures; its internal RT color attachment isn't exposed via `Texture_Mip_Count`. RT color textures are always 1 mip level anyway. Returning 0 (the "unknown handle" sentinel) is fine for them. |
| Unknown handles | `Texture_Mip_Count(0)` or `Texture_Mip_Count(unregistered)` returns 0. Callers check `<= 1` which treats both 0 and 1 as "no mips" — safer default. |
| TextureClass reconciliation policy | One-way demotion only: "file has no mips" → drop filter to `FILTER_TYPE_NONE`. The opposite case ("file has mips but ctor asked for `MIP_LEVELS_1`") is left alone — the caller explicitly said they don't want mips. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Added pure-virtual `uint8_t Texture_Mip_Count(uintptr_t handle)`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declared override; added `#include <unordered_map>` + `std::unordered_map<uintptr_t, uint8_t> m_textureMipCounts` member. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | `Create_Texture_RGBA8` computes `mips = ceil_log2(max(w,h)) + 1` when `mipmap==true`, else 1, and stores in map. `Create_Texture_From_Memory` stores `image->m_numMips`. `Destroy_Texture` erases the entry. `DestroyPipelineResources` clears the whole map. Added `Texture_Mip_Count` body. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | `TextureClass::Init` post-load: if `Texture_Mip_Count(handle) <= 1`, `Filter.Set_Mip_Mapping(FILTER_TYPE_NONE)`. |
| `tests/tst_bgfx_texcache/main.cpp` | Added an assertion after invariant 1: the hand-built single-mip DDS returns `Texture_Mip_Count == 1`. Includes `IRenderBackend.h` + `RenderBackendRuntime.h`. |

### The reconciliation branch

```cpp
if (handle != 0) {
    if (IRenderBackend* b = RenderBackendRuntime::Get_Active()) {
        if (b->Texture_Mip_Count(handle) <= 1)
            Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
    }
}
```

The Filter's `MipMapFilter` feeds into `TextureFilterClass::Apply`
(5h.34), which calls `Set_Sampler_State(stage, SamplerStateDesc{
hasMips=false, mipFilter=POINT, ... })` when `MipMapFilter ==
FILTER_TYPE_NONE`. The backend's `SamplerFlags` translator then
OR-s `BGFX_SAMPLER_MIP_POINT` into the flags passed to
`bgfx::setTexture`, which is the bgfx idiom for "this texture
has no mips; don't try to sample linearly between levels".

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21,
bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx z_ww3d2 -- -j` | OK. |
| All 23 bgfx test targets built | OK. |
| All 23 bgfx tests executed | PASSED. `tst_bgfx_texcache` now also asserts `Texture_Mip_Count(h1) == 1` on the hand-built single-mip DDS. |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `Texture_Mip_Count` callers | Two — `TextureClass::Init` (production), `tst_bgfx_texcache` (test). |
| `m_textureMipCounts` writes | Three (two creates + Destroy_Texture erase) + one clear in `DestroyPipelineResources`. |

## What this phase buys

- `TextureClass` + `TextureFilterClass` now honor the actual mip
  chain present in the file. A DDS with a baked 10-level mip chain
  uses linear mip filtering; a DDS with only level 0 uses point
  mip filtering. No more undefined-LOD sampling.
- The backend now has a generic "texture metadata" query pattern
  (`Texture_Mip_Count` is the first). If future phases need
  format, dimensions, or other properties post-creation, the same
  handle-keyed-map pattern extends naturally.
- `IRenderBackend` becomes **28 virtual methods** — the
  adapter-level surface for texture lifetime / metadata is now
  complete enough to cover every DX8 TextureClass use case.

## Deferred

| Item | Stage | Note |
|---|---|---|
| `Texture_Dimensions` accessor | later | Would let TextureClass populate Width/Height post-load without guessing. Not blocking; DX8 path reads them from `D3DTexture` which is a DX8-only artifact. |
| Per-handle format accessor | later | Game code currently only branches on format at creation time. |
| Thread-safe mip-count map | later | Matches the texture cache's thread-safety assumption: main thread only. Revisit if the loader thread comes back. |
| In-game verification on real DDS mip chains | later | Unit tests prove the plumbing; an end-to-end scene render with mipped terrain/model textures confirms LOD selection works in practice. |

## Meta-status on the 5h arc

- **36 sub-phases** complete (5h.1 → 5h.36).
- **23 bgfx tests**, all green.
- `IRenderBackend`: **28 virtual methods** (`Texture_Mip_Count`
  added).
- Texture adapter surface is now feature-complete:
  - Creation: RGBA8 / From_Memory / Render_Target (3 paths).
  - Update: Update_Texture_RGBA8 + format-aware surface upload (5h.35).
  - Binding: Set_Texture + Set_Sampler_State (5h.34).
  - Lifetime: cache-refcounted (5h.33) / procedural (5h.28) / RT (5h.29).
  - Metadata: Texture_Mip_Count (5h.36).

The remaining 5h-era work is integration verification rather than
new code.
