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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/texture.cpp                            $*
 *                                                                                             *
 *                  $Org Author:: Steve_t                                                     $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 85                                                          $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   FileListTextureClass::Load_Frame_Surface -- Load source texture                           *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "texture.h"
#include "dx8texman.h"

// Phase 5h.29 — IRenderBackend / RenderBackendRuntime needed here for the
// bgfx RT cleanup in the base-class dtor (below). Also re-included in the
// inner #ifndef RTS_RENDERER_DX8 block for TextureClass::Init / Apply.
// Phase 5h.33 — BgfxTextureCache::Release needed for the dtor's cache-
// owned branch (refcount decrement when a TextureClass goes away).
#ifndef RTS_RENDERER_DX8
#include "WW3D2/RenderBackendRuntime.h"
#include "WW3D2/IRenderBackend.h"
#include "BGFXDevice/Common/BgfxTextureCache.h"
#endif

// Phase 5h.19 — base-class ctor + dtor shared by both DX8 and bgfx builds.
// In bgfx mode the ctor path is cold (no TextureClass subclass is
// constructed yet — they stay inside the guard), but the symbols must
// link for derived-class ctors to eventually call up the inheritance
// chain. Deps: `D3DTexture->Release()` resolves to the compat-shim
// AddRef/Release stub; `DX8TextureManagerClass::Remove` compiles in
// both modes (dx8texman.cpp has no RTS_RENDERER_DX8 guard).
static unsigned unused_texture_id;

/*!
 * KM General base constructor for texture classes
 */
TextureBaseClass::TextureBaseClass
(
	unsigned int width,
	unsigned int height,
	enum MipCountType mip_level_count,
	enum PoolType pool,
	bool rendertarget,
	bool reducible
)
:	MipLevelCount(mip_level_count),
	D3DTexture(nullptr),
	Initialized(false),
   Name(""),
	FullPath(""),
	texture_id(unused_texture_id++),
	IsLightmap(false),
	IsProcedural(false),
	IsReducible(reducible),
	IsCompressionAllowed(false),
	InactivationTime(0),
	ExtendedInactivationTime(0),
	LastInactivationSyncTime(0),
	LastAccessed(0),
	Width(width),
	Height(height),
	Pool(pool),
	Dirty(false),
	TextureLoadTask(nullptr),
	ThumbnailLoadTask(nullptr),
	HSVShift(0.0f,0.0f,0.0f)
{
}


//**********************************************************************************************
//! Base texture class destructor
/*! KJM
*/
TextureBaseClass::~TextureBaseClass()
{
	delete TextureLoadTask;
	TextureLoadTask=nullptr;
	delete ThumbnailLoadTask;
	ThumbnailLoadTask=nullptr;

	if (D3DTexture)
	{
		D3DTexture->Release();
		D3DTexture = nullptr;
	}

	// Phase 5h.29 — release the bgfx render target if this texture owns one.
	// BgfxRenderTarget stays 0 in DX8 mode and on file-loaded / non-RT
	// procedural textures, so this is a cheap null-check elsewhere.
#ifndef RTS_RENDERER_DX8
	if (BgfxRenderTarget != 0)
	{
		if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
		{
			b->Destroy_Render_Target(BgfxRenderTarget);
		}
		BgfxRenderTarget = 0;
		BgfxTexture = 0;   // the RT's color texture dies with the RT
	}
	else if (BgfxTexture != 0)
	{
		// Phase 5h.33 — two flavors of non-RT bgfx textures reach the dtor:
		//   * cache-owned  — loaded via BgfxTextureCache::Get_Or_Load_File
		//                    during TextureClass::Init; identified by a
		//                    non-empty full path. Decrement the cache's
		//                    refcount; the backing texture is destroyed
		//                    only when the last TextureClass referencing
		//                    the path goes away.
		//   * procedural   — allocated by Create_Texture_RGBA8 in the
		//                    surface ctor (5h.32) or the width/height ctor
		//                    (5h.28). Identified by an empty full path.
		//                    Destroy unconditionally — nobody else holds
		//                    this handle.
		const StringClass& path = Get_Full_Path();
		if (!path.Is_Empty())
		{
			BgfxTextureCache::Release(path.str());
		}
		else if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
		{
			b->Destroy_Texture(BgfxTexture);
		}
		BgfxTexture = 0;
	}
#endif

	DX8TextureManagerClass::Remove(this);
}

// Phase 5h.20 — pure-CPU TextureBaseClass methods. Deps resolve via the WW3D
// static un-guard from the same phase (Get_Sync_Time is an inline that reads
// WW3D::SyncTime, now always defined). Invalidate has an early-return for
// TextureLoadTask != nullptr; in bgfx mode those pointers stay nullptr
// (TextureLoader is still guarded), so the body reaches D3DTexture->Release
// (compat-shim stub) + LastAccessed update.
void TextureBaseClass::Set_Texture_Name(const char * name)
{
	Name=name;
}

void TextureBaseClass::Set_HSV_Shift(const Vector3 &hsv_shift)
{
	Invalidate();
	HSVShift=hsv_shift;
}

void TextureBaseClass::Invalidate()
{
	if (TextureLoadTask) {
		return;
	}
	if (ThumbnailLoadTask) {
		return;
	}

	// Don't invalidate procedural textures
	if (IsProcedural) {
		return;
	}

	if (D3DTexture)
	{
		D3DTexture->Release();
		D3DTexture = nullptr;
	}

#ifndef RTS_RENDERER_DX8
	// Phase 5h.33 — mirror the DX8 "drop the D3DTexture" behavior on the
	// bgfx side. Only cache-owned (file-loaded) handles are touched here;
	// procedural handles (which we've already skipped above via the
	// IsProcedural early-return) and RT handles (left in place — RT
	// textures aren't invalidated, only destroyed at dtor time) are not.
	// Next Apply() will re-fetch from the cache via TextureClass::Init.
	if (BgfxTexture != 0 && BgfxRenderTarget == 0)
	{
		const StringClass& path = Get_Full_Path();
		if (!path.Is_Empty())
		{
			BgfxTextureCache::Release(path.str());
		}
		BgfxTexture = 0;
	}
#endif

	Initialized=false;

	LastAccessed=WW3D::Get_Sync_Time();
}

// Phase 5h.21 — D3D texture accessors. Both `D3DTexture->AddRef/Release`
// resolve through the compat-shim; `WW3D::Get_Sync_Time()` now links
// (moved above the ww3d guard in 5h.20). The bgfx-mode `D3DTexture`
// pointer stays nullptr until 5h.22+ wires the texture-load path, so
// these accessors return null / no-op but are linkable.
IDirect3DBaseTexture8 * TextureBaseClass::Peek_D3D_Base_Texture() const
{
	LastAccessed=WW3D::Get_Sync_Time();
	return D3DTexture;
}

void TextureBaseClass::Set_D3D_Base_Texture(IDirect3DBaseTexture8* tex)
{
	// (gth) Generals does stuff directly with the D3DTexture pointer so lets
	// reset the access timer whenever someon messes with this pointer.
	LastAccessed=WW3D::Get_Sync_Time();

	if (D3DTexture != nullptr) {
		D3DTexture->Release();
	}
	D3DTexture = tex;
	if (D3DTexture != nullptr) {
		D3DTexture->AddRef();
	}
}

// Phase 5h.21 — priority pass-through. Compat-shim GetPriority/SetPriority
// return 0; in bgfx mode priority is meaningless (bgfx manages its own
// texture cache eviction). Callers that asserted on null D3DTexture in DX8
// keep doing so; the nullptr path fires WWASSERT_PRINT + returns 0.
unsigned int TextureBaseClass::Get_Priority()
{
	if (!D3DTexture)
	{
		WWASSERT_PRINT(0, "Get_Priority: D3DTexture is null!");
		return 0;
	}

	return D3DTexture->GetPriority();
}

unsigned int TextureBaseClass::Set_Priority(unsigned int priority)
{
	if (!D3DTexture)
	{
		WWASSERT_PRINT(0, "Set_Priority: D3DTexture is null!");
		return 0;
	}

	return D3DTexture->SetPriority(priority);
}

// Phase 5h.22 — MissingTexture + TextureLoader bridge methods. In bgfx mode
// the functions they delegate to exist as no-op stubs (MissingTexture's
// `_Get_Missing_Texture` returns nullptr; TextureLoader's `Request_Thumbnail`
// is a no-op). So these methods link but are runtime-cold until Phase 5h.25+
// wires the full texture-load pipeline.
#include "missingtexture.h"
#include "textureloader.h"

void TextureBaseClass::Load_Locked_Surface()
{
	// WWPROFILE intentionally dropped here — wwprofile.h pulls in threading
	// macros we haven't verified for bgfx-mode builds yet. Re-add once the
	// profile macros are confirmed safe outside the guard.
	if (D3DTexture) D3DTexture->Release();
	D3DTexture=nullptr;
	TextureLoader::Request_Thumbnail(this);
	Initialized=false;
}

bool TextureBaseClass::Is_Missing_Texture()
{
	bool flag = false;
	IDirect3DBaseTexture8 *missing_texture = MissingTexture::_Get_Missing_Texture();

	if (D3DTexture == missing_texture)
		flag = true;

	if (missing_texture)
	{
		missing_texture->Release();
	}

	return flag;
}

// Phase 5h.23 — TextureBaseClass total-accounting + invalidation methods.
// Each walks `WW3DAssetManager::Get_Instance()->Texture_Hash()`. In bgfx
// mode `Get_Instance()` returns nullptr (nothing constructs the asset
// manager), so each method null-guards and returns a zero / empty result.
// Moved here so `DX8VertexBufferClass::Create_Vertex_Buffer`'s recovery
// path can call `Invalidate_Old_Unused_Textures` without the 5h.15 surgical
// `#ifdef` guard.
#include "assetmgr.h"

void TextureBaseClass::Invalidate_Old_Unused_Textures(unsigned invalidation_time_override)
{
	if (WW3D::Get_Thumbnail_Enabled() == false) {
		return;
	}
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return;

	unsigned synctime=WW3D::Get_Sync_Time();
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		TextureClass* tex=ite.Peek_Value();
		if (tex->Initialized && tex->InactivationTime)
		{
			unsigned age=synctime-tex->LastAccessed;
			if (invalidation_time_override)
			{
				if (age>invalidation_time_override)
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
			else
			{
				if (age>(tex->InactivationTime+tex->ExtendedInactivationTime))
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
		}
	}
}

int TextureBaseClass::_Get_Total_Locked_Surface_Size()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int total=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ()) {
		TextureBaseClass* tex=ite.Peek_Value();
		if (!tex->Initialized) total+=tex->Get_Texture_Memory_Usage();
	}
	return total;
}

int TextureBaseClass::_Get_Total_Texture_Size()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int total=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ())
		total+=ite.Peek_Value()->Get_Texture_Memory_Usage();
	return total;
}

int TextureBaseClass::_Get_Total_Lightmap_Texture_Size()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int total=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ()) {
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Lightmap()) total+=tex->Get_Texture_Memory_Usage();
	}
	return total;
}

int TextureBaseClass::_Get_Total_Procedural_Texture_Size()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int total=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ()) {
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Procedural()) total+=tex->Get_Texture_Memory_Usage();
	}
	return total;
}

int TextureBaseClass::_Get_Total_Texture_Count()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int count=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ()) ++count;
	return count;
}

int TextureBaseClass::_Get_Total_Lightmap_Texture_Count()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int count=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ())
		if (ite.Peek_Value()->Is_Lightmap()) ++count;
	return count;
}

int TextureBaseClass::_Get_Total_Procedural_Texture_Count()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int count=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ())
		if (ite.Peek_Value()->Is_Procedural()) ++count;
	return count;
}

int TextureBaseClass::_Get_Total_Locked_Surface_Count()
{
	WW3DAssetManager* mgr = WW3DAssetManager::Get_Instance();
	if (mgr == nullptr) return 0;
	int count=0;
	HashTemplateIterator<StringClass,TextureClass*> ite(mgr->Texture_Hash());
	for (ite.First ();!ite.Is_Done();ite.Next ())
		if (!ite.Peek_Value()->Initialized) ++count;
	return count;
}

// Phase 5h.24 — last two TextureBaseClass methods. Apply_Null forwards to
// DX8Wrapper::Set_DX8_Texture which is inline (header); the DX8CALL macro
// it uses is a no-op in bgfx mode, and the Textures[] static array it
// updates is defined outside the guard since 5h.13 era. Get_Reduction
// reads WW3D::Get_Texture_Reduction + Is_Large_Texture_Extra_Reduction_Enabled
// which landed above the ww3d.cpp guard in the 5h.24 prerequisite step.
#include "dx8wrapper.h"

void TextureBaseClass::Apply_Null(unsigned int stage)
{
	// Mirror the bgfx-side unbind so SelectProgram's `hasStage1` test (driven
	// by BgfxBackend::m_stageTexture[stage]) can see the cleared state. Without
	// this, a prior 3D draw's stage-1 binding survives forever and any later
	// 2D draw (which submits with TEX2 in dynamic_fvf_type) routes to the
	// two-stage `tex2` program, sampling a stale stage-1 texture and producing
	// black quads.
	if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
		b->Set_Texture(stage, 0);
	DX8Wrapper::Set_DX8_Texture(stage, nullptr);
}

unsigned TextureBaseClass::Get_Reduction() const
{
	if (MipLevelCount==MIP_LEVELS_1) return 0;
	if (Width <= 32 || Height <= 32) return 0;

	int reduction=WW3D::Get_Texture_Reduction();

	if (WW3D::Is_Large_Texture_Extra_Reduction_Enabled() && (Width > 256 || Height > 256)) {
		reduction++;
	}
	if (MipLevelCount && reduction>MipLevelCount) {
		reduction=MipLevelCount;
	}
	return reduction;
}

// Phase 5h.25/5h.27 — bgfx-mode TextureClass implementations. The DX8 path
// (the main #ifdef block below) handles real GPU texture creation + mipmap
// loading + texture-loader thread handoff. In bgfx mode the equivalent
// responsibilities land on `BgfxTextureCache::Get_Or_Load_File` (from 5h.3),
// which decodes DDS/KTX/PNG via bimg and uploads through the active
// IRenderBackend. Ctors store only the metadata; Init triggers the cache
// lookup + handle assignment; Apply forwards the handle to the backend.
#ifndef RTS_RENDERER_DX8

#include "BGFXDevice/Common/BgfxTextureCache.h"
#include "WW3D2/RenderBackendRuntime.h"
#include "WW3D2/IRenderBackend.h"

const unsigned DEFAULT_INACTIVATION_TIME_BGFX = 20000;

TextureClass::TextureClass
(
	unsigned width,
	unsigned height,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureBaseClass(width, height, mip_level_count, pool, rendertarget, allow_reduction),
	TextureFormat(format),
	Filter(mip_level_count)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;
	LastAccessed=WW3D::Get_Sync_Time();

	// Phase 5h.28/29 — allocate backend storage. Two paths:
	//   - rendertarget == false: plain 2D texture via Create_Texture_RGBA8(null).
	//     Empty contents; callers fill via the still-to-come Update_Texture.
	//   - rendertarget == true: frame-buffer-backed via Create_Render_Target.
	//     The RT's color attachment is extracted via Get_Render_Target_Texture
	//     and stored as the sampler handle, so `Apply(stage)` binds the RT's
	//     color texture for post-process sampling. The RT handle itself is
	//     kept in `BgfxRenderTarget` so the dtor can release it.
	if (width > 0 && height > 0)
	{
		if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
		{
			if (rendertarget)
			{
				const uintptr_t rt = b->Create_Render_Target(
					static_cast<uint16_t>(width),
					static_cast<uint16_t>(height),
					/*hasDepth=*/true);
				Set_Bgfx_Render_Target(rt);
				if (rt != 0)
				{
					Set_Bgfx_Handle(b->Get_Render_Target_Texture(rt));
				}
			}
			else
			{
				const bool mipmap = (mip_level_count != MIP_LEVELS_1);
				const uintptr_t h = b->Create_Texture_RGBA8(
					/*pixels=*/nullptr,
					static_cast<uint16_t>(width),
					static_cast<uint16_t>(height),
					mipmap);
				Set_Bgfx_Handle(h);
			}
		}
	}
}

TextureClass::TextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureBaseClass(0, 0, mip_level_count),
	TextureFormat(texture_format),
	Filter(mip_level_count)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME_BGFX;
	IsReducible=allow_reduction;

	if (name)
	{
		// Copy the lightmap-flag detection (see DX8 ctor) — materials that
		// reference this texture need the flag to route through the right
		// shader path.
		for (const char* p=name; *p; ++p)
		{
			if (*p=='+')
			{
				IsLightmap=true;
				break;
			}
		}
		Set_Texture_Name(name);
	}
	if (full_path)
		Set_Full_Path(full_path);

	LastAccessed=WW3D::Get_Sync_Time();
	// Initialized stays false — the BgfxTextureCache bridge in 5h.27 flips
	// it when the texture file has been loaded + uploaded.
}

TextureClass::TextureClass
(
	SurfaceClass* surface,
	MipCountType mip_level_count
)
:	TextureBaseClass(0, 0, mip_level_count),
	TextureFormat(surface ? surface->Get_Surface_Format() : WW3D_FORMAT_UNKNOWN),
	Filter(mip_level_count)
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	// Phase 5h.32 — take on the surface's dimensions + back it with a real
	// bgfx texture handle, then tie the surface to that handle so any
	// subsequent Lock/Unlock on the surface re-uploads into this texture.
	if (surface) {
		SurfaceClass::SurfaceDescription sd;
		surface->Get_Description(sd);
		Width = sd.Width;
		Height = sd.Height;
		if (IRenderBackend* backend = RenderBackendRuntime::Get_Active()) {
			if (sd.Width && sd.Height) {
				const uintptr_t handle = backend->Create_Texture_RGBA8(
					nullptr,
					static_cast<uint16_t>(sd.Width),
					static_cast<uint16_t>(sd.Height),
					/*mipmap=*/false);
				if (handle) {
					Set_Bgfx_Handle(handle);
					surface->Set_Associated_Texture(handle);
					// Seed the GPU copy with whatever the surface already
					// holds. A Lock/Unlock cycle is the cheapest way to
					// route through SurfaceClass's upload path without
					// duplicating the BGRA→RGBA swizzle here.
					int pitch = 0;
					(void)surface->Lock(&pitch);
					surface->Unlock();
				}
			}
		}
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

TextureClass::TextureClass(IDirect3DBaseTexture8* /*d3d_texture*/)
:	TextureBaseClass(0, 0, MIP_LEVELS_ALL),
	Filter(MIP_LEVELS_ALL)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;
	LastAccessed=WW3D::Get_Sync_Time();
}

void TextureClass::Init()
{
	if (Initialized) return;

	// Phase 5h.27 — the headline wire-up. Resolve the cache-key path:
	// `Get_Full_Path()` returns full_path when set, otherwise the texture
	// name. If both are empty (procedural textures from the width/height
	// ctor, for example), skip the cache load — those textures stay as
	// empty handles and bind to the backend's placeholder.
	if (Peek_Bgfx_Handle() == 0)
	{
		const StringClass& path = Get_Full_Path();
		if (!path.Is_Empty())
		{
			uintptr_t handle = BgfxTextureCache::Get_Or_Load_File(path.str());
			Set_Bgfx_Handle(handle);

			// Phase 5h.36 — if the file on disk had no mip chain, downgrade
			// the Filter's mip mode so `Filter.Apply(stage)` pushes MIP_POINT
			// + hasMips=false to the backend. Without this a DDS baked with
			// one mip level would be sampled with a linear mip filter,
			// yielding undefined (driver-dependent) results at higher LODs.
			if (handle != 0)
			{
				if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
				{
					if (b->Texture_Mip_Count(handle) <= 1)
						Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
				}
			}
		}
	}

	Initialized = true;
	LastAccessed = WW3D::Get_Sync_Time();
}

void TextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* /*tex*/,
	bool initialized,
	bool disable_auto_invalidation
)
{
	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime=0;
}

void TextureClass::Apply(unsigned int stage)
{
	if (!Initialized) Init();
	LastAccessed = WW3D::Get_Sync_Time();

	// Phase 5h.27 — bind the cached bgfx handle to the requested stage.
	// A zero handle falls back to the backend's built-in 2×2 placeholder
	// (`BgfxBackend::m_placeholderTexture`), which is the same behavior
	// the stub had pre-5h.27.
	if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
	{
		b->Set_Texture(stage, Peek_Bgfx_Handle());
	}

	// DX8 side-effect stays (updates the Textures[] array so callers that
	// peek via DX8Wrapper see the null-binding state); DX8CALL is a no-op
	// in bgfx mode so this is cheap.
	DX8Wrapper::Set_DX8_Texture(stage, nullptr);

	// Phase 5h.34 — push the per-stage sampler state (filter + addr + mips).
	Filter.Apply(stage);
}

unsigned TextureClass::Get_Texture_Memory_Usage() const
{
	// No D3D texture allocated in bgfx mode today; real bgfx-side memory is
	// accounted by bgfx's own stats API (not plumbed through here yet).
	return 0;
}

// Phase 5h.26 — ZTextureClass stubs. Z-only textures (depth buffers) are not
// a primary render path in bgfx mode; render targets handle their own depth
// attachments. Stubs just keep the symbol table populated.
ZTextureClass::ZTextureClass
(
	unsigned width,
	unsigned height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	PoolType pool
)
:	TextureBaseClass(width, height, mip_level_count, pool, /*rendertarget=*/true, /*reducible=*/false),
	DepthStencilTextureFormat(zformat)
{
	Initialized=true;
	IsProcedural=true;
	LastAccessed=WW3D::Get_Sync_Time();
}

void ZTextureClass::Apply(unsigned int /*stage*/)
{
	// Depth textures aren't sampled in the uber-shader today; no-op.
}

void ZTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* /*tex*/,
	bool initialized,
	bool /*disable_auto_invalidation*/
)
{
	if (initialized) Initialized=true;
}

IDirect3DSurface8* ZTextureClass::Get_D3D_Surface_Level(unsigned int /*level*/)
{
	return nullptr;
}

unsigned ZTextureClass::Get_Texture_Memory_Usage() const
{
	return 0;
}

// Phase 5h.26 — CubeTextureClass stubs. Inherits from TextureClass so the
// base ctor gets called through the TextureClass-for-derived overload in the
// header. Three of the four overloads exist to catch asset-manager paths
// that might try to load a cubemap; they reduce to the same minimum init.
CubeTextureClass::CubeTextureClass
(
	unsigned width,
	unsigned height,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureClass(width, height, mip_level_count, pool, rendertarget, format, allow_reduction)
{
	Initialized=true;
	IsProcedural=true;
	LastAccessed=WW3D::Get_Sync_Time();
}

CubeTextureClass::CubeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(name, full_path, mip_level_count, texture_format, allow_compression, allow_reduction)
{
}

CubeTextureClass::CubeTextureClass
(
	SurfaceClass *surface,
	MipCountType mip_level_count
)
:	TextureClass(surface, mip_level_count)
{
}

CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureClass(d3d_texture)
{
}

void CubeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* /*tex*/,
	bool initialized,
	bool disable_auto_invalidation
)
{
	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime=0;
}

// Phase 5h.26 — VolumeTextureClass stubs. Same pattern as CubeTextureClass
// but with an additional `depth` dimension the DX8 ctor uses for slice count.
// Stubs just remember it in the Depth member.
VolumeTextureClass::VolumeTextureClass
(
	unsigned width,
	unsigned height,
	unsigned depth,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureClass(width, height, mip_level_count, pool, rendertarget, format, allow_reduction)
{
	Depth=static_cast<int>(depth);
	Initialized=true;
	IsProcedural=true;
	LastAccessed=WW3D::Get_Sync_Time();
}

VolumeTextureClass::VolumeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(name, full_path, mip_level_count, texture_format, allow_compression, allow_reduction)
{
	Depth=0;
}

VolumeTextureClass::VolumeTextureClass
(
	SurfaceClass *surface,
	MipCountType mip_level_count
)
:	TextureClass(surface, mip_level_count)
{
	Depth=0;
}

VolumeTextureClass::VolumeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureClass(d3d_texture)
{
	Depth=0;
}

void VolumeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* /*tex*/,
	bool initialized,
	bool disable_auto_invalidation
)
{
	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime=0;
}

#endif // !RTS_RENDERER_DX8

#ifdef RTS_RENDERER_DX8

#include <d3d8.h>
#include <d3dx8core.h>
#include "dx8wrapper.h"
#include "TARGA.h"
#include <nstrdup.h>
#include "w3d_file.h"
#include "assetmgr.h"
#include "formconv.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "ffactory.h"
#include "dx8caps.h"
#include "meshmatdesc.h"
#include "texturethumbnail.h"
#include "wwprofile.h"

const unsigned DEFAULT_INACTIVATION_TIME=20000;

// This throttles submissions to the background texture loading queue.
static unsigned TexturesAppliedPerFrame;
const unsigned MAX_TEXTURES_APPLIED_PER_FRAME=2;




// Phase 5h.23 — TextureBaseClass::Invalidate_Old_Unused_Textures moved above the guard.





// Phase 5h.20 — TextureBaseClass::Invalidate moved above the guard.

// Phase 5h.21 — TextureBaseClass::Peek_D3D_Base_Texture and Set_D3D_Base_Texture moved above the guard.

// Phase 5h.22 — TextureBaseClass::Load_Locked_Surface and Is_Missing_Texture moved above the guard.


// Phase 5h.20 — TextureBaseClass::Set_Texture_Name moved above the guard.




// Phase 5h.21 — TextureBaseClass::Get_Priority / Set_Priority moved above the guard.


// Phase 5h.24 — TextureBaseClass::Apply_Null and Get_Reduction moved above the guard.

// Phase 5h.20 — TextureBaseClass::Set_HSV_Shift moved above the guard.

// Phase 5h.23 — all TextureBaseClass::_Get_Total_* methods moved above the guard.

/*************************************************************************
**                             TextureClass
*************************************************************************/
TextureClass::TextureClass
(
	unsigned width,
	unsigned height,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureBaseClass(width, height, mip_level_count, pool, rendertarget,allow_reduction),
	Filter(mip_level_count),
	TextureFormat(format)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Texture
		(
			width,
			height,
			format,
			mip_level_count,
			d3dpool,
			rendertarget
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width,
			height,
			format,
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureBaseClass(0, 0, mip_level_count),
	Filter(mip_level_count),
	TextureFormat(texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds
	IsReducible=allow_reduction;

	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat))
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Poke_Texture(nullptr);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb)
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled())
	{
		if (TextureLoader::Is_DX8_Thread())
		{
			Init();
		}
	}
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	SurfaceClass *surface,
	MipCountType mip_level_count
)
:  TextureBaseClass(0,0,mip_level_count),
	Filter(mip_level_count),
	TextureFormat(surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Texture
		(
			surface->Peek_D3D_Surface(),
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	Set_D3D_Base_Texture(d3d_texture);
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Initialise the texture
/*!
*/
void TextureClass::Init()
{
	// If the texture has already been initialised we should exit now
	if (Initialized) return;

	WWPROFILE("TextureClass::Init");

	// If the texture has recently been inactivated, increase the inactivation time (this texture obviously
	// should not have been inactivated yet).
	if (InactivationTime && LastInactivationSyncTime)
	{
		if ((WW3D::Get_Sync_Time()-LastInactivationSyncTime)<InactivationTime)
		{
			ExtendedInactivationTime=3*InactivationTime;
		}
		LastInactivationSyncTime=0;
	}


	if (!Peek_D3D_Base_Texture())
	{
		if (!WW3D::Get_Thumbnail_Enabled() || MipLevelCount==MIP_LEVELS_1)
		{
//		if (MipLevelCount==MIP_LEVELS_1) {
			TextureLoader::Request_Foreground_Loading(this);
		}
		else
		{
			WW3DFormat format=TextureFormat;
			Load_Locked_Surface();
			TextureFormat=format;
		}
	}

	if (!Initialized)
	{
		TextureLoader::Request_Background_Loading(this);
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void TextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	if (initialized)
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
	surface->Release();

}


//**********************************************************************************************
//! Apply texture states
/*!
*/
void TextureClass::Apply(unsigned int stage)
{
	// Initialization needs to be done when texture is used if it hasn't been done before.
	// XBOX always initializes textures at creation time.
	if (!Initialized)
	{
		Init();

		/* was in battlefield// Non-thumbnailed textures are always initialized when used
		if (MipLevelCount==MIP_LEVELS_1)
		{
		}
		// Thumbnailed textures have delayed initialization and a background loading system
		else
		{
			// Limit the number of texture initializations per frame to reduce stuttering
			if (TexturesAppliedPerFrame<MAX_TEXTURES_APPLIED_PER_FRAME)
			{
				TexturesAppliedPerFrame++;
				Init();
			}
			else
			{
				// If texture can't be initialized in this frame, at least make sure we have the thumbnail.
				if (!Peek_Texture())
				{
					WW3DFormat format=TextureFormat;
					Load_Locked_Surface();
					TextureFormat=format;
				}
			}
		}*/
	}
	LastAccessed=WW3D::Get_Sync_Time();

	DX8_RECORD_TEXTURE(this);

	// Set texture itself
	if (WW3D::Is_Texturing_Enabled())
	{
		DX8Wrapper::Set_DX8_Texture(stage, Peek_D3D_Base_Texture());
	}
	else
	{
		DX8Wrapper::Set_DX8_Texture(stage, nullptr);
	}

	Filter.Apply(stage);
}

//**********************************************************************************************
//! Get surface from mip level
/*!
*/
SurfaceClass *TextureClass::Get_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture())
	{
		WWASSERT_PRINT(0, "Get_Surface_Level: D3DTexture is null!");
		return nullptr;
	}

	IDirect3DSurface8 *d3d_surface = nullptr;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	SurfaceClass *surface = new SurfaceClass(d3d_surface);
	d3d_surface->Release();

	return surface;
}

//**********************************************************************************************
//! Get surface description for a mip level
/*!
*/
void TextureClass::Get_Level_Description( SurfaceClass::SurfaceDescription & desc, unsigned int level )
{
	SurfaceClass * surf = Get_Surface_Level(level);
	if (surf != nullptr) {
		surf->Get_Description(desc);
	}
	REF_PTR_RELEASE(surf);
}

//**********************************************************************************************
//! Get D3D surface from mip level
/*!
*/
IDirect3DSurface8 *TextureClass::Get_D3D_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture())
	{
		WWASSERT_PRINT(0, "Get_D3D_Surface_Level: D3DTexture is null!");
		return nullptr;
	}

	IDirect3DSurface8 *d3d_surface = nullptr;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	return d3d_surface;
}

//**********************************************************************************************
//! Get texture memory usage
/*!
*/
unsigned TextureClass::Get_Texture_Memory_Usage() const
{
	int size=0;
	if (!Peek_D3D_Texture()) return 0;
	for (unsigned i=0;i<Peek_D3D_Texture()->GetLevelCount();++i)
	{
		D3DSURFACE_DESC desc;
		DX8_ErrorCode(Peek_D3D_Texture()->GetLevelDesc(i,&desc));
		size+=desc.Size;
	}
	return size;
}


// Utility functions
TextureClass* Load_Texture(ChunkLoadClass & cload)
{
	// Assume failure
	TextureClass *newtex = nullptr;

	char name[256];
	if (cload.Open_Chunk () && (cload.Cur_Chunk_ID () == W3D_CHUNK_TEXTURE))
	{

		W3dTextureInfoStruct texinfo;
		bool hastexinfo = false;

		/*
		** Read in the texture filename, and a possible texture info structure.
		*/
		while (cload.Open_Chunk()) {
			switch (cload.Cur_Chunk_ID()) {
				case W3D_CHUNK_TEXTURE_NAME:
					cload.Read(&name,cload.Cur_Chunk_Length());
					break;

				case W3D_CHUNK_TEXTURE_INFO:
					cload.Read(&texinfo,sizeof(W3dTextureInfoStruct));
					hastexinfo = true;
					break;
			};
			cload.Close_Chunk();
		}
		cload.Close_Chunk();

		/*
		** Get the texture from the asset manager
		*/
		if (hastexinfo)
		{

			MipCountType mipcount;

			bool no_lod = ((texinfo.Attributes & W3DTEXTURE_NO_LOD) == W3DTEXTURE_NO_LOD);

			if (no_lod)
			{
				mipcount = MIP_LEVELS_1;
			}
			else
			{
				switch (texinfo.Attributes & W3DTEXTURE_MIP_LEVELS_MASK) {

					case W3DTEXTURE_MIP_LEVELS_ALL:
						mipcount = MIP_LEVELS_ALL;
						break;

					case W3DTEXTURE_MIP_LEVELS_2:
						mipcount = MIP_LEVELS_2;
						break;

					case W3DTEXTURE_MIP_LEVELS_3:
						mipcount = MIP_LEVELS_3;
						break;

					case W3DTEXTURE_MIP_LEVELS_4:
						mipcount = MIP_LEVELS_4;
						break;

					default:
						WWASSERT (false);
						mipcount = MIP_LEVELS_ALL;
						break;
				}
			}

			WW3DFormat format=WW3D_FORMAT_UNKNOWN;

			switch (texinfo.Attributes & W3DTEXTURE_TYPE_MASK)
			{

				case W3DTEXTURE_TYPE_COLORMAP:
					// Do nothing.
					break;

				case W3DTEXTURE_TYPE_BUMPMAP:
				{
					if (DX8Wrapper::Is_Initted() && DX8Wrapper::Get_Current_Caps()->Support_Bump_Envmap())
					{
						// No mipmaps to bumpmap for now
						mipcount=MIP_LEVELS_1;

						if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_U8V8)) format=WW3D_FORMAT_U8V8;
						else if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_X8L8V8U8)) format=WW3D_FORMAT_X8L8V8U8;
						else if (DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(WW3D_FORMAT_L6V5U5)) format=WW3D_FORMAT_L6V5U5;
					}
					break;
				}

				default:
					WWASSERT (false);
					break;
			}

			newtex = WW3DAssetManager::Get_Instance()->Get_Texture (name, mipcount, format);

			if (no_lod)
			{
				newtex->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
			}
			bool u_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_U) != 0);
			newtex->Get_Filter().Set_U_Addr_Mode(u_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
			bool v_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_V) != 0);
			newtex->Get_Filter().Set_V_Addr_Mode(v_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);

		} else
		{
			newtex = WW3DAssetManager::Get_Instance()->Get_Texture(name);
		}

		WWASSERT(newtex);
	}

	// Return a pointer to the new texture
	return newtex;
}

// Utility function used by Save_Texture
void setup_texture_attributes(TextureClass * tex, W3dTextureInfoStruct * texinfo)
{
	texinfo->Attributes = 0;

	if (tex->Get_Filter().Get_Mip_Mapping() == TextureFilterClass::FILTER_TYPE_NONE) texinfo->Attributes |= W3DTEXTURE_NO_LOD;
	if (tex->Get_Filter().Get_U_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP) texinfo->Attributes |= W3DTEXTURE_CLAMP_U;
	if (tex->Get_Filter().Get_V_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP) texinfo->Attributes |= W3DTEXTURE_CLAMP_V;
}


void Save_Texture(TextureClass * texture,ChunkSaveClass & csave)
{
	const char * filename;
	W3dTextureInfoStruct texinfo;
	memset(&texinfo,0,sizeof(texinfo));

	filename = texture->Get_Full_Path();

	setup_texture_attributes(texture, &texinfo);

	csave.Begin_Chunk(W3D_CHUNK_TEXTURE_NAME);
	csave.Write(filename,strlen(filename)+1);
	csave.End_Chunk();

	if ((texinfo.Attributes != 0) || (texinfo.AnimType != 0) || (texinfo.FrameCount != 0)) {
		csave.Begin_Chunk(W3D_CHUNK_TEXTURE_INFO);
		csave.Write(&texinfo, sizeof(texinfo));
		csave.End_Chunk();
	}
}


/*!
 *	KJM depth stencil texture constructor
 */
ZTextureClass::ZTextureClass
(
	unsigned width,
	unsigned height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	PoolType pool
)
:	TextureBaseClass(width,height, mip_level_count, pool),
	DepthStencilTextureFormat(zformat)
{
	D3DPOOL d3dpool=(D3DPOOL)0;
	switch (pool)
	{
	case POOL_DEFAULT: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM: d3dpool=D3DPOOL_SYSTEMMEM;	break;
	default:	WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_ZTexture
		(
			width,
			height,
			zformat,
			mip_level_count,
			d3dpool
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8ZTextureTrackerClass *track=new DX8ZTextureTrackerClass
		(
			width,
			height,
			zformat,
			mip_level_count,
			this
		);
		DX8TextureManagerClass::Add(track);
	}
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	LastAccessed=WW3D::Get_Sync_Time();
}


//**********************************************************************************************
//! Apply depth stencil texture
/*! KM
*/
void ZTextureClass::Apply(unsigned int stage)
{
	DX8Wrapper::Set_DX8_Texture(stage, Peek_D3D_Base_Texture());
}

//**********************************************************************************************
//! Apply new surface to texture
/*! KM
*/
void ZTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_D3D_Texture());
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	if (initialized)
	{
		DepthStencilTextureFormat=D3DFormat_To_WW3DZFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
	surface->Release();
}

//**********************************************************************************************
//! Get D3D surface from mip level
/*!
*/
IDirect3DSurface8* ZTextureClass::Get_D3D_Surface_Level(unsigned int level)
{
	if (!Peek_D3D_Texture())
	{
		WWASSERT_PRINT(0, "Get_D3D_Surface_Level: D3DTexture is null!");
		return nullptr;
	}

	IDirect3DSurface8 *d3d_surface = nullptr;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(level, &d3d_surface));
	return d3d_surface;
}

//**********************************************************************************************
//! Get texture memory usage
/*!
*/
unsigned ZTextureClass::Get_Texture_Memory_Usage() const
{
	int size=0;
	if (!Peek_D3D_Texture()) return 0;
	for (unsigned i=0;i<Peek_D3D_Texture()->GetLevelCount();++i)
	{
		D3DSURFACE_DESC desc;
		DX8_ErrorCode(Peek_D3D_Texture()->GetLevelDesc(i,&desc));
		size+=desc.Size;
	}
	return size;
}



/*************************************************************************
**                             CubeTextureClass
*************************************************************************/
CubeTextureClass::CubeTextureClass
(
	unsigned width,
	unsigned height,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, format, mip_level_count, pool, rendertarget)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			width,
			height,
			format,
			mip_level_count,
			d3dpool,
			rendertarget
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width,
			height,
			format,
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat))
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Poke_Texture(nullptr);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb)
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled())
	{
		if (TextureLoader::Is_DX8_Thread())
		{
			Init();
		}
	}
}

// don't know if these are needed
#if 0
// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	SurfaceClass *surface,
	MipCountType mip_level_count
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			surface->Peek_D3D_Surface(),
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	Peek_Texture()->AddRef();
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}
#endif

//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void CubeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(Peek_D3D_CubeTexture()->GetLevelDesc(0,&d3d_desc));

	if (initialized)
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
	}
}


/*************************************************************************
**                             VolumeTextureClass
*************************************************************************/
VolumeTextureClass::VolumeTextureClass
(
	unsigned width,
	unsigned height,
	unsigned depth,
	WW3DFormat format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, format, mip_level_count, pool, rendertarget),
  Depth(depth)
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	switch (format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	D3DPOOL d3dpool=(D3DPOOL)0;
	switch(pool)
	{
	case POOL_DEFAULT		: d3dpool=D3DPOOL_DEFAULT; break;
	case POOL_MANAGED		: d3dpool=D3DPOOL_MANAGED; break;
	case POOL_SYSTEMMEM	: d3dpool=D3DPOOL_SYSTEMMEM; break;
	default: WWASSERT(0);
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Volume_Texture
		(
			width,
			height,
			depth,
			format,
			mip_level_count,
			d3dpool
		)
	);

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		DX8TextureTrackerClass *track=new DX8TextureTrackerClass
		(
			width,
			height,
			format,
			mip_level_count,
			this,
			rendertarget
		);
		DX8TextureManagerClass::Add(track);
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
VolumeTextureClass::VolumeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format),
	Depth(0)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	case WW3D_FORMAT_U8V8:		// Bumpmap
	case WW3D_FORMAT_L6V5U5:	// Bumpmap
	case WW3D_FORMAT_X8L8V8U8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!DX8Wrapper::Is_Initted() || !DX8Wrapper::Get_Current_Caps()->Support_Texture_Format(TextureFormat))
		{
			TextureFormat=WW3D_FORMAT_UNKNOWN;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Filter.Set_Min_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			Filter.Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_FAST);
			if (mip_level_count!=MIP_LEVELS_1) Filter.Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_FAST);
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Poke_Texture(nullptr);
	}

	// Find original size from the thumbnail (but don't create thumbnail texture yet!)
	ThumbnailClass* thumb=ThumbnailManagerClass::Peek_Thumbnail_Instance_From_Any_Manager(Get_Full_Path());
	if (thumb)
	{
		Width=thumb->Get_Original_Texture_Width();
		Height=thumb->Get_Original_Texture_Height();
 		if (MipLevelCount!=MIP_LEVELS_1) {
 			MipLevelCount=(MipCountType)thumb->Get_Original_Texture_Mip_Level_Count();
 		}
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// If the thumbnails are not enabled, init the texture at this point to avoid stalling when the
	// mesh is rendered.
	if (!WW3D::Get_Thumbnail_Enabled())
	{
		if (TextureLoader::Is_DX8_Thread())
		{
			Init();
		}
	}
}

// don't know if these are needed
#if 0
// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	SurfaceClass *surface,
	MipCountType mip_level_count
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, surface->Get_Surface_Format())
{
	IsProcedural=true;
	Initialized=true;
	IsReducible=false;

	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	Width=sd.Width;
	Height=sd.Height;
	switch (sd.Format)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Poke_Texture
	(
		DX8Wrapper::_Create_DX8_Cube_Texture
		(
			surface->Peek_D3D_Surface(),
			mip_level_count
		)
	);
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass(IDirect3DBaseTexture8* d3d_texture)
:	TextureBaseClass
	(
		0,
		0,
		((MipCountType)d3d_texture->GetLevelCount())
	),
	Filter((MipCountType)d3d_texture->GetLevelCount())
{
	Initialized=true;
	IsProcedural=true;
	IsReducible=false;

	Peek_Texture()->AddRef();
	IDirect3DSurface8* surface;
	DX8_ErrorCode(Peek_D3D_Texture()->GetSurfaceLevel(0,&surface));
	D3DSURFACE_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DSURFACE_DESC));
	DX8_ErrorCode(surface->GetDesc(&d3d_desc));
	Width=d3d_desc.Width;
	Height=d3d_desc.Height;
	TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
	switch (TextureFormat)
	{
	case WW3D_FORMAT_DXT1:
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}
#endif




//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void VolumeTextureClass::Apply_New_Surface
(
	IDirect3DBaseTexture8* d3d_texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	IDirect3DBaseTexture8* d3d_tex=Peek_D3D_Base_Texture();

	if (d3d_tex) d3d_tex->Release();

	Poke_Texture(d3d_texture);//TextureLoadTask->Peek_D3D_Texture();
	d3d_texture->AddRef();

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(d3d_texture);
	D3DVOLUME_DESC d3d_desc;
	::ZeroMemory(&d3d_desc, sizeof(D3DVOLUME_DESC));

	DX8_ErrorCode(Peek_D3D_VolumeTexture()->GetLevelDesc(0,&d3d_desc));

	if (initialized)
	{
		TextureFormat=D3DFormat_To_WW3DFormat(d3d_desc.Format);
		Width=d3d_desc.Width;
		Height=d3d_desc.Height;
		Depth=d3d_desc.Depth;
	}
}
#endif // RTS_RENDERER_DX8
