/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "MeshDirectRender.h"

#include "mesh.h"
#include "meshmdl.h"
#include "meshmatdesc.h"
#include "meshgeometry.h"
#include "rendobj.h"
#include "htree.h"
#include "rinfo.h"
#include "shader.h"
#include "vertmaterial.h"
#include "texture.h"
#include "dx8wrapper.h"
#include "WW3D2/IRenderBackend.h"
#include "WW3D2/RenderBackendRuntime.h"
#include "WW3D2/BackendDescriptors.h"

#include "matrix3d.h"
#include "vector3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>

// Phase D11: shared counter, defined here in Core/ so both per-target
// mesh.cpp + ww3d.cpp pairs see the same symbol via the extern in
// MeshDirectRender.h.
int g_PhaseD11_StaticSortDeferCount = 0;

namespace WW3D2 {

namespace {

struct DirectVert
{
	float    pos[3];
	float    norm[3];
	uint32_t diffuse;
	float    uv[2];
};
static_assert(sizeof(DirectVert) == 36, "DirectVert layout drift");

VertexLayoutDesc BuildLayout()
{
	VertexLayoutDesc d;
	d.stride    = sizeof(DirectVert);
	d.attrCount = 4;
	d.attrs[0]  = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0,  0 };
	d.attrs[1]  = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, 12 };
	d.attrs[2]  = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, 24 };
	d.attrs[3]  = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, 28 };
	return d;
}

// Single-frame, single-thread scratch — re-used across mesh draws.
std::vector<DirectVert> g_vertScratch;
std::vector<uint16_t>   g_idxScratch;
std::vector<Vector3>    g_skinPos;
std::vector<Vector3>    g_skinNorm;

// Phase D11 diagnostic logger — fires once per unique mesh-name encountered
// by Render_Mesh_Direct_Bgfx. Mirrors the Surface_Warn_Once pattern in
// surfaceclass.cpp. Output is grep-able via "[PhaseD11:directsubmit]".
void Diag_LogDirectSubmit_Once(MeshClass& mesh, MeshModelClass* model)
{
	static const char* seen[256] = {};
	static int seenCount = 0;

	const char* name = model->Get_Name();
	if (name == nullptr) name = "<unnamed>";

	for (int i = 0; i < seenCount; ++i) {
		if (seen[i] == name) return;
		if (std::strcmp(seen[i], name) == 0) return;
	}
	if (seenCount < static_cast<int>(sizeof(seen) / sizeof(seen[0]))) {
		seen[seenCount++] = name;
	} else {
		// Cap reached — stop logging new names but don't crash.
		return;
	}

	const int passCount = model->Get_Pass_Count();
	const int sortLevel = model->Get_Sort_Level();

	char flagBuf[96];
	flagBuf[0] = '\0';
	auto append = [&](const char* tag) {
		if (flagBuf[0]) std::strncat(flagBuf, "|", sizeof(flagBuf) - std::strlen(flagBuf) - 1);
		std::strncat(flagBuf, tag, sizeof(flagBuf) - std::strlen(flagBuf) - 1);
	};
	if (model->Get_Flag(MeshGeometryClass::SKIN))          append("SKIN");
	if (model->Get_Flag(MeshGeometryClass::SORT))          append("SORT");
	if (model->Get_Flag(MeshGeometryClass::TWO_SIDED))     append("TWO_SIDED");
	if (model->Get_Flag(MeshGeometryClass::ALIGNED))       append("ALIGNED");
	if (model->Get_Flag(MeshGeometryClass::ORIENTED))      append("ORIENTED");
	if (model->Get_Flag(MeshGeometryClass::CAST_SHADOW))   append("CAST_SHADOW");
	if (model->Get_Flag(MeshGeometryClass::PRELIT_VERTEX)) append("PRELIT_VERTEX");
	if (model->Get_Flag(MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_PASS))    append("PRELIT_LM_MP");
	if (model->Get_Flag(MeshGeometryClass::PRELIT_LIGHTMAP_MULTI_TEXTURE)) append("PRELIT_LM_MT");
	if (flagBuf[0] == '\0') std::strncpy(flagBuf, "-", sizeof(flagBuf) - 1);

	std::fprintf(stderr,
		"[PhaseD11:directsubmit] name=%s passes=%d sortLevel=%d flags=%s",
		name, passCount, sortLevel, flagBuf);

	const int kPasses = passCount > 0 ? passCount : 1;
	for (int p = 0; p < kPasses && p < MeshMatDescClass::MAX_PASSES; ++p) {
		for (int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; ++s) {
			const char* texLabel = "null";
			char texNameBuf[256];
			texNameBuf[0] = '\0';
			if (model->Has_Texture_Array(p, s)) {
				texLabel = "ARR";
			} else if (TextureClass* tex = model->Peek_Single_Texture(p, s)) {
				const char* tname = tex->Get_Texture_Name();
				if (tname != nullptr && tname[0] != '\0') {
					std::snprintf(texNameBuf, sizeof(texNameBuf), "%s", tname);
					texLabel = texNameBuf;
				} else {
					texLabel = "<noname>";
				}
			}
			std::fprintf(stderr, " p%ds%d=%s", p, s, texLabel);
		}
	}
	std::fprintf(stderr, "\n");
}

}  // namespace

const char* Direct_Submit_Status_Name(DirectSubmitStatus s)
{
	switch (s) {
		case kDirectSubmit_Submitted:           return "submitted";
		case kDirectSubmit_NoBackend:           return "no-backend";
		case kDirectSubmit_NoModel:             return "no-model";
		case kDirectSubmit_SkinNoContainer:     return "skin-no-container";
		case kDirectSubmit_SkinNoHTree:         return "skin-no-htree";
		case kDirectSubmit_EmptyGeometry:       return "empty-geometry";
		case kDirectSubmit_NoVertsOrTris:       return "no-verts-or-tris";
		case kDirectSubmit_SkinBoneOutOfRange:  return "skin-bone-oor";
	}
	return "unknown";
}

// [PhaseD13c] per-frame mesh-name → tris accumulator. Cleared in
// Phase_D13c_Mesh_Frame_End. Single-threaded (renderer thread).
namespace {
struct PhaseD13cMeshAcc { uint32_t calls = 0; uint32_t tris = 0; };
std::unordered_map<std::string, PhaseD13cMeshAcc>& Phase_D13c_Map()
{
	static std::unordered_map<std::string, PhaseD13cMeshAcc> s_map;
	return s_map;
}
}

void Phase_D13c_Mesh_Frame_End(unsigned frameIndex)
{
	auto& m = Phase_D13c_Map();
	if (frameIndex == 1 || frameIndex == 30 || frameIndex == 60 || frameIndex == 120 ||
	    frameIndex == 300 || frameIndex == 600 || frameIndex == 1200 ||
	    frameIndex == 1800 || frameIndex == 2400 || frameIndex == 3000) {
		std::vector<std::pair<std::string, PhaseD13cMeshAcc>> sorted;
		sorted.reserve(m.size());
		for (auto& kv : m) sorted.emplace_back(kv.first, kv.second);
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
			return a.second.tris > b.second.tris;
		});
		const size_t topN = std::min<size_t>(sorted.size(), 12);
		std::fprintf(stderr, "[PhaseD13c:meshtop] frame=%u uniqueNames=%zu top%zu:",
			frameIndex, sorted.size(), topN);
		for (size_t i = 0; i < topN; ++i) {
			std::fprintf(stderr, " %s=%u/%u", sorted[i].first.c_str(),
				sorted[i].second.calls, sorted[i].second.tris);
		}
		std::fprintf(stderr, "\n");
	}
	m.clear();
}

DirectSubmitStatus Render_Mesh_Direct_Bgfx(MeshClass& mesh, RenderInfoClass& rinfo)
{
	IRenderBackend* b = RenderBackendRuntime::Get_Active();
	if (b == nullptr)
		return kDirectSubmit_NoBackend;
	b->Set_Source_Tag(IRenderBackend::kSrcMeshDirect);

	MeshModelClass* model = mesh.Peek_Model();
	if (model == nullptr)
		return kDirectSubmit_NoModel;

	{
		const char* nm = mesh.Get_Name();
		if (nm != nullptr) {
			auto& acc = Phase_D13c_Map()[nm];
			acc.calls += 1;
			acc.tris  += static_cast<uint32_t>(model->Get_Polygon_Count());
		}
	}

	// Phase D11: log this mesh's identity + material state on first sight.
	// Cheap: dedup is a small linear scan, fires once per unique name.
	Diag_LogDirectSubmit_Once(mesh, model);

	const bool isSkin = model->Get_Flag(MeshGeometryClass::SKIN);

	// Phase D10: CPU-skinned submit path. Skin meshes need an HTree
	// (held by the parent HLOD/Hierarchy) to resolve bone transforms.
	// Without one, fall through to the legacy warning path.
	const HTreeClass* htree = nullptr;
	if (isSkin) {
		RenderObjClass* container = mesh.Get_Container();
		if (container == nullptr)
			return kDirectSubmit_SkinNoContainer;
		htree = container->Get_HTree();
		if (htree == nullptr)
			return kDirectSubmit_SkinNoHTree;
	}

	const int vCount   = model->Get_Vertex_Count();
	const int triCount = model->Get_Polygon_Count();
	if (vCount <= 0 || triCount <= 0)
		return kDirectSubmit_EmptyGeometry;

	// [PhaseD13c.3] skip skin meshes whose bone-link indices exceed the
	// HTree's pivot count. In the BGFX build `WW3DAssetManager::Get_HTree`
	// is stubbed to nullptr (ww3d2_bgfx_stubs.cpp:211), which makes
	// `Animatable3DObjClass` fall back to `HTreeClass::Init_Default()`
	// (1 pivot). CPU skinning would then read OOB matrices and emit
	// garbage flat-quad triangles all over the screen. Drop the draw
	// until real HTree loading is wired (D14).
	if (isSkin && htree != nullptr) {
		const int np = htree->Num_Pivots();
		if (const uint16* blinks = model->Get_Vertex_Bone_Links()) {
			for (int i = 0; i < vCount; ++i) {
				if (blinks[i] >= np) {
					static const char* skipped[256] = {};
					static int skippedCount = 0;
					const char* mname = mesh.Get_Name();
					if (mname == nullptr) mname = "<unnamed>";
					bool already = false;
					for (int s = 0; s < skippedCount; ++s) {
						if (skipped[s] == mname || std::strcmp(skipped[s], mname) == 0) {
							already = true; break;
						}
					}
					if (!already && skippedCount < 256) {
						skipped[skippedCount++] = mname;
						std::fprintf(stderr,
							"[PhaseD13c3:skipBoneOOR] name=%s htreePivots=%d badBoneIdx=%d "
							"(stubbed Get_HTree → 1-pivot fallback; bgfx infantry collapses)\n",
							mname, np, blinks[i]);
					}
					return kDirectSubmit_SkinBoneOutOfRange;
				}
			}
		}
	}

	const Vector3*   pos     = nullptr;
	const Vector3*   norm    = nullptr;
	if (isSkin) {
		// MeshClass::Get_Deformed_Vertices does CPU skinning via HTree
		// bone transforms (which the HLOD update has placed in world
		// space), so the produced verts are submission-ready and the
		// world transform passed to the backend below is Identity —
		// matching the VisRasterizer precedent in mesh.cpp.
		g_skinPos.resize(static_cast<size_t>(vCount));
		g_skinNorm.resize(static_cast<size_t>(vCount));
		mesh.Get_Deformed_Vertices(g_skinPos.data(), g_skinNorm.data());
		pos  = g_skinPos.data();
		norm = g_skinNorm.data();
	} else {
		pos  = model->Get_Vertex_Array();
		norm = model->Get_Vertex_Normal_Array();
	}

	const Vector2*   uv      = model->Get_UV_Array_By_Index(0);
	const unsigned*  diffuse = model->Get_Color_Array(0, /*create*/ false);
	const TriIndex*  tris    = model->Get_Polygon_Array();

	if (pos == nullptr || tris == nullptr)
		return kDirectSubmit_NoVertsOrTris;

	g_vertScratch.resize(static_cast<size_t>(vCount));
	for (int i = 0; i < vCount; ++i)
	{
		DirectVert& v = g_vertScratch[i];
		v.pos[0]  = pos[i].X;
		v.pos[1]  = pos[i].Y;
		v.pos[2]  = pos[i].Z;
		v.norm[0] = norm ? norm[i].X : 0.f;
		v.norm[1] = norm ? norm[i].Y : 0.f;
		v.norm[2] = norm ? norm[i].Z : 1.f;
		v.diffuse = diffuse ? diffuse[i] : 0xFFFFFFFFu;
		v.uv[0]   = uv ? uv[i].X : 0.f;
		v.uv[1]   = uv ? uv[i].Y : 0.f;
	}

	g_idxScratch.resize(static_cast<size_t>(triCount) * 3u);
	for (int i = 0; i < triCount; ++i)
	{
		g_idxScratch[i * 3 + 0] = static_cast<uint16_t>(tris[i].I);
		g_idxScratch[i * 3 + 1] = static_cast<uint16_t>(tris[i].J);
		g_idxScratch[i * 3 + 2] = static_cast<uint16_t>(tris[i].K);
	}

	// [PhaseD13c.2] one-shot bbox + skin-bone diagnostic per unique mesh
	// name. Confirms whether collapsed (zero-extent) skinned vertices
	// are the silver-rectangle source.
	{
		static const char* seenBbox[256] = {};
		static int seenBboxCount = 0;
		const char* mname = mesh.Get_Name();
		if (mname == nullptr) mname = "<unnamed>";
		bool already = false;
		for (int i = 0; i < seenBboxCount; ++i) {
			if (seenBbox[i] == mname || std::strcmp(seenBbox[i], mname) == 0) {
				already = true; break;
			}
		}
		if (!already && seenBboxCount < 256) {
			seenBbox[seenBboxCount++] = mname;
			float minX = pos[0].X, maxX = pos[0].X;
			float minY = pos[0].Y, maxY = pos[0].Y;
			float minZ = pos[0].Z, maxZ = pos[0].Z;
			for (int i = 1; i < vCount; ++i) {
				if (pos[i].X < minX) minX = pos[i].X; else if (pos[i].X > maxX) maxX = pos[i].X;
				if (pos[i].Y < minY) minY = pos[i].Y; else if (pos[i].Y > maxY) maxY = pos[i].Y;
				if (pos[i].Z < minZ) minZ = pos[i].Z; else if (pos[i].Z > maxZ) maxZ = pos[i].Z;
			}
			const float extX = maxX - minX;
			const float extY = maxY - minY;
			const float extZ = maxZ - minZ;
			std::fprintf(stderr,
				"[PhaseD13c2:bbox] name=%s skin=%d verts=%d ext=(%.2f,%.2f,%.2f) min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)",
				mname, isSkin ? 1 : 0, vCount,
				extX, extY, extZ, minX, minY, minZ, maxX, maxY, maxZ);
			if (isSkin && htree != nullptr) {
				const int np = htree->Num_Pivots();
				int maxBoneIdx = 0;
				int oobCount = 0;
				if (const uint16* blinks = const_cast<MeshModelClass*>(model)->Get_Vertex_Bone_Links()) {
					for (int i = 0; i < vCount; ++i) {
						const int b = blinks[i];
						if (b > maxBoneIdx) maxBoneIdx = b;
						if (b >= np) ++oobCount;
					}
				}
				std::fprintf(stderr,
					" htreePivots=%d maxBone=%d oobBonelinks=%d/%d",
					np, maxBoneIdx, oobCount, vCount);
				const int n = (np < 3) ? np : 3;
				for (int p = 0; p < n; ++p) {
					const Vector3 t = htree->Get_Transform(p).Get_Translation();
					std::fprintf(stderr, " bone%d.t=(%.2f,%.2f,%.2f)",
						p, t.X, t.Y, t.Z);
				}
			}
			std::fprintf(stderr, "\n");
		}
	}

	const VertexLayoutDesc layout = BuildLayout();
	const int passCount = model->Get_Pass_Count();
	const bool needsMultiPass = (passCount > 1);
	const bool needsArray =
		model->Has_Texture_Array(0, 0) || model->Has_Texture_Array(0, 1);

	// Phase D12 fast path — bit-for-bit D10 behaviour for the common
	// single-pass / single-texture case. The 85+ inventory meshes that
	// already rendered correctly post-D10 take this branch unchanged
	// (state-setter order matters for the bgfx pipeline; reordering it
	// even with equivalent operations produced regressions).
	if (!needsMultiPass && !needsArray)
	{
		if (VertexMaterialClass* vmat = model->Peek_Material(0, 0))
			DX8Wrapper::Set_Material(vmat);
		if (TextureClass* tex0 = model->Peek_Single_Texture(0, 0))
			DX8Wrapper::Set_Texture(0, tex0);
		else
			DX8Wrapper::Set_Texture(0, static_cast<TextureBaseClass*>(nullptr));
		DX8Wrapper::Set_Shader(model->Get_Single_Shader(0));

		DX8Wrapper::Set_Transform(D3DTS_WORLD,
		                          isSkin ? Matrix3D::Identity : mesh.Get_Transform());

		if (rinfo.light_environment != nullptr)
			DX8Wrapper::Set_Light_Environment(rinfo.light_environment);

		DX8Wrapper::Apply_Render_State_Changes();
		b->Draw_Triangles_Dynamic(
			g_vertScratch.data(),
			static_cast<unsigned>(vCount),
			layout,
			g_idxScratch.data(),
			static_cast<unsigned>(g_idxScratch.size()));

		return kDirectSubmit_Submitted;
	}

	// Phase D12 slow path — multi-pass material and/or per-polygon
	// texture array. Engaged only when the matdesc requires it; D11
	// catalogued exactly four ZH menu meshes that need this:
	//   PMWALLCHN3, UIRGRD_SKN.MUZZLEFX01, UITUNF_SKN.MUZZLEFX01 (arrays)
	//   CVJUNK_D.WINDOWS (multi-pass).
	{
		static const char* seenSlow[64] = {};
		static int seenSlowCount = 0;
		const char* mname = model->Get_Name();
		if (mname == nullptr) mname = "<unnamed>";
		bool already = false;
		for (int i = 0; i < seenSlowCount; ++i) {
			if (seenSlow[i] == mname || std::strcmp(seenSlow[i], mname) == 0) {
				already = true; break;
			}
		}
		if (!already && seenSlowCount < 64) {
			seenSlow[seenSlowCount++] = mname;
			std::fprintf(stderr,
				"[PhaseD12:slowpath] name=%s passes=%d arr00=%d arr01=%d\n",
				mname, passCount,
				model->Has_Texture_Array(0, 0) ? 1 : 0,
				model->Has_Texture_Array(0, 1) ? 1 : 0);
		}
	}
	// Vertex buffer is shared across passes (matdesc pattern is
	// "shared geometry, varying state per pass"); only material /
	// shader / texture rebind per pass.
	const Matrix3D worldXform =
		isSkin ? Matrix3D::Identity : mesh.Get_Transform();
	const int kPasses = passCount > 0 ? passCount : 1;

	for (int pass = 0; pass < kPasses && pass < MeshMatDescClass::MAX_PASSES; ++pass)
	{
		if (VertexMaterialClass* vmat = model->Peek_Material(0, pass))
			DX8Wrapper::Set_Material(vmat);
		DX8Wrapper::Set_Shader(model->Get_Single_Shader(pass));

		const bool arrayTex0 = model->Has_Texture_Array(pass, 0);
		const bool arrayTex1 = model->Has_Texture_Array(pass, 1);

		if (!arrayTex0 && !arrayTex1)
		{
			// Multi-pass with single textures per stage: one draw per pass.
			if (TextureClass* tex0 = model->Peek_Single_Texture(pass, 0))
				DX8Wrapper::Set_Texture(0, tex0);
			else
				DX8Wrapper::Set_Texture(0, static_cast<TextureBaseClass*>(nullptr));
			if (TextureClass* tex1 = model->Peek_Single_Texture(pass, 1))
				DX8Wrapper::Set_Texture(1, tex1);

			DX8Wrapper::Set_Transform(D3DTS_WORLD, worldXform);
			if (rinfo.light_environment != nullptr)
				DX8Wrapper::Set_Light_Environment(rinfo.light_environment);
			DX8Wrapper::Apply_Render_State_Changes();
			b->Draw_Triangles_Dynamic(
				g_vertScratch.data(),
				static_cast<unsigned>(vCount),
				layout,
				g_idxScratch.data(),
				static_cast<unsigned>(g_idxScratch.size()));
		}
		else
		{
			// Per-polygon texture-array path: scan triangles, batch
			// contiguous runs of equal (tex0, tex1) into single draws.
			// `Peek_Texture(pidx, pass, stage)` already handles the
			// array-vs-single fallback (meshmatdesc.cpp:542).
			int runStart = 0;
			TextureClass* runTex0 = model->Peek_Texture(0, pass, 0);
			TextureClass* runTex1 = arrayTex1
				? model->Peek_Texture(0, pass, 1)
				: model->Peek_Single_Texture(pass, 1);

			auto submitRun = [&](int endTri) {
				if (runTex0)
					DX8Wrapper::Set_Texture(0, runTex0);
				else
					DX8Wrapper::Set_Texture(0, static_cast<TextureBaseClass*>(nullptr));
				if (runTex1)
					DX8Wrapper::Set_Texture(1, runTex1);
				DX8Wrapper::Set_Transform(D3DTS_WORLD, worldXform);
				if (rinfo.light_environment != nullptr)
					DX8Wrapper::Set_Light_Environment(rinfo.light_environment);
				DX8Wrapper::Apply_Render_State_Changes();
				const int runLen = endTri - runStart;
				if (runLen > 0) {
					b->Draw_Triangles_Dynamic(
						g_vertScratch.data(),
						static_cast<unsigned>(vCount),
						layout,
						&g_idxScratch[static_cast<size_t>(runStart) * 3u],
						static_cast<unsigned>(runLen) * 3u);
				}
			};

			for (int i = 1; i < triCount; ++i)
			{
				TextureClass* t0 = model->Peek_Texture(i, pass, 0);
				TextureClass* t1 = arrayTex1
					? model->Peek_Texture(i, pass, 1)
					: runTex1;
				if (t0 != runTex0 || t1 != runTex1) {
					submitRun(i);
					runStart = i;
					runTex0 = t0;
					runTex1 = t1;
				}
			}
			submitRun(triCount);
		}
	}

	return kDirectSubmit_Submitted;
}

}  // namespace WW3D2
