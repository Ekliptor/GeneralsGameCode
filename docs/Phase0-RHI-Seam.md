# Phase 0 — Renderer RHI Seam & Math-Leak Cleanup

## Goal

Per the cross-platform port plan (`docs/CrossPlatformPort-Plan.md`), Phase 0 is the
no-behavior-change prelude: introduce the seam for a future bgfx backend and strip
the D3DX math surface from call sites so only genuinely DX-coupled code keeps a
D3D dependency. No new rendering features, no bytes-on-disk changes, no runtime
differences expected in replay/multiplayer.

## What Phase 0 delivered

1. **`d3dx8math.h` leak-site migration.** 17 files transitively included
   `<d3dx8math.h>`. 13 of them only did so for pure math or dead-include reasons
   and have been migrated to WWMath. The remaining 4 are either the intentional
   WWMath↔D3D bridge or genuinely DX-coupled and will be addressed in Phase 5.
2. **`IRenderBackend.h` interface skeleton.** A pure-abstract header at
   `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` documents the RHI
   surface a Phase 5 bgfx backend must implement. Not used at call sites yet —
   `DX8Wrapper` is still a collection of static methods and wiring virtual
   dispatch through it is Phase 5's job.
3. **CMake `RTS_RENDERER` option.** New cache variable gates the renderer
   backend. Only `dx8` is valid today (emits `RTS_RENDERER_DX8=1`); setting
   `bgfx` produces a fatal error until Phase 5. Appears in the feature summary.

## Leak-site disposition

| File | Before | After |
|---|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/pointgr.cpp` | `D3DXMatrixRotationZ`, `D3DX_PI`, reinterpret to `D3DXMATRIX` | Direct Matrix4x4 Z-rotation fill using `WWMATH_PI` |
| `Generals/.../WW3D2/sortingrenderer.cpp` | 3× `D3DX*`-based transform blocks, one with explicit `D3DXMatrixTranspose` | Matrix4x4 product with manual column-dot (`mtx[k][j]*v[k]`) — byte-compatible |
| `GeneralsMD/.../WW3D2/sortingrenderer.cpp` | 3× same pattern as Generals | Same migration |
| `Generals/.../BezierSegment.{h,cpp}` | `D3DXVECTOR4`, `D3DXMATRIX`, `D3DXVec4Transform`, `D3DXVec4Dot` | `Vector4`, `Matrix4x4`, `M*v`, `a*b` (dot). Basis matrix is symmetric so column-vec form gives same result |
| `GeneralsMD/.../BezierSegment.{h,cpp}` | Same as Generals | Same migration |
| `Core/GameEngineDevice/.../W3DView.cpp` | Dead `#include` only | Removed |
| `Generals/.../Shadow/W3DShadow.cpp` | Dead `#include` only | Removed |
| `GeneralsMD/.../Shadow/W3DShadow.cpp` | Dead `#include` only | Removed |
| `Generals/.../Shadow/W3DProjectedShadow.cpp` | `#include` + `(_D3DMATRIX*)` cast of `Matrix3D` in `SetTransform` call | Include removed; cast preserved (still DX8-bound, moves in Phase 5) |
| `GeneralsMD/.../Shadow/W3DProjectedShadow.cpp` | Same as Generals | Same |
| `Generals/.../Shadow/W3DVolumetricShadow.cpp` | `D3DXVECTOR4` as `D3DFVF_XYZRHW` vertex-struct field | `Vector4` (binary-identical layout: 4 floats XYZW) |
| `GeneralsMD/.../Shadow/W3DVolumetricShadow.cpp` | Same as Generals | Same |

## Deferred to Phase 5

| File | Reason |
|---|---|
| `Core/Libraries/Source/WWVegas/WWMath/matrix3d.cpp` | Implements the `To_D3DMATRIX` / `To_Matrix4x4` bridge. Still needed as long as `DX8Wrapper` exists. Will be gated behind `RTS_RENDERER_DX8` when bgfx lands. |
| `Core/Libraries/Source/WWVegas/WWMath/matrix4.cpp` | Same — bridge implementation. |
| `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp` | Contains vs_1_1 / ps_1_1 assembly shaders via `D3DXAssembleShader` and `ID3DXBuffer`, plus direct `SetPixelShaderConstant` / `SetVertexShaderConstant` calls. The plan's claim "no HLSL/shader assets" overlooked these DX8 assembly shaders. Porting this to bgfx requires rewriting the shaders in bgfx's shading language (Phase 5). |

## Byte-exact preservation

The sortingrenderer migration deserves callout. The original code did:
```cpp
D3DXMATRIX mtx = (D3DXMATRIX&)world * (D3DXMATRIX&)view;   // row-vector row-major
D3DXVec3Transform(&out, &vec, &mtx);                         // out = v * mtx
```
`state->sorting_state.world` is stored as `D3DMATRIX` (16 row-major floats).
`Matrix4x4` has the same physical layout (`Vector4 Row[4]` = 16 floats in the
same order), so `reinterpret_cast<const Matrix4x4&>(d3dmatrix)` is well-defined.
Matrix multiplication in both `D3DXMATRIX` and `Matrix4x4` uses the standard
`result[i][j] = sum_k a[i][k]*b[k][j]` — they agree byte-for-byte.

The only difference is vector semantics:
- D3DX row-vector: `(v*M)[j] = sum_k v[k]*M[k][j]` (dot with column j)
- WWMath column-vector: `(M*v)[i] = sum_k M[i][k]*v[k]` (dot with row i)

To produce the same bytes as `v*M`, WWMath takes column j directly:
```cpp
out[j] = sum_k M[k][j]*v[k] + M[3][j]
```
which is what the migrated code does:
```cpp
vec.X*mtx[0][0] + vec.Y*mtx[1][0] + vec.Z*mtx[2][0] + mtx[3][0]
```

Same operand order, same multiply-adds → same float bits.

For `W3DVolumetricShadow.cpp` the `D3DXVECTOR4 p` field was a bare
`D3DFVF_XYZRHW` position (4 floats: screen-space x, y, z, rhw). `Vector4` stores
`{X, Y, Z, W}` in the same order, so the replacement is a drop-in binary change.

## Remaining `windows.h` / DX exposure in Phase 0 core code

The Phase 0 static-sweep target is "no `d3dx8math.h` outside
`WW3D2/matrix{3d,4}.cpp` and `W3DWater.cpp`." The sweep after this phase:

```text
docs/CrossPlatformPort-Plan.md                       (plan document)
Core/Libraries/Source/WWVegas/WWMath/matrix4.cpp    (bridge)
Core/Libraries/Source/WWVegas/WWMath/matrix3d.cpp   (bridge)
Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp  (deferred)
```

Four hits, all expected. The larger d3d8.h / `DX8Wrapper` / Win32Device surface
is by design untouched — those are Phase 2 through 5.

## Verification

- **CMake configure**: `cmake -DRTS_RENDERER=dx8 .` succeeds; `-DRTS_RENDERER=bgfx`
  fails with a clear message.
- **Feature summary**: `RendererBackend, Renderer backend: dx8` appears in the
  configure output.
- **Math equivalence**: BezierSegment basis matrix symmetry and sortingrenderer
  column-dot identity have been verified algebraically (see "Byte-exact
  preservation"). Replay bit-identity verification is left for the CI harness
  that doesn't run locally on macOS yet.
- **No build harness run on this machine**: cross-compilation via Docker + Wine
  (`scripts/docker-build.sh`) is how one would smoke-test the changes end-to-end.

## Pointers for Phase 1+

- `IRenderBackend.h` is the target for Phase 5. Start by deriving `BgfxBackend`
  from it and implementing `Init` → `Clear` → single textured quad → mesh
  before hooking game code.
- When Phase 5 begins, retire the `reinterpret_cast<const Matrix4x4&>(D3DMATRIX)`
  pattern in `sortingrenderer.cpp` by plumbing `Matrix4x4` directly through
  `RenderStateStruct` (the D3DMATRIX field there is the last structural DX leak
  in the sorting path).
- `W3DWater.cpp` assembly shaders need rewriting into bgfx HLSL. Start there
  once the clear-to-textured-quad happy path works on macOS Metal.
- `Dependencies/Utility/Utility/pseh_compat.h` (SEH compat) and the ATL/COM
  shims can be retired once the embedded IE browser dies in Phase 3.
