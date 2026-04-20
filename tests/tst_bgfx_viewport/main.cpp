/*
**	Phase 5h.6 smoke test: IRenderBackend::Set_Viewport.
**
**	Flow:
**	  Frame 1 — clear the 64×64 RT to red. RT is uniformly red afterwards.
**	  Frame 2 — restrict the view's viewport to (16,16,32,32) via
**	            Set_Viewport, then draw a full-NDC blue quad with no clear.
**	            The scissor is expected to keep the draw inside the sub-rect;
**	            pixels outside should retain the red fill from frame 1.
**
**	Capture and assert:
**	  * sub-rect corners (16,16) / (47,47) / (32,32) read as blue
**	  * outside-rect samples (0,0) / (63,63) / (8,8) / (55,55) read as red
**
**	Exits `tst_bgfx_viewport: PASSED` on success.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "WW3D2/BackendDescriptors.h"
#include "vector4.h"

namespace
{

constexpr uint16_t kRTW = 64;
constexpr uint16_t kRTH = 64;

struct PosColorVertex
{
	float x, y, z;
	uint32_t abgr;
};

bool NearEq(uint8_t a, uint8_t b)
{
	const int d = int(a) - int(b);
	return d >= -3 && d <= 3;
}

// BGRA byte order. Red (1,0,0,1) → (B=0,G=0,R=255,A=255).
bool IsRed(const uint8_t* p)
{
	return NearEq(p[0], 0) && NearEq(p[1], 0) && NearEq(p[2], 255) && NearEq(p[3], 255);
}

// Blue (0,0,1,1) → (B=255,G=0,R=0,A=255).
bool IsBlue(const uint8_t* p)
{
	return NearEq(p[0], 255) && NearEq(p[1], 0) && NearEq(p[2], 0) && NearEq(p[3], 255);
}

void PrintPixel(const char* label, int x, int y, const uint8_t* p)
{
	fprintf(stderr, "  %s (%d,%d): BGRA %d,%d,%d,%d\n",
	        label, x, y, p[0], p[1], p[2], p[3]);
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_viewport", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, /*hasDepth=*/true);
	if (rt == 0)
	{
		fprintf(stderr, "Create_Render_Target returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Identity transforms so NDC coordinates pass straight through.
	float identity[16] = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	backend.Set_World_Transform(identity);
	backend.Set_View_Transform(identity);
	backend.Set_Projection_Transform(identity);

	// vcolor program — position + per-vertex color, no lighting.
	ShaderStateDesc shader;
	shader.depthCmp        = ShaderStateDesc::DEPTH_LESS;
	shader.cullEnable      = false;
	shader.texturingEnable = false;
	backend.Set_Shader(shader);
	MaterialDesc material;
	material.useLighting = false;
	backend.Set_Material(material);

	// Full-NDC quad, all vertices blue. ABGR in little-endian bytes:
	// 0xFFFF0000 == (A=FF, B=FF, G=00, R=00) — pure blue.
	const uint32_t kBlueABGR = 0xFFFF0000u;
	const PosColorVertex quadVerts[4] = {
		{ -1.0f, -1.0f, 0.5f, kBlueABGR },
		{  1.0f, -1.0f, 0.5f, kBlueABGR },
		{  1.0f,  1.0f, 0.5f, kBlueABGR },
		{ -1.0f,  1.0f, 0.5f, kBlueABGR },
	};
	const uint16_t quadIdx[6] = { 0, 1, 2, 0, 2, 3 };

	VertexLayoutDesc layout;
	layout.stride = sizeof(PosColorVertex);
	layout.attrCount = 2;
	layout.attrs[0] = { VertexAttributeDesc::SEM_POSITION, VertexAttributeDesc::TYPE_FLOAT32,          3, 0, offsetof(PosColorVertex, x)    };
	layout.attrs[1] = { VertexAttributeDesc::SEM_COLOR0,   VertexAttributeDesc::TYPE_UINT8_NORMALIZED, 4, 0, offsetof(PosColorVertex, abgr) };

	// ---- Frame 1: fill the RT with red --------------------------------------
	backend.Set_Render_Target(rt);
	backend.Clear(true, true, Vector4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f);
	backend.Begin_Scene();
	backend.End_Scene(true);

	// ---- Frame 2: viewport-restricted blue quad -----------------------------
	backend.Set_Render_Target(rt);
	backend.Set_Viewport(16, 16, 32, 32);
	backend.Begin_Scene();
	backend.Draw_Triangles_Dynamic(quadVerts, 4, layout, quadIdx, 6);
	backend.End_Scene(true);

	// ---- Readback -----------------------------------------------------------
	const uint32_t bytes = uint32_t(kRTW) * uint32_t(kRTH) * 4u;
	uint8_t* pixels = static_cast<uint8_t*>(std::malloc(bytes));
	if (!pixels)
	{
		fprintf(stderr, "malloc failed\n");
		backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	if (!backend.Capture_Render_Target(rt, pixels, bytes))
	{
		fprintf(stderr, "Capture_Render_Target returned false\n");
		std::free(pixels);
		backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	auto PixelAt = [pixels](int x, int y) -> const uint8_t* {
		return &pixels[(y * kRTW + x) * 4];
	};

	struct Sample { int x, y; bool expectBlue; const char* label; };
	const Sample samples[] = {
		{  0,  0, false, "outside tl   " },
		{ 63,  0, false, "outside tr   " },
		{  0, 63, false, "outside bl   " },
		{ 63, 63, false, "outside br   " },
		{  8,  8, false, "outside near " },
		{ 55, 55, false, "outside far  " },
		{ 16, 16, true,  "inside tl    " },
		{ 47, 16, true,  "inside tr    " },
		{ 16, 47, true,  "inside bl    " },
		{ 47, 47, true,  "inside br    " },
		{ 32, 32, true,  "inside center" },
	};

	bool allOk = true;
	for (const Sample& s : samples)
	{
		const uint8_t* p = PixelAt(s.x, s.y);
		const bool ok = s.expectBlue ? IsBlue(p) : IsRed(p);
		if (!ok)
		{
			fprintf(stderr, "viewport mismatch:\n");
			PrintPixel(s.label, s.x, s.y, p);
			fprintf(stderr, "    expected: %s\n", s.expectBlue ? "blue (inside)" : "red (outside)");
			allOk = false;
		}
	}

	std::free(pixels);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!allOk)
	{
		fprintf(stderr, "tst_bgfx_viewport: FAILED\n");
		return 1;
	}
	fprintf(stderr, "tst_bgfx_viewport: PASSED\n");
	return 0;
}
