/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "WW3D2/IRenderBackend.h"
#include "WW3D2/BackendDescriptors.h"

#include <bgfx/bgfx.h>
#include <unordered_map>
#include <vector>

class BgfxBackend final : public IRenderBackend
{
public:
	BgfxBackend() = default;
	~BgfxBackend() override;

	bool Init(void* windowHandle, int width, int height, bool windowed) override;
	void Shutdown() override;
	bool Reset(int width, int height, bool windowed) override;

	void Begin_Scene() override;
	void End_Scene(bool flip) override;
	void Clear(bool color, bool depth, const Vector4& clearColor, float z) override;

	void Set_World_Transform(const float m[16]) override;
	void Set_View_Transform(const float m[16]) override;
	void Set_Projection_Transform(const float m[16]) override;

	void Set_Shader(const ShaderStateDesc& shader) override;
	void Set_Material(const MaterialDesc& material) override;
	void Set_Light(unsigned index, const LightDesc* light) override;

	uintptr_t Create_Texture_RGBA8(const void* pixels,
	                               uint16_t width,
	                               uint16_t height,
	                               bool mipmap) override;
	uintptr_t Create_Texture_From_Memory(const void* data, uint32_t size) override;
	void Update_Texture_RGBA8(uintptr_t handle, const void* pixels, uint16_t width, uint16_t height) override;
	void Destroy_Texture(uintptr_t handle) override;
	void Set_Texture(unsigned stage, uintptr_t handle) override;
	void Set_Sampler_State(unsigned stage, const SamplerStateDesc& sampler) override;
	uint8_t Texture_Mip_Count(uintptr_t handle) override;

	void Set_Vertex_Buffer(const void* data,
	                       unsigned vertexCount,
	                       const VertexLayoutDesc& layout) override;
	void Set_Index_Buffer(const uint16_t* data, unsigned indexCount) override;

	void Set_Viewport(int16_t x, int16_t y,
	                  uint16_t width, uint16_t height) override;

	void Draw_Indexed(unsigned minVertexIndex, unsigned numVertices,
	                  unsigned startIndex, unsigned primitiveCount) override;
	void Draw(unsigned startVertex, unsigned primitiveCount) override;
	void Draw_Triangles_Dynamic(const void* verts, unsigned vertexCount,
	                            const VertexLayoutDesc& layout,
	                            const uint16_t* indices, unsigned indexCount) override;

	uintptr_t Create_Render_Target(uint16_t width, uint16_t height, bool hasDepth) override;
	void Destroy_Render_Target(uintptr_t handle) override;
	void Set_Render_Target(uintptr_t handle) override;
	uintptr_t Get_Render_Target_Texture(uintptr_t handle) override;
	bool Capture_Render_Target(uintptr_t handle, void* pixels, uint32_t byteCapacity) override;

	const char* Backend_Name() const override { return "bgfx"; }

	// Smoke-test entry points from Phase 5e/5f. Each lazy-initializes its own
	// program/layout/textures, submits one primitive at a hardcoded NDC
	// quadrant, and releases resources in Shutdown(). Not part of
	// IRenderBackend.
	void DrawSmokeTriangle();
	void DrawSmokeSolidQuad();
	void DrawSmokeVColorQuad();
	void DrawSmokeLitQuad();

public:
	// Phase 5i — the game configures viewports and scissor rects in LOGICAL
	// game pixels (e.g. 800x600) while the bgfx back-buffer is the real
	// drawable size (3008x1692 on a fullscreen Retina display). Without a
	// scale the default viewport covers the full framebuffer but explicit
	// Set_Viewport(0,0,800,600) calls collapse the render into the top-left
	// corner, leaving the rest as the clear color (the "pink border"). This
	// setter stores the logical dims so Set_Viewport can scale by
	// physical/logical. A value of 0 disables scaling (1:1 mapping).
	void Set_Logical_Resolution(int logicalW, int logicalH);

private:
	bool m_initialized = false;
	int m_width = 0;
	int m_height = 0;
	// Logical game resolution. Zero means "no scale, inputs already in
	// back-buffer pixels" (keeps the smoke tests and any future 1:1 cases
	// working without special-casing).
	int m_logicalW = 0;
	int m_logicalH = 0;

	// ---- Phase 5e/5f smoke-test resources --------------------------------

	bool m_smokeInit = false;
	bgfx::VertexLayout m_smokeLayout;
	bgfx::ProgramHandle m_smokeProgram = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle m_smokeTexture = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_smokeSampler = BGFX_INVALID_HANDLE;

	bool m_solidInit = false;
	bgfx::VertexLayout m_solidLayout;
	bgfx::ProgramHandle m_solidProgram = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_solidColorUniform = BGFX_INVALID_HANDLE;

	bool m_vcolorInit = false;
	bgfx::VertexLayout m_vcolorLayout;
	bgfx::ProgramHandle m_vcolorProgram = BGFX_INVALID_HANDLE;

	bool m_litInit = false;
	bgfx::VertexLayout m_litLayout;
	bgfx::ProgramHandle m_litProgram = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle m_litTexture = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_litSampler = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_litLightDir = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_litLightColor = BGFX_INVALID_HANDLE;

	// ---- Phase 5g IRenderBackend state -----------------------------------

	void InitPipelineResources();   // lazy, first Set_*_Buffer call
	void DestroyPipelineResources();

	// Phase 5m — shared setup for all draw paths. Applies view/proj, world
	// transform, program selection, solid-color uniform, light uniforms
	// (multi-lit only), both texture stages, and the state mask. Caller does
	// setVertexBuffer + setIndexBuffer + submit afterwards. `attrMask` comes
	// from the caller's currently-bound vertex layout.
	bgfx::ProgramHandle ApplyDrawState(uint32_t attrMask);

	bool m_pipelineInit = false;

	// Uber programs (shared with the smoke methods above, but loaded via
	// separate handles for the production path to avoid depending on lazy-
	// init order). Populated in InitPipelineResources().
	bgfx::ProgramHandle m_progSolid   = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle m_progVColor  = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle m_progTex     = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle m_progTexMLit = BGFX_INVALID_HANDLE;  // Phase 5l: N-slot directional lighting
	bgfx::ProgramHandle m_progTex2    = BGFX_INVALID_HANDLE;  // Phase 5k: two-stage modulate

	// Shared uniforms (one-time allocation on init).
	bgfx::UniformHandle m_uSolidColor    = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_uSampler       = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle m_uSampler1      = BGFX_INVALID_HANDLE;  // Phase 5k: stage-1 sampler
	bgfx::UniformHandle m_uLightDirArr   = BGFX_INVALID_HANDLE;  // Phase 5l: vec4[kMaxLights]
	bgfx::UniformHandle m_uLightColorArr = BGFX_INVALID_HANDLE;  // Phase 5l: vec4[kMaxLights]
	bgfx::UniformHandle m_uLightPosArr   = BGFX_INVALID_HANDLE;  // Phase 5h.9: vec4[kMaxLights] xyz=pos, w=range(0=directional)
	bgfx::UniformHandle m_uLightSpotArr  = BGFX_INVALID_HANDLE;  // Phase 5h.10: vec4[kMaxLights] xyz=spotDir, w=outerCos(<0=not spot)
	bgfx::UniformHandle m_uLightSpecArr  = BGFX_INVALID_HANDLE;  // Phase 5h.11: vec4[kMaxLights] rgb=per-light specular color
	bgfx::UniformHandle m_uMaterialSpec  = BGFX_INVALID_HANDLE;  // Phase 5h.11: vec4 rgb=matSpec, w=power
	bgfx::UniformHandle m_uCutoutRef     = BGFX_INVALID_HANDLE;  // Phase 5n: .x = cutout threshold (u_cutoutRef)
	bgfx::UniformHandle m_uFogColor      = BGFX_INVALID_HANDLE;  // Phase 5o: rgb fog color
	bgfx::UniformHandle m_uFogRange      = BGFX_INVALID_HANDLE;  // Phase 5o: .x=start, .y=end, .z=enable

	// 2×2 white placeholder — bound by default and by Set_Texture until
	// real TextureBaseClass ↔ bgfx integration lands in Phase 5i.
	bgfx::TextureHandle m_placeholderTexture = BGFX_INVALID_HANDLE;

	// Cached descriptor state.
	float m_worldMtx[16] = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	float m_viewMtx[16]  = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	float m_projMtx[16]  = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	bool m_viewProjDirty = true;

	ShaderStateDesc m_shader;
	MaterialDesc m_material;

	// Phase 5l — up to 4 directional light slots. `m_lightEnabled[i]` tracks
	// which slots are active; disabled slots upload as zero-color so the
	// shader's accumulation loop degenerates to a no-op on them.
	static constexpr unsigned kMaxLights = 4;
	bool      m_lightEnabled[kMaxLights] = { false, false, false, false };
	LightDesc m_lights[kMaxLights];

	// Current VB / IB (persistent bgfx handles — replaced on subsequent
	// Set_*_Buffer or destroyed in Shutdown).
	bgfx::VertexBufferHandle m_currentVB = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle  m_currentIB = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout       m_currentVBLayout;
	VertexLayoutDesc         m_currentVBDesc;

	// Present-attribute mask derived from the last-bound layout. Used by the
	// uber-shader selector alongside shader/material hints.
	uint32_t m_vbAttrMask = 0;

	// Phase 5i — texture management.
	// Opaque handles returned by `Create_Texture_RGBA8` are pointers to
	// heap-allocated `bgfx::TextureHandle`s; the allocation lives until
	// `Destroy_Texture` or `Shutdown`. Tracked in `m_ownedTextures` so
	// Shutdown can free any handles the caller forgot to release.
	// `m_stageTexture` holds the currently-bound handle per texture stage;
	// 0 means "use the built-in placeholder".
	std::vector<bgfx::TextureHandle*> m_ownedTextures;

	// Phase 5h.36 — mip-level count per owned texture. Populated by
	// `Create_Texture_RGBA8` (0 mips → 1 level, mipmap=true → runtime-genned,
	// value stored is the level count bgfx will have) and
	// `Create_Texture_From_Memory` (reads `bimg::ImageContainer::m_numMips`).
	// Removed on `Destroy_Texture`. Consumed by `Texture_Mip_Count` so
	// callers can demote their sampler mip filter when the file on disk
	// didn't actually carry a mip chain.
	std::unordered_map<uintptr_t, uint8_t> m_textureMipCounts;

	static constexpr unsigned kMaxTextureStages = 2;
	uintptr_t m_stageTexture[kMaxTextureStages] = { 0, 0 };

	// Phase 5h.34 — per-stage sampler flags in bgfx's native format. Computed
	// once per Set_Sampler_State call and OR-ed into `setTexture` at submit
	// time. Default is "bilinear + wrap" which matches the filter/addr
	// defaults used before 5h.34.
	uint32_t  m_stageSamplerFlags[kMaxTextureStages] = { 0, 0 };

	// Phase 5p — off-screen render targets. Each RT owns a bgfx FrameBuffer
	// and a dedicated view ID; the backbuffer keeps view 0. Inside
	// ApplyDrawState / Clear / Begin_Scene we submit to `m_currentView`
	// instead of hard-coded 0. `setViewOrder` is rebuilt on every
	// Create_Render_Target so all RT views submit before the backbuffer.
	struct BgfxRenderTarget
	{
		bgfx::FrameBufferHandle fb       = BGFX_INVALID_HANDLE;
		bgfx::TextureHandle*    texWrap  = nullptr;   // heap wrapper of fb's color attachment
		// Phase 5q — sibling staging texture flagged BLIT_DST + READ_BACK.
		// Create_Render_Target allocates it lazily on the first Capture_
		// Render_Target call so RTs that never get captured don't pay the
		// extra texture's memory cost.
		bgfx::TextureHandle     captureTex = BGFX_INVALID_HANDLE;
		uint16_t                viewId   = 0;
		uint16_t                width    = 0;
		uint16_t                height   = 0;
	};
	std::vector<BgfxRenderTarget*> m_renderTargets;
	uint16_t m_currentView = 0;   // 0 == backbuffer
	uint16_t m_nextViewId  = 1;   // view IDs handed to new RTs

	void UpdateViewOrder();
};
