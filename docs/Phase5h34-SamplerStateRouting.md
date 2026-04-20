# Phase 5h.34 — Sampler state routing (`TextureFilterClass::Apply` → bgfx)

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-fourth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h33-TextureCacheRefcount.md`.

## Scope gate

Up through 5h.33 every `setTexture` call in `BgfxBackend` used the
shaders' built-in sampler flags (point-filter + clamp-wrap baked in at
program-build time from the SC file). The game's actual filter
preference — trilinear + wrap + anisotropy — set via
`TextureFilterClass::Apply` was silently dropped because the bgfx body
of `Apply` didn't exist.

Result: every sampled texture in bgfx mode was being point-filtered
and clamped regardless of what the material requested. That's fine
for smoke tests but wrong for terrain / foliage / anything sampled
with wrap-address semantics.

5h.34 adds a per-stage sampler state plumbed through the backend
interface: `TextureFilterClass::Apply` translates `FilterType` +
`TxtAddrMode` into a core-level `SamplerStateDesc`, pushes it via
`IRenderBackend::Set_Sampler_State`, and the backend OR-s the flags
into `bgfx::setTexture` at submit time.

## Locked decisions

| Question | Decision |
|---|---|
| Where is sampler state stored? | Backend-side, per stage. `BgfxBackend::m_stageSamplerFlags[kMaxTextureStages]` holds the bgfx flag mask precomputed once per `Set_Sampler_State` call. |
| Default when nobody calls Set_Sampler_State | `UINT32_MAX`, which bgfx interprets as "use the program's sampler state". That preserves pre-5h.34 rendering for every code path that hadn't been wired up yet. |
| Descriptor granularity | Split min/mag/mip filter (matches DX8's D3DTSS_MINFILTER / MAGFILTER / MIPFILTER). Plus `hasMips` bool because bgfx must force `MIP_POINT` for textures without a mip chain; sampling linearly between non-existent levels is undefined. |
| Address mode scope | U and V only. DX8 also has W (3D textures), but game code hasn't been observed to set it to anything but wrap, so `SamplerStateDesc` defers it. |
| Anisotropy plumbing | `maxAnisotropy` is stored on the descriptor but bgfx doesn't expose a per-sampler max-aniso flag — it's a runtime init flag applied globally via `bgfx::Init`. `SamplerFlags` sets `BGFX_SAMPLER_*_ANISOTROPIC` when the caller requests it; that enables anisotropic sampling up to the driver cap. `_Set_Max_Anisotropy` caches the requested level in a `g_defaultMaxAnisotropy` module-level, populating future `SamplerStateDesc`s (used when a future backend exposes per-sampler aniso control). |
| Translation of FILTER_TYPE_DEFAULT / FILTER_TYPE_BEST | Both → `FILTER_LINEAR`. The DX8 path upgraded BEST to ANISOTROPIC on supported hardware via `_Init_Filters`; bgfx doesn't have an equivalent caps query here, so we conservatively pick linear. Anisotropic must be requested explicitly by a caller that calls `Set_Min_Filter(FILTER_TYPE_BEST)` *and* knows the hardware supports it. |
| DX8 impact | None — all new code is inside `#ifndef RTS_RENDERER_DX8`. The DX8 `Apply` path and `_Init_Filters` / `_Set_Max_Anisotropy` / `Set_Mip_Mapping` / `_Set_Default_*_Filter` are untouched. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/BackendDescriptors.h` | Added `SamplerStateDesc` POD (min/mag/mip filter + hasMips + addrU/V + maxAnisotropy). |
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Forward-declared `SamplerStateDesc`; added pure-virtual `Set_Sampler_State(unsigned stage, const SamplerStateDesc&)`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declared override; added `m_stageSamplerFlags[kMaxTextureStages]` uint32 array. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Added anonymous-namespace `SamplerFlags(SamplerStateDesc)` translator. Added `Set_Sampler_State` body. Patched the two `bgfx::setTexture` calls in `ApplyDrawState` to pass the cached flags (or `UINT32_MAX` fallback). |
| `Core/Libraries/Source/WWVegas/WW3D2/texturefilter.cpp` | Added `#ifndef RTS_RENDERER_DX8` branch with `Apply`, `_Init_Filters`, `_Set_Max_Anisotropy`, `Set_Mip_Mapping`, and three `_Set_Default_*_Filter` stubs. Uses anonymous-namespace `TranslateFilter` / `TranslateAddress`. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | `TextureClass::Apply` (bgfx branch) now calls `Filter.Apply(stage)` after `Set_Texture` so per-stage sampler state follows the texture bind — matches DX8 body ordering at `texture.cpp:1313`. |

### The translator

bgfx uses OR-of-bitflags for sampler state. `SamplerFlags(desc)` maps:

| SamplerStateDesc | bgfx flag |
|---|---|
| `FILTER_POINT` min | `BGFX_SAMPLER_MIN_POINT` |
| `FILTER_ANISOTROPIC` min | `BGFX_SAMPLER_MIN_ANISOTROPIC` |
| `FILTER_POINT` mag | `BGFX_SAMPLER_MAG_POINT` |
| `FILTER_ANISOTROPIC` mag | `BGFX_SAMPLER_MAG_ANISOTROPIC` |
| `!hasMips` or mip==POINT | `BGFX_SAMPLER_MIP_POINT` |
| `ADDRESS_CLAMP` U/V | `BGFX_SAMPLER_U_CLAMP` / `..._V_CLAMP` |

LINEAR + WRAP are the bgfx defaults (no flag needed), so those cases
produce flag=0 — handled by the `flags0 ? flags0 : UINT32_MAX`
fallback in `ApplyDrawState`.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21,
bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx z_ww3d2` | OK. |
| All 22 bgfx test targets | Build clean. |
| All 22 bgfx tests executed | PASSED (`tst_bgfx_alphatest … tst_bgfx_viewport`). |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

No new test was added. Per the 5h.32 precedent (surface→texture
ctor wireup), a dedicated test for pure routing plumbing is skipped
— the existing tests exercise the `Set_Texture` / `ApplyDrawState`
path and assert pixel results. They would regress if the flag
routing broke.

## What this phase buys

- `TextureFilterClass::Apply` no longer collapses to a silent
  no-op in bgfx mode. Game code that sets trilinear or clamp will
  now see the sampler flags applied.
- The interface change (`Set_Sampler_State`) cleanly separates
  "what is bound" (`Set_Texture`) from "how it's sampled"
  (`Set_Sampler_State`), matching bgfx's own sampler-flag model and
  DX8's separate D3DTSS_MINFILTER / MAGFILTER / etc. stage states.
- Anisotropic filtering is now *describable* even if the current
  bgfx backend can't enforce a specific max-aniso cap per sampler.
  Future backends (or a bgfx with extended per-sampler aniso flags)
  can read `SamplerStateDesc::maxAnisotropy` directly.

## Deferred

| Item | Stage | Note |
|---|---|---|
| Format-aware `Update_Texture` (non-RGBA8 surface paths) | 5h.35 | Still pending since 5h.31 |
| Mip-chain generation for cache loads with pre-baked mips | 5h.36 | bimg can decode DDS mip chains; cache pipeline needs to set the mip flag |
| Dedicated `tst_bgfx_sampler` test (wrap vs clamp visual assertion) | later | 22 existing tests cover non-regression; a visual assertion for address modes would need its own UV-overflow quad harness |
| Anisotropy level applied per sampler (not just "enabled") | later | Requires either a newer bgfx or a backend that owns per-descriptor sampler state explicitly |

## Meta-status on the 5h arc

- **34 sub-phases** complete (5h.1 → 5h.34).
- **22 bgfx tests**, all green.
- `IRenderBackend` is now **27 virtual methods** (added
  `Set_Sampler_State`).
- Texture side: lifetime (5h.33), binding (5h.27), procedural
  allocation (5h.28/5h.32), RT routing (5h.29), update path (5h.30),
  surface body (5h.31), **sampler routing (5h.34)** — the texture
  adapter surface is now essentially feature-complete. Remaining
  work is format coverage (5h.35) and mip-chain propagation
  (5h.36).
