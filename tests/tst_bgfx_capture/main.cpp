/*
**	Phase 5q smoke test: CPU readback via Capture_Render_Target.
**
**	Creates a 64×64 RT, clears it to a known color, captures the color
**	attachment back to a CPU buffer, and asserts that a handful of
**	pixels match the clear color within ±2 LSB tolerance (float→uint8
**	quantization slack). This is the first test in the suite that makes
**	pixel-value assertions rather than just "runs without crashing" —
**	the infrastructure the 5h adapter will want to regress-test against.
**
**	Exits with `tst_bgfx_capture: PASSED` on success. Any mismatched
**	sample prints the offending coordinates + expected / actual bytes.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "vector4.h"

namespace
{

constexpr uint16_t kRTW = 64;
constexpr uint16_t kRTH = 64;

bool NearEq(uint8_t a, uint8_t b)
{
	const int d = int(a) - int(b);
	return d >= -2 && d <= 2;
}

// BGRA8 layout: byte order [B, G, R, A]. Clear color (R=0.2, G=0.8, B=0.4,
// A=1.0) quantizes to (R=51, G=204, B=102, A=255). Memory layout [102, 204, 51, 255].
bool CheckPixel(const uint8_t* px, const char* label, int x, int y)
{
	const uint8_t eB = 102, eG = 204, eR = 51, eA = 255;
	const bool ok = NearEq(px[0], eB) && NearEq(px[1], eG)
	             && NearEq(px[2], eR) && NearEq(px[3], eA);
	if (!ok)
	{
		fprintf(stderr, "  %s (%d,%d): got BGRA %d,%d,%d,%d expected near %d,%d,%d,%d\n",
		        label, x, y, px[0], px[1], px[2], px[3], eB, eG, eR, eA);
	}
	return ok;
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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_capture", kW, kH, SDL_WINDOW_HIDDEN);
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

	const Vector4 clearColor(0.2f, 0.8f, 0.4f, 1.0f);

	// One-shot: bind RT, clear, submit the view, capture.
	backend.Set_Render_Target(rt);
	backend.Clear(true, true, clearColor, 1.0f);
	backend.Begin_Scene();
	backend.End_Scene(true);

	const uint32_t bytes = uint32_t(kRTW) * uint32_t(kRTH) * 4u;
	uint8_t* pixels = static_cast<uint8_t*>(std::malloc(bytes));
	if (!pixels)
	{
		fprintf(stderr, "malloc failed\n");
		backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	const bool captured = backend.Capture_Render_Target(rt, pixels, bytes);
	if (!captured)
	{
		fprintf(stderr, "Capture_Render_Target returned false\n");
		std::free(pixels);
		backend.Destroy_Render_Target(rt); backend.Shutdown();
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Spot-check the four corners plus the center.
	struct SP { int x, y; const char* label; };
	const SP samples[] = {
		{             0,             0, "top-left     " },
		{     kRTW - 1,             0, "top-right    " },
		{             0,     kRTH - 1, "bottom-left  " },
		{     kRTW - 1,     kRTH - 1, "bottom-right " },
		{     kRTW / 2,     kRTH / 2, "center       " },
	};

	bool allOk = true;
	for (const SP& s : samples)
	{
		const uint8_t* p = &pixels[(s.y * kRTW + s.x) * 4];
		if (!CheckPixel(p, s.label, s.x, s.y))
			allOk = false;
	}

	std::free(pixels);
	backend.Destroy_Render_Target(rt);
	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!allOk)
	{
		fprintf(stderr, "tst_bgfx_capture: FAILED\n");
		return 1;
	}
	fprintf(stderr, "tst_bgfx_capture: PASSED\n");
	return 0;
}
