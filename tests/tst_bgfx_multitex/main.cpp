/*
**	Phase 5k smoke test: two-stage texture modulate through IRenderBackend.
**
**	Renders the Phase 5g cube with a vertex layout that carries two UV sets
**	(uv0, uv1) and binds two RGBA8 textures via Set_Texture(0, h0) and
**	Set_Texture(1, h1). The backend's uber-shader selector picks the tex2
**	program (fs_tex2 does `base * overlay` in the fragment). UV0 tracks the
**	face the standard way; UV1 is UV0 × 2 so the overlay tiles 2× across
**	every face, giving a visible "grid on top of checkerboard" look.
**
**	Exits after ~3s with `tst_bgfx_multitex: PASSED`.
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
	float u0, v0;
	float u1, v1;
};

constexpr CubeVertex kCubeVerts[24] = {
	// +X face
	{  0.5f, -0.5f, -0.5f,  0, 1,  0, 2 },
	{  0.5f,  0.5f, -0.5f,  0, 0,  0, 0 },
	{  0.5f,  0.5f,  0.5f,  1, 0,  2, 0 },
	{  0.5f, -0.5f,  0.5f,  1, 1,  2, 2 },
	// -X face
	{ -0.5f, -0.5f,  0.5f,  0, 1,  0, 2 },
	{ -0.5f,  0.5f,  0.5f,  0, 0,  0, 0 },
	{ -0.5f,  0.5f, -0.5f,  1, 0,  2, 0 },
	{ -0.5f, -0.5f, -0.5f,  1, 1,  2, 2 },
	// +Y face
	{ -0.5f,  0.5f,  0.5f,  0, 1,  0, 2 },
	{  0.5f,  0.5f,  0.5f,  0, 0,  0, 0 },
	{  0.5f,  0.5f, -0.5f,  1, 0,  2, 0 },
	{ -0.5f,  0.5f, -0.5f,  1, 1,  2, 2 },
	// -Y face
	{ -0.5f, -0.5f, -0.5f,  0, 1,  0, 2 },
	{  0.5f, -0.5f, -0.5f,  0, 0,  0, 0 },
	{  0.5f, -0.5f,  0.5f,  1, 0,  2, 0 },
	{ -0.5f, -0.5f,  0.5f,  1, 1,  2, 2 },
	// +Z face
	{  0.5f, -0.5f,  0.5f,  0, 1,  0, 2 },
	{  0.5f,  0.5f,  0.5f,  0, 0,  0, 0 },
	{ -0.5f,  0.5f,  0.5f,  1, 0,  2, 0 },
	{ -0.5f, -0.5f,  0.5f,  1, 1,  2, 2 },
	// -Z face
	{ -0.5f, -0.5f, -0.5f,  0, 1,  0, 2 },
	{ -0.5f,  0.5f, -0.5f,  0, 0,  0, 0 },
	{  0.5f,  0.5f, -0.5f,  1, 0,  2, 0 },
	{  0.5f, -0.5f, -0.5f,  1, 1,  2, 2 },
};

constexpr uint16_t kCubeIndices[36] = {
	 0, 1, 2,   0, 2, 3,
	 4, 5, 6,   4, 6, 7,
	 8, 9,10,   8,10,11,
	12,13,14,  12,14,15,
	16,17,18,  16,18,19,
	20,21,22,  20,22,23,
};

// 16×16 base: 4×4 blocks alternating orange / teal.
void MakeBase(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		const int bx = x / 4;
		const int by = y / 4;
		const bool isOrange = ((bx + by) & 1) == 0;
		uint8_t r, g, b;
		if (isOrange) { r = 0xE8; g = 0x80; b = 0x28; }
		else          { r = 0x28; g = 0xA0; b = 0xB0; }
		uint8_t* p = &out[(y*16 + x) * 4];
		p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF;
	}
}

// 16×16 overlay: mostly white with a 1-texel-thick black grid every 8 pixels
// (so with UV1=2×UV0 the grid prints every ~4 base-texels per face edge).
// The grid modulates the base colour to ~0 on the seam lines, giving a clean
// "two samplers multiplied" look that's distinct from either texture alone.
void MakeOverlay(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		const bool onGrid = (x % 8 == 0) || (y % 8 == 0);
		const uint8_t v = onGrid ? 0x10 : 0xFF;
		uint8_t* p = &out[(y*16 + x) * 4];
		p[0] = v; p[1] = v; p[2] = v; p[3] = 0xFF;
	}
}

void MakeIdentity(float o[16])
{
	std::memset(o, 0, 16*sizeof(float));
	o[0]=o[5]=o[10]=o[15]=1;
}
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
	d.attrCount = 3;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32, 3, 0, offsetof(CubeVertex, x)  };
	d.attrs[1] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32, 2, 0, offsetof(CubeVertex, u0) };
	d.attrs[2] = { VertexAttributeDesc::SEM_TEXCOORD1, VertexAttributeDesc::TYPE_FLOAT32, 2, 0, offsetof(CubeVertex, u1) };
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_multitex", kW, kH, SDL_WINDOW_RESIZABLE);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	VertexLayoutDesc layout;
	BuildCubeLayout(layout);
	backend.Set_Vertex_Buffer(kCubeVerts, 24, layout);
	backend.Set_Index_Buffer(kCubeIndices, 36);

	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = false;  // tex2 is unlit — selector should pick it over texLit
	backend.Set_Material(material);

	uint8_t basePx[16*16*4], overlayPx[16*16*4];
	MakeBase(basePx);
	MakeOverlay(overlayPx);
	const uintptr_t tex0 = backend.Create_Texture_RGBA8(basePx,    16, 16, /*mipmap=*/false);
	const uintptr_t tex1 = backend.Create_Texture_RGBA8(overlayPx, 16, 16, /*mipmap=*/false);
	if (tex0 == 0 || tex1 == 0)
	{
		fprintf(stderr, "Create_Texture_RGBA8 returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	backend.Set_Texture(0, tex0);
	backend.Set_Texture(1, tex1);

	float proj[16], view[16];
	MakePerspective(proj, 60.0f * 3.14159265f / 180.0f, float(kW)/float(kH), 0.1f, 100.0f);
	MakeLookAt(view, 0.0f, 1.2f, -3.0f, 0, 0, 0);
	backend.Set_Projection_Transform(proj);
	backend.Set_View_Transform(view);

	const Vector4 clearColor(0.07f, 0.08f, 0.12f, 1.0f);
	const uint64_t t0 = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) running = false;

		const float t = (SDL_GetTicks() - t0) * 0.001f;
		if (t > 3.0f) running = false;

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

	backend.Destroy_Texture(tex0);
	backend.Destroy_Texture(tex1);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();
	fprintf(stderr, "tst_bgfx_multitex: PASSED\n");
	return 0;
}
