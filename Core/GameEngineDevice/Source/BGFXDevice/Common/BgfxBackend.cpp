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

#include <Utility/CppMacros.h>
#include "BGFXDevice/Common/BgfxBackend.h"
#include "WW3D2/RenderBackendRuntime.h"
#include "vector4.h"

#include <bgfx/bgfx.h>
// The shader-compile pipeline (see ../CMakeLists.txt) only emits the profiles
// bgfxToolUtils.cmake selects for this platform — metal/glsl/essl on macOS.
// bgfx's default platform table still lists SPIRV (Vulkan/MoltenVK) and WGSL
// as runtime-supported on macOS, so BGFX_EMBEDDED_SHADER would reference
// array symbols we never generate. Force them off so the macro falls back to
// the empty no-op form. At runtime bgfx auto-detects Metal here, which we do
// compile.
#define BGFX_PLATFORM_SUPPORTS_SPIRV 0
#define BGFX_PLATFORM_SUPPORTS_WGSL  0
#include <bgfx/embedded_shader.h>
#include <bgfx/platform.h>
#include <cstdio>
#include <cstring>

// Phase 5e — embedded shader bytes produced at build time by shaderc via the
// bgfx_compile_shaders(AS_HEADERS) helper. One header per (shader, profile)
// pair, each declaring a byte array named `<shader>_<profile_ext>` (e.g.
// `vs_triangle_mtl`, `fs_triangle_glsl`). BGFX_EMBEDDED_SHADER picks the
// array matching the runtime renderer type. The set of profiles compiled is
// platform-driven by bgfxToolUtils.cmake — on macOS: metal, glsl, essl.
#include <vs_triangle/metal/vs_triangle.sc.bin.h>
#include <vs_triangle/glsl/vs_triangle.sc.bin.h>
#include <vs_triangle/essl/vs_triangle.sc.bin.h>
#include <fs_triangle/metal/fs_triangle.sc.bin.h>
#include <fs_triangle/glsl/fs_triangle.sc.bin.h>
#include <fs_triangle/essl/fs_triangle.sc.bin.h>

// Phase 5f uber-shader permutations.
#include <vs_solid/metal/vs_solid.sc.bin.h>
#include <vs_solid/glsl/vs_solid.sc.bin.h>
#include <vs_solid/essl/vs_solid.sc.bin.h>
#include <fs_solid/metal/fs_solid.sc.bin.h>
#include <fs_solid/glsl/fs_solid.sc.bin.h>
#include <fs_solid/essl/fs_solid.sc.bin.h>
#include <vs_vcolor/metal/vs_vcolor.sc.bin.h>
#include <vs_vcolor/glsl/vs_vcolor.sc.bin.h>
#include <vs_vcolor/essl/vs_vcolor.sc.bin.h>
#include <fs_vcolor/metal/fs_vcolor.sc.bin.h>
#include <fs_vcolor/glsl/fs_vcolor.sc.bin.h>
#include <fs_vcolor/essl/fs_vcolor.sc.bin.h>
#include <vs_tex_lit/metal/vs_tex_lit.sc.bin.h>
#include <vs_tex_lit/glsl/vs_tex_lit.sc.bin.h>
#include <vs_tex_lit/essl/vs_tex_lit.sc.bin.h>
#include <fs_tex_lit/metal/fs_tex_lit.sc.bin.h>
#include <fs_tex_lit/glsl/fs_tex_lit.sc.bin.h>
#include <fs_tex_lit/essl/fs_tex_lit.sc.bin.h>

// Phase 5k multi-stage modulate pair.
#include <vs_tex2/metal/vs_tex2.sc.bin.h>
#include <vs_tex2/glsl/vs_tex2.sc.bin.h>
#include <vs_tex2/essl/vs_tex2.sc.bin.h>
#include <fs_tex2/metal/fs_tex2.sc.bin.h>
#include <fs_tex2/glsl/fs_tex2.sc.bin.h>
#include <fs_tex2/essl/fs_tex2.sc.bin.h>

// Phase 5l 4-slot directional-lighting pair.
#include <vs_tex_mlit/metal/vs_tex_mlit.sc.bin.h>
#include <vs_tex_mlit/glsl/vs_tex_mlit.sc.bin.h>
#include <vs_tex_mlit/essl/vs_tex_mlit.sc.bin.h>
#include <fs_tex_mlit/metal/fs_tex_mlit.sc.bin.h>
#include <fs_tex_mlit/glsl/fs_tex_mlit.sc.bin.h>
#include <fs_tex_mlit/essl/fs_tex_mlit.sc.bin.h>

namespace
{
	const bgfx::EmbeddedShader s_embeddedShaders[] =
	{
		BGFX_EMBEDDED_SHADER(vs_triangle),
		BGFX_EMBEDDED_SHADER(fs_triangle),
		BGFX_EMBEDDED_SHADER(vs_solid),
		BGFX_EMBEDDED_SHADER(fs_solid),
		BGFX_EMBEDDED_SHADER(vs_vcolor),
		BGFX_EMBEDDED_SHADER(fs_vcolor),
		BGFX_EMBEDDED_SHADER(vs_tex_lit),
		BGFX_EMBEDDED_SHADER(fs_tex_lit),
		BGFX_EMBEDDED_SHADER(vs_tex2),
		BGFX_EMBEDDED_SHADER(fs_tex2),
		BGFX_EMBEDDED_SHADER(vs_tex_mlit),
		BGFX_EMBEDDED_SHADER(fs_tex_mlit),
		BGFX_EMBEDDED_SHADER_END()
	};

	// 4×4 identity matrix reused as view/projection/model for the smoke tests
	// so the quad vertices (already in clip-space NDC) render at their
	// hardcoded screen quadrant regardless of any DX8Wrapper state.
	const float kIdentity4x4[16] = {
		1.f, 0.f, 0.f, 0.f,
		0.f, 1.f, 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f,
		0.f, 0.f, 0.f, 1.f,
	};
}

BgfxBackend::~BgfxBackend()
{
	if (m_initialized)
		Shutdown();
}

bool BgfxBackend::Init(void* windowHandle, int width, int height, bool windowed)
{
	if (m_initialized)
		return true;
	if (!windowHandle)
		return false;

	// Single-threaded mode: call renderFrame before init so bgfx doesn't
	// spawn its own render thread.
	bgfx::renderFrame();

	bgfx::Init init;
	init.type = bgfx::RendererType::Count; // auto-detect
	init.resolution.width = static_cast<uint32_t>(width);
	init.resolution.height = static_cast<uint32_t>(height);
	init.resolution.reset = windowed ? BGFX_RESET_NONE : BGFX_RESET_FULLSCREEN;
	init.platformData.nwh = windowHandle;
#if defined(__APPLE__)
	init.platformData.ndt = nullptr;
#elif defined(__linux__)
	init.platformData.ndt = nullptr; // TODO: set X11 Display* or Wayland wl_display*
#endif

	if (!bgfx::init(init))
	{
		fprintf(stderr, "BgfxBackend::Init: bgfx::init failed\n");
		return false;
	}

	m_width = width;
	m_height = height;
	m_initialized = true;

	bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));

	const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
	fprintf(stderr, "BgfxBackend::Init: %s (%dx%d, %s)\n",
	        bgfx::getRendererName(rendererType), width, height,
	        windowed ? "windowed" : "fullscreen");

	// Phase 5h.1 — publish ourselves as the active backend so production
	// DX8 call sites (once they're wired up) can route through the interface.
	RenderBackendRuntime::Set_Active(this);

	return true;
}

void BgfxBackend::Shutdown()
{
	if (!m_initialized)
		return;

	if (m_smokeInit)
	{
		if (bgfx::isValid(m_smokeProgram)) bgfx::destroy(m_smokeProgram);
		if (bgfx::isValid(m_smokeTexture)) bgfx::destroy(m_smokeTexture);
		if (bgfx::isValid(m_smokeSampler)) bgfx::destroy(m_smokeSampler);
		m_smokeProgram = BGFX_INVALID_HANDLE;
		m_smokeTexture = BGFX_INVALID_HANDLE;
		m_smokeSampler = BGFX_INVALID_HANDLE;
		m_smokeInit = false;
	}

	if (m_solidInit)
	{
		if (bgfx::isValid(m_solidProgram))      bgfx::destroy(m_solidProgram);
		if (bgfx::isValid(m_solidColorUniform)) bgfx::destroy(m_solidColorUniform);
		m_solidProgram      = BGFX_INVALID_HANDLE;
		m_solidColorUniform = BGFX_INVALID_HANDLE;
		m_solidInit = false;
	}

	if (m_vcolorInit)
	{
		if (bgfx::isValid(m_vcolorProgram)) bgfx::destroy(m_vcolorProgram);
		m_vcolorProgram = BGFX_INVALID_HANDLE;
		m_vcolorInit = false;
	}

	if (m_litInit)
	{
		if (bgfx::isValid(m_litProgram))    bgfx::destroy(m_litProgram);
		if (bgfx::isValid(m_litTexture))    bgfx::destroy(m_litTexture);
		if (bgfx::isValid(m_litSampler))    bgfx::destroy(m_litSampler);
		if (bgfx::isValid(m_litLightDir))   bgfx::destroy(m_litLightDir);
		if (bgfx::isValid(m_litLightColor)) bgfx::destroy(m_litLightColor);
		m_litProgram    = BGFX_INVALID_HANDLE;
		m_litTexture    = BGFX_INVALID_HANDLE;
		m_litSampler    = BGFX_INVALID_HANDLE;
		m_litLightDir   = BGFX_INVALID_HANDLE;
		m_litLightColor = BGFX_INVALID_HANDLE;
		m_litInit = false;
	}

	DestroyPipelineResources();

	bgfx::shutdown();
	m_initialized = false;

	// Phase 5h.1 — clear the runtime pointer before we vanish so any
	// production call sites that race with teardown see `nullptr` and
	// fall back to their DX8 path (or no-op) rather than dereferencing
	// a destroyed backend.
	if (RenderBackendRuntime::Get_Active() == this)
		RenderBackendRuntime::Set_Active(nullptr);

	fprintf(stderr, "BgfxBackend::Shutdown: complete\n");
}

bool BgfxBackend::Reset(int width, int height, bool windowed)
{
	if (!m_initialized)
		return false;
	m_width = width;
	m_height = height;
	bgfx::reset(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
	            windowed ? BGFX_RESET_NONE : BGFX_RESET_FULLSCREEN);
	bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
	return true;
}

void BgfxBackend::Begin_Scene()
{
	bgfx::touch(m_currentView);
}

void BgfxBackend::End_Scene(bool flip)
{
	bgfx::frame(flip);
}

void BgfxBackend::Clear(bool color, bool depth, const Vector4& clearColor, float z)
{
	uint16_t flags = 0;
	if (color) flags |= BGFX_CLEAR_COLOR;
	if (depth) flags |= BGFX_CLEAR_DEPTH;

	uint8_t r = static_cast<uint8_t>(clearColor.X * 255.0f);
	uint8_t g = static_cast<uint8_t>(clearColor.Y * 255.0f);
	uint8_t b = static_cast<uint8_t>(clearColor.Z * 255.0f);
	uint8_t a = static_cast<uint8_t>(clearColor.W * 255.0f);
	uint32_t rgba = (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(a);

	bgfx::setViewClear(m_currentView, flags, rgba, z, 0);
}

// --- Phase 5g — IRenderBackend production path -------------------------------
//
// All of the methods below were empty stubs from Phase 5a. In 5g they become
// real: transforms + shader/material/light state are cached, vertex + index
// buffers are created as persistent bgfx handles, and Draw_Indexed picks one
// of the four uber programs compiled in Phase 5f based on the vertex layout's
// attribute mask, the material's lighting flag, and the shader's texturing
// flag.
//
// Still deferred:
//   * DX8Wrapper → IRenderBackend adapter (populate descriptors from
//     ShaderClass / VertexMaterialClass / FVFInfoClass) — 5h / 7.
//   * Real TextureBaseClass integration — 5i (DDS via bimg).
//   * Persistent VB/IB cache keyed on `VertexBufferClass*` — 5h.
//   * Multi-stage textures, detail color/alpha, specular — 5h.

#include "WW3D2/BackendDescriptors.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

namespace
{

uint64_t TranslateDepthCmp(ShaderStateDesc::DepthCmp c)
{
	switch (c)
	{
	case ShaderStateDesc::DEPTH_NEVER:    return BGFX_STATE_DEPTH_TEST_NEVER;
	case ShaderStateDesc::DEPTH_LESS:     return BGFX_STATE_DEPTH_TEST_LESS;
	case ShaderStateDesc::DEPTH_EQUAL:    return BGFX_STATE_DEPTH_TEST_EQUAL;
	case ShaderStateDesc::DEPTH_LEQUAL:   return BGFX_STATE_DEPTH_TEST_LEQUAL;
	case ShaderStateDesc::DEPTH_GREATER:  return BGFX_STATE_DEPTH_TEST_GREATER;
	case ShaderStateDesc::DEPTH_NOTEQUAL: return BGFX_STATE_DEPTH_TEST_NOTEQUAL;
	case ShaderStateDesc::DEPTH_GEQUAL:   return BGFX_STATE_DEPTH_TEST_GEQUAL;
	case ShaderStateDesc::DEPTH_ALWAYS:   return BGFX_STATE_DEPTH_TEST_ALWAYS;
	}
	return BGFX_STATE_DEPTH_TEST_LEQUAL;
}

uint64_t TranslateBlendFactor(ShaderStateDesc::BlendFactor b, bool isSrc)
{
	switch (b)
	{
	case ShaderStateDesc::BLEND_ZERO:           return BGFX_STATE_BLEND_ZERO;
	case ShaderStateDesc::BLEND_ONE:            return BGFX_STATE_BLEND_ONE;
	case ShaderStateDesc::BLEND_SRC_COLOR:      return BGFX_STATE_BLEND_SRC_COLOR;
	case ShaderStateDesc::BLEND_INV_SRC_COLOR:  return BGFX_STATE_BLEND_INV_SRC_COLOR;
	case ShaderStateDesc::BLEND_SRC_ALPHA:      return BGFX_STATE_BLEND_SRC_ALPHA;
	case ShaderStateDesc::BLEND_INV_SRC_ALPHA:  return BGFX_STATE_BLEND_INV_SRC_ALPHA;
	case ShaderStateDesc::BLEND_DST_COLOR:      return BGFX_STATE_BLEND_DST_COLOR;
	case ShaderStateDesc::BLEND_INV_DST_COLOR:  return BGFX_STATE_BLEND_INV_DST_COLOR;
	}
	return isSrc ? BGFX_STATE_BLEND_ONE : BGFX_STATE_BLEND_ZERO;
}

uint64_t BuildStateMask(const ShaderStateDesc& s)
{
	uint64_t state = BGFX_STATE_MSAA;
	if (s.colorWrite) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
	if (s.depthWrite) state |= BGFX_STATE_WRITE_Z;
	state |= TranslateDepthCmp(s.depthCmp);
	if (s.cullEnable) state |= BGFX_STATE_CULL_CCW; // bgfx default winding for CCW front
	// Blend: only encode when not the opaque default (ONE / ZERO).
	if (!(s.srcBlend == ShaderStateDesc::BLEND_ONE && s.dstBlend == ShaderStateDesc::BLEND_ZERO))
	{
		const uint64_t src = TranslateBlendFactor(s.srcBlend, true);
		const uint64_t dst = TranslateBlendFactor(s.dstBlend, false);
		state |= BGFX_STATE_BLEND_FUNC(src, dst);
	}
	return state;
}

// Attribute-presence mask: one bit per VertexAttributeDesc::Semantic.
uint32_t AttrMaskFromLayout(const VertexLayoutDesc& d)
{
	uint32_t mask = 0;
	for (uint16_t i = 0; i < d.attrCount; ++i)
		mask |= (1u << d.attrs[i].semantic);
	return mask;
}

void TranslateLayout(const VertexLayoutDesc& d, bgfx::VertexLayout& out)
{
	out.begin();
	for (uint16_t i = 0; i < d.attrCount; ++i)
	{
		const VertexAttributeDesc& a = d.attrs[i];
		bgfx::Attrib::Enum bgfxAttr = bgfx::Attrib::Position;
		switch (a.semantic)
		{
		case VertexAttributeDesc::SEM_POSITION:  bgfxAttr = bgfx::Attrib::Position;  break;
		case VertexAttributeDesc::SEM_NORMAL:    bgfxAttr = bgfx::Attrib::Normal;    break;
		case VertexAttributeDesc::SEM_COLOR0:    bgfxAttr = bgfx::Attrib::Color0;    break;
		case VertexAttributeDesc::SEM_TEXCOORD0: bgfxAttr = bgfx::Attrib::TexCoord0; break;
		case VertexAttributeDesc::SEM_TEXCOORD1: bgfxAttr = bgfx::Attrib::TexCoord1; break;
		default: break;
		}
		const bool normalized = (a.type == VertexAttributeDesc::TYPE_UINT8_NORMALIZED);
		const bgfx::AttribType::Enum bgfxType =
			normalized ? bgfx::AttribType::Uint8 : bgfx::AttribType::Float;
		out.add(bgfxAttr, a.count, bgfxType, normalized);
	}
	out.end();
}

} // namespace

void BgfxBackend::InitPipelineResources()
{
	if (m_pipelineInit)
		return;

	const bgfx::RendererType::Enum t = bgfx::getRendererType();
	m_progSolid  = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "vs_solid"),
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "fs_solid"), true);
	m_progVColor = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "vs_vcolor"),
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "fs_vcolor"), true);
	m_progTex    = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "vs_triangle"),
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "fs_triangle"), true);
	m_progTexMLit = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "vs_tex_mlit"),
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "fs_tex_mlit"), true);
	m_progTex2   = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "vs_tex2"),
		bgfx::createEmbeddedShader(s_embeddedShaders, t, "fs_tex2"), true);

	m_uSolidColor    = bgfx::createUniform("u_solidColor",    bgfx::UniformType::Vec4);
	m_uSampler       = bgfx::createUniform("s_texture",       bgfx::UniformType::Sampler);
	m_uSampler1      = bgfx::createUniform("s_texture1",      bgfx::UniformType::Sampler);
	m_uLightDirArr   = bgfx::createUniform("u_lightDirArr",   bgfx::UniformType::Vec4, kMaxLights);
	m_uLightColorArr = bgfx::createUniform("u_lightColorArr", bgfx::UniformType::Vec4, kMaxLights);
	m_uLightPosArr   = bgfx::createUniform("u_lightPosArr",   bgfx::UniformType::Vec4, kMaxLights);
	m_uLightSpotArr  = bgfx::createUniform("u_lightSpotArr",  bgfx::UniformType::Vec4, kMaxLights);
	m_uLightSpecArr  = bgfx::createUniform("u_lightSpecArr",  bgfx::UniformType::Vec4, kMaxLights);
	m_uMaterialSpec  = bgfx::createUniform("u_materialSpec",  bgfx::UniformType::Vec4);
	m_uCutoutRef     = bgfx::createUniform("u_cutoutRef",     bgfx::UniformType::Vec4);
	m_uFogColor      = bgfx::createUniform("u_fogColor",      bgfx::UniformType::Vec4);
	m_uFogRange      = bgfx::createUniform("u_fogRange",      bgfx::UniformType::Vec4);

	// 2×2 white placeholder so the textured/lit programs have something to
	// sample until Phase 5i brings real texture upload.
	static const uint8_t kWhite[2*2*4] = {
		0xFF,0xFF,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF,
	};
	m_placeholderTexture = bgfx::createTexture2D(
		2, 2, false, 1, bgfx::TextureFormat::RGBA8,
		BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT
		| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
		bgfx::copy(kWhite, sizeof(kWhite)));

	m_pipelineInit = true;
}

void BgfxBackend::DestroyPipelineResources()
{
	// Phase 5p — release any RTs the caller forgot about. bgfx::destroy on
	// the FrameBuffer disposes its attached color + depth textures too;
	// the heap wrapper we allocated for Set_Texture binding is ours to
	// delete separately.
	for (auto* rt : m_renderTargets)
	{
		if (rt)
		{
			if (bgfx::isValid(rt->captureTex))
				bgfx::destroy(rt->captureTex);
			if (bgfx::isValid(rt->fb))
				bgfx::destroy(rt->fb);
			delete rt->texWrap;
			delete rt;
		}
	}
	m_renderTargets.clear();
	m_currentView = 0;
	m_nextViewId  = 1;

	// Free any user-owned textures that weren't released via Destroy_Texture.
	for (auto* owned : m_ownedTextures)
	{
		if (owned)
		{
			if (bgfx::isValid(*owned))
				bgfx::destroy(*owned);
			delete owned;
		}
	}
	m_ownedTextures.clear();
	m_textureMipCounts.clear();
	for (unsigned i = 0; i < kMaxTextureStages; ++i)
		m_stageTexture[i] = 0;

	if (!m_pipelineInit)
		return;
	if (bgfx::isValid(m_progSolid))          bgfx::destroy(m_progSolid);
	if (bgfx::isValid(m_progVColor))         bgfx::destroy(m_progVColor);
	if (bgfx::isValid(m_progTex))            bgfx::destroy(m_progTex);
	if (bgfx::isValid(m_progTexMLit))        bgfx::destroy(m_progTexMLit);
	if (bgfx::isValid(m_progTex2))           bgfx::destroy(m_progTex2);
	if (bgfx::isValid(m_uSolidColor))        bgfx::destroy(m_uSolidColor);
	if (bgfx::isValid(m_uSampler))           bgfx::destroy(m_uSampler);
	if (bgfx::isValid(m_uSampler1))          bgfx::destroy(m_uSampler1);
	if (bgfx::isValid(m_uLightDirArr))       bgfx::destroy(m_uLightDirArr);
	if (bgfx::isValid(m_uLightColorArr))     bgfx::destroy(m_uLightColorArr);
	if (bgfx::isValid(m_uLightPosArr))       bgfx::destroy(m_uLightPosArr);
	if (bgfx::isValid(m_uLightSpotArr))      bgfx::destroy(m_uLightSpotArr);
	if (bgfx::isValid(m_uLightSpecArr))      bgfx::destroy(m_uLightSpecArr);
	if (bgfx::isValid(m_uMaterialSpec))      bgfx::destroy(m_uMaterialSpec);
	if (bgfx::isValid(m_uCutoutRef))          bgfx::destroy(m_uCutoutRef);
	if (bgfx::isValid(m_uFogColor))           bgfx::destroy(m_uFogColor);
	if (bgfx::isValid(m_uFogRange))           bgfx::destroy(m_uFogRange);
	if (bgfx::isValid(m_placeholderTexture)) bgfx::destroy(m_placeholderTexture);
	if (bgfx::isValid(m_currentVB))          bgfx::destroy(m_currentVB);
	if (bgfx::isValid(m_currentIB))          bgfx::destroy(m_currentIB);
	m_progSolid = m_progVColor = m_progTex = m_progTexMLit = m_progTex2 = BGFX_INVALID_HANDLE;
	m_uSolidColor = m_uSampler = m_uSampler1 = BGFX_INVALID_HANDLE;
	m_uLightDirArr = m_uLightColorArr = m_uLightPosArr = m_uLightSpotArr = BGFX_INVALID_HANDLE;
	m_uLightSpecArr = m_uMaterialSpec = m_uCutoutRef = BGFX_INVALID_HANDLE;
	m_uFogColor = m_uFogRange = BGFX_INVALID_HANDLE;
	m_placeholderTexture = BGFX_INVALID_HANDLE;
	m_currentVB = BGFX_INVALID_HANDLE;
	m_currentIB = BGFX_INVALID_HANDLE;
	m_pipelineInit = false;
}

void BgfxBackend::Set_World_Transform(const float m[16])
{
	std::memcpy(m_worldMtx, m, sizeof(m_worldMtx));
}

void BgfxBackend::Set_View_Transform(const float m[16])
{
	std::memcpy(m_viewMtx, m, sizeof(m_viewMtx));
	m_viewProjDirty = true;
}

void BgfxBackend::Set_Projection_Transform(const float m[16])
{
	std::memcpy(m_projMtx, m, sizeof(m_projMtx));
	m_viewProjDirty = true;
}

void BgfxBackend::Set_Shader(const ShaderStateDesc& shader)
{
	m_shader = shader;
}

void BgfxBackend::Set_Material(const MaterialDesc& material)
{
	m_material = material;
}

void BgfxBackend::Set_Light(unsigned index, const LightDesc* light)
{
	// Phase 5l — 4 slots. `light == nullptr` disables the slot; otherwise the
	// descriptor is copied and slot 0's `ambient` field is treated as the
	// scene-global ambient term by the multi-lit shader.
	if (index >= kMaxLights)
		return;
	m_lightEnabled[index] = (light != nullptr);
	if (light)
		m_lights[index] = *light;
}

// --- Phase 5i texture management ---------------------------------------------

uintptr_t BgfxBackend::Create_Texture_RGBA8(const void* pixels,
                                            uint16_t width,
                                            uint16_t height,
                                            bool mipmap)
{
	if (!m_initialized || width == 0 || height == 0)
		return 0;
	InitPipelineResources();

	uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	// Point-sample without mips; trilinear with mips for a more game-like look.
	if (!mipmap)
	{
		flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
	}

	const bool hasMips = mipmap;

	// Phase 5h.28 — accept `pixels == nullptr` for procedural textures. Callers
	// that build their own content (render targets sampled back, CPU-driven
	// paint buffers) allocate an empty texture here and populate it later via
	// a future Update_Texture path. bgfx::createTexture2D with NULL memory
	// yields an uninitialized texture — contents are driver-dependent until
	// the first write, so never sample without filling.
	const bgfx::Memory* mem = nullptr;
	if (pixels != nullptr)
	{
		const uint32_t byteSize = uint32_t(width) * uint32_t(height) * 4u;
		mem = bgfx::copy(pixels, byteSize);
	}

	bgfx::TextureHandle handle = bgfx::createTexture2D(
		width, height, hasMips, 1, bgfx::TextureFormat::RGBA8, flags, mem);

	if (!bgfx::isValid(handle))
		return 0;

	auto* owned = new bgfx::TextureHandle(handle);
	m_ownedTextures.push_back(owned);
	const uintptr_t result = reinterpret_cast<uintptr_t>(owned);

	// Phase 5h.36 — record mip count. `mipmap==true` lets bgfx generate a full
	// chain down to 1x1; we don't know the exact level count without a log2
	// of max(w,h), so compute it. `mipmap==false` → 1 level.
	uint8_t mips = 1;
	if (mipmap)
	{
		uint16_t m = width > height ? width : height;
		while (m > 1) { m >>= 1; ++mips; }
	}
	m_textureMipCounts[result] = mips;

	return result;
}

void BgfxBackend::Update_Texture_RGBA8(uintptr_t handle,
                                        const void* pixels,
                                        uint16_t width,
                                        uint16_t height)
{
	// Phase 5h.30 — upload `width * height * 4` bytes to the texture at mip 0.
	// Handle 0 or null pixels is a silent no-op (callers may hold
	// unallocated handles). bgfx::updateTexture2D copies the data before
	// returning; callers can free their buffer immediately.
	if (!m_initialized || handle == 0 || pixels == nullptr || width == 0 || height == 0)
		return;

	auto* owned = reinterpret_cast<bgfx::TextureHandle*>(handle);
	if (!bgfx::isValid(*owned))
		return;

	const uint32_t byteSize = uint32_t(width) * uint32_t(height) * 4u;
	bgfx::updateTexture2D(
		*owned,
		/*layer=*/0,
		/*mip=*/0,
		/*x=*/0, /*y=*/0,
		width, height,
		bgfx::copy(pixels, byteSize));
}

uintptr_t BgfxBackend::Create_Texture_From_Memory(const void* data, uint32_t size)
{
	if (!m_initialized || data == nullptr || size == 0)
		return 0;
	InitPipelineResources();

	// bimg ships a simple heap-backed DefaultAllocator; one shared instance
	// is plenty for the decode path. Static-local initialization is thread-
	// safe per C++11 and happens once for the process lifetime.
	static bx::DefaultAllocator s_allocator;

	bimg::ImageContainer* image = bimg::imageParse(&s_allocator, data, size);
	if (image == nullptr)
	{
		fprintf(stderr, "BgfxBackend::Create_Texture_From_Memory: bimg::imageParse failed (size=%u)\n", size);
		return 0;
	}

	// The mip-chain bytes live at `image->m_data`; bgfx::makeRef keeps the
	// pointer valid until the release callback frees the ImageContainer.
	const bgfx::Memory* mem = bgfx::makeRef(
		image->m_data, image->m_size,
		[](void*, void* userData) {
			auto* img = static_cast<bimg::ImageContainer*>(userData);
			bimg::imageFree(img);
		},
		image);

	const bool hasMips = image->m_numMips > 1;
	uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	if (!hasMips)
		flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

	bgfx::TextureHandle handle = bgfx::createTexture2D(
		static_cast<uint16_t>(image->m_width),
		static_cast<uint16_t>(image->m_height),
		hasMips, 1,
		static_cast<bgfx::TextureFormat::Enum>(image->m_format),
		flags, mem);

	if (!bgfx::isValid(handle))
	{
		// bgfx took ownership of `mem`; the release callback will free the
		// ImageContainer even if createTexture2D failed.
		return 0;
	}

	auto* owned = new bgfx::TextureHandle(handle);
	m_ownedTextures.push_back(owned);
	const uintptr_t result = reinterpret_cast<uintptr_t>(owned);

	// Phase 5h.36 — bimg tells us the pre-baked mip count (>=1).
	m_textureMipCounts[result] = static_cast<uint8_t>(image->m_numMips);

	return result;
}

void BgfxBackend::Destroy_Texture(uintptr_t handle)
{
	if (handle == 0)
		return;
	auto* owned = reinterpret_cast<bgfx::TextureHandle*>(handle);
	for (auto it = m_ownedTextures.begin(); it != m_ownedTextures.end(); ++it)
	{
		if (*it == owned)
		{
			if (bgfx::isValid(*owned))
				bgfx::destroy(*owned);
			delete owned;
			m_ownedTextures.erase(it);
			break;
		}
	}
	// Unbind if this was the current texture on any stage.
	for (unsigned i = 0; i < kMaxTextureStages; ++i)
		if (m_stageTexture[i] == handle)
			m_stageTexture[i] = 0;

	// Phase 5h.36 — drop any mip-count bookkeeping.
	m_textureMipCounts.erase(handle);
}

uint8_t BgfxBackend::Texture_Mip_Count(uintptr_t handle)
{
	auto it = m_textureMipCounts.find(handle);
	return (it == m_textureMipCounts.end()) ? uint8_t(0) : it->second;
}

void BgfxBackend::Set_Texture(unsigned stage, uintptr_t handle)
{
	if (stage >= kMaxTextureStages)
		return;
	m_stageTexture[stage] = handle;
}

// --- Phase 5h.34 sampler-state routing ---------------------------------------

namespace {

// Map `SamplerStateDesc` → bgfx sampler flag mask. Returning 0 means "use
// defaults" (bilinear + wrap + trilinear-if-mips), which matches the flags
// baked into the bgfx programs pre-5h.34.
uint32_t SamplerFlags(const SamplerStateDesc& s)
{
	uint32_t flags = 0;

	// Min filter: BGFX default is linear (flag 0). Only emit flags when we
	// need point or anisotropic.
	switch (s.minFilter)
	{
	case SamplerStateDesc::FILTER_POINT:        flags |= BGFX_SAMPLER_MIN_POINT; break;
	case SamplerStateDesc::FILTER_ANISOTROPIC:  flags |= BGFX_SAMPLER_MIN_ANISOTROPIC; break;
	case SamplerStateDesc::FILTER_LINEAR:       /* default */ break;
	}
	switch (s.magFilter)
	{
	case SamplerStateDesc::FILTER_POINT:        flags |= BGFX_SAMPLER_MAG_POINT; break;
	case SamplerStateDesc::FILTER_ANISOTROPIC:  flags |= BGFX_SAMPLER_MAG_ANISOTROPIC; break;
	case SamplerStateDesc::FILTER_LINEAR:       /* default */ break;
	}

	// Mip filter: if there are no mips the texture was created with 0 levels
	// past mip 0, and bgfx must not try to sample linearly between them — force
	// MIP_POINT. When mips exist, LINEAR is the bgfx default (no flag needed).
	if (!s.hasMips || s.mipFilter == SamplerStateDesc::FILTER_POINT)
		flags |= BGFX_SAMPLER_MIP_POINT;

	// Address modes. BGFX default is repeat/wrap (flag 0).
	if (s.addressU == SamplerStateDesc::ADDRESS_CLAMP) flags |= BGFX_SAMPLER_U_CLAMP;
	if (s.addressV == SamplerStateDesc::ADDRESS_CLAMP) flags |= BGFX_SAMPLER_V_CLAMP;

	// Anisotropy is controlled by the filter mode bits above — bgfx doesn't
	// expose an explicit max-aniso sampler flag (it's a backend-level setting
	// applied globally via bgfx::Init anisotropy caps). `maxAnisotropy` is
	// stored for future per-sampler backends but currently unused here.

	return flags;
}

} // namespace

void BgfxBackend::Set_Sampler_State(unsigned stage, const SamplerStateDesc& sampler)
{
	if (stage >= kMaxTextureStages)
		return;
	m_stageSamplerFlags[stage] = SamplerFlags(sampler);
}

// --- Phase 5p render-target management ---------------------------------------

void BgfxBackend::UpdateViewOrder()
{
	// Default bgfx view order is ascending view ID. We need RTs (views 1..N)
	// to submit before the backbuffer (view 0) every frame, otherwise the
	// backbuffer pass sampling an RT reads a stale frame. Explicit order:
	// [1, 2, ..., N, 0].
	if (m_renderTargets.empty())
		return;
	const size_t count = m_renderTargets.size() + 1;
	std::vector<bgfx::ViewId> order;
	order.reserve(count);
	for (auto* rt : m_renderTargets)
		order.push_back(rt->viewId);
	order.push_back(0);
	bgfx::setViewOrder(0, static_cast<uint16_t>(count), order.data());
}

uintptr_t BgfxBackend::Create_Render_Target(uint16_t width, uint16_t height, bool hasDepth)
{
	if (!m_initialized || width == 0 || height == 0)
		return 0;
	InitPipelineResources();

	// Color attachment — BGRA8 is the portable swap-chain-compatible format;
	// using it keeps render-to-texture → sample-to-backbuffer conversion-free.
	// RT + READ_BACK together isn't universally supported (Metal in
	// particular rejects the combination); Phase 5q's capture path goes
	// through a separate `m_capture` BLIT_DST + READ_BACK sibling that we
	// blit into on demand.
	const uint64_t colorFlags = BGFX_TEXTURE_RT
		| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	bgfx::TextureHandle attachments[2];
	uint8_t attachCount = 0;
	attachments[attachCount++] = bgfx::createTexture2D(
		width, height, false, 1, bgfx::TextureFormat::BGRA8, colorFlags);
	if (hasDepth)
	{
		const uint64_t depthFlags = BGFX_TEXTURE_RT_WRITE_ONLY;
		attachments[attachCount++] = bgfx::createTexture2D(
			width, height, false, 1, bgfx::TextureFormat::D24S8, depthFlags);
	}

	bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(attachCount, attachments, true /* destroyTextures */);
	if (!bgfx::isValid(fb))
		return 0;

	auto* rt = new BgfxRenderTarget();
	rt->fb      = fb;
	rt->width   = width;
	rt->height  = height;
	rt->viewId  = m_nextViewId++;
	rt->texWrap = new bgfx::TextureHandle(bgfx::getTexture(fb, 0));

	bgfx::setViewFrameBuffer(rt->viewId, fb);
	bgfx::setViewRect(rt->viewId, 0, 0, width, height);

	m_renderTargets.push_back(rt);
	UpdateViewOrder();
	return reinterpret_cast<uintptr_t>(rt);
}

void BgfxBackend::Destroy_Render_Target(uintptr_t handle)
{
	if (handle == 0)
		return;
	auto* target = reinterpret_cast<BgfxRenderTarget*>(handle);
	for (auto it = m_renderTargets.begin(); it != m_renderTargets.end(); ++it)
	{
		if (*it == target)
		{
			if (bgfx::isValid(target->captureTex))
				bgfx::destroy(target->captureTex);
			if (bgfx::isValid(target->fb))
				bgfx::destroy(target->fb);  // destroys attached textures too
			// Unbind from stages before deleting the wrapper.
			for (unsigned i = 0; i < kMaxTextureStages; ++i)
			{
				if (m_stageTexture[i] == reinterpret_cast<uintptr_t>(target->texWrap))
					m_stageTexture[i] = 0;
			}
			delete target->texWrap;
			if (m_currentView == target->viewId)
				m_currentView = 0;
			delete target;
			m_renderTargets.erase(it);
			UpdateViewOrder();
			return;
		}
	}
}

void BgfxBackend::Set_Render_Target(uintptr_t handle)
{
	if (handle == 0)
	{
		m_currentView = 0;
	}
	else
	{
		auto* target = reinterpret_cast<BgfxRenderTarget*>(handle);
		m_currentView = target->viewId;
	}
	// The new view hasn't had its view-proj uploaded this frame; force the
	// next ApplyDrawState to resend setViewTransform for whichever view is
	// now active.
	m_viewProjDirty = true;
}

uintptr_t BgfxBackend::Get_Render_Target_Texture(uintptr_t handle)
{
	if (handle == 0)
		return 0;
	auto* target = reinterpret_cast<BgfxRenderTarget*>(handle);
	return reinterpret_cast<uintptr_t>(target->texWrap);
}

bool BgfxBackend::Capture_Render_Target(uintptr_t handle, void* pixels, uint32_t byteCapacity)
{
	if (!m_initialized || handle == 0 || pixels == nullptr)
		return false;
	auto* target = reinterpret_cast<BgfxRenderTarget*>(handle);
	if (!bgfx::isValid(*target->texWrap))
		return false;

	const uint32_t required = uint32_t(target->width) * uint32_t(target->height) * 4u;
	if (byteCapacity < required)
		return false;

	// Lazy-allocate the staging texture. On Metal the RT color texture can't
	// be flagged READ_BACK directly, so we maintain a sibling BLIT_DST +
	// READ_BACK texture and copy the color attachment into it on demand.
	if (!bgfx::isValid(target->captureTex))
	{
		target->captureTex = bgfx::createTexture2D(
			target->width, target->height, false, 1,
			bgfx::TextureFormat::BGRA8,
			BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
		if (!bgfx::isValid(target->captureTex))
			return false;
	}

	// Schedule the blit on the RT's own view so it executes immediately
	// after whatever draws the caller issued, then the readback. bgfx::blit
	// runs at view-submit time; bgfx::readTexture returns the frame number
	// after which the destination's CPU copy is guaranteed populated.
	bgfx::blit(target->viewId, target->captureTex, 0, 0, *target->texWrap,
	           0, 0, target->width, target->height);
	const uint32_t targetFrame = bgfx::readTexture(target->captureTex, pixels);
	for (uint32_t i = 0; i < 8; ++i)
	{
		const uint32_t current = bgfx::frame();
		if (current >= targetFrame)
			return true;
	}
	return false;
}

void BgfxBackend::Set_Vertex_Buffer(const void* data,
                                    unsigned vertexCount,
                                    const VertexLayoutDesc& layout)
{
	if (!m_initialized)
		return;
	InitPipelineResources();

	if (bgfx::isValid(m_currentVB))
	{
		bgfx::destroy(m_currentVB);
		m_currentVB = BGFX_INVALID_HANDLE;
	}
	if (vertexCount == 0 || data == nullptr)
		return;

	m_currentVBDesc = layout;
	m_vbAttrMask = AttrMaskFromLayout(layout);
	TranslateLayout(layout, m_currentVBLayout);

	const uint32_t byteSize = static_cast<uint32_t>(layout.stride) * vertexCount;
	m_currentVB = bgfx::createVertexBuffer(bgfx::copy(data, byteSize), m_currentVBLayout);
}

void BgfxBackend::Set_Index_Buffer(const uint16_t* data, unsigned indexCount)
{
	if (!m_initialized)
		return;
	InitPipelineResources();

	if (bgfx::isValid(m_currentIB))
	{
		bgfx::destroy(m_currentIB);
		m_currentIB = BGFX_INVALID_HANDLE;
	}
	if (indexCount == 0 || data == nullptr)
		return;

	m_currentIB = bgfx::createIndexBuffer(bgfx::copy(data, indexCount * sizeof(uint16_t)));
}

void BgfxBackend::Set_Viewport(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
	// Phase 5h.6 — bgfx tracks a viewport per view, so the draw lands on
	// whichever RT `m_currentView` currently points at. Passing `0×0` resets
	// to the full current target so the game can drop back to the backbuffer
	// viewport without the backend re-caching dimensions itself.
	if (!m_initialized)
		return;

	if (width == 0 || height == 0)
	{
		uint16_t w = static_cast<uint16_t>(m_width);
		uint16_t h = static_cast<uint16_t>(m_height);
		if (m_currentView != 0)
		{
			for (auto* rt : m_renderTargets)
			{
				if (rt && rt->viewId == m_currentView)
				{
					w = rt->width;
					h = rt->height;
					break;
				}
			}
		}
		bgfx::setViewRect(m_currentView, 0, 0, w, h);
		return;
	}

	// bgfx's setViewRect takes origin in pixels from top-left and unsigned
	// extents. Negative `x`/`y` aren't supported — clamp to 0 and shrink the
	// visible extent so the clipped rect still lands inside the target.
	uint16_t ox = (x < 0) ? static_cast<uint16_t>(0) : static_cast<uint16_t>(x);
	uint16_t oy = (y < 0) ? static_cast<uint16_t>(0) : static_cast<uint16_t>(y);
	bgfx::setViewRect(m_currentView, ox, oy, width, height);
}

static bgfx::ProgramHandle SelectProgram(uint32_t attrMask,
                                         const ShaderStateDesc& shader,
                                         const MaterialDesc& material,
                                         bool hasStage1,
                                         bgfx::ProgramHandle solid,
                                         bgfx::ProgramHandle vcolor,
                                         bgfx::ProgramHandle tex,
                                         bgfx::ProgramHandle texMLit,
                                         bgfx::ProgramHandle tex2)
{
	const bool hasNormal = (attrMask & (1u << VertexAttributeDesc::SEM_NORMAL))    != 0;
	const bool hasColor0 = (attrMask & (1u << VertexAttributeDesc::SEM_COLOR0))    != 0;
	const bool hasUV0    = (attrMask & (1u << VertexAttributeDesc::SEM_TEXCOORD0)) != 0;
	const bool hasUV1    = (attrMask & (1u << VertexAttributeDesc::SEM_TEXCOORD1)) != 0;

	// Phase 5k — two-stage modulate wins over the lit / single-tex paths when
	// the caller explicitly bound a second texture and supplied a UV1 stream.
	if (shader.texturingEnable && hasStage1 && hasUV0 && hasUV1)
		return tex2;
	// Phase 5l — N-slot directional lighting whenever a caller enabled
	// useLighting and supplied a normal + UV0. Single-light callers land here
	// too; their unused slots upload as zero-color so the accumulation loop
	// collapses to the single-light case.
	if (material.useLighting && hasNormal && hasUV0)
		return texMLit;
	if (shader.texturingEnable && hasUV0)
		return tex;
	if (hasColor0)
		return vcolor;
	return solid;
}

bgfx::ProgramHandle BgfxBackend::ApplyDrawState(uint32_t attrMask)
{
	// Phase 5p — m_viewProjDirty is always re-uploaded after a
	// Set_Render_Target switch (the new view has no transform state), so
	// the simple "per-frame" invalidation from 5g still works here.
	if (m_viewProjDirty)
	{
		bgfx::setViewTransform(m_currentView, m_viewMtx, m_projMtx);
		m_viewProjDirty = false;
	}
	bgfx::setTransform(m_worldMtx);

	const bool hasStage1 = (m_stageTexture[1] != 0);
	const bgfx::ProgramHandle prog = SelectProgram(
		attrMask, m_shader, m_material, hasStage1,
		m_progSolid, m_progVColor, m_progTex, m_progTexMLit, m_progTex2);

	// u_solidColor doubles as the "material diffuse × opacity" push for the
	// unlit solid program. The textured programs read color from the vertex
	// stream (vcolor) or texture × vertex color (tex / tex_mlit).
	const float solidColor[4] = {
		m_material.diffuse[0],
		m_material.diffuse[1],
		m_material.diffuse[2],
		m_material.opacity
	};
	bgfx::setUniform(m_uSolidColor, solidColor);

	// Phase 5l — upload the full 4-slot light arrays only when the multi-lit
	// program is actually selected. Disabled slots zero-fill their color so
	// the accumulation loop in vs_tex_mlit contributes nothing for them.
	// Slot 0's ambient becomes the scene-global ambient term.
	if (prog.idx == m_progTexMLit.idx)
	{
		float dirs[kMaxLights * 4];
		float colors[kMaxLights * 4];
		float positions[kMaxLights * 4];
		float spots[kMaxLights * 4];
		float specs[kMaxLights * 4];
		const float globalAmbient = m_lightEnabled[0] ? m_lights[0].ambient : 0.0f;
		for (unsigned i = 0; i < kMaxLights; ++i)
		{
			if (m_lightEnabled[i])
			{
				dirs[i*4+0] = m_lights[i].direction[0];
				dirs[i*4+1] = m_lights[i].direction[1];
				dirs[i*4+2] = m_lights[i].direction[2];
				dirs[i*4+3] = globalAmbient;
				// Phase 5h.10 — pack spot inner cos into the previously unused
				// color.w channel; the shader only reads it when the slot's
				// spot outer cos >= 0.
				colors[i*4+0] = m_lights[i].color[0] * m_lights[i].intensity;
				colors[i*4+1] = m_lights[i].color[1] * m_lights[i].intensity;
				colors[i*4+2] = m_lights[i].color[2] * m_lights[i].intensity;
				colors[i*4+3] = m_lights[i].spotInnerCos;
				// Phase 5h.9 — point/spot lights carry world-space position +
				// positive attenuation range in .w; directional slots upload
				// .w = 0 (branchless `step(0.0001, .w)` selects directional).
				const bool isPointOrSpot = (m_lights[i].type != LightDesc::LIGHT_DIRECTIONAL);
				positions[i*4+0] = m_lights[i].position[0];
				positions[i*4+1] = m_lights[i].position[1];
				positions[i*4+2] = m_lights[i].position[2];
				positions[i*4+3] = isPointOrSpot ? m_lights[i].attenuationRange : 0.0f;
				// Phase 5h.10 — spot direction + outer cos. `outerCos < 0`
				// flags the slot as "not a spot"; directional and point
				// lights upload -1 so the shader's cone mask collapses to 1.
				const bool isSpot = (m_lights[i].type == LightDesc::LIGHT_SPOT);
				spots[i*4+0] = m_lights[i].spotDirection[0];
				spots[i*4+1] = m_lights[i].spotDirection[1];
				spots[i*4+2] = m_lights[i].spotDirection[2];
				spots[i*4+3] = isSpot ? m_lights[i].spotOuterCos : -1.0f;
				// Phase 5h.11 — per-light specular color. Zero-fill disables
				// the specular contribution from this slot without a branch.
				specs[i*4+0] = m_lights[i].specular[0] * m_lights[i].intensity;
				specs[i*4+1] = m_lights[i].specular[1] * m_lights[i].intensity;
				specs[i*4+2] = m_lights[i].specular[2] * m_lights[i].intensity;
				specs[i*4+3] = 0.0f;
			}
			else
			{
				dirs[i*4+0] = 0.0f; dirs[i*4+1] = -1.0f; dirs[i*4+2] = 0.0f; dirs[i*4+3] = globalAmbient;
				colors[i*4+0] = 0.0f; colors[i*4+1] = 0.0f; colors[i*4+2] = 0.0f; colors[i*4+3] = 0.0f;
				positions[i*4+0] = 0.0f; positions[i*4+1] = 0.0f; positions[i*4+2] = 0.0f; positions[i*4+3] = 0.0f;
				spots[i*4+0] = 0.0f; spots[i*4+1] = 0.0f; spots[i*4+2] = 1.0f; spots[i*4+3] = -1.0f;
				specs[i*4+0] = 0.0f; specs[i*4+1] = 0.0f; specs[i*4+2] = 0.0f; specs[i*4+3] = 0.0f;
			}
		}
		bgfx::setUniform(m_uLightDirArr,   dirs,      kMaxLights);
		bgfx::setUniform(m_uLightColorArr, colors,    kMaxLights);
		bgfx::setUniform(m_uLightPosArr,   positions, kMaxLights);
		bgfx::setUniform(m_uLightSpotArr,  spots,     kMaxLights);
		bgfx::setUniform(m_uLightSpecArr,  specs,     kMaxLights);

		// Material specular (rgb + shininess). Uploaded whenever mlit is
		// selected so a 0-color descriptor still zeros last-frame state.
		const float matSpec[4] = {
			m_material.specularColor[0],
			m_material.specularColor[1],
			m_material.specularColor[2],
			m_material.specularPower
		};
		bgfx::setUniform(m_uMaterialSpec, matSpec);
	}

	// Phase 5n — cutout threshold. Only the three textured programs
	// (tex / tex_mlit / tex2) declare `u_cutoutRef`; the solid / vcolor
	// programs discard nothing. Passing zero keeps the discard branch cold
	// for callers that don't use alpha-test (alpha is always >= 0).
	const bool isTexturedProg = (prog.idx == m_progTex.idx)
	                         || (prog.idx == m_progTexMLit.idx)
	                         || (prog.idx == m_progTex2.idx);
	if (isTexturedProg)
	{
		const float ar = m_shader.alphaTest ? m_shader.alphaTestRef : 0.0f;
		const float alphaRef[4] = { ar, 0.0f, 0.0f, 0.0f };
		bgfx::setUniform(m_uCutoutRef, alphaRef);
	}

	// Phase 5o — linear vertex fog. All five uber programs declare
	// u_fogColor + u_fogRange, so the upload is unconditional. `.z` carries
	// the enable flag; the vertex shader's `mix(1.0, fogFactor, enable)`
	// drops to 1.0 (no fog) when disabled. fogEnd - fogStart is clamped by
	// max(range, 0.001) in the shader so zero-init uniforms on a never-
	// configured caller stay NaN-free.
	{
		const float fogColor[4] = {
			m_shader.fogColor[0], m_shader.fogColor[1], m_shader.fogColor[2], 0.0f
		};
		const float fogRange[4] = {
			m_shader.fogStart,
			m_shader.fogEnd,
			m_shader.fogEnable ? 1.0f : 0.0f,
			0.0f
		};
		bgfx::setUniform(m_uFogColor, fogColor);
		bgfx::setUniform(m_uFogRange, fogRange);
	}

	// Bind stage 0 — fall back to the built-in 2×2 white placeholder when
	// nothing is bound so programs that sample never read garbage.
	// Phase 5h.34: per-stage sampler flags from Set_Sampler_State override the
	// program-bake defaults. `UINT32_MAX` tells bgfx "use the sampler state
	// encoded at program-build time".
	{
		bgfx::TextureHandle bound = m_placeholderTexture;
		if (m_stageTexture[0] != 0)
		{
			auto* owned = reinterpret_cast<bgfx::TextureHandle*>(m_stageTexture[0]);
			if (bgfx::isValid(*owned))
				bound = *owned;
		}
		const uint32_t flags0 = m_stageSamplerFlags[0] ? m_stageSamplerFlags[0] : UINT32_MAX;
		bgfx::setTexture(0, m_uSampler, bound, flags0);
	}

	// Phase 5k — only the tex2 program declares s_texture1. bgfx validates
	// sampler bindings against the program; binding to any other program
	// triggers a warning.
	if (hasStage1 && prog.idx == m_progTex2.idx)
	{
		auto* owned = reinterpret_cast<bgfx::TextureHandle*>(m_stageTexture[1]);
		if (bgfx::isValid(*owned))
		{
			const uint32_t flags1 = m_stageSamplerFlags[1] ? m_stageSamplerFlags[1] : UINT32_MAX;
			bgfx::setTexture(1, m_uSampler1, *owned, flags1);
		}
	}

	bgfx::setState(BuildStateMask(m_shader));
	return prog;
}

void BgfxBackend::Draw_Indexed(unsigned /*minVertexIndex*/,
                               unsigned /*numVertices*/,
                               unsigned startIndex,
                               unsigned primitiveCount)
{
	if (!m_initialized || !m_pipelineInit
	    || !bgfx::isValid(m_currentVB) || !bgfx::isValid(m_currentIB))
		return;

	const bgfx::ProgramHandle prog = ApplyDrawState(m_vbAttrMask);
	bgfx::setVertexBuffer(0, m_currentVB);
	bgfx::setIndexBuffer(m_currentIB, startIndex, primitiveCount * 3);
	bgfx::submit(m_currentView, prog);
}

void BgfxBackend::Draw(unsigned startVertex, unsigned primitiveCount)
{
	if (!m_initialized || !m_pipelineInit || !bgfx::isValid(m_currentVB))
		return;

	const bgfx::ProgramHandle prog = ApplyDrawState(m_vbAttrMask);
	bgfx::setVertexBuffer(0, m_currentVB, startVertex, primitiveCount * 3);
	bgfx::submit(m_currentView, prog);
}

void BgfxBackend::Draw_Triangles_Dynamic(const void* verts,
                                         unsigned vertexCount,
                                         const VertexLayoutDesc& layout,
                                         const uint16_t* indices,
                                         unsigned indexCount)
{
	if (!m_initialized || verts == nullptr || vertexCount == 0)
		return;
	InitPipelineResources();

	// Non-indexed form: indexCount must be 0 (or indices == nullptr). Indexed
	// form requires a triangle-count's worth of indices.
	const bool indexed = (indices != nullptr && indexCount > 0);
	if (indexed && (indexCount % 3 != 0))
		return;
	if (!indexed && (vertexCount % 3 != 0))
		return;

	bgfx::VertexLayout bLayout;
	TranslateLayout(layout, bLayout);
	const uint32_t attrMask = AttrMaskFromLayout(layout);

	// bgfx requires per-frame transient capacity reservation before alloc.
	// If the caller is trying to push more than the transient pool holds,
	// silently drop — matches DX8's "draw nothing if the dynamic VB is full"
	// failure mode. Production code should split large uploads.
	if (bgfx::getAvailTransientVertexBuffer(vertexCount, bLayout) < vertexCount)
		return;
	if (indexed && bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount)
		return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::allocTransientVertexBuffer(&tvb, vertexCount, bLayout);
	std::memcpy(tvb.data, verts, uint32_t(layout.stride) * vertexCount);

	bgfx::TransientIndexBuffer tib;
	if (indexed)
	{
		bgfx::allocTransientIndexBuffer(&tib, indexCount);
		std::memcpy(tib.data, indices, indexCount * sizeof(uint16_t));
	}

	const bgfx::ProgramHandle prog = ApplyDrawState(attrMask);
	bgfx::setVertexBuffer(0, &tvb);
	if (indexed)
		bgfx::setIndexBuffer(&tib);
	bgfx::submit(m_currentView, prog);
}

// --- Phase 5e smoke triangle -------------------------------------------------

void BgfxBackend::DrawSmokeTriangle()
{
	if (!m_initialized)
		return;

	if (!m_smokeInit)
	{
		m_smokeLayout.begin()
			.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();

		const bgfx::RendererType::Enum type = bgfx::getRendererType();
		bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_triangle");
		bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_triangle");
		m_smokeProgram = bgfx::createProgram(vs, fs, true /* destroy shaders on program release */);

		// 2x2 RGBA8 checkerboard (magenta / yellow).
		static const uint8_t texels[2*2*4] =
		{
			0xFF, 0x00, 0xFF, 0xFF,  0xFF, 0xFF, 0x00, 0xFF,
			0xFF, 0xFF, 0x00, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,
		};
		m_smokeTexture = bgfx::createTexture2D(
			2, 2, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT
			| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
			bgfx::copy(texels, sizeof(texels)));

		m_smokeSampler = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);

		m_smokeInit = true;
	}

	// Identity view/projection — triangle coords are already in clip space.
	// Top-right quadrant in the tst_bgfx_uber layout.
	bgfx::setViewTransform(0, kIdentity4x4, kIdentity4x4);

	struct PosUV { float x, y, z; float u, v; };
	static const PosUV kVerts[3] = {
		{ 0.1f, 0.1f, 0.0f,  0.0f, 1.0f },
		{ 0.9f, 0.1f, 0.0f,  1.0f, 1.0f },
		{ 0.5f, 0.9f, 0.0f,  0.5f, 0.0f },
	};
	static const uint16_t kIdx[3] = { 0, 1, 2 };

	if (bgfx::getAvailTransientVertexBuffer(3, m_smokeLayout) < 3
	 || bgfx::getAvailTransientIndexBuffer(3) < 3)
		return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer  tib;
	bgfx::allocTransientVertexBuffer(&tvb, 3, m_smokeLayout);
	bgfx::allocTransientIndexBuffer(&tib, 3);
	std::memcpy(tvb.data, kVerts, sizeof(kVerts));
	std::memcpy(tib.data, kIdx,   sizeof(kIdx));

	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	bgfx::setTexture(0, m_smokeSampler, m_smokeTexture);
	bgfx::setState(
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA);
	bgfx::submit(0, m_smokeProgram);
}

// --- Phase 5f uber-shader smoke quads ----------------------------------------
//
// Each of the three quads below lives in one quadrant of clip space (top-left,
// bottom-left, bottom-right). Together with DrawSmokeTriangle (top-right) they
// cover the four uber-shader permutations wired up in Phase 5f:
//   solid    — position only, fragment color from uniform
//   vcolor   — position + vertex color
//   triangle — position + uv + texture (Phase 5e, the "tex" permutation)
//   tex_lit  — position + normal + vertex color + uv + directional light
//
// Each method lazy-creates its own program / vertex layout / texture / uniform
// handles on first call and releases them in Shutdown(). They share `view 0`
// with the other backend calls; the test harness issues them in series after
// Begin_Scene.

void BgfxBackend::DrawSmokeSolidQuad()
{
	if (!m_initialized)
		return;

	if (!m_solidInit)
	{
		m_solidLayout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.end();

		const bgfx::RendererType::Enum type = bgfx::getRendererType();
		bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_solid");
		bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_solid");
		m_solidProgram      = bgfx::createProgram(vs, fs, true);
		m_solidColorUniform = bgfx::createUniform("u_solidColor", bgfx::UniformType::Vec4);

		m_solidInit = true;
	}

	bgfx::setViewTransform(0, kIdentity4x4, kIdentity4x4);

	// Top-left quadrant.
	struct Pos { float x, y, z; };
	static const Pos kVerts[4] = {
		{ -0.9f,  0.1f, 0.0f },
		{ -0.1f,  0.1f, 0.0f },
		{ -0.1f,  0.9f, 0.0f },
		{ -0.9f,  0.9f, 0.0f },
	};
	static const uint16_t kIdx[6] = { 0, 1, 2,  0, 2, 3 };

	if (bgfx::getAvailTransientVertexBuffer(4, m_solidLayout) < 4
	 || bgfx::getAvailTransientIndexBuffer(6) < 6)
		return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer  tib;
	bgfx::allocTransientVertexBuffer(&tvb, 4, m_solidLayout);
	bgfx::allocTransientIndexBuffer(&tib, 6);
	std::memcpy(tvb.data, kVerts, sizeof(kVerts));
	std::memcpy(tib.data, kIdx,   sizeof(kIdx));

	const float solidColor[4] = { 0.85f, 0.20f, 0.25f, 1.0f }; // crimson
	bgfx::setUniform(m_solidColorUniform, solidColor);

	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	bgfx::setState(
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA);
	bgfx::submit(0, m_solidProgram);
}

void BgfxBackend::DrawSmokeVColorQuad()
{
	if (!m_initialized)
		return;

	if (!m_vcolorInit)
	{
		m_vcolorLayout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, /*normalized*/ true)
			.end();

		const bgfx::RendererType::Enum type = bgfx::getRendererType();
		bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_vcolor");
		bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_vcolor");
		m_vcolorProgram = bgfx::createProgram(vs, fs, true);

		m_vcolorInit = true;
	}

	bgfx::setViewTransform(0, kIdentity4x4, kIdentity4x4);

	// Bottom-left quadrant. RGBA packed little-endian to match the vertex
	// layout's Uint8 normalized Color0 (bgfx wants ABGR on all backends).
	struct PosColor { float x, y, z; uint32_t abgr; };
	static const PosColor kVerts[4] = {
		{ -0.9f, -0.9f, 0.0f, 0xFF0000FFu }, // red    (BL)
		{ -0.1f, -0.9f, 0.0f, 0xFF00FF00u }, // green  (BR)
		{ -0.1f, -0.1f, 0.0f, 0xFFFF0000u }, // blue   (TR)
		{ -0.9f, -0.1f, 0.0f, 0xFFFFFFFFu }, // white  (TL)
	};
	static const uint16_t kIdx[6] = { 0, 1, 2,  0, 2, 3 };

	if (bgfx::getAvailTransientVertexBuffer(4, m_vcolorLayout) < 4
	 || bgfx::getAvailTransientIndexBuffer(6) < 6)
		return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer  tib;
	bgfx::allocTransientVertexBuffer(&tvb, 4, m_vcolorLayout);
	bgfx::allocTransientIndexBuffer(&tib, 6);
	std::memcpy(tvb.data, kVerts, sizeof(kVerts));
	std::memcpy(tib.data, kIdx,   sizeof(kIdx));

	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	bgfx::setState(
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA);
	bgfx::submit(0, m_vcolorProgram);
}

void BgfxBackend::DrawSmokeLitQuad()
{
	if (!m_initialized)
		return;

	if (!m_litInit)
	{
		m_litLayout.begin()
			.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();

		const bgfx::RendererType::Enum type = bgfx::getRendererType();
		bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_tex_lit");
		bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_tex_lit");
		m_litProgram = bgfx::createProgram(vs, fs, true);

		// 2x2 checkerboard — white / dark-gray so the lighting contribution is
		// the dominant visual rather than the albedo.
		static const uint8_t texels[2*2*4] =
		{
			0xFF, 0xFF, 0xFF, 0xFF,  0x40, 0x40, 0x40, 0xFF,
			0x40, 0x40, 0x40, 0xFF,  0xFF, 0xFF, 0xFF, 0xFF,
		};
		m_litTexture = bgfx::createTexture2D(
			2, 2, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT
			| BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
			bgfx::copy(texels, sizeof(texels)));

		m_litSampler    = bgfx::createUniform("s_texture",    bgfx::UniformType::Sampler);
		m_litLightDir   = bgfx::createUniform("u_lightDir",   bgfx::UniformType::Vec4);
		m_litLightColor = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);

		m_litInit = true;
	}

	bgfx::setViewTransform(0, kIdentity4x4, kIdentity4x4);

	// Bottom-right quadrant. Per-vertex normals fan outward so the directional
	// light (pointing +X in world) produces a visible intensity gradient across
	// the face — dark on the left, bright on the right.
	struct Vertex {
		float x, y, z;
		float nx, ny, nz;
		uint32_t abgr;
		float u, v;
	};
	static const Vertex kVerts[4] = {
		{  0.1f, -0.9f, 0.0f,  -0.7f, -0.7f, 0.15f, 0xFFFFFFFFu, 0.0f, 1.0f }, // BL
		{  0.9f, -0.9f, 0.0f,   0.7f, -0.7f, 0.15f, 0xFFFFFFFFu, 1.0f, 1.0f }, // BR
		{  0.9f, -0.1f, 0.0f,   0.7f,  0.7f, 0.15f, 0xFFFFFFFFu, 1.0f, 0.0f }, // TR
		{  0.1f, -0.1f, 0.0f,  -0.7f,  0.7f, 0.15f, 0xFFFFFFFFu, 0.0f, 0.0f }, // TL
	};
	static const uint16_t kIdx[6] = { 0, 1, 2,  0, 2, 3 };

	if (bgfx::getAvailTransientVertexBuffer(4, m_litLayout) < 4
	 || bgfx::getAvailTransientIndexBuffer(6) < 6)
		return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer  tib;
	bgfx::allocTransientVertexBuffer(&tvb, 4, m_litLayout);
	bgfx::allocTransientIndexBuffer(&tib, 6);
	std::memcpy(tvb.data, kVerts, sizeof(kVerts));
	std::memcpy(tib.data, kIdx,   sizeof(kIdx));

	// Directional light pointing down the world +X axis (fragments whose
	// normals face -X light up brightest). w = ambient.
	const float lightDir[4]   = { 1.0f, 0.0f, 0.0f, 0.15f };
	const float lightColor[4] = { 1.0f, 0.95f, 0.85f, 0.0f };
	bgfx::setUniform(m_litLightDir,   lightDir);
	bgfx::setUniform(m_litLightColor, lightColor);

	bgfx::setTransform(kIdentity4x4); // u_model identity so normal transform is a no-op
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	bgfx::setTexture(0, m_litSampler, m_litTexture);
	bgfx::setState(
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
		| BGFX_STATE_MSAA);
	bgfx::submit(0, m_litProgram);
}
