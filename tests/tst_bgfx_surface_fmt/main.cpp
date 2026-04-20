/*
**	Phase 5h.35 smoke test: SurfaceClass format-aware Update_Texture routing.
**
**	Exercises non-RGBA8 surface formats that 5h.31 silently skipped:
**	  - WW3D_FORMAT_A4R4G4B4 (font glyphs / UI)
**	  - WW3D_FORMAT_R5G6B5   (16-bit opaque textures)
**	  - WW3D_FORMAT_L8       (luminance)
**
**	For each format: create the surface, fill with a solid color, upload to
**	a bgfx texture via Unlock(), render a textured NDC quad to a 32x32 RT,
**	capture + assert the center pixel matches the expected RGB value within
**	±5 LSB (extra slack for 4-bit-nibble and 5-bit-channel quantization).
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
constexpr int kTol = 5;

struct PosUVVertex { float x, y, z; float u, v; };

bool NearEq(uint8_t a, uint8_t b) { const int d = int(a) - int(b); return d >= -kTol && d <= kTol; }

// Render a 4x4 surface-backed texture onto a full-NDC quad into a 32x32 RT,
// return the captured center pixel BGRA. Caller owns the surface and tears
// down RT/tex after checking.
bool RenderAndCapture(BgfxBackend& backend, uintptr_t tex, uint8_t outBGRA[4])
{
	const uintptr_t rt = backend.Create_Render_Target(kRTW, kRTH, true);
	if (rt == 0) return false;

	const float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
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
	bool ok = pixels && backend.Capture_Render_Target(rt, pixels, bytes);
	if (ok) {
		const uint8_t* c = &pixels[(16 * kRTW + 16) * 4];
		outBGRA[0] = c[0]; outBGRA[1] = c[1]; outBGRA[2] = c[2]; outBGRA[3] = c[3];
	}
	std::free(pixels);
	backend.Destroy_Render_Target(rt);
	return ok;
}

bool CheckFormat(BgfxBackend& backend,
                 WW3DFormat fmt, const char* name,
                 unsigned pixelBytes,
                 const uint8_t* sourcePattern,     // one pixel of the source-format bytes
                 uint8_t expR, uint8_t expG, uint8_t expB)
{
	const uintptr_t tex = backend.Create_Texture_RGBA8(nullptr, 4, 4, false);
	if (tex == 0) { fprintf(stderr, "%s: Create_Texture_RGBA8 returned 0\n", name); return false; }

	auto* surface = new SurfaceClass(4, 4, fmt);
	surface->Set_Associated_Texture(tex);

	int pitch = 0;
	auto* locked = static_cast<unsigned char*>(surface->Lock(&pitch));
	if (!locked) {
		fprintf(stderr, "%s: Lock returned null\n", name);
		delete surface; backend.Destroy_Texture(tex); return false;
	}
	for (int y = 0; y < 4; ++y) {
		unsigned char* row = locked + y * pitch;
		for (int x = 0; x < 4; ++x) {
			std::memcpy(row + x * pixelBytes, sourcePattern, pixelBytes);
		}
	}
	surface->Unlock();

	uint8_t center[4] = { 0, 0, 0, 0 };
	const bool captured = RenderAndCapture(backend, tex, center);

	delete surface;
	backend.Destroy_Texture(tex);

	if (!captured) { fprintf(stderr, "%s: capture failed\n", name); return false; }

	// Capture is BGRA; the expected RGB is translated from the source format.
	const bool ok = NearEq(center[0], expB)
	             && NearEq(center[1], expG)
	             && NearEq(center[2], expR)
	             && NearEq(center[3], 255);
	if (!ok) {
		fprintf(stderr, "%s: center BGRA = %d,%d,%d,%d (expected ~%d,%d,%d,255)\n",
		        name, center[0], center[1], center[2], center[3], expB, expG, expR);
	} else {
		fprintf(stderr, "  %s ok (center BGRA = %d,%d,%d,%d)\n",
		        name, center[0], center[1], center[2], center[3]);
	}
	return ok;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
	constexpr int kW = 320, kH = 240;
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_surface_fmt", kW, kH, SDL_WINDOW_HIDDEN);
	if (!window) { SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	BgfxBackend backend;
	if (!backend.Init(nwh, kW, kH, true)) {
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	bool ok = true;

	// A4R4G4B4 red: ARGB = 0xF, 0xF, 0, 0 → packed 0xFF00 LE → bytes {0x00, 0xFF}.
	// Expansion: nibble 0xF → 0xFF so expected RGB = (255, 0, 0).
	{
		const uint8_t px[2] = { 0x00, 0xFF };
		ok &= CheckFormat(backend, WW3D_FORMAT_A4R4G4B4, "A4R4G4B4(red)", 2, px, 255, 0, 0);
	}

	// R5G6B5 green: R=0, G=0x3F, B=0 → packed 0x07E0 LE → bytes {0xE0, 0x07}.
	// 6-bit 0x3F → (0x3F<<2)|(0x3F>>4) = 0xFC|3 = 0xFF.
	{
		const uint8_t px[2] = { 0xE0, 0x07 };
		ok &= CheckFormat(backend, WW3D_FORMAT_R5G6B5, "R5G6B5(green)", 2, px, 0, 255, 0);
	}

	// L8 mid-gray 0x80 → RGB (0x80, 0x80, 0x80).
	{
		const uint8_t px[1] = { 0x80 };
		ok &= CheckFormat(backend, WW3D_FORMAT_L8, "L8(0x80)", 1, px, 0x80, 0x80, 0x80);
	}

	// A1R5G5B5 blue: A=1, R=0, G=0, B=0x1F → packed 0x801F LE → {0x1F, 0x80}.
	// 5-bit 0x1F → (0x1F<<3)|(0x1F>>2) = 0xF8|7 = 0xFF.
	{
		const uint8_t px[2] = { 0x1F, 0x80 };
		ok &= CheckFormat(backend, WW3D_FORMAT_A1R5G5B5, "A1R5G5B5(blue)", 2, px, 0, 0, 255);
	}

	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "tst_bgfx_surface_fmt: %s\n", ok ? "PASSED" : "FAILED");
	return ok ? 0 : 1;
}
