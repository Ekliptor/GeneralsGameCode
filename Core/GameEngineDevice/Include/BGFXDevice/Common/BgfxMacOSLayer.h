/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
*/

#pragma once

#if defined(__APPLE__)

// Tiny Cocoa probe + fixup used by BgfxBackend::Init on macOS. The
// CAMetalLayer bgfx attaches to SDL-managed NSWindows can end up with a
// default (zero) frame so it only displays a corner of its drawable —
// the "pink border" symptom. `Fit_Layer_To_View` re-seats the layer's
// frame/autoresizing/contentsScale against the content view so the
// drawable maps 1:1 to on-screen pixels.

namespace BgfxMacOSLayer
{
	bool Fit_Layer_To_View(void* nsWindowOrView);
}

#endif // __APPLE__
