// d3dx8math.h — Stub for non-DX8 builds (Phase 5d).
// Intercepts #include <d3dx8math.h> from matrix4.cpp, matrix3d.cpp.
// All D3DX math was migrated to WWMath in Phase 5c.
// D3DXMATRIX is still referenced by conversion helpers in matrix4.cpp/matrix3d.cpp.
#pragma once
#include "d3d8.h"

#ifndef _D3DX8MATH_STUB_DEFINED
#define _D3DX8MATH_STUB_DEFINED

// D3DXMATRIX — extends D3DMATRIX with constructors/operators.
// Under bgfx, it's just a typedef since the conversion helpers only copy bytes.
struct D3DXMATRIX : public D3DMATRIX {};

#endif
