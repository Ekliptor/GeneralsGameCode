// d3dx8core.h — Stub for non-DX8 builds (Phase 5d).
// Intercepts #include <d3dx8core.h> from dx8wrapper.cpp, texture.cpp, missingtexture.cpp.
#pragma once
#include "d3d8.h"

// Stub for D3DXFilterTexture — the real call is behind DX8Wrapper::Generate_Mipmaps
// which is a no-op under bgfx.  This declaration satisfies any remaining direct calls
// in WW3D2 internal code that weren't migrated in Phase 5c.
#ifndef RTS_RENDERER_DX8
inline HRESULT D3DXFilterTexture(IDirect3DBaseTexture8*, void*, DWORD, DWORD) { return S_OK; }
#endif
