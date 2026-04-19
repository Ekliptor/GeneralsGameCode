/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#pragma once

// BackendDescriptors
// ==================
// Phase 5g: Core-level POD render-state vocabulary for `IRenderBackend`. Lets
// the bgfx backend live in `Core/GameEngineDevice/Source/BGFXDevice/` without
// a transitive dependency on per-game WW3D2 (`Generals/Code/.../WW3D2/` or
// `GeneralsMD/Code/.../WW3D2/`), where `ShaderClass`, `VertexMaterialClass`,
// `VertexBufferClass`, etc. currently live.
//
// When DX8Wrapper is eventually routed through IRenderBackend (Phase 5h / 7),
// the adapter will populate these descriptors from ShaderClass /
// VertexMaterialClass / FVFInfoClass — the enum sets below are chosen to map
// 1:1 onto those types. For now only `tst_bgfx_mesh` builds descriptors
// directly; production game code still calls `DX8Wrapper::*` statics.
//
// All members are default-initialized to the "pass-through, opaque, no-blend,
// depth-test-on, cull-on, no-lighting" DX8 defaults.

#include <cstdint>

// ---- Vertex layout -----------------------------------------------------------

struct VertexAttributeDesc
{
	enum Semantic : uint8_t
	{
		SEM_POSITION   = 0,
		SEM_NORMAL     = 1,
		SEM_COLOR0     = 2,
		SEM_TEXCOORD0  = 3,
		SEM_TEXCOORD1  = 4,
		SEM_COUNT,
	};

	enum Type : uint8_t
	{
		TYPE_FLOAT32           = 0,  // e.g. position, normal, uv
		TYPE_UINT8_NORMALIZED  = 1,  // e.g. vertex color (ABGR little-endian)
	};

	Semantic       semantic = SEM_POSITION;
	Type           type     = TYPE_FLOAT32;
	uint8_t        count    = 0;     // elements: 1..4
	uint8_t        pad_     = 0;     // unused; keeps struct on a 4-byte boundary
	uint16_t       offset   = 0;     // byte offset within the vertex
};

struct VertexLayoutDesc
{
	static constexpr uint16_t kMaxAttrs = 6;

	uint16_t            stride    = 0;  // total vertex size in bytes
	uint16_t            attrCount = 0;  // number of valid entries in `attrs`
	VertexAttributeDesc attrs[kMaxAttrs];
};

// ---- Shader / render state ---------------------------------------------------

struct ShaderStateDesc
{
	// Enum values chosen to parallel `ShaderClass::DepthCompareType`,
	// `SrcBlendFuncType`, `DstBlendFuncType`. Numbering is local to the Core
	// descriptor; the DX8Wrapper adapter (Phase 5h) will map them through.

	enum DepthCmp : uint8_t
	{
		DEPTH_NEVER        = 0,
		DEPTH_LESS         = 1,
		DEPTH_EQUAL        = 2,
		DEPTH_LEQUAL       = 3,
		DEPTH_GREATER      = 4,
		DEPTH_NOTEQUAL     = 5,
		DEPTH_GEQUAL       = 6,
		DEPTH_ALWAYS       = 7,
	};

	enum BlendFactor : uint8_t
	{
		BLEND_ZERO                = 0,
		BLEND_ONE                 = 1,
		BLEND_SRC_COLOR           = 2,
		BLEND_INV_SRC_COLOR       = 3,
		BLEND_SRC_ALPHA           = 4,
		BLEND_INV_SRC_ALPHA       = 5,
		BLEND_DST_COLOR           = 6,
		BLEND_INV_DST_COLOR       = 7,
	};

	DepthCmp     depthCmp         = DEPTH_LEQUAL;
	bool         depthWrite       = true;
	bool         colorWrite       = true;
	BlendFactor  srcBlend         = BLEND_ONE;
	BlendFactor  dstBlend         = BLEND_ZERO;
	bool         cullEnable       = true;
	bool         alphaTest        = false;
	bool         texturingEnable  = true;  // selector hint for the uber-shader table
	// Phase 5n — cutout threshold. Active when `alphaTest` is true: any
	// fragment whose sampled alpha is < `alphaTestRef` is `discard`-ed.
	// Default 0.5 matches the D3DCMPFUNC_GREATEREQUAL + D3DRS_ALPHAREF of
	// 128 that Generals uses for foliage / fences / cutout sprites.
	float        alphaTestRef     = 0.5f;

	// Phase 5o — linear vertex fog. When `fogEnable` is false, `fogColor`
	// and the range fields are ignored; when true, every vertex's view-space
	// distance |z| is mapped to `f = clamp((fogEnd - |z|) / (fogEnd -
	// fogStart), 0, 1)`, and the fragment's final RGB is lerped toward
	// `fogColor` by `(1 - f)`. Matches D3DFOG_LINEAR / D3DRS_FOGVERTEXMODE
	// which is what Generals uses for scene fog.
	bool         fogEnable        = false;
	float        fogStart         = 0.0f;
	float        fogEnd           = 100.0f;
	float        fogColor[3]      = { 0.5f, 0.5f, 0.6f };
};

// ---- Material ---------------------------------------------------------------

struct MaterialDesc
{
	float diffuse[3]   = { 1.0f, 1.0f, 1.0f };
	float ambient[3]   = { 0.2f, 0.2f, 0.2f };
	float emissive[3]  = { 0.0f, 0.0f, 0.0f };
	float opacity      = 1.0f;
	bool  useLighting  = false;   // selector hint: false → pick an unlit program
};

// ---- Light ------------------------------------------------------------------

struct LightDesc
{
	enum Type : uint8_t
	{
		LIGHT_DIRECTIONAL = 0,
		LIGHT_POINT       = 1,
		LIGHT_SPOT        = 2,     // 5g implements only directional; others placeholder
	};

	Type  type              = LIGHT_DIRECTIONAL;
	float direction[3]      = { 0.0f, -1.0f, 0.0f };  // world-space, from light to scene
	float color[3]          = { 1.0f,  1.0f,  1.0f };
	float ambient           = 0.15f;
	float intensity         = 1.0f;
	float position[3]       = { 0.0f,  0.0f,  0.0f };  // point/spot only
	float attenuationRange  = 100.0f;                  // point/spot only
};
