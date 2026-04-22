/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include <Utility/CppMacros.h>
#include "BGFXDevice/Common/BgfxBootstrap.h"
#include "BGFXDevice/Common/BgfxBackend.h"
#include "BGFXDevice/Common/BgfxTextureCache.h"
#include "SDLDevice/Common/SDLGlobals.h"

#include <cstdio>  // fprintf in Ensure_Init's hwnd-changed warning path.

namespace BgfxBootstrap
{

namespace
{
	// Process-wide singleton. Pointer sentinel so we can distinguish "never
	// constructed" from "constructed but mid-shutdown". The Init/Shutdown
	// path is single-threaded (same-thread as DX8Wrapper lifecycle) so no
	// atomic needed.
	BgfxBackend* s_instance = nullptr;
	int          s_width    = 0;
	int          s_height   = 0;
	bool         s_windowed = false;
	void*        s_hwnd     = nullptr;
}

bool Ensure_Init(void* hwnd, int width, int height, bool windowed)
{
	// Callers (DX8Wrapper) give us the LOGICAL game resolution (e.g. 800x600).
	// On Retina / high-DPI displays, or whenever SDL_WINDOW_FULLSCREEN is
	// used on macOS, the back-buffer pixel size can be much larger than
	// that. bgfx uses the values we pass to bgfx::init/reset to size the
	// Metal drawable and the default view rect — so if we hand it logical
	// dims the drawable only covers a sub-rect of the native view, and the
	// uncovered margin shows through as undefined pixels (the pink border
	// symptom we chased in phase 5i). Query SDL for the real pixel size and
	// hand that to bgfx; keep s_width/s_height as the pixel dims too, so
	// the default viewport (Set_Viewport(0,0,0,0)) and any explicit
	// viewport callers that ask for "full back-buffer" cover every pixel.
	int bgfxW = width;
	int bgfxH = height;
	int pixW = 0, pixH = 0;
	if (SDLDevice::getWindowPixelSize(pixW, pixH))
	{
		bgfxW = pixW;
		bgfxH = pixH;
	}
	if (s_instance != nullptr)
	{
		// Already up. If the caller's geometry differs, reset in place so
		// the existing handle stays stable — DX8Wrapper can reconfigure
		// resolution at runtime without recreating the backend.
		if (hwnd != s_hwnd)
		{
			fprintf(stderr, "BgfxBootstrap::Ensure_Init: hwnd changed mid-session — "
			                "recreating backend\n");
			Shutdown();
		}
		else if (bgfxW != s_width || bgfxH != s_height || windowed != s_windowed)
		{
			s_instance->Reset(bgfxW, bgfxH, windowed);
			s_instance->Set_Logical_Resolution(width, height);
			s_width = bgfxW; s_height = bgfxH; s_windowed = windowed;
			return true;
		}
		else
		{
			// Pixel size may change without the logical resolution
			// changing (monitor swap), so refresh the scale every call.
			s_instance->Set_Logical_Resolution(width, height);
			return true;
		}
	}

	// Fresh construction. BgfxBackend::Init calls
	// RenderBackendRuntime::Set_Active(this) on success, so the runtime
	// seam goes live as a side effect.
	auto* backend = new BgfxBackend();
	if (!backend->Init(hwnd, bgfxW, bgfxH, windowed))
	{
		delete backend;
		return false;
	}
	backend->Set_Logical_Resolution(width, height);
	s_instance = backend;
	s_hwnd     = hwnd;
	s_width    = bgfxW;
	s_height   = bgfxH;
	s_windowed = windowed;
	return true;
}

void Shutdown()
{
	if (s_instance == nullptr)
		return;
	// Phase 5h.3 — release any cached texture handles before the backend
	// tears down, so Destroy_Texture sees a live backend. After backend
	// Shutdown, Destroy_Texture would be called on a dangling pointer.
	BgfxTextureCache::Clear_All();

	// BgfxBackend::Shutdown clears RenderBackendRuntime's pointer.
	s_instance->Shutdown();
	delete s_instance;
	s_instance = nullptr;
	s_hwnd     = nullptr;
	s_width    = 0;
	s_height   = 0;
	s_windowed = false;
}

bool Is_Initialized()
{
	return s_instance != nullptr;
}

int Get_Width()  { return s_width;  }
int Get_Height() { return s_height; }

} // namespace BgfxBootstrap
