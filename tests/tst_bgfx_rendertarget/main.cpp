/*
**	Phase 5p smoke test: off-screen render targets (picture-in-picture).
**
**	Pass 1 — draw a lit, spinning checkered cube into a 256×256 offscreen
**	        render target (view > 0), backed by a bgfx FrameBuffer with
**	        BGRA8 color + D24S8 depth.
**	Pass 2 — clear the backbuffer (view 0) and draw a large flat quad
**	        with the RT's color texture bound on stage 0. The quad fills
**	        ~60% of the window, so the whole thing reads as "a preview of
**	        scene A embedded inside scene B".
**
**	Also stress-tests Destroy_Render_Target (called before Shutdown) —
**	the teardown path is as important as construction for a real adapter.
**
**	Exits after ~3s with `tst_bgfx_rendertarget: PASSED`.
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

struct CubeVertex
{
	float x, y, z;
	float nx, ny, nz;
	uint32_t abgr;
	float u, v;
};

constexpr CubeVertex kCubeVerts[24] = {
	{  0.5f, -0.5f, -0.5f,   1, 0, 0,  0xFFFFFFFFu, 0, 1 },
	{  0.5f,  0.5f, -0.5f,   1, 0, 0,  0xFFFFFFFFu, 0, 0 },
	{  0.5f,  0.5f,  0.5f,   1, 0, 0,  0xFFFFFFFFu, 1, 0 },
	{  0.5f, -0.5f,  0.5f,   1, 0, 0,  0xFFFFFFFFu, 1, 1 },
	{ -0.5f, -0.5f,  0.5f,  -1, 0, 0,  0xFFFFFFFFu, 0, 1 },
	{ -0.5f,  0.5f,  0.5f,  -1, 0, 0,  0xFFFFFFFFu, 0, 0 },
	{ -0.5f,  0.5f, -0.5f,  -1, 0, 0,  0xFFFFFFFFu, 1, 0 },
	{ -0.5f, -0.5f, -0.5f,  -1, 0, 0,  0xFFFFFFFFu, 1, 1 },
	{ -0.5f,  0.5f,  0.5f,   0, 1, 0,  0xFFFFFFFFu, 0, 1 },
	{  0.5f,  0.5f,  0.5f,   0, 1, 0,  0xFFFFFFFFu, 0, 0 },
	{  0.5f,  0.5f, -0.5f,   0, 1, 0,  0xFFFFFFFFu, 1, 0 },
	{ -0.5f,  0.5f, -0.5f,   0, 1, 0,  0xFFFFFFFFu, 1, 1 },
	{ -0.5f, -0.5f, -0.5f,   0,-1, 0,  0xFFFFFFFFu, 0, 1 },
	{  0.5f, -0.5f, -0.5f,   0,-1, 0,  0xFFFFFFFFu, 0, 0 },
	{  0.5f, -0.5f,  0.5f,   0,-1, 0,  0xFFFFFFFFu, 1, 0 },
	{ -0.5f, -0.5f,  0.5f,   0,-1, 0,  0xFFFFFFFFu, 1, 1 },
	{  0.5f, -0.5f,  0.5f,   0, 0, 1,  0xFFFFFFFFu, 0, 1 },
	{  0.5f,  0.5f,  0.5f,   0, 0, 1,  0xFFFFFFFFu, 0, 0 },
	{ -0.5f,  0.5f,  0.5f,   0, 0, 1,  0xFFFFFFFFu, 1, 0 },
	{ -0.5f, -0.5f,  0.5f,   0, 0, 1,  0xFFFFFFFFu, 1, 1 },
	{ -0.5f, -0.5f, -0.5f,   0, 0,-1,  0xFFFFFFFFu, 0, 1 },
	{ -0.5f,  0.5f, -0.5f,   0, 0,-1,  0xFFFFFFFFu, 0, 0 },
	{  0.5f,  0.5f, -0.5f,   0, 0,-1,  0xFFFFFFFFu, 1, 0 },
	{  0.5f, -0.5f, -0.5f,   0, 0,-1,  0xFFFFFFFFu, 1, 1 },
};
constexpr uint16_t kCubeIndices[36] = {
	 0, 1, 2,   0, 2, 3,  4, 5, 6,   4, 6, 7,
	 8, 9,10,   8,10,11, 12,13,14,  12,14,15,
	16,17,18,  16,18,19, 20,21,22,  20,22,23,
};

struct QuadVertex
{
	float x, y, z;
	float u, v;
};

// Fullscreen-ish quad in XY plane (facing +Z). Drawn after the RT so the
// backbuffer gets the RT's color texture projected across it.
constexpr QuadVertex kQuadVerts[4] = {
	{ -0.8f, -0.6f, 0.0f,  0.0f, 1.0f },
	{  0.8f, -0.6f, 0.0f,  1.0f, 1.0f },
	{  0.8f,  0.6f, 0.0f,  1.0f, 0.0f },
	{ -0.8f,  0.6f, 0.0f,  0.0f, 0.0f },
};
constexpr uint16_t kQuadIndices[6] = { 0, 1, 2,  0, 2, 3 };

void MakeChecker(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		const int bx = x / 4;
		const int by = y / 4;
		const bool a = ((bx + by) & 1) == 0;
		uint8_t* p = &out[(y*16 + x) * 4];
		if (a) { p[0] = 0xE8; p[1] = 0xB0; p[2] = 0x28; }
		else   { p[0] = 0x20; p[1] = 0x50; p[2] = 0x90; }
		p[3] = 0xFF;
	}
}

void MakeIdentity(float o[16]) { std::memset(o, 0, 16*sizeof(float)); o[0]=o[5]=o[10]=o[15]=1; }
void MakeRotateY(float o[16], float a) { float c=std::cos(a),s=std::sin(a); MakeIdentity(o); o[0]=c; o[2]=-s; o[8]=s; o[10]=c; }
void MakeRotateX(float o[16], float a) { float c=std::cos(a),s=std::sin(a); MakeIdentity(o); o[5]=c; o[6]=s; o[9]=-s; o[10]=c; }
void Mul(float o[16], const float a[16], const float b[16])
{
	float t[16];
	for (int i=0;i<4;++i) for (int j=0;j<4;++j)
	{
		float s=0; for (int k=0;k<4;++k) s+=a[i*4+k]*b[k*4+j];
		t[i*4+j]=s;
	}
	std::memcpy(o,t,sizeof(t));
}
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

void BuildCubeLayout(VertexLayoutDesc& d)
{
	d.stride = sizeof(CubeVertex);
	d.attrCount = 4;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(CubeVertex, x)    };
	d.attrs[1] = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(CubeVertex, nx)   };
	d.attrs[2] = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(CubeVertex, abgr) };
	d.attrs[3] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, offsetof(CubeVertex, u)    };
}

void BuildQuadLayout(VertexLayoutDesc& d)
{
	d.stride = sizeof(QuadVertex);
	d.attrCount = 2;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32, 3, 0, offsetof(QuadVertex, x) };
	d.attrs[1] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32, 2, 0, offsetof(QuadVertex, u) };
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_rendertarget", kW, kH, SDL_WINDOW_RESIZABLE);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// --- Shared resources ---------------------------------------------------
	uint8_t pixels[16*16*4];
	MakeChecker(pixels);
	const uintptr_t cubeTex = backend.Create_Texture_RGBA8(pixels, 16, 16, false);

	// --- Off-screen render target -------------------------------------------
	constexpr uint16_t kRTW = 256, kRTH = 256;
	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, /*hasDepth=*/true);
	if (rt == 0)
	{
		fprintf(stderr, "Create_Render_Target returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	const uintptr_t rtColorTex = backend.Get_Render_Target_Texture(rt);

	// --- Matrices reused across both passes ---------------------------------
	float rtProj[16], rtView[16];
	MakePerspective(rtProj, 60.0f * 3.14159265f / 180.0f, float(kRTW)/float(kRTH), 0.1f, 100.0f);
	MakeLookAt(rtView, 0.0f, 1.2f, -3.0f, 0, 0, 0);

	float bbProj[16], bbView[16];
	MakePerspective(bbProj, 60.0f * 3.14159265f / 180.0f, float(kW)/float(kH), 0.1f, 100.0f);
	MakeLookAt(bbView, 0.0f, 0.0f, -1.8f, 0, 0, 0);

	const Vector4 rtClearColor(0.12f, 0.10f, 0.20f, 1.0f);   // scene-A color
	const Vector4 bbClearColor(0.30f, 0.30f, 0.35f, 1.0f);   // scene-B surround

	const uint64_t t0 = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) running = false;

		const float t = (SDL_GetTicks() - t0) * 0.001f;
		if (t > 3.0f) running = false;

		// ==== Pass 1: cube → RT ============================================
		backend.Set_Render_Target(rt);

		VertexLayoutDesc cubeLayout; BuildCubeLayout(cubeLayout);
		backend.Set_Vertex_Buffer(kCubeVerts, 24, cubeLayout);
		backend.Set_Index_Buffer(kCubeIndices, 36);

		ShaderStateDesc sh1;
		sh1.depthCmp        = ShaderStateDesc::DEPTH_LESS;
		sh1.texturingEnable = true;
		backend.Set_Shader(sh1);

		MaterialDesc m1;
		m1.useLighting = true;
		backend.Set_Material(m1);

		LightDesc L;
		L.direction[0] = -0.4f; L.direction[1] = -0.8f; L.direction[2] = 0.3f;
		L.color[0] = 1.0f; L.color[1] = 0.95f; L.color[2] = 0.85f;
		L.ambient = 0.3f;
		backend.Set_Light(0, &L);

		backend.Set_Texture(0, cubeTex);

		backend.Set_Projection_Transform(rtProj);
		backend.Set_View_Transform(rtView);

		float rx[16], ry[16], world[16];
		MakeRotateX(rx, 0.35f);
		MakeRotateY(ry, t * 1.2f);
		Mul(world, rx, ry);
		backend.Set_World_Transform(world);

		backend.Clear(true, true, rtClearColor, 1.0f);
		backend.Begin_Scene();
		backend.Draw_Indexed(0, 24, 0, 12);
		// End_Scene is for the *frame*, not per view; call it once at the
		// very end below.

		// ==== Pass 2: quad-with-RT-texture → backbuffer ====================
		backend.Set_Render_Target(0);

		VertexLayoutDesc quadLayout; BuildQuadLayout(quadLayout);
		backend.Set_Vertex_Buffer(kQuadVerts, 4, quadLayout);
		backend.Set_Index_Buffer(kQuadIndices, 6);

		ShaderStateDesc sh2;
		sh2.depthCmp        = ShaderStateDesc::DEPTH_LESS;
		sh2.texturingEnable = true;
		sh2.cullEnable      = false;   // quad faces +Z; don't worry about winding
		backend.Set_Shader(sh2);

		MaterialDesc m2;
		m2.useLighting = false;  // pick the `tex` program, sample straight
		backend.Set_Material(m2);
		backend.Set_Light(0, nullptr);

		backend.Set_Texture(0, rtColorTex);

		backend.Set_Projection_Transform(bbProj);
		backend.Set_View_Transform(bbView);
		float bbWorld[16]; MakeIdentity(bbWorld);
		backend.Set_World_Transform(bbWorld);

		backend.Clear(true, true, bbClearColor, 1.0f);
		backend.Begin_Scene();
		backend.Draw_Indexed(0, 4, 0, 2);

		backend.End_Scene(true);
	}

	backend.Destroy_Render_Target(rt);
	backend.Destroy_Texture(cubeTex);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();
	fprintf(stderr, "tst_bgfx_rendertarget: PASSED\n");
	return 0;
}
