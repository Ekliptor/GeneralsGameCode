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

// Phase D7 — direct-submit BGFX path for rigid MeshClass instances.
// Phase D10 — extended to CPU-skinned meshes (bone matrices resolved
// from the parent HLOD's HTreeClass). Bypasses the DX8 polygon-
// renderer batching system (`TheDX8MeshRenderer.Register_Mesh_Type` /
// `DX8TextureCategoryClass::Add_Render_Task` / etc., all stubbed in
// `ww3d2_bgfx_stubs.cpp` for the bgfx build) by reading vertex /
// index / material data directly from `MeshGeometryClass` /
// `MeshModelClass` / `MeshMatDescClass` and pushing it through
// `DX8Wrapper` → `IRenderBackend::Draw_Triangles_Dynamic`.
//
// Phase D11 — return a status enum so the caller can log per-mesh
// fall-through reasons instead of a single latched warning.

class MeshClass;
class RenderInfoClass;

namespace WW3D2 {

enum DirectSubmitStatus
{
	kDirectSubmit_Submitted = 0,
	kDirectSubmit_NoBackend,
	kDirectSubmit_NoModel,
	kDirectSubmit_SkinNoContainer,
	kDirectSubmit_SkinNoHTree,
	kDirectSubmit_EmptyGeometry,
	kDirectSubmit_NoVertsOrTris,
	// [PhaseD13c.3] mesh's bone-link array references bone indices outside
	// the HTree's pivot range. Happens in BGFX builds where
	// `WW3DAssetManager::Get_HTree` is stubbed to nullptr, so HLOD
	// containers fall back to a 1-pivot Init_Default HTree even though the
	// mesh expects a full skeleton. CPU skinning would read OOB bone
	// matrices and produce garbage flat polygons; we drop the submission
	// until proper HTree loading lands.
	kDirectSubmit_SkinBoneOutOfRange,
};

DirectSubmitStatus Render_Mesh_Direct_Bgfx(MeshClass& mesh, RenderInfoClass& rinfo);

const char* Direct_Submit_Status_Name(DirectSubmitStatus s);

// [PhaseD13c] called from BgfxBackend::End_Scene at logarithmic frame
// indices to dump the top-N per-mesh-name tris contributors and reset
// the per-frame accumulator. `frameIndex == 0` resets without logging.
void Phase_D13c_Mesh_Frame_End(unsigned frameIndex);

}  // namespace WW3D2

// Phase D11: incremented from MeshClass::Render's static-sort defer branch
// (per-target mesh.cpp copies); read from WW3D::Render_And_Clear_Static_Sort_Lists
// to report the magnitude of the BGFX null-guard drop. Defined once in
// MeshDirectRender.cpp (Core, shared by both targets).
extern int g_PhaseD11_StaticSortDeferCount;
