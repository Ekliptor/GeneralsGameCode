/*
**	Phase 5h.10 smoke test: spot-light cone mask.
**
**	Setup:
**	  * 64×64 RT, white 2×2 texture, 17×17 tessellated quad at z=0.5
**	    (NDC-ish) facing the camera, identity transforms.
**	  * Slot 0: type=SPOT, position=(0,0,0), spotDirection=(0,0,1)
**	    (light at camera plane, cone axis pointing at the quad), range=2
**	    (atten≈0.75 at the quad center), outer-cone cos=0.75 (~41° half-
**	    angle), inner cos=0.82 (~35° half-angle). Numbers picked so the
**	    cone width spans ~32 pixels at the 64×64 RT — wide enough for the
**	    33×33 tessellation to resolve without swallowing the lit pixel
**	    count into interpolation loss.
**
**	Expected per-vertex at (x,y,0.5):
**	  L       = (-x,-y,-0.5)/d, d = sqrt(x²+y²+0.25)
**	  cosAng  = dot((0,0,1), -L) = 0.5/d
**	  center (0,0,0.5): d=0.5 → cosAng=1 → mask=1, atten=0.75 → 0.75 → ≈191.
**	  x=0.48   (pixel 16): d≈0.696 → cosAng=0.718 → below outer → mask=0.
**	  corners (±1,±1): d>1.5 → cosAng<0.33 → mask=0.
**
**	Assertions:
**	  * center pixel ≥ 150          (inside cone, N·L ≈ 1, pointAtten ≈ 0.9)
**	  * quarter-way-out (16,32)  ~0 (outside cone → mask kills it)
**	  * corners (2,2)/(61,61)    ~0 (outside cone)
**	  * center >> quarter-out    proves the cone is doing real work.
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
		v.abgr = 0xFFFFFFFFu;
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_spot_light", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
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
	backend.Set_Material(material);

	LightDesc l;
	l.type = LightDesc::LIGHT_SPOT;
	l.position[0] = 0.0f;
	l.position[1] = 0.0f;
	l.position[2] = 0.0f;
	l.color[0] = 1.0f; l.color[1] = 1.0f; l.color[2] = 1.0f;
	l.ambient   = 0.0f;
	l.intensity = 1.0f;
	l.attenuationRange = 2.0f;
	l.spotDirection[0] = 0.0f;
	l.spotDirection[1] = 0.0f;
	l.spotDirection[2] = 1.0f;
	l.spotOuterCos = 0.75f;               // ~41° half-angle outer
	l.spotInnerCos = 0.82f;               // ~35° half-angle inner
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

	const int center    = Lum(32, 32);
	const int quarter   = Lum(16, 32);   // one-quarter from left edge
	const int cornerTL  = Lum(2, 2);
	const int cornerBR  = Lum(kRTW - 3, kRTH - 3);

	fprintf(stderr, "samples: center=%d quarter=%d corners=%d,%d\n",
	        center, quarter, cornerTL, cornerBR);

	bool ok = true;
	if (center < 150)                          { fprintf(stderr, "center too dim (%d < 150)\n", center); ok = false; }
	if (quarter > 20)                          { fprintf(stderr, "quarter-out bleeds (%d > 20)\n", quarter); ok = false; }
	if (cornerTL > 5 || cornerBR > 5)          { fprintf(stderr, "corners bleed (%d,%d)\n", cornerTL, cornerBR); ok = false; }
	if (center - quarter < 120)                { fprintf(stderr, "cone not sharp enough (%d)\n", center - quarter); ok = false; }

	std::free(pixels);
	backend.Destroy_Texture(tex);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok)
	{
		fprintf(stderr, "tst_bgfx_spot_light: FAILED\n");
		return 1;
	}
	fprintf(stderr, "tst_bgfx_spot_light: PASSED\n");
	return 0;
}
