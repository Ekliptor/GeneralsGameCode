/*
**	Phase 5m smoke test: fire-and-forget per-frame geometry through
**	IRenderBackend::Draw_Triangles_Dynamic.
**
**	A 16×16 grid of vertices whose Y position is a 2D sine wave driven by
**	elapsed time — recomputed on the CPU every frame, uploaded into bgfx
**	transient buffers by Draw_Triangles_Dynamic, and rendered with analytic
**	normals through the Phase 5l multi-light program. No persistent
**	bgfx VB/IB is created; the transient pool reclaims the bytes at frame
**	end. Exits after ~3s with `tst_bgfx_dynamic: PASSED`.
**
**	This is the streaming-geometry primitive for particles, decals, dynamic
**	terrain, and HUD — everything that would otherwise thrash the GPU
**	allocator by destroying + recreating a persistent VB per frame.
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

struct GridVertex
{
	float x, y, z;
	float nx, ny, nz;
	uint32_t abgr;
	float u, v;
};

constexpr int kGrid = 16;                  // vertices per side
constexpr int kGridVerts = kGrid * kGrid;
constexpr int kGridQuads = (kGrid - 1) * (kGrid - 1);
constexpr int kGridIndices = kGridQuads * 6;

GridVertex g_verts[kGridVerts];
uint16_t   g_indices[kGridIndices];

void BuildIndices()
{
	int k = 0;
	for (int y = 0; y < kGrid - 1; ++y)
	for (int x = 0; x < kGrid - 1; ++x)
	{
		const uint16_t i00 = uint16_t(y * kGrid + x);
		const uint16_t i10 = uint16_t(y * kGrid + x + 1);
		const uint16_t i01 = uint16_t((y + 1) * kGrid + x);
		const uint16_t i11 = uint16_t((y + 1) * kGrid + x + 1);
		g_indices[k++] = i00; g_indices[k++] = i01; g_indices[k++] = i11;
		g_indices[k++] = i00; g_indices[k++] = i11; g_indices[k++] = i10;
	}
}

// f(x,z,t) = amp * [ sin(kx + wt) + cos(kz + wt) ]
// df/dx   = amp *   k * cos(kx + wt)
// df/dz   = amp * (-k) * sin(kz + wt)
// Tangent vectors: Tx = (1, df/dx, 0), Tz = (0, df/dz, 1)
// Normal = normalize(Tz × Tx) = normalize((-df/dx, 1, -df/dz))
void FillVerts(float t)
{
	constexpr float kExtent = 1.5f;
	constexpr float kAmp    = 0.18f;
	constexpr float kFreq   = 3.5f;
	constexpr float kOmega  = 2.0f;

	for (int y = 0; y < kGrid; ++y)
	for (int x = 0; x < kGrid; ++x)
	{
		const float fx = (float(x) / float(kGrid - 1)) * 2.0f - 1.0f;  // -1..1
		const float fz = (float(y) / float(kGrid - 1)) * 2.0f - 1.0f;
		const float wx = fx * kExtent;
		const float wz = fz * kExtent;

		const float phaseX = kFreq * wx + kOmega * t;
		const float phaseZ = kFreq * wz + kOmega * t;
		const float height = kAmp * (std::sin(phaseX) + std::cos(phaseZ));

		const float dfdx =  kAmp * kFreq * std::cos(phaseX);
		const float dfdz = -kAmp * kFreq * std::sin(phaseZ);
		float nx = -dfdx;
		float ny =  1.0f;
		float nz = -dfdz;
		const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
		nx /= nl; ny /= nl; nz /= nl;

		GridVertex& v = g_verts[y * kGrid + x];
		v.x = wx; v.y = height; v.z = wz;
		v.nx = nx; v.ny = ny; v.nz = nz;
		v.abgr = 0xFFFFFFFFu;
		v.u = float(x) / float(kGrid - 1);
		v.v = float(y) / float(kGrid - 1);
	}
}

void MakeGrayTexture(uint8_t out[16*16*4])
{
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		uint8_t* p = &out[(y*16 + x) * 4];
		// Soft horizontal gradient so the wave's perspective is easy to read.
		p[0] = 0x80 + (x * 0x50 / 16);
		p[1] = 0xA0;
		p[2] = 0xC0 - (y * 0x30 / 16);
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

void BuildGridLayout(VertexLayoutDesc& d)
{
	d.stride = sizeof(GridVertex);
	d.attrCount = 4;
	d.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(GridVertex, x)    };
	d.attrs[1] = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(GridVertex, nx)   };
	d.attrs[2] = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(GridVertex, abgr) };
	d.attrs[3] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, offsetof(GridVertex, u)    };
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_dynamic", kW, kH, SDL_WINDOW_RESIZABLE);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	BuildIndices();

	VertexLayoutDesc layout;
	BuildGridLayout(layout);

	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.cullEnable      = false;  // render both sides of the wave
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = true;
	backend.Set_Material(material);

	LightDesc light;
	light.direction[0] = -0.4f; light.direction[1] = -0.8f; light.direction[2] = 0.3f;
	light.color[0] = 1.0f; light.color[1] = 0.95f; light.color[2] = 0.85f;
	light.ambient = 0.25f;
	backend.Set_Light(0, &light);

	uint8_t pixels[16*16*4];
	MakeGrayTexture(pixels);
	const uintptr_t tex = backend.Create_Texture_RGBA8(pixels, 16, 16, /*mipmap=*/false);
	if (tex == 0)
	{
		fprintf(stderr, "Create_Texture_RGBA8 returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	backend.Set_Texture(0, tex);

	float proj[16], view[16], world[16];
	MakePerspective(proj, 55.0f * 3.14159265f / 180.0f, float(kW)/float(kH), 0.1f, 100.0f);
	MakeLookAt(view, 0.0f, 1.3f, -2.4f, 0, 0, 0);
	MakeIdentity(world);
	backend.Set_Projection_Transform(proj);
	backend.Set_View_Transform(view);
	backend.Set_World_Transform(world);

	const Vector4 clearColor(0.07f, 0.08f, 0.12f, 1.0f);
	const uint64_t t0 = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) running = false;

		const float t = (SDL_GetTicks() - t0) * 0.001f;
		if (t > 3.0f) running = false;

		FillVerts(t);

		backend.Clear(true, true, clearColor, 1.0f);
		backend.Begin_Scene();
		backend.Draw_Triangles_Dynamic(
			g_verts, kGridVerts, layout,
			g_indices, kGridIndices);
		backend.End_Scene(true);
	}

	backend.Destroy_Texture(tex);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();
	fprintf(stderr, "tst_bgfx_dynamic: PASSED\n");
	return 0;
}
