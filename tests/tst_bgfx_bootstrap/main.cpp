/*
**	Phase 5h.2 smoke test: BgfxBootstrap singleton lifetime.
**
**	Six invariants:
**	  1. Is_Initialized() is false before any call.
**	  2. Ensure_Init(hwnd, w, h, windowed) succeeds; Is_Initialized() true;
**	     RenderBackendRuntime::Get_Active() non-null.
**	  3. A second Ensure_Init with identical args is a no-op; the runtime
**	     backend pointer stays the same (same instance).
**	  4. Ensure_Init with a changed resolution keeps the same backend
**	     instance (in-place Reset path) — Get_Active() returns same pointer.
**	  5. Shutdown() clears Is_Initialized() and drops the runtime pointer.
**	  6. A fresh Ensure_Init after Shutdown brings a backend back up
**	     (pointer identity is NOT asserted — glibc / libc++ often reuse
**	     the address freshly delete'd; what matters is that Get_Active
**	     returns non-null and is independent of the old instance's
**	     lifetime).
**
**	Tests the ownership semantics that Phase 5h.3+ will rely on — if
**	Ensure_Init returns a stale pointer from a previously-shutdown
**	instance, texture / mesh upload would crash.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

#include "BGFXDevice/Common/BgfxBootstrap.h"
#include "SDLDevice/Common/SDLGlobals.h"
#include "WW3D2/RenderBackendRuntime.h"
#include "WW3D2/IRenderBackend.h"

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
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_bootstrap", 320, 240, SDL_WINDOW_HIDDEN);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	bool ok = true;

	// Invariant 1 — virgin state.
	ok &= !FailIf(BgfxBootstrap::Is_Initialized(),
	              "invariant 1: Is_Initialized() true before Ensure_Init");
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != nullptr,
	              "invariant 1: Get_Active() not null before Ensure_Init");

	// Invariant 2 — first Ensure_Init brings the backend up.
	ok &= !FailIf(!BgfxBootstrap::Ensure_Init(nwh, 320, 240, true),
	              "invariant 2: first Ensure_Init returned false");
	ok &= !FailIf(!BgfxBootstrap::Is_Initialized(),
	              "invariant 2: Is_Initialized() still false after Ensure_Init");
	IRenderBackend* first = RenderBackendRuntime::Get_Active();
	ok &= !FailIf(first == nullptr,
	              "invariant 2: Get_Active() null after Ensure_Init");

	// Invariant 3 — idempotent on identical args.
	ok &= !FailIf(!BgfxBootstrap::Ensure_Init(nwh, 320, 240, true),
	              "invariant 3: idempotent Ensure_Init returned false");
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != first,
	              "invariant 3: backend pointer changed on no-op Ensure_Init");

	// Invariant 4 — Reset-in-place on changed dims.
	ok &= !FailIf(!BgfxBootstrap::Ensure_Init(nwh, 640, 480, true),
	              "invariant 4: Reset-triggering Ensure_Init returned false");
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != first,
	              "invariant 4: backend pointer changed on Reset path");

	// Invariant 5 — Shutdown drains cleanly.
	BgfxBootstrap::Shutdown();
	ok &= !FailIf(BgfxBootstrap::Is_Initialized(),
	              "invariant 5: Is_Initialized() true after Shutdown");
	ok &= !FailIf(RenderBackendRuntime::Get_Active() != nullptr,
	              "invariant 5: Get_Active() not null after Shutdown");

	// Invariant 6 — can re-init after shutdown, yields a live backend.
	// (We deliberately don't assert the pointer differs from `first` —
	// libc/libc++ often reuse freshly-freed addresses.)
	ok &= !FailIf(!BgfxBootstrap::Ensure_Init(nwh, 320, 240, true),
	              "invariant 6: re-Ensure_Init returned false");
	ok &= !FailIf(!BgfxBootstrap::Is_Initialized(),
	              "invariant 6: Is_Initialized() false after re-Ensure_Init");
	ok &= !FailIf(RenderBackendRuntime::Get_Active() == nullptr,
	              "invariant 6: Get_Active() null after re-Ensure_Init");
	(void)first;  // silence unused-warning; kept for future pointer-identity checks

	BgfxBootstrap::Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok) { fprintf(stderr, "tst_bgfx_bootstrap: FAILED\n"); return 1; }
	fprintf(stderr, "tst_bgfx_bootstrap: PASSED\n");
	return 0;
}
