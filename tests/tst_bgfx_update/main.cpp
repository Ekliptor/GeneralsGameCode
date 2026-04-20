/*
**	Phase 5h.30 smoke test: Update_Texture_RGBA8 uploads pixel data to an
**	already-allocated bgfx texture handle.
**
**	Flow:
**	  1. Allocate an empty 4×4 texture via Create_Texture_RGBA8(nullptr, …).
**	  2. Upload solid red pixels via Update_Texture_RGBA8.
**	  3. Render a full-NDC quad sampling the texture to a 32×32 RT.
**	  4. Capture the RT and assert the sampled pixels are red.
**
**	This mirrors the Phase 5h.28 procedural texture path (null-allocation)
**	plus the new Update_Texture_RGBA8 upload. Without 5h.30's update the
**	texture contents are undefined; with it, the sampled output matches
**	the uploaded data.
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

constexpr uint16_t kRTW = 32;
constexpr uint16_t kRTH = 32;

struct PosUVVertex
{
	float x, y, z;
	float u, v;
};

bool NearEq(uint8_t a, uint8_t b, int tol = 3)
{
	const int d = int(a) - int(b);
	return d >= -tol && d <= tol;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init failed\n"); return 1; }
	constexpr int kW = 320, kH = 240;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_update", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Step 1: allocate empty texture. Phase 5h.28 extension — null pixels
	// yield an uninitialized 4×4 texture.
	const uintptr_t tex = backend.Create_Texture_RGBA8(
		/*pixels=*/nullptr, /*w=*/4, /*h=*/4, /*mipmap=*/false);
	if (tex == 0)
	{
		fprintf(stderr, "Create_Texture_RGBA8(null) returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Step 2: upload solid red. RGBA8 layout: (R, G, B, A).
	uint8_t redPixels[4 * 4 * 4];
	for (int i = 0; i < 16; ++i)
	{
		redPixels[i*4 + 0] = 0xFF;   // R
		redPixels[i*4 + 1] = 0x00;   // G
		redPixels[i*4 + 2] = 0x00;   // B
		redPixels[i*4 + 3] = 0xFF;   // A
	}
	backend.Update_Texture_RGBA8(tex, redPixels, 4, 4);

	// Step 3: render a textured quad to an RT.
	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, /*hasDepth=*/true);
	if (rt == 0)
	{
		backend.Destroy_Texture(tex); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	float identity[16] = {
		1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,0.f,0.f,1.f };
	backend.Set_World_Transform(identity);
	backend.Set_View_Transform(identity);
	backend.Set_Projection_Transform(identity);

	ShaderStateDesc shader;
	shader.cullEnable = false;
	shader.texturingEnable = true;
	backend.Set_Shader(shader);

	MaterialDesc material;
	material.useLighting = false;
	backend.Set_Material(material);

	backend.Set_Texture(0, tex);

	const PosUVVertex quad[4] = {
		{ -1.0f, -1.0f, 0.5f, 0.0f, 1.0f },
		{  1.0f, -1.0f, 0.5f, 1.0f, 1.0f },
		{  1.0f,  1.0f, 0.5f, 1.0f, 0.0f },
		{ -1.0f,  1.0f, 0.5f, 0.0f, 0.0f },
	};
	const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

	VertexLayoutDesc layout;
	layout.stride = sizeof(PosUVVertex);
	layout.attrCount = 2;
	layout.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32, 3, 0, offsetof(PosUVVertex, x) };
	layout.attrs[1] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32, 2, 0, offsetof(PosUVVertex, u) };

	backend.Set_Render_Target(rt);
	backend.Clear(true, true, Vector4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
	backend.Begin_Scene();
	backend.Draw_Triangles_Dynamic(quad, 4, layout, idx, 6);
	backend.End_Scene(true);

	// Step 4: capture + assert. The uber-shader's tex program multiplies
	// sampled texel by vertex color (default white); without alpha test or
	// fog, the output is the raw sampled color. BGRA8 output layout.
	const uint32_t bytes = uint32_t(kRTW) * uint32_t(kRTH) * 4u;
	uint8_t* pixels = static_cast<uint8_t*>(std::malloc(bytes));
	if (!pixels || !backend.Capture_Render_Target(rt, pixels, bytes))
	{
		fprintf(stderr, "Capture failed\n");
		std::free(pixels);
		backend.Destroy_Texture(tex); backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Sample the RT's center — should be red (BGRA: B=0, G=0, R=255, A=255).
	const uint8_t* center = &pixels[(16 * kRTW + 16) * 4];
	const bool ok = NearEq(center[0], 0)
	             && NearEq(center[1], 0)
	             && NearEq(center[2], 255)
	             && NearEq(center[3], 255);

	if (!ok)
	{
		fprintf(stderr, "center BGRA = %d,%d,%d,%d (expected near 0,0,255,255)\n",
		        center[0], center[1], center[2], center[3]);
	}

	std::free(pixels);
	backend.Destroy_Texture(tex);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok)
	{
		fprintf(stderr, "tst_bgfx_update: FAILED\n");
		return 1;
	}
	fprintf(stderr, "tst_bgfx_update: PASSED\n");
	return 0;
}
