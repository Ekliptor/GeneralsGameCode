# Phase 5b — D3D type removal from public RHI

Companion doc to `docs/CrossPlatformPort-Plan.md`. Second stage of Phase 5
(Renderer bgfx backend). Follows the structure of `Phase0-RHI-Seam.md` through
`Phase5a-Bgfx-Bringup.md`.

## Objective

Remove Direct3D types (`D3DMATRIX`, `D3DLIGHT8`) from `RenderStateStruct` in
`dx8wrapper.h` so the shared render-state representation is backend-agnostic.
Existing DX8 rendering continues to work identically. The bgfx backend does not
use `RenderStateStruct` yet (that is Phase 5d), but this change unblocks it.

## Locked decisions

| Question | Decision |
|---|---|
| Matrix storage convention | **Column-major `Matrix4x4`** (WWMath convention). Transpose pushed to `Apply_Render_State_Changes` at the D3D call site via `To_D3DMATRIX()`. |
| Light struct | **`RenderLight`** POD struct in `WW3D2/RenderLight.h`. Mirrors D3DLIGHT8 fields using `Vector3`. Enum values match `D3DLIGHTTYPE` for trivial `static_cast`. |
| `d3d8.h` in dx8wrapper.h | **Stays.** `DX8Wrapper` class still has D3D device pointers, render states, etc. Full removal is Phase 5d. |
| Sorting renderer math | Multiplication order reversed (`view * world`), dot pattern changed from column-dot to row-dot. Algebraically equivalent (proven in plan). |

## Source-level changes

### New files

- `Core/Libraries/Source/WWVegas/WW3D2/RenderLight.h` — engine-native light
  snapshot struct. `RenderLightType` enum (POINT=1, SPOT=2, DIRECTIONAL=3),
  `RenderLight` with Vector3 Diffuse/Specular/Ambient/Position/Direction +
  float attenuation/range/spot params. No D3D dependency.

### Edited files

| File | Change |
|---|---|
| `dx8wrapper.h` | (1) `#include "RenderLight.h"`. (2) `RenderStateStruct`: `D3DLIGHT8 Lights[4]` → `RenderLight Lights[4]`; `D3DMATRIX world/view` → `Matrix4x4 world/view`. (3) `Set_Transform` inlines: store `Matrix4x4` directly (no `To_D3DMATRIX`). (4) `Get_Transform` inlines: read `Matrix4x4` directly (no `To_Matrix4x4`). |
| `dx8wrapper.cpp` | (1) Local `To_D3DLIGHT8(RenderLight)` / `To_RenderLight(D3DLIGHT8)` helpers. (2) `Apply_Render_State_Changes`: converts world/view via `To_D3DMATRIX()` and lights via `To_D3DLIGHT8()` at the D3D call site. (3) `Set_World_Identity`/`Set_View_Identity`: `Matrix4x4::Make_Identity()`. (4) `Set_Light(D3DLIGHT8*)`: converts to `RenderLight` on store. (5) `Set_Light(LightClass&)`: builds `RenderLight` directly. (6) `Set_Light_Environment`: builds `RenderLight` directly. |
| `Generals/.../sortingrenderer.cpp` | (1) Local `To_D3DLIGHT8` helper. (2) `Apply_Render_State`: converts matrices via `To_D3DMATRIX()`, lights via `To_D3DLIGHT8()`. (3) Three bounding-sphere sites: removed `reinterpret_cast`, reversed mul order (`view * world`), changed column-dot to row-dot. (4) Z-extraction loop: `mtx[i][2]` → `mtx[2][i]`. |
| `GeneralsMD/.../sortingrenderer.cpp` | Same changes as Generals copy. Additional identity-check and Z-dot indices swapped. |

## Mathematical equivalence (sorting renderer)

Let `W` = WWMath column-major world matrix, `V` = column-major view matrix.

- **Old code** stored `To_D3DMATRIX(W)` (row-major) in `render_state.world`.
  `reinterpret_cast<Matrix4x4&>` yielded `W^T`. Old computation:
  `mtx = W^T * V^T`, column-dot: `z = sum_i v[i]*mtx[i][2] + mtx[3][2]`.
  This computes `(V * W * [v,1])[2]`.

- **New code** stores `W` directly. New computation:
  `mtx = V * W`, row-dot: `z = sum_j mtx[2][j]*v[j] + mtx[2][3]`.
  This computes `(V * W * [v,1])[2]`. Same result.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21.

| Command | Outcome |
|---|---|
| `cmake .. -DRTS_RENDERER=bgfx -DRTS_PLATFORM=sdl -DRTS_AUDIO=openal -DRTS_VIDEO=ffmpeg -DRTS_BUILD_CORE_EXTRAS=ON` | OK. |
| `cmake --build . --target corei_bgfx` | OK. |
| `cmake --build . --target tst_bgfx_clear` | OK. |
| `./tests/tst_bgfx_clear/tst_bgfx_clear` | OK. `BgfxBackend::Init: Metal (800x600, windowed)` → `PASSED`. |

Static sweep:

| Pattern | Result |
|---|---|
| `D3DMATRIX` in `RenderStateStruct` | Zero — only in `DX8Wrapper` privates. |
| `D3DLIGHT8` in `RenderStateStruct` | Zero — only in `DX8Wrapper` method signatures. |
| `reinterpret_cast.*sorting_state.(world\|view)` | Zero. |

## Deferred to later Phase 5 stages

| Item | Stage |
|---|---|
| DX-leaking client TUs (W3DShaderManager, W3DSnow, W3DSmudge, etc.) | 5c |
| DX8Wrapper → IRenderBackend facade (game targets build under bgfx) | 5d |
| First textured triangle in bgfx | 5e |
| Fixed-function uber-shaders + shaderc pipeline | 5f |
| Mesh rendering parity | 5g |
| Terrain + water shader rewrite (bgfx HLSL/GLSL) | 5h |
| DDS loading via bimg | 5i |
| Full scene parity + golden-image CI test | 5j |
| macOS .app bundle + CI jobs | 5k |
