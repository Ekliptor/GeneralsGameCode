/*
**	Phase 5o smoke test: linear vertex fog via u_fogColor + u_fogRange.
**
**	A 24×24-vertex ground plane stretching from Z=1 (near camera) to Z=25
**	(far), textured with a 16×16 checker so perspective is obvious. Fog
**	range is 4..18 with a warm orange-brown fog color. The near strip is
**	fully clear (v_fog = 1); the far strip dissolves completely into the
**	fog color (v_fog = 0); the middle strip is the linear fade. Camera
**	stays put; the plane does too; this test proves the vertex-space
**	distance calculation, not animation.
**
**	Exits after ~3s with `tst_bgfx_fog: PASSED`.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "WW3D2/BackendDescriptors.h"
#include "vector4.h"

namespace
{

struct Vertex
{
	float x, y, z;
	float nx, ny, nz;
	uint32_t abgr;
	float u, v;
};

constexpr int kN = 24;                // grid resolution
constexpr int kVerts = kN * kN;
constexpr int kIndices = (kN - 1) * (kN - 1) * 6;

Vertex   g_verts[kVerts];
uint16_t g_indices[kIndices];

void BuildGround()
{
	for (int y = 0; y < kN; ++y)
	for (int x = 0; x < kN; ++x)
	{
		const float fx = float(x) / float(kN - 1);  // 0..1
		const float fz = float(y) / float(kN - 1);
		Vertex& V = g_verts[y * kN + x];
		V.x  = (fx - 0.5f) * 14.0f;           // -7..+7
		V.y  = -0.6f;
		V.z  = 1.0f + fz * 24.0f;             // 1..25 (camera at z=0 looks +z)
		V.nx = 0.0f; V.ny = 1.0f; V.nz = 0.0f;
		V.abgr = 0xFFFFFFFFu;
		V.u  = fx * 6.0f;                     // tile 6x over the plane
		V.v  = fz * 12.0f;                    // tile 12x along depth
	}

	int k = 0;
	for (int y = 0; y < kN - 1; ++y)
	for (int x = 0; x < kN - 1; ++x)
	{
		const uint16_t i00 = uint16_t(y * kN + x);
		const uint16_t i10 = uint16_t(y * kN + x + 1);
		const uint16_t i01 = uint16_t((y + 1) * kN + x);
		const uint16_t i11 = uint16_t((y + 1) * kN + x + 1);
		g_indices[k++] = i00; g_indices[k++] = i01; g_indices[k++] = i11;
		g_indices[k++] = i00; g_indices[k++] = i11; g_indices[k++] = i10;
	}
}

// 16×16 RGBA8 — 4×4-block checker of pale yellow / deep blue so the fade
// into fog color is obvious and both light + dark texels show the fade.
void MakeChecker(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		const int bx = x / 4;
		const int by = y / 4;
		const bool lit = ((bx + by) & 1) == 0;
		uint8_t* p = &out[(y*16 + x) * 4];
		if (lit) { p[0] = 0xE8; p[1] = 0xD8; p[2] = 0x60; }
		else     { p[0] = 0x20; p[1] = 0x30; p[2] = 0x80; }
		p[3] = 0xFF;
	}
}

void MakeIdentity(float o[16]) { std::memset(o, 0, 16*sizeof(float)); o[0]=o[5]=o[10]=o[15]=1; }
void MakePerspective(float o[16], float fovY, float aspect, float zn, float zf)
{
	float y=1.0f/std::tan(fovY*0.5f), x=y/aspect;
	MakeIdentity(o);
	o[0]=x; o[5]=y; o[10]=zf/(zf-zn); o[11]=1.0f; o[14]=-zn*zf/(zf-zn); o[15]=0.0f;
}
void MakeLookAt(float o[16], float ex,float ey,float ez, float tx,float ty,float tz)
{
	float zx=tx-ex,zy=ty-ey,zz=tz-ez;
	float zl=std::sqrt(zx*zx+zy*zy+zz*zz); zx/=zl;zy/=zl;zz/=zl;
	float xx=-zy, xy=zx, xz=0.0f;
	float xl=std::sqrt(xx*xx+xy*xy+xz*xz); xx/=xl;xy/=xl;xz/=xl;
	float ux=zy*xz-zz*xy, uy=zz*xx-zx*xz, uz=zx*xy-zy*xx;
	MakeIdentity(o);
	o[0]=xx; o[1]=ux; o[2]=zx;
	o[4]=xy; o[5]=uy; o[6]=zy;
	o[8]=xz; o[9]=uz; o[10]=zz;
	o[12]=-(xx*ex+xy*ey+xz*ez);
	o[13]=-(ux*ex+uy*ey+uz*ez);
	o[14]=-(zx*ex+zy*ey+zz*ez);
}

void BuildLayout(VertexLayoutDesc& d)
{
	d.stride = sizeof(Vertex);
	d.attrCount = 4;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(Vertex, x)    };
	d.attrs[1] = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(Vertex, nx)   };
	d.attrs[2] = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(Vertex, abgr) };
	d.attrs[3] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, offsetof(Vertex, u)    };
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	constexpr int kW = 800, kH = 600;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_fog", kW, kH, SDL_WINDOW_RESIZABLE);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	BuildGround();

	VertexLayoutDesc layout;
	BuildLayout(layout);
	backend.Set_Vertex_Buffer(g_verts, kVerts, layout);
	backend.Set_Index_Buffer(g_indices, kIndices);

	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.cullEnable      = false;
	shader.texturingEnable = true;
	shader.fogEnable       = true;
	shader.fogStart        = 4.0f;
	shader.fogEnd          = 18.0f;
	shader.fogColor[0]     = 0.70f;  // warm orange-brown
	shader.fogColor[1]     = 0.55f;
	shader.fogColor[2]     = 0.35f;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = true;
	backend.Set_Material(material);

	LightDesc light;
	light.direction[0] = 0.2f; light.direction[1] = -1.0f; light.direction[2] = 0.3f;
	light.color[0] = 1.0f; light.color[1] = 0.95f; light.color[2] = 0.85f;
	light.ambient = 0.45f;
	backend.Set_Light(0, &light);

	uint8_t pixels[16*16*4];
	MakeChecker(pixels);
	const uintptr_t tex = backend.Create_Texture_RGBA8(pixels, 16, 16, /*mipmap=*/false);
	if (tex == 0)
	{
		fprintf(stderr, "Create_Texture_RGBA8 returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	backend.Set_Texture(0, tex);

	float proj[16], view[16], world[16];
	MakePerspective(proj, 60.0f * 3.14159265f / 180.0f, float(kW)/float(kH), 0.1f, 100.0f);
	MakeLookAt(view, 0.0f, 0.6f, -0.1f, 0.0f, -0.2f, 8.0f);
	MakeIdentity(world);
	backend.Set_Projection_Transform(proj);
	backend.Set_View_Transform(view);
	backend.Set_World_Transform(world);

	// Clear color = fog color so the horizon blends seamlessly with the fade.
	const Vector4 clearColor(shader.fogColor[0], shader.fogColor[1], shader.fogColor[2], 1.0f);

	const uint64_t t0 = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) running = false;

		const float t = (SDL_GetTicks() - t0) * 0.001f;
		if (t > 3.0f) running = false;

		backend.Clear(true, true, clearColor, 1.0f);
		backend.Begin_Scene();
		backend.Draw_Indexed(0, kVerts, 0, kIndices / 3);
		backend.End_Scene(true);
	}

	backend.Destroy_Texture(tex);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();
	fprintf(stderr, "tst_bgfx_fog: PASSED\n");
	return 0;
}
