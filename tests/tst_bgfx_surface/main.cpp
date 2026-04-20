/*
**	Phase 5h.31 smoke test: SurfaceClass bgfx-mode CPU-backed path.
**
**	Flow:
**	  1. Create a bgfx 4x4 RGBA8 texture via Create_Texture_RGBA8(null).
**	  2. Allocate SurfaceClass(4, 4, A8R8G8B8), bind it to that handle via
**	     Set_Associated_Texture.
**	  3. Lock(), fill CPU buffer with red (BGRA in memory), Unlock() — which
**	     should auto-upload through IRenderBackend::Update_Texture_RGBA8.
**	  4. Render a full-NDC quad sampling the texture into a 32x32 RT.
**	  5. Capture + assert the center pixel is red.
**
**	Proves the SurfaceClass → Update_Texture_RGBA8 routing introduced in 5h.31
**	works end-to-end: a caller that only knows about SurfaceClass can drive
**	live pixel updates into a bgfx texture without touching the backend
**	directly.
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
#include "WW3D2/surfaceclass.h"
#include "vector4.h"

namespace
{

constexpr uint16_t kRTW = 32;
constexpr uint16_t kRTH = 32;

struct PosUVVertex { float x, y, z; float u, v; };

bool NearEq(uint8_t a, uint8_t b, int tol = 3)
{
	const int d = int(a) - int(b);
	return d >= -tol && d <= tol;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
	constexpr int kW = 320, kH = 240;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_surface", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true))
	{
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Allocate bgfx texture (empty).
	const uintptr_t tex = backend.Create_Texture_RGBA8(nullptr, 4, 4, false);
	if (tex == 0) {
		fprintf(stderr, "Create_Texture_RGBA8(null) returned 0\n");
		backend.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Allocate a 4x4 A8R8G8B8 surface and bind it to the texture.
	SurfaceClass* surface = new SurfaceClass(4, 4, WW3D_FORMAT_A8R8G8B8);
	surface->Set_Associated_Texture(tex);

	// Lock, fill red (BGRA: B=0, G=0, R=255, A=255), unlock.
	int pitch = 0;
	auto* locked = static_cast<unsigned char*>(surface->Lock(&pitch));
	if (!locked || pitch != 16) {
		fprintf(stderr, "Lock returned bad pointer/pitch (ptr=%p pitch=%d)\n",
		        (void*)locked, pitch);
		delete surface; backend.Destroy_Texture(tex); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	for (int y = 0; y < 4; ++y)
	for (int x = 0; x < 4; ++x) {
		unsigned char* p = locked + y * pitch + x * 4;
		p[0] = 0x00; // B
		p[1] = 0x00; // G
		p[2] = 0xFF; // R
		p[3] = 0xFF; // A
	}
	surface->Unlock(); // triggers Update_Texture_RGBA8

	// Render: textured quad into RT.
	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, true);
	if (rt == 0) {
		delete surface; backend.Destroy_Texture(tex); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	float identity[16] = {
		1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
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
		{ -1.f, -1.f, 0.5f, 0.f, 1.f },
		{  1.f, -1.f, 0.5f, 1.f, 1.f },
		{  1.f,  1.f, 0.5f, 1.f, 0.f },
		{ -1.f,  1.f, 0.5f, 0.f, 0.f },
	};
	const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

	VertexLayoutDesc layout;
	layout.stride = sizeof(PosUVVertex);
	layout.attrCount = 2;
	layout.attrs[0] = { VertexAttributeDesc::SEM_POSITION,  VertexAttributeDesc::TYPE_FLOAT32, 3, 0, offsetof(PosUVVertex, x) };
	layout.attrs[1] = { VertexAttributeDesc::SEM_TEXCOORD0, VertexAttributeDesc::TYPE_FLOAT32, 2, 0, offsetof(PosUVVertex, u) };

	backend.Set_Render_Target(rt);
	backend.Clear(true, true, Vector4(0.f, 0.f, 0.f, 1.f), 1.f);
	backend.Begin_Scene();
	backend.Draw_Triangles_Dynamic(quad, 4, layout, idx, 6);
	backend.End_Scene(true);

	const uint32_t bytes = uint32_t(kRTW) * uint32_t(kRTH) * 4u;
	uint8_t* pixels = static_cast<uint8_t*>(std::malloc(bytes));
	if (!pixels || !backend.Capture_Render_Target(rt, pixels, bytes)) {
		fprintf(stderr, "Capture failed\n");
		std::free(pixels);
		delete surface;
		backend.Destroy_Texture(tex); backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	const uint8_t* center = &pixels[(16 * kRTW + 16) * 4];
	const bool ok = NearEq(center[0], 0)
	             && NearEq(center[1], 0)
	             && NearEq(center[2], 255)
	             && NearEq(center[3], 255);

	if (!ok) {
		fprintf(stderr, "center BGRA = %d,%d,%d,%d (expected ~0,0,255,255)\n",
		        center[0], center[1], center[2], center[3]);
	}

	std::free(pixels);
	delete surface;
	backend.Destroy_Texture(tex);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "tst_bgfx_surface: %s\n", ok ? "PASSED" : "FAILED");
	return ok ? 0 : 1;
}
