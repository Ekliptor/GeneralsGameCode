# Phase 5c — D3DX dependency removal from client TUs

Companion doc to `docs/CrossPlatformPort-Plan.md`. Third stage of Phase 5
(Renderer bgfx backend). Follows `Phase5b-D3DTypeRemoval.md`.

## Objective

Remove `#include <d3dx8*.h>` from all game-level source files outside the DX8
wrapper implementation (`Core/Libraries/Source/WWVegas/WW3D2/`). Replace D3DX
math types and functions with WWMath equivalents. Move D3DX utility calls behind
`DX8Wrapper`. Existing DX8 rendering continues to work identically. The bgfx
backend does not use these client files yet (that is Phase 5d+).

## Locked decisions

| Question | Decision |
|---|---|
| D3DXMATRIX → Matrix4x4 | **Byte-compatible swap.** Both are 16 contiguous floats. D3DXMatrixInverse ↔ Matrix4x4::Inverse() produce identical results. |
| Convention in client code | **D3D row-major bytes.** Client files call `_Get_DX8_Transform` which reads raw D3DMATRIX. Matrix4x4 holds row-major bytes, not WWMath column-major. |
| D3DXVECTOR4 → Vector4 | **Byte-compatible.** Member names change: `.x`→`.X`, `.y`→`.Y`, `.z`→`.Z`. |
| D3DXFilterTexture | **`DX8Wrapper::Generate_Mipmaps()`** wraps the D3DX call. |
| D3DXAssembleShader | **`#ifdef _WIN32`** guard. Will be replaced with bgfx shaders in Phase 5f. |
| D3DXGetFVFVertexSize | **Manual FVF size calculation** in dx8fvf.cpp. |

## Source-level changes

### New helpers

- `Matrix4x4::Make_Translation(x, y, z)` — identity with `Row[3]=(x,y,z,1)`.
- `Matrix4x4::Make_Scale(sx, sy, sz)` — diagonal `(sx,sy,sz,1)`.
- `DX8Wrapper::_Get_DX8_Transform(D3DTRANSFORMSTATETYPE, Matrix4x4&)` — raw
  byte overload (no convention transpose).
- `DX8Wrapper::_Set_DX8_Transform(D3DTRANSFORMSTATETYPE, const Matrix4x4&)` — same.
- `DX8Wrapper::Generate_Mipmaps(IDirect3DBaseTexture8*)` — wraps D3DXFilterTexture.

### Edited files

| File | Change |
|---|---|
| `WWMath/matrix4.h` | Added `Make_Translation`, `Make_Scale` static factory methods. |
| `WW3D2/dx8wrapper.h` | Added Matrix4x4 overloads of `_Get/_Set_DX8_Transform`, `Generate_Mipmaps` declaration. |
| `WW3D2/dx8wrapper.cpp` | Added `Generate_Mipmaps` implementation. |
| `W3DShaderManager.cpp` | Replaced ~40 D3DXMATRIX, ~40 D3DXVECTOR4, 15 D3DXMatrixInverse, 6 Translation, 8 Scaling, and SetPixelShaderConstant patterns. Removed `#include "d3dx8tex.h"`. |
| `Water/W3DWater.cpp` | Replaced 12 D3DXMATRIX, 3 D3DXVECTOR4, D3DX math, `._NN` member access, `operator()`. Guarded D3DXAssembleShader with `#ifdef _WIN32`. Removed `#include "d3dx8math.h"`. |
| `W3DTreeBuffer.cpp` | Replaced 3 D3DXMATRIX, D3DXMatrixMultiply/Transpose, D3DXFilterTexture. Removed `#include "d3dx8tex.h"`. |
| `TerrainTex.cpp` | Replaced 6 D3DXMATRIX, D3DXMatrixInverse/Scaling/Translation, 4 D3DXFilterTexture. Removed `#include "d3dx8tex.h"`. |
| `BaseHeightMap.cpp` | Removed unused `#include <d3dx8core.h>`. |
| `FlatHeightMap.cpp` | Same. |
| `CameraShakeSystem.cpp` | Same. |
| `HeightMap.cpp` | Same. |
| Generals/`dx8fvf.cpp` | Replaced D3DXGetFVFVertexSize with manual FVF calculation. Removed `#include <d3dx8core.h>`. |
| GeneralsMD/`dx8fvf.cpp` | Same. |
| Generals/`assetmgr.cpp` | Removed unused `#include <d3dx8core.h>`. |
| GeneralsMD/`assetmgr.cpp` | Same. |
| Generals/`dx8vertexbuffer.cpp` | Same. |
| GeneralsMD/`dx8vertexbuffer.cpp` | Same. |
| Generals/`BezFwdIterator.cpp` | Replaced D3DXVECTOR4→Vector4, D3DXVec4Transform→Matrix4x4 operator*. |
| GeneralsMD/`BezFwdIterator.cpp` | Same. |

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
| `#include.*d3dx8` in `Core/GameEngineDevice/` | Zero. |
| `#include.*d3dx8` in `Generals/Code/Libraries/`, `GeneralsMD/Code/Libraries/` | Zero. |
| `#include.*d3dx8` in `Generals/Code/GameEngine/`, `GeneralsMD/Code/GameEngine/` | Zero. |
| `D3DXMATRIX` or `D3DXVECTOR4` in client TU files | Zero. |
| Remaining `d3dx8` includes | Only in `Core/Libraries/Source/WWVegas/WW3D2/` (DX8 backend internals) and `*/Tools/WorldBuilder/` (out of scope). |

## Deferred to later Phase 5 stages

| Item | Stage |
|---|---|
| DX8Wrapper → IRenderBackend facade (game targets build under bgfx) | 5d |
| First textured triangle in bgfx | 5e |
| Fixed-function uber-shaders + shaderc pipeline | 5f |
| Mesh rendering parity | 5g |
| Terrain + water shader rewrite (bgfx HLSL/GLSL) | 5h |
| DDS loading via bimg | 5i |
| Full scene parity + golden-image CI test | 5j |
| macOS .app bundle + CI jobs | 5k |
