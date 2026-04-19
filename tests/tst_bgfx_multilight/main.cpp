/*
**	Phase 5l smoke test: 4-slot directional lighting accumulation.
**
**	Renders the Phase 5g cube with the Phase 5i checkerboard texture and
**	THREE directional lights (red/green/blue) aimed from +X, +Y, +Z. Slot 3
**	stays disabled. Each face of the cube receives a different RGB blend
**	based on which light(s) face it — the top face picks up green, the +X
**	face picks up red, the +Z face picks up blue, and faces pointing at two
**	lights at once blend to yellow / magenta / cyan. Exits after ~3s with
**	`tst_bgfx_multilight: PASSED`.
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
	 0, 1, 2,   0, 2, 3,
	 4, 5, 6,   4, 6, 7,
	 8, 9,10,   8,10,11,
	12,13,14,  12,14,15,
	16,17,18,  16,18,19,
	20,21,22,  20,22,23,
};

// Dim neutral-gray texture so the RGB light contributions dominate visually.
void MakeGrayTexture(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		uint8_t* p = &out[(y*16 + x) * 4];
		p[0] = p[1] = p[2] = 0xC0; p[3] = 0xFF;
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

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	constexpr int kW = 800, kH = 600;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_multilight", kW, kH, SDL_WINDOW_RESIZABLE);
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
	material.useLighting = true;
	backend.Set_Material(material);

	// Three lights: red from -X (lights the +X face), green from -Y (lights
	// the +Y / top face), blue from -Z (lights the +Z face). Slot 0 also
	// carries the global ambient. Slot 3 is left disabled on purpose so the
	// test exercises zero-fill for unused slots.
	LightDesc l0;
	l0.direction[0] =  1.0f; l0.direction[1] =  0.0f; l0.direction[2] =  0.0f;
	l0.color[0] = 1.0f; l0.color[1] = 0.0f; l0.color[2] = 0.0f;
	l0.ambient = 0.12f;
	backend.Set_Light(0, &l0);

	LightDesc l1;
	l1.direction[0] =  0.0f; l1.direction[1] =  1.0f; l1.direction[2] =  0.0f;
	l1.color[0] = 0.0f; l1.color[1] = 1.0f; l1.color[2] = 0.0f;
	l1.ambient = 0.0f;
	backend.Set_Light(1, &l1);

	LightDesc l2;
	l2.direction[0] =  0.0f; l2.direction[1] =  0.0f; l2.direction[2] =  1.0f;
	l2.color[0] = 0.0f; l2.color[1] = 0.0f; l2.color[2] = 1.0f;
	l2.ambient = 0.0f;
	backend.Set_Light(2, &l2);

	// Slot 3 explicitly left unset — backend should zero-fill it.

	uint8_t pixels[16*16*4];
	MakeGrayTexture(pixels);
	const uintptr_t tex = backend.Create_Texture_RGBA8(pixels, 16, 16, /*mipmap=*/false);
	if (tex == 0)
	{
		fprintf(stderr, "Create_Texture_RGBA8 returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	backend.Set_Texture(0, tex);

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

	backend.Destroy_Texture(tex);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();
	fprintf(stderr, "tst_bgfx_multilight: PASSED\n");
	return 0;
}
