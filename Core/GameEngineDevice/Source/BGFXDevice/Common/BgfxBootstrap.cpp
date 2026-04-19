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

#include <cstdio>

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
		else if (width != s_width || height != s_height || windowed != s_windowed)
		{
			s_instance->Reset(width, height, windowed);
			s_width = width; s_height = height; s_windowed = windowed;
			return true;
		}
		else
		{
			return true;
		}
	}

	// Fresh construction. BgfxBackend::Init calls
	// RenderBackendRuntime::Set_Active(this) on success, so the runtime
	// seam goes live as a side effect.
	auto* backend = new BgfxBackend();
	if (!backend->Init(hwnd, width, height, windowed))
	{
		delete backend;
		return false;
	}
	s_instance = backend;
	s_hwnd     = hwnd;
	s_width    = width;
	s_height   = height;
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

} // namespace BgfxBootstrap
