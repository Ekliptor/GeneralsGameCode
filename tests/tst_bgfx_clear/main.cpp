/*
**	Phase 5a smoke test: create an SDL window, init bgfx, clear to a solid
**	color, swap for ~2 seconds, then shut down cleanly. Verifies that the
**	bgfx backend can initialize Metal (macOS) or D3D11 (Windows) through the
**	SDL3 native-window-handle path.
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

	constexpr int kWidth = 800;
	constexpr int kHeight = 600;

	SDL_Window* window = SDL_CreateWindow("tst_bgfx_clear", kWidth, kHeight, SDL_WINDOW_RESIZABLE);
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

	const Vector4 clearColor(0.19f, 0.38f, 0.50f, 1.0f); // #306080

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
		backend.End_Scene(true);
	}

	backend.Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	fprintf(stderr, "tst_bgfx_clear: PASSED\n");
	return 0;
}
