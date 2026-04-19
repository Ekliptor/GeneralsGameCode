/*
**	Phase 5g smoke test: drive IRenderBackend end-to-end.
**
**	Builds a unit cube with per-face normals + distinct per-face colors, then
**	each frame pushes:
**	   Set_View/Projection_Transform   — orbit camera + perspective
**	   Set_World_Transform             — rotating model
**	   Set_Shader / Set_Material / Set_Light
**	   Set_Vertex_Buffer / Set_Index_Buffer
**	   Draw_Indexed                    — 12 triangles
**
**	The program-selector inside BgfxBackend picks `tex_lit` for this layout
**	(has_normal + has_uv + material.useLighting = true), so a visibly lit
**	cube with per-face albedo is proof the full pipeline works. Runs ~3s
**	then exits `tst_bgfx_mesh: PASSED`.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cstdint>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "WW3D2/BackendDescriptors.h"
#include "vector4.h"

namespace
{

struct CubeVertex
{
	float x, y, z;
	float nx, ny, nz;
	uint32_t abgr;
	float u, v;
};

// Unit cube centered at origin, 24 vertices (4 per face), per-face normals.
// Color0 is distinct per face so the albedo is obvious even without texture
// differences (the backend binds a 2×2 white placeholder).
constexpr CubeVertex kCubeVerts[24] = {
	// +X face (right) — red
	{  0.5f, -0.5f, -0.5f,   1, 0, 0,  0xFF0000FFu, 0, 1 },
	{  0.5f,  0.5f, -0.5f,   1, 0, 0,  0xFF0000FFu, 0, 0 },
	{  0.5f,  0.5f,  0.5f,   1, 0, 0,  0xFF0000FFu, 1, 0 },
	{  0.5f, -0.5f,  0.5f,   1, 0, 0,  0xFF0000FFu, 1, 1 },
	// -X face (left) — green
	{ -0.5f, -0.5f,  0.5f,  -1, 0, 0,  0xFF00FF00u, 0, 1 },
	{ -0.5f,  0.5f,  0.5f,  -1, 0, 0,  0xFF00FF00u, 0, 0 },
	{ -0.5f,  0.5f, -0.5f,  -1, 0, 0,  0xFF00FF00u, 1, 0 },
	{ -0.5f, -0.5f, -0.5f,  -1, 0, 0,  0xFF00FF00u, 1, 1 },
	// +Y face (top) — blue
	{ -0.5f,  0.5f,  0.5f,   0, 1, 0,  0xFFFF0000u, 0, 1 },
	{  0.5f,  0.5f,  0.5f,   0, 1, 0,  0xFFFF0000u, 0, 0 },
	{  0.5f,  0.5f, -0.5f,   0, 1, 0,  0xFFFF0000u, 1, 0 },
	{ -0.5f,  0.5f, -0.5f,   0, 1, 0,  0xFFFF0000u, 1, 1 },
	// -Y face (bottom) — yellow
	{ -0.5f, -0.5f, -0.5f,   0,-1, 0,  0xFF00FFFFu, 0, 1 },
	{  0.5f, -0.5f, -0.5f,   0,-1, 0,  0xFF00FFFFu, 0, 0 },
	{  0.5f, -0.5f,  0.5f,   0,-1, 0,  0xFF00FFFFu, 1, 0 },
	{ -0.5f, -0.5f,  0.5f,   0,-1, 0,  0xFF00FFFFu, 1, 1 },
	// +Z face (front) — magenta
	{  0.5f, -0.5f,  0.5f,   0, 0, 1,  0xFFFF00FFu, 0, 1 },
	{  0.5f,  0.5f,  0.5f,   0, 0, 1,  0xFFFF00FFu, 0, 0 },
	{ -0.5f,  0.5f,  0.5f,   0, 0, 1,  0xFFFF00FFu, 1, 0 },
	{ -0.5f, -0.5f,  0.5f,   0, 0, 1,  0xFFFF00FFu, 1, 1 },
	// -Z face (back) — cyan
	{ -0.5f, -0.5f, -0.5f,   0, 0,-1,  0xFFFFFF00u, 0, 1 },
	{ -0.5f,  0.5f, -0.5f,   0, 0,-1,  0xFFFFFF00u, 0, 0 },
	{  0.5f,  0.5f, -0.5f,   0, 0,-1,  0xFFFFFF00u, 1, 0 },
	{  0.5f, -0.5f, -0.5f,   0, 0,-1,  0xFFFFFF00u, 1, 1 },
};

constexpr uint16_t kCubeIndices[36] = {
	 0, 1, 2,   0, 2, 3,      // +X
	 4, 5, 6,   4, 6, 7,      // -X
	 8, 9,10,   8,10,11,      // +Y
	12,13,14,  12,14,15,      // -Y
	16,17,18,  16,18,19,      // +Z
	20,21,22,  20,22,23,      // -Z
};

void MakeIdentity(float out[16])
{
	std::memset(out, 0, 16*sizeof(float));
	out[0] = out[5] = out[10] = out[15] = 1.0f;
}

// D3D/bgfx row-major: translation at [12..14].
void MakeTranslate(float out[16], float x, float y, float z)
{
	MakeIdentity(out);
	out[12] = x; out[13] = y; out[14] = z;
}

void MakeRotateY(float out[16], float radians)
{
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	MakeIdentity(out);
	out[0]  =  c; out[2]  = -s;
	out[8]  =  s; out[10] =  c;
}

void MakeRotateX(float out[16], float radians)
{
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	MakeIdentity(out);
	out[5]  =  c; out[6]  =  s;
	out[9]  = -s; out[10] =  c;
}

// Row-major 4×4 multiply: out = a * b
void Mul(float out[16], const float a[16], const float b[16])
{
	float tmp[16];
	for (int i = 0; i < 4; ++i)
	for (int j = 0; j < 4; ++j)
	{
		float s = 0.0f;
		for (int k = 0; k < 4; ++k)
			s += a[i*4 + k] * b[k*4 + j];
		tmp[i*4 + j] = s;
	}
	std::memcpy(out, tmp, sizeof(tmp));
}

// D3D-style left-handed perspective. NDC z ∈ [0, 1] matches bgfx's D3D
// conventions; on GL/Metal bgfx internally remaps.
void MakePerspective(float out[16], float fovY_rad, float aspect, float znear, float zfar)
{
	const float yScale = 1.0f / std::tan(fovY_rad * 0.5f);
	const float xScale = yScale / aspect;
	MakeIdentity(out);
	out[0]  = xScale;
	out[5]  = yScale;
	out[10] = zfar / (zfar - znear);
	out[11] = 1.0f;
	out[14] = -znear * zfar / (zfar - znear);
	out[15] = 0.0f;
}

// Left-handed look-at. `target - eye` defines +Z, world up = (0,1,0).
void MakeLookAt(float out[16], float ex, float ey, float ez,
                float tx, float ty, float tz)
{
	float zx = tx-ex, zy = ty-ey, zz = tz-ez;
	float zl = std::sqrt(zx*zx + zy*zy + zz*zz);
	zx /= zl; zy /= zl; zz /= zl;
	// right = up × z, up world = (0,1,0)
	float xx = 0.0f*zz - 1.0f*zy;
	float xy = 1.0f*zx - 0.0f*zz;
	float xz = 0.0f*zy - 0.0f*zx;
	float xl = std::sqrt(xx*xx + xy*xy + xz*xz);
	xx /= xl; xy /= xl; xz /= xl;
	// up = z × right
	float ux = zy*xz - zz*xy;
	float uy = zz*xx - zx*xz;
	float uz = zx*xy - zy*xx;

	MakeIdentity(out);
	out[0]  = xx;  out[1]  = ux;  out[2]  = zx;
	out[4]  = xy;  out[5]  = uy;  out[6]  = zy;
	out[8]  = xz;  out[9]  = uz;  out[10] = zz;
	out[12] = -(xx*ex + xy*ey + xz*ez);
	out[13] = -(ux*ex + uy*ey + uz*ez);
	out[14] = -(zx*ex + zy*ey + zz*ez);
}

void BuildCubeLayout(VertexLayoutDesc& d)
{
	d.stride = sizeof(CubeVertex);
	d.attrCount = 4;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(CubeVertex, x)    };
	d.attrs[1] = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(CubeVertex, nx)   };
	d.attrs[2] = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(CubeVertex, abgr) };
	d.attrs[3] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, offsetof(CubeVertex, u)    };
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	constexpr int kWidth  = 800;
	constexpr int kHeight = 600;

	SDL_Window* window = SDL_CreateWindow("tst_bgfx_mesh", kWidth, kHeight, SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh)
	{
		fprintf(stderr, "getNativeWindowHandle returned null\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	BgfxBackend backend;
	if (!backend.Init(nwh, kWidth, kHeight, true))
	{
		fprintf(stderr, "BgfxBackend::Init failed\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	VertexLayoutDesc layout;
	BuildCubeLayout(layout);
	backend.Set_Vertex_Buffer(kCubeVerts, 24, layout);
	backend.Set_Index_Buffer(kCubeIndices, 36);

	// Static shader/material/light descriptors — set once.
	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.depthWrite      = true;
	shader.cullEnable      = true;
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.diffuse[0] = 1.0f;
	material.diffuse[1] = 1.0f;
	material.diffuse[2] = 1.0f;
	material.opacity    = 1.0f;
	material.useLighting = true;
	backend.Set_Material(material);

	LightDesc light;
	light.type = LightDesc::LIGHT_DIRECTIONAL;
	// Direction pointing from light into scene (down + slightly forward/right)
	light.direction[0] = -0.4f;
	light.direction[1] = -0.8f;
	light.direction[2] =  0.3f;
	light.color[0] = 1.0f; light.color[1] = 0.95f; light.color[2] = 0.85f;
	light.ambient   = 0.20f;
	light.intensity = 1.0f;
	backend.Set_Light(0, &light);

	float proj[16];
	MakePerspective(proj,
	                60.0f * 3.14159265f / 180.0f,
	                float(kWidth) / float(kHeight),
	                0.1f, 100.0f);
	backend.Set_Projection_Transform(proj);

	float view[16];
	MakeLookAt(view, 0.0f, 1.2f, -3.0f,  0.0f, 0.0f, 0.0f);
	backend.Set_View_Transform(view);

	const Vector4 clearColor(0.07f, 0.08f, 0.12f, 1.0f);

	const uint64_t startTicks = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
			if (event.type == SDL_EVENT_QUIT) running = false;

		const float t = float(SDL_GetTicks() - startTicks) * 0.001f;
		if (t > 3.0f) running = false;

		// World = rotateY(t) * rotateX(0.35)
		float rx[16], ry[16], world[16];
		MakeRotateX(rx, 0.35f);
		MakeRotateY(ry, t * 0.9f);
		Mul(world, rx, ry);
		backend.Set_World_Transform(world);

		backend.Clear(true, true, clearColor, 1.0f);
		backend.Begin_Scene();
		backend.Draw_Indexed(0, 24, 0, 12);
		backend.End_Scene(true);
	}

	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "tst_bgfx_mesh: PASSED\n");
	return 0;
}
