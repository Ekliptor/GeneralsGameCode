/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#pragma once

// BgfxBootstrap
// =============
// Phase 5h.2 — owns the production-singleton BgfxBackend instance so
// DX8Wrapper (and eventually any other caller) has a single place to ask
// "give me a running bgfx backend configured for this window and
// resolution". Internally:
//
//   * First `Ensure_Init` call constructs a static BgfxBackend and runs
//     `Init(hwnd, w, h, windowed)`; `RenderBackendRuntime::Set_Active`
//     fires inside the backend's Init on success.
//   * Subsequent `Ensure_Init` calls with matching dimensions no-op
//     (success-idempotent). Mismatched dimensions trigger `Reset`.
//   * `Shutdown` drains the backend and clears the runtime slot.
//
// Lifetime:
//   Ensure_Init → runtime has a live backend
//   Ensure_Init (same args, same hwnd) → still alive, no-op
//   Ensure_Init (different w / h / windowed) → Reset in place
//   Shutdown → runtime slot is nullptr; instance destroyed
//   Ensure_Init (after Shutdown) → re-creates a fresh instance
//
// This header is only compiled into bgfx builds. DX8-only builds don't
// include it (the DX8Wrapper non-bgfx branch takes no action).

namespace BgfxBootstrap
{
	// Idempotent: creates the backend on first call, Resets on later
	// calls with changed resolution / windowing. Returns false on bgfx
	// init failure.
	bool Ensure_Init(void* hwnd, int width, int height, bool windowed);

	// Drains the backend if one exists. Safe to call on a fresh / already
	// shut-down bootstrap.
	void Shutdown();

	// True when a live backend is present (i.e. between Ensure_Init and
	// Shutdown). Mostly useful for debug / assertion code.
	bool Is_Initialized();

	// Current back-buffer dimensions as configured by the most recent
	// Ensure_Init / Reset. Zero when no backend is live. Exposed so
	// DX8Wrapper's bgfx-mode shims (back-buffer surface, clipping rects)
	// can read the real resolution instead of assuming a fixed default.
	int  Get_Width();
	int  Get_Height();
}
