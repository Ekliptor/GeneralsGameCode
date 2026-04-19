/*
**	Phase 5h.1 smoke test: RenderBackendRuntime seam.
**
**	Asserts the four invariants of the runtime singleton:
**	  1. Before any backend is constructed, Get_Active() is nullptr.
**	  2. After BgfxBackend::Init(), Get_Active() returns &backend.
**	  3. After BgfxBackend::Shutdown(), Get_Active() is nullptr again.
**	  4. A second Init / Shutdown cycle on the same instance round-trips
**	     correctly — the seam doesn't latch after one use.
**
**	No rendering; this is a pure lifecycle test. Exits with
**	`tst_bgfx_runtime: PASSED` on success, or prints the offending
**	invariant on failure.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "WW3D2/RenderBackendRuntime.h"

namespace
{

bool FailIf(bool cond, const char* msg)
{
	if (cond) { fprintf(stderr, "  %s\n", msg); return true; }
	return false;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_runtime", 320, 240, SDL_WINDOW_HIDDEN);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	bool ok = true;

	// Invariant 1 — runtime starts empty.
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != nullptr,
	              "invariant 1: Get_Active() not null before Init");

	BgfxBackend backend;
	if (!backend.Init(nwh, 320, 240, true))
	{
		fprintf(stderr, "BgfxBackend::Init failed\n");
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	// Invariant 2 — Init registers us.
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != static_cast<IRenderBackend*>(&backend),
	              "invariant 2: Get_Active() != &backend after Init");

	backend.Shutdown();

	// Invariant 3 — Shutdown deregisters.
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != nullptr,
	              "invariant 3: Get_Active() not null after Shutdown");

	// Invariant 4 — second Init/Shutdown cycle round-trips.
	if (!backend.Init(nwh, 320, 240, true))
	{
		fprintf(stderr, "BgfxBackend::Init (second cycle) failed\n");
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != static_cast<IRenderBackend*>(&backend),
	              "invariant 4a: Get_Active() != &backend after second Init");
	backend.Shutdown();
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != nullptr,
	              "invariant 4b: Get_Active() not null after second Shutdown");

	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok) { fprintf(stderr, "tst_bgfx_runtime: FAILED\n"); return 1; }
	fprintf(stderr, "tst_bgfx_runtime: PASSED\n");
	return 0;
}
