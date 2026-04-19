/*
**	Phase 5j smoke test: exercise `Create_Texture_From_Memory` by
**	constructing a minimal uncompressed A8R8G8B8 DDS in memory, handing it
**	to bimg, and rendering the Phase 5g spinning cube with the decoded
**	texture. A successful run proves:
**
**	  - bimg links into corei_bgfx
**	  - bimg::imageParse recognizes a legacy A8R8G8B8 DDS
**	  - bgfx::createTexture2D accepts the decoded bytes + format
**	  - the mip-chain release callback fires (no leak / double-free)
**
**	The DDS format is trivial enough to write by hand — 128-byte header +
**	pixel data. Using uncompressed RGBA sidesteps the DXT compressor and
**	keeps the test dependency-free.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>

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
	 0, 1, 2,   0, 2, 3,    4, 5, 6,   4, 6, 7,
	 8, 9,10,   8,10,11,   12,13,14,  12,14,15,
	16,17,18,  16,18,19,   20,21,22,  20,22,23,
};

// Little-endian helpers — DDS is LE.
void WriteU32(std::vector<uint8_t>& buf, uint32_t v)
{
	buf.push_back(uint8_t(v));
	buf.push_back(uint8_t(v >> 8));
	buf.push_back(uint8_t(v >> 16));
	buf.push_back(uint8_t(v >> 24));
}

// Build a legacy uncompressed A8R8G8B8 DDS (128-byte header + pixel data).
// Encodes a 16×16 diagonal-stripe pattern: blue background, orange stripes
// every 4 pixels, green dots at the corners.
std::vector<uint8_t> MakeDDS_A8R8G8B8_16x16()
{
	constexpr uint32_t W = 16, H = 16;
	std::vector<uint8_t> out;
	out.reserve(128 + W*H*4);

	// Magic
	out.push_back('D'); out.push_back('D'); out.push_back('S'); out.push_back(' ');

	// DDS_HEADER (124 bytes)
	//   DDSD_CAPS(0x1) | DDSD_HEIGHT(0x2) | DDSD_WIDTH(0x4) | DDSD_PIXELFORMAT(0x1000) = 0x1007
	constexpr uint32_t kFlags = 0x1 | 0x2 | 0x4 | 0x1000;
	WriteU32(out, 124);          // dwSize
	WriteU32(out, kFlags);       // dwFlags
	WriteU32(out, H);            // dwHeight
	WriteU32(out, W);            // dwWidth
	WriteU32(out, W*4);          // dwPitchOrLinearSize
	WriteU32(out, 0);            // dwDepth
	WriteU32(out, 0);            // dwMipMapCount (0 ⇒ just base level)
	for (int i = 0; i < 11; ++i) // dwReserved1[11]
		WriteU32(out, 0);

	// DDS_PIXELFORMAT (32 bytes)
	//   DDPF_ALPHAPIXELS(0x1) | DDPF_RGB(0x40) = 0x41
	WriteU32(out, 32);               // dwSize
	WriteU32(out, 0x41);             // dwFlags
	WriteU32(out, 0);                // dwFourCC
	WriteU32(out, 32);               // dwRGBBitCount
	WriteU32(out, 0x00FF0000);       // dwRBitMask
	WriteU32(out, 0x0000FF00);       // dwGBitMask
	WriteU32(out, 0x000000FF);       // dwBBitMask
	WriteU32(out, 0xFF000000);       // dwABitMask

	// Caps
	WriteU32(out, 0x1000);           // dwCaps = DDSCAPS_TEXTURE
	WriteU32(out, 0);                // dwCaps2
	WriteU32(out, 0);                // dwCaps3
	WriteU32(out, 0);                // dwCaps4
	WriteU32(out, 0);                // dwReserved2

	// Pixel data: 16×16 A8R8G8B8 — stored little-endian dword 0xAARRGGBB so
	// bytes per pixel come out as B, G, R, A in the file.
	for (uint32_t y = 0; y < H; ++y)
	for (uint32_t x = 0; x < W; ++x)
	{
		uint8_t R = 0x20, G = 0x40, B = 0x90; // deep blue
		if ((x + y) % 4 == 0) { R = 0xE8; G = 0x80; B = 0x20; } // orange stripe
		const bool corner = (x < 2 || x >= W-2) && (y < 2 || y >= H-2);
		if (corner) { R = 0x30; G = 0xD0; B = 0x40; } // green corner
		out.push_back(B);
		out.push_back(G);
		out.push_back(R);
		out.push_back(0xFF); // alpha
	}
	return out;
}

void MakeIdentity(float o[16]) { std::memset(o,0,16*sizeof(float)); o[0]=o[5]=o[10]=o[15]=1; }
void MakeRotateY(float o[16], float a) { float c=std::cos(a),s=std::sin(a); MakeIdentity(o); o[0]=c; o[2]=-s; o[8]=s; o[10]=c; }
void MakeRotateX(float o[16], float a) { float c=std::cos(a),s=std::sin(a); MakeIdentity(o); o[5]=c; o[6]=s; o[9]=-s; o[10]=c; }
void Mul(float o[16], const float a[16], const float b[16])
{
	float t[16];
	for (int i=0;i<4;++i) for (int j=0;j<4;++j)
	{ float s=0; for (int k=0;k<4;++k) s+=a[i*4+k]*b[k*4+j]; t[i*4+j]=s; }
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
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init failed\n"); return 1; }
	constexpr int kW = 800, kH = 600;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_bimg", kW, kH, SDL_WINDOW_RESIZABLE);
	if (!window) { SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	VertexLayoutDesc layout;
	BuildCubeLayout(layout);
	backend.Set_Vertex_Buffer(kCubeVerts, 24, layout);
	backend.Set_Index_Buffer(kCubeIndices, 36);

	ShaderStateDesc shader;
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = true;
	backend.Set_Material(material);

	LightDesc light;
	light.direction[0] = -0.4f; light.direction[1] = -0.8f; light.direction[2] = 0.3f;
	light.ambient = 0.25f;
	backend.Set_Light(0, &light);

	// The whole point of this test: decode via bimg rather than uploading
	// raw RGBA bytes.
	const std::vector<uint8_t> dds = MakeDDS_A8R8G8B8_16x16();
	const uintptr_t tex = backend.Create_Texture_From_Memory(dds.data(),
	                                                          uint32_t(dds.size()));
	if (tex == 0)
	{
		fprintf(stderr, "Create_Texture_From_Memory returned 0\n");
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
	fprintf(stderr, "tst_bgfx_bimg: PASSED\n");
	return 0;
}
