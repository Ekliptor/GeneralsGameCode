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

#include "SDLDevice/Common/SDLGlobals.h"

#include <SDL3/SDL.h>

namespace SDLDevice
{
	SDL_Window *TheSDLWindow = nullptr;

	void *getNativeWindowHandle()
	{
		if (!TheSDLWindow)
			return nullptr;
		SDL_PropertiesID props = SDL_GetWindowProperties(TheSDLWindow);
#if defined(_WIN32)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
		void *wl = SDL_GetPointerProperty(props, "SDL.window.wayland.surface", nullptr);
		if (wl)
			return wl;
		return SDL_GetPointerProperty(props, "SDL.window.x11.window", nullptr);
#else
		return nullptr;
#endif
	}

	bool getWindowPixelSize(int &w, int &h)
	{
		if (!TheSDLWindow)
			return false;
		int pw = 0, ph = 0;
		if (!SDL_GetWindowSizeInPixels(TheSDLWindow, &pw, &ph))
			return false;
		if (pw <= 0 || ph <= 0)
			return false;
		w = pw;
		h = ph;
		return true;
	}
}
