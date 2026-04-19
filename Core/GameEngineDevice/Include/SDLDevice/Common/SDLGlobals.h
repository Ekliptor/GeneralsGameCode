/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: SDLGlobals.h ////////////////////////////////////////////////////////
//
// Phase 2 — Cross-platform window/input/timing backend (SDL3).
// Holds the process-global SDL_Window pointer and platform-bridging helpers
// so legacy Windows code (W3D/DX8 HWND consumers) can still participate.
////////////////////////////////////////////////////////////////////////////////

#pragma once

struct SDL_Window;

namespace SDLDevice
{
	// The single game-owned SDL window, created in AppMain before GameMain()
	// is called. Cleared on shutdown. Nullptr outside of the SDL platform path.
	extern SDL_Window *TheSDLWindow;

	// Returns the platform-native window handle from the SDL window.
	// Windows: HWND.  macOS: NSWindow*.  Linux: X11 Window (cast to void*).
	// Used by DX8 init (HWND), bgfx platform data, cursor-clip, and title bar.
	void *getNativeWindowHandle();
}
