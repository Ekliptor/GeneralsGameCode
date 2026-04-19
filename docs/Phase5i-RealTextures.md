# Phase 5i — Real RGBA8 texture upload through IRenderBackend

Companion doc to `docs/CrossPlatformPort-Plan.md`. Eighth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5g-IRenderBackendPath.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter + multi-stage textures — is
still open; sequenced after 5i because it builds on the texture-ownership
model introduced here.)

## Objective

Replace Phase 5g's "bind the built-in 2×2 white placeholder for every draw"
with a real caller-owned texture path: `IRenderBackend::Create_Texture_RGBA8`
uploads pixels to bgfx, returns an opaque handle, and `Set_Texture(stage,
handle)` binds it at `Draw_Indexed` time. `Destroy_Texture` releases it. A
new `tst_bgfx_texture` harness proves the round-trip by rendering the Phase
5g spinning cube with a procedurally-generated 16×16 checkerboard.

The goal is deliberately **not** DDS/KTX decoding yet — that needs `bimg`
integration and DDS mipchain round-tripping, which is best combined with the
5h adapter work where `TextureBaseClass` is already iterating the mipchain.
5i ships just the ownership and binding plumbing.

## Locked decisions

| Question | Decision |
|---|---|
| Handle type | **`uintptr_t`** treated as an opaque cookie. Concretely the backend returns `reinterpret_cast<uintptr_t>(heap_allocated_bgfx::TextureHandle*)`; 0 is the null / unbound sentinel. Heap-allocation avoids the sentinel collision where a valid `bgfx::TextureHandle::idx == 0` would otherwise be indistinguishable from "unbound". |
| Pixel format | **RGBA8 only**, tightly-packed, row-major. Everything the game needs eventually compresses to DXT1/3/5 on-disk, but at bind time the existing DX8 path hands the driver uncompressed when mipmaps are generated at runtime; RGBA8 covers the common case. Compressed-block uploads are 5j alongside the `bimg` DDS parser. |
| Mipmap semantics | `Create_Texture_RGBA8(… mipmap=true)` lets bgfx auto-generate a mipchain and the sampler uses trilinear filtering; `mipmap=false` is point-sampled (crisp procedural patterns survive minification for regression tests). |
| Ownership | Caller owns the handle: `Create_Texture_RGBA8` pairs with `Destroy_Texture`. The backend also tracks every live handle in an internal vector so `Shutdown` can recover anything the caller forgot to release without leaking bgfx resources. |
| Multi-stage | Stage 0 is the only one wired through to the uber programs; `BgfxBackend::Set_Texture(1, …)` is accepted but currently unused (programs only sample `s_texture` at unit 0). Multi-stage / detail blending is 5h. |
| Fallback | If no texture is bound on stage 0 (handle 0 or the bound handle is invalid), the backend falls back to the built-in 2×2 white placeholder so `tex` / `tex_lit` programs always have something to sample. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Replace the placeholder `Set_Texture(unsigned, const void*)` with three methods: `Create_Texture_RGBA8(pixels, width, height, mipmap) -> uintptr_t`, `Destroy_Texture(uintptr_t)`, and `Set_Texture(stage, uintptr_t handle)`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Match new signatures. Add `std::vector<bgfx::TextureHandle*> m_ownedTextures` (tracks live handles for Shutdown cleanup) + `m_stageTexture[kMaxTextureStages]` (current binding per stage). `#include <vector>`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Implement the three methods. `Create_Texture_RGBA8` wraps `bgfx::createTexture2D(…)` with `bgfx::copy` for the pixel data; returns `reinterpret_cast<uintptr_t>(new bgfx::TextureHandle(…))`. `Destroy_Texture` finds the entry in `m_ownedTextures`, destroys the bgfx resource, deletes the wrapper, erases, and unbinds from any stage still holding it. `Set_Texture` just stashes `handle` in `m_stageTexture[stage]`. `Draw_Indexed` and `Draw` consult `m_stageTexture[0]` at submit time, falling back to `m_placeholderTexture` when 0 or invalid. `DestroyPipelineResources` walks `m_ownedTextures` so nothing leaks on `Shutdown`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_texture)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_texture/main.cpp` | Clone of `tst_bgfx_mesh` with per-face vertex color flipped to white (so the albedo comes from the texture, not the vertex stream) and an additional `Create_Texture_RGBA8` + `Set_Texture(0, handle)` step using a 16×16 4×4-block orange/teal checkerboard with a white diagonal stripe. Releases the handle via `Destroy_Texture` before `Shutdown`. |
| `tests/tst_bgfx_texture/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`; links `corei_bgfx` + `SDL3::SDL3`. |
| `docs/Phase5i-RealTextures.md` | This doc. |

### Texture ownership model

Heap-allocated `bgfx::TextureHandle` wrappers give us three guarantees:

1. **Stable opaque handles** — the backend exposes a `uintptr_t`, never bgfx's internal `idx`. The test never needs to `#include <bgfx/bgfx.h>`.
2. **Null safety** — `handle == 0` means "unbound"; because we're returning `new`-allocated pointers, no legitimate texture ever has value 0.
3. **Leak safety** — `m_ownedTextures` is the authoritative set. `Destroy_Texture` removes an entry; `Shutdown` drains what's left. A caller that leaks a `Create_Texture_RGBA8` handle loses only the wrapper allocation (the bgfx resource is freed regardless).

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target corei_bgfx` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_clear tst_bgfx_triangle tst_bgfx_uber tst_bgfx_mesh tst_bgfx_texture` | OK. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | `tst_bgfx_clear: PASSED` (regression). |
| `./tests/tst_bgfx_triangle/tst_bgfx_triangle` | `tst_bgfx_triangle: PASSED` (regression). |
| `./tests/tst_bgfx_uber/tst_bgfx_uber` | `tst_bgfx_uber: PASSED` (regression). |
| `./tests/tst_bgfx_mesh/tst_bgfx_mesh` | Placeholder cube still renders white-lit per face → `tst_bgfx_mesh: PASSED` (regression: placeholder fallback still works when `m_stageTexture[0] == 0`). |
| `./tests/tst_bgfx_texture/tst_bgfx_texture` | Cube visibly textured with orange/teal checkerboard + white diagonal stripe, lit by the same directional light as 5g → `tst_bgfx_texture: PASSED`. |
| `cmake-build-release` reconfigure with `RTS_RENDERER=dx8` | OK. DX8 game targets still configure — zero production call sites affected (no code calls `IRenderBackend::` in production yet). |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero (unchanged since 5a). |
| `bgfx::` calls in `Core/Libraries/Source/WWVegas/WW3D2/` | Zero. |
| Matrix4x4 / ShaderClass / VertexMaterialClass referenced by `corei_bgfx` | Zero. |

## Deferred

| Item | Stage |
|---|---|
| DX8Wrapper → IRenderBackend adapter (`TextureBaseClass::Get_ID` → bgfx handle cache, ShaderClass→ShaderStateDesc adapter, per-game-class extraction) | 5h |
| `bimg`-based DDS / KTX loader (+ mipchain upload, DXT1/3/5/BC7) | 5j |
| Compressed texture formats at runtime (`BGFX_TEXTURE_FORMAT_BC*`) | 5j |
| Multi-stage texture sampling in the uber shaders (detail color / alpha / secondary gradient) | 5h |
| Texture filtering modes derived from game state (anisotropic, mipmap bias) | 5h |
| Async texture streaming / partial uploads | later |
| Cube maps, volume textures, render targets | later |
