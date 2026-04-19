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

#include "SDLDevice/Common/SDLGameEngine.h"
#include "SDLDevice/GameClient/SDLKeyboard.h"

#include "Common/GameAudio.h"
#include "GameClient/Mouse.h"
#include "Win32Device/GameClient/Win32Mouse.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Win32 message constants reused when translating SDL mouse events so the
// existing Win32Mouse::addWin32Event feeder can consume them unchanged.
// See Win32Mouse::translateEvent() for the accepted message set.
#endif

SDLKeyboard *TheSDLKeyboard = nullptr;
extern Win32Mouse *TheWin32Mouse;

SDLGameEngine::SDLGameEngine() = default;
SDLGameEngine::~SDLGameEngine() = default;

#ifdef _WIN32
namespace
{
	LPARAM packMousePos(Sint32 x, Sint32 y)
	{
		return (static_cast<LPARAM>(y & 0xFFFF) << 16) | (x & 0xFFFF);
	}

	// Translate an SDL mouse event into the equivalent WM_* message + LPARAM so
	// the legacy Win32Mouse::translateEvent() can consume it without a second
	// code path. wParam's button flags aren't read there, so we leave them 0.
	bool translateMouseEvent(const SDL_Event &e, UINT &msg, WPARAM &wParam, LPARAM &lParam)
	{
		wParam = 0;
		switch (e.type)
		{
			case SDL_EVENT_MOUSE_MOTION:
				msg = WM_MOUSEMOVE;
				lParam = packMousePos(static_cast<Sint32>(e.motion.x), static_cast<Sint32>(e.motion.y));
				return true;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				const bool down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
				const bool dbl = down && e.button.clicks >= 2;
				switch (e.button.button)
				{
					case SDL_BUTTON_LEFT:
						msg = dbl ? WM_LBUTTONDBLCLK : (down ? WM_LBUTTONDOWN : WM_LBUTTONUP);
						break;
					case SDL_BUTTON_MIDDLE:
						msg = dbl ? WM_MBUTTONDBLCLK : (down ? WM_MBUTTONDOWN : WM_MBUTTONUP);
						break;
					case SDL_BUTTON_RIGHT:
						msg = dbl ? WM_RBUTTONDBLCLK : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
						break;
					default:
						return false;
				}
				lParam = packMousePos(static_cast<Sint32>(e.button.x), static_cast<Sint32>(e.button.y));
				return true;
			}
			case SDL_EVENT_MOUSE_WHEEL:
			{
				// WM_MOUSEWHEEL: HIWORD(wParam) = signed wheel delta (WHEEL_DELTA = 120),
				// lParam = screen-space cursor position (translateEvent calls ScreenToClient).
				const Sint32 delta = static_cast<Sint32>(e.wheel.y * 120.0f);
				msg = 0x020A; // WM_MOUSEWHEEL literal, matches Win32Mouse::translateEvent
				wParam = (static_cast<WPARAM>(delta & 0xFFFF) << 16);
				float mx = 0.0f, my = 0.0f;
				SDL_GetGlobalMouseState(&mx, &my);
				lParam = packMousePos(static_cast<Sint32>(mx), static_cast<Sint32>(my));
				return true;
			}
			default:
				return false;
		}
	}
}
#endif // _WIN32

void SDLGameEngine::serviceWindowsOS()
{
	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
			case SDL_EVENT_QUIT:
				setQuitting(TRUE);
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				setIsActive(TRUE);
				if (TheAudio)
					TheAudio->unmuteAudio(AudioManager::MuteAudioReason_WindowFocus);
				if (TheMouse)
					TheMouse->refreshCursorCapture();
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				setIsActive(FALSE);
				if (TheAudio)
					TheAudio->muteAudio(AudioManager::MuteAudioReason_WindowFocus);
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				if (TheSDLKeyboard)
					TheSDLKeyboard->pushEvent(e);
				break;

#ifdef _WIN32
			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:
			{
				if (!TheWin32Mouse)
					break;
				UINT msg = 0;
				WPARAM wParam = 0;
				LPARAM lParam = 0;
				if (translateMouseEvent(e, msg, wParam, lParam))
					TheWin32Mouse->addWin32Event(msg, wParam, lParam, e.common.timestamp / 1000000u);
				break;
			}
#endif

			default:
				break;
		}
	}
}
