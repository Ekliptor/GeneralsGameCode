/*
**	Phase 5h.11 smoke test: Blinn-Phong per-vertex specular.
**
**	Setup:
**	  * 64×64 RT, white 2×2 texture, 33×33 tessellated quad at z=0.5
**	    facing the camera (N = 0,0,-1), identity transforms (camera at
**	    world origin via u_invView[3].xyz = 0).
**	  * Vertex color = 0x00000000 (alpha 0 too, but we force A=FF by
**	    OR'ing 0xFF000000) so the diffuse path contributes ZERO — only
**	    specular lands on the fragment.
**	  * Slot 0: type=DIRECTIONAL, direction=(0,0,1) (light points at
**	    +Z), color=0 (no diffuse), specular=(1,1,1).
**	  * Material: diffuse=0, specularColor=(1,1,1), specularPower=32.
**
**	Expected per-vertex specular at (x,y,0.5):
**	  L = (0,0,-1), V = normalize(origin - pos) = -pos / |pos|.
**	  For the center vertex (0,0,0.5), V=(0,0,-1) → H=N=(0,0,-1) →
**	  N·H = 1 → specF = 1 → highlight at full strength.
**	  For corner (1,1,0.5): V≈(-.667,-.667,-.333), L+V=(-.667,-.667,-1.333),
**	  H≈(-.408,-.408,-.816), N·H≈0.816, pow(.816, 32)≈0.002 — near zero.
**
**	Assertions (BGRA, after texture×vertex_color=0 + v_specular add):
**	  * center (32,32) ≥ 200   (peak highlight)
**	  * corners (2,2)/(61,61) ≤ 15 (tight Blinn-Phong falloff)
**	  * center − mid (16,32) ≥ 80 (highlight genuinely narrow)
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

constexpr uint16_t kRTW = 64;
constexpr uint16_t kRTH = 64;
constexpr int kGrid  = 33;
constexpr int kGridV = kGrid * kGrid;
constexpr int kGridI = (kGrid - 1) * (kGrid - 1) * 6;

struct GridVertex
{
	float x, y, z;
	float nx, ny, nz;
	uint32_t abgr;
	float u, v;
};

GridVertex g_verts[kGridV];
uint16_t   g_indices[kGridI];

void BuildGrid()
{
	for (int y = 0; y < kGrid; ++y)
	for (int x = 0; x < kGrid; ++x)
	{
		const float fx = (float(x) / float(kGrid - 1)) * 2.0f - 1.0f;
		const float fy = (float(y) / float(kGrid - 1)) * 2.0f - 1.0f;
		GridVertex& v = g_verts[y * kGrid + x];
		v.x = fx; v.y = fy; v.z = 0.5f;
		v.nx = 0.0f; v.ny = 0.0f; v.nz = -1.0f;
		v.abgr = 0xFF000000u;           // A=FF, diffuse=0 → only specular shows
		v.u = float(x) / float(kGrid - 1);
		v.v = float(y) / float(kGrid - 1);
	}
	int k = 0;
	for (int y = 0; y < kGrid - 1; ++y)
	for (int x = 0; x < kGrid - 1; ++x)
	{
		const uint16_t i00 = uint16_t(y * kGrid + x);
		const uint16_t i10 = uint16_t(y * kGrid + x + 1);
		const uint16_t i01 = uint16_t((y + 1) * kGrid + x);
		const uint16_t i11 = uint16_t((y + 1) * kGrid + x + 1);
		g_indices[k++] = i00; g_indices[k++] = i10; g_indices[k++] = i11;
		g_indices[k++] = i00; g_indices[k++] = i11; g_indices[k++] = i01;
	}
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	constexpr int kW = 320, kH = 240;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_specular", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	BuildGrid();

	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, /*hasDepth=*/true);
	if (rt == 0) { backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	float identity[16] = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	backend.Set_World_Transform(identity);
	backend.Set_View_Transform(identity);
	backend.Set_Projection_Transform(identity);

	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.cullEnable      = false;
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = true;
	material.specularColor[0] = 1.0f;
	material.specularColor[1] = 1.0f;
	material.specularColor[2] = 1.0f;
	material.specularPower    = 32.0f;
	backend.Set_Material(material);

	LightDesc l;
	l.type = LightDesc::LIGHT_DIRECTIONAL;
	l.direction[0] = 0.0f;
	l.direction[1] = 0.0f;
	l.direction[2] = 1.0f;   // from light toward scene (+Z)
	l.color[0] = 0.0f; l.color[1] = 0.0f; l.color[2] = 0.0f;     // no diffuse
	l.specular[0] = 1.0f; l.specular[1] = 1.0f; l.specular[2] = 1.0f;
	l.ambient = 0.0f;
	l.intensity = 1.0f;
	backend.Set_Light(0, &l);

	uint8_t whitePx[2*2*4];
	for (int i = 0; i < 4; ++i) { whitePx[i*4+0]=whitePx[i*4+1]=whitePx[i*4+2]=0xFF; whitePx[i*4+3]=0xFF; }
	const uintptr_t tex = backend.Create_Texture_RGBA8(whitePx, 2, 2, /*mipmap=*/false);
	if (tex == 0) { backend.Destroy_Render_Target(rt); backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
	backend.Set_Texture(0, tex);

	VertexLayoutDesc layout;
	layout.stride = sizeof(GridVertex);
	layout.attrCount = 4;
	layout.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(GridVertex, x)    };
	layout.attrs[1] = { VertexAttributeDesc::SEM_NORMAL,    VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(GridVertex, nx)   };
	layout.attrs[2] = { VertexAttributeDesc::SEM_COLOR0,    VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(GridVertex, abgr) };
	layout.attrs[3] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32,          2, 0, offsetof(GridVertex, u)    };

	backend.Set_Render_Target(rt);
	backend.Clear(true, true, Vector4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
	backend.Begin_Scene();
	backend.Draw_Triangles_Dynamic(g_verts, kGridV, layout, g_indices, kGridI);
	backend.End_Scene(true);

	const uint32_t bytes = uint32_t(kRTW) * uint32_t(kRTH) * 4u;
	uint8_t* pixels = static_cast<uint8_t*>(std::malloc(bytes));
	if (!pixels || !backend.Capture_Render_Target(rt, pixels, bytes))
	{
		fprintf(stderr, "readback failed\n");
		std::free(pixels);
		backend.Destroy_Texture(tex); backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	auto Lum = [pixels](int x, int y) -> int {
		const uint8_t* p = &pixels[(y * kRTW + x) * 4];
		return (int(p[0]) + int(p[1]) + int(p[2])) / 3;
	};

	const int center   = Lum(32, 32);
	const int mid      = Lum(16, 32);
	const int cornerTL = Lum(2, 2);
	const int cornerBR = Lum(kRTW - 3, kRTH - 3);

	fprintf(stderr, "samples: center=%d mid=%d corners=%d,%d\n",
	        center, mid, cornerTL, cornerBR);

	bool ok = true;
	if (center < 200)                       { fprintf(stderr, "highlight too dim (%d < 200)\n", center); ok = false; }
	if (cornerTL > 15 || cornerBR > 15)     { fprintf(stderr, "corners bleed (%d,%d)\n", cornerTL, cornerBR); ok = false; }
	if (center - mid < 80)                  { fprintf(stderr, "highlight not tight (%d)\n", center - mid); ok = false; }

	std::free(pixels);
	backend.Destroy_Texture(tex);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok)
	{
		fprintf(stderr, "tst_bgfx_specular: FAILED\n");
		return 1;
	}
	fprintf(stderr, "tst_bgfx_specular: PASSED\n");
	return 0;
}
