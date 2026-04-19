/*
**	Phase 5f smoke test: exercises all four bgfx uber-shader permutations —
**	solid, vcolor, tex (the Phase 5e triangle), and tex_lit — by drawing one
**	primitive per permutation in a separate screen quadrant. Runs for ~2s
**	then shuts down cleanly. A successful run proves each embedded shader
**	compiled, each program linked, and the runtime renderer (Metal on macOS,
**	D3D11 on Windows, GL fallback) accepts every vertex layout we intend to
**	use in Phase 5g's real draw path.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "vector4.h"

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	constexpr int kWidth  = 800;
	constexpr int kHeight = 600;

	SDL_Window* window = SDL_CreateWindow("tst_bgfx_uber", kWidth, kHeight, SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh)
	{
		fprintf(stderr, "getNativeWindowHandle returned null\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	BgfxBackend backend;
	if (!backend.Init(nwh, kWidth, kHeight, true))
	{
		fprintf(stderr, "BgfxBackend::Init failed\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	const Vector4 clearColor(0.12f, 0.12f, 0.14f, 1.0f); // near-black so all 4 quads pop

	const uint64_t startTicks = SDL_GetTicks();
	bool running = true;
	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
				running = false;
		}

		if (SDL_GetTicks() - startTicks > 2000)
			running = false;

		backend.Clear(true, true, clearColor, 1.0f);
		backend.Begin_Scene();
		backend.DrawSmokeSolidQuad();    // top-left    — crimson
		backend.DrawSmokeTriangle();     // top-right   — magenta/yellow checkerboard
		backend.DrawSmokeVColorQuad();   // bottom-left — rainbow
		backend.DrawSmokeLitQuad();      // bottom-right — lit white/gray checkerboard
		backend.End_Scene(true);
	}

	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "tst_bgfx_uber: PASSED\n");
	return 0;
}
