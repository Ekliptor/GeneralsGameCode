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

// IRenderBackend
// ==============
// Cross-platform-port render backend seam (see docs/Phase0-RHI-Seam.md and
// docs/Phase5g-IRenderBackendPath.md). Phase 5g is the first stage that
// actually implements this interface end-to-end in `BgfxBackend`; production
// game code still calls `DX8Wrapper::*` statics and will be routed through
// here later (5h / Phase 7, once DX8 is retired).
//
// The vocabulary is Core-level PODs (`BackendDescriptors.h`) rather than
// per-game types like `ShaderClass` / `VertexMaterialClass` — this keeps the
// bgfx backend free of dependencies on `Generals/Code/.../WW3D2/`.

#include <cstdint>

class Vector4;
struct ShaderStateDesc;
struct MaterialDesc;
struct LightDesc;
struct VertexLayoutDesc;
struct SamplerStateDesc;

class IRenderBackend
{
public:
	virtual ~IRenderBackend() = default;

	// Lifetime
	virtual bool Init(void* windowHandle, int width, int height, bool windowed) = 0;
	virtual void Shutdown() = 0;
	virtual bool Reset(int width, int height, bool windowed) = 0;

	// Frame
	virtual void Begin_Scene() = 0;
	virtual void End_Scene(bool flip) = 0;
	virtual void Clear(bool color, bool depth, const Vector4& clearColor, float z) = 0;

	// Transforms. Matrices are 16 floats in D3D-style row-major layout
	// (translation in the last row, elements [12..14]) — matches WWMath's
	// `Matrix4x4::Row[4]` and bgfx's `setTransform` convention. Callers in
	// game code should pass `&mtx.Row[0].X` or equivalent.
	virtual void Set_World_Transform(const float m[16]) = 0;
	virtual void Set_View_Transform(const float m[16]) = 0;
	virtual void Set_Projection_Transform(const float m[16]) = 0;

	// Render state
	virtual void Set_Shader(const ShaderStateDesc& shader) = 0;
	virtual void Set_Material(const MaterialDesc& material) = 0;
	// `light == nullptr` disables the slot.
	virtual void Set_Light(unsigned index, const LightDesc* light) = 0;

	// Texture management (Phase 5i). Handles are opaque `uintptr_t`s that
	// survive until `Destroy_Texture` or `Shutdown`. A handle of 0 is the
	// null / unbound sentinel — `Set_Texture(stage, 0)` drops to the
	// built-in 2×2 white placeholder.
	//
	// Pixel data is tightly-packed RGBA8 (`width × height × 4` bytes) in
	// row-major order. `mipmap` is a hint — the backend may choose to ignore
	// it for formats / sizes where generation is unsupported.
	virtual uintptr_t Create_Texture_RGBA8(const void* pixels,
	                                       uint16_t width,
	                                       uint16_t height,
	                                       bool mipmap) = 0;

	// Phase 5j. Decodes a DDS / KTX / PNG / TGA byte blob (anything bimg
	// recognizes) and uploads the full mipchain in the image's own format.
	// Returns 0 on parse failure. Same ownership model as Create_Texture_RGBA8.
	virtual uintptr_t Create_Texture_From_Memory(const void* data,
	                                             uint32_t size) = 0;

	// Phase 5h.30. Uploads fresh RGBA8 pixels to an existing texture at
	// mip level 0. Intended for procedural textures allocated via
	// `Create_Texture_RGBA8(nullptr, w, h, …)` (Phase 5h.28) that later
	// need real pixel data. `handle == 0` is a no-op (simplifies callers
	// that may hold an unallocated handle). `pixels` must point to at
	// least `w * h * 4` bytes; partial updates aren't exposed yet.
	virtual void Update_Texture_RGBA8(uintptr_t handle,
	                                  const void* pixels,
	                                  uint16_t width,
	                                  uint16_t height) = 0;

	virtual void Destroy_Texture(uintptr_t handle) = 0;
	virtual void Set_Texture(unsigned stage, uintptr_t handle) = 0;

	// Phase 5h.36 — diagnostic mip-count accessor for cache-loaded textures.
	// Returns the number of mip levels the texture was created with
	// (1 == no mip chain; >1 == pre-baked mips). Returns 0 if the handle
	// isn't known to the backend. Lets `TextureClass` / `TextureFilterClass`
	// reconcile their ctor-time "I want mips" preference with what the file
	// on disk actually provided.
	virtual uint8_t Texture_Mip_Count(uintptr_t handle) = 0;

	// Phase 5h.34 — per-stage sampler state (filter + address mode +
	// anisotropy). Cached inside the backend and applied on the next
	// `Set_Texture` / draw submission. Called by `TextureFilterClass::Apply`
	// in bgfx mode. Defaults (bilinear / wrap / no-mips) are applied to any
	// stage that was never configured.
	virtual void Set_Sampler_State(unsigned stage, const SamplerStateDesc& sampler) = 0;

	// Resource binding — ownership stays with the backend: a subsequent
	// Set_* call or Shutdown() releases the previously bound buffer.
	virtual void Set_Vertex_Buffer(const void* data,
	                               unsigned vertexCount,
	                               const VertexLayoutDesc& layout) = 0;
	virtual void Set_Index_Buffer(const uint16_t* data,
	                              unsigned indexCount) = 0;

	// Phase 5h.6 — viewport scissor/origin. Origin is top-left pixel of the
	// current render target (backbuffer or RT), `width`/`height` in pixels.
	// Passing `width == 0 || height == 0` resets the viewport to cover the
	// full current target — useful after a capture-only RT draw where the
	// game wants to restore the backbuffer's full-screen viewport without
	// caching the backbuffer dimensions.
	virtual void Set_Viewport(int16_t x, int16_t y,
	                          uint16_t width, uint16_t height) = 0;

	// Draw
	virtual void Draw_Indexed(unsigned minVertexIndex,
	                          unsigned numVertices,
	                          unsigned startIndex,
	                          unsigned primitiveCount) = 0;
	virtual void Draw(unsigned startVertex, unsigned primitiveCount) = 0;

	// Phase 5p — off-screen render targets. `Create_Render_Target` allocates
	// a color (BGRA8) + optional depth attachment of the given size and
	// returns an opaque handle. `Set_Render_Target(h)` binds that RT for
	// subsequent Clear / Begin_Scene / Draw_* calls; `h == 0` switches back
	// to the default backbuffer. `Get_Render_Target_Texture(h)` returns an
	// opaque texture handle (same type as `Create_Texture_RGBA8` returns)
	// that can be bound with `Set_Texture(stage, tex)` and sampled by the
	// uber-shaders; its lifetime is tied to the RT — do NOT pass it to
	// `Destroy_Texture`. `Destroy_Render_Target` releases the RT and its
	// embedded color + depth textures.
	virtual uintptr_t Create_Render_Target(uint16_t width, uint16_t height,
	                                       bool hasDepth) = 0;
	virtual void Destroy_Render_Target(uintptr_t handle) = 0;
	virtual void Set_Render_Target(uintptr_t handle) = 0;
	virtual uintptr_t Get_Render_Target_Texture(uintptr_t handle) = 0;

	// Phase 5q — synchronous CPU readback of an RT's color attachment. On
	// success, `pixels` is filled with `rtWidth * rtHeight * 4` bytes in
	// BGRA8 little-endian layout (matches the default RT color format). On
	// failure returns false: `handle == 0`, buffer too small, or the
	// backend can't read back the texture. Implementations may block for
	// 1–2 frames while bgfx's GPU→CPU copy drains. Intended for screenshot
	// capture and golden-image regression tests; hot-path renderers should
	// not call this per frame.
	virtual bool Capture_Render_Target(uintptr_t handle,
	                                   void* pixels,
	                                   uint32_t byteCapacity) = 0;

	// Asynchronous backbuffer screenshot. The backend queues a request and
	// writes the image to `path` inside the next frame's GPU→CPU drain
	// (bgfx uses its CallbackI::screenShot for this). Returns false if the
	// backend can't honour the request (not initialized, null path, or no
	// backbuffer capture path). DX8 impls can leave this as the default
	// no-op stub — the W3DDisplay DX8 path captures via surface lock
	// directly.
	virtual bool Request_Back_Buffer_Screenshot(const char* /*path*/) { return false; }

	// Phase 5m — fire-and-forget per-frame draw. `verts` + `indices` are
	// copied into backend-owned transient buffers that live for the current
	// frame only; no handle is returned. `indices == nullptr` + `indexCount
	// == 0` draws `vertexCount / 3` non-indexed triangles; otherwise
	// `indexCount` must be a multiple of 3. Intended for streaming geometry
	// (particles, decals, dynamic terrain, HUD) where creating a persistent
	// VB/IB per frame would thrash the GPU allocator.
	virtual void Draw_Triangles_Dynamic(const void* verts,
	                                    unsigned vertexCount,
	                                    const VertexLayoutDesc& layout,
	                                    const uint16_t* indices,
	                                    unsigned indexCount) = 0;

	// Identification (for diagnostics / golden-image testing)
	virtual const char* Backend_Name() const = 0;
};
