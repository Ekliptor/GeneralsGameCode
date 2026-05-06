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
#include "SDLDevice/Common/SDLGlobals.h"
#include "SDLDevice/GameClient/SDLKeyboard.h"

#include "Common/GameAudio.h"
#include "Common/GameDefines.h"
#include "GameClient/Mouse.h"
#include "Win32Device/GameClient/Win32Mouse.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
// osdep.h provides UINT/WPARAM/LPARAM + WM_* constants on non-Win32 so the
// same translateMouseEvent helper can feed TheWin32Mouse (which is the same
// object on every platform — W3DGameClient::createMouse always returns a
// W3DMouse : Win32Mouse).
#include <Utility/osdep.h>
#endif

SDLKeyboard *TheSDLKeyboard = nullptr;
extern Win32Mouse *TheWin32Mouse;

SDLGameEngine::SDLGameEngine() = default;
SDLGameEngine::~SDLGameEngine() = default;

namespace
{
	// SDL3 + SDL_WINDOW_FULLSCREEN on macOS reports mouse events in the
	// window's logical-point coordinate space, which snaps to the display's
	// logical size (e.g. 1440×900 on a 2880×1800 Retina display). The game's
	// UI is hardcoded against a 800×600 design space (DEFAULT_DISPLAY_*). In
	// windowed mode the SDL window is 800×600 logical, so the two spaces
	// coincide and no scaling is needed. In fullscreen they diverge and every
	// click lands off the widget regions. Scale here to bridge the gap.
	float s_mouseScaleX = 1.0f;
	float s_mouseScaleY = 1.0f;
	bool  s_mouseScaleSeeded = false;

	// Bitmask of mouse buttons we believe are currently down (we forwarded a
	// DOWN to TheWin32Mouse but no matching UP yet). Used to detect SDL3
	// dropping a BUTTON_UP across a focus boundary (Mission Control, hot
	// corners, Cmd-Tab, dock-show) — without reconciliation, Mouse::process
	// MouseEvent stays wedged in MBS_Down and the next single-click is eaten
	// by the edge-triggered state machine.
	Uint32 s_heldButtons = 0;

	void refreshMouseScale()
	{
		s_mouseScaleSeeded = true;
		if (!SDLDevice::TheSDLWindow) {
			s_mouseScaleX = 1.0f;
			s_mouseScaleY = 1.0f;
			return;
		}
		// SDL3 with SDL_WINDOW_HIGH_PIXEL_DENSITY reports mouse events in
		// the window's logical-point coordinate system (per SDL3 migration
		// docs), so the right scale is DEFAULT_DISPLAY_WIDTH / windowPoints.
		// We can't trust SDL_GetWindowSize directly — on macOS fullscreen
		// it has been observed returning the creation request (800×600)
		// instead of the post-fullscreen point size. Derive points from
		// SDL_GetWindowSizeInPixels (the real drawable, which BgfxBackend
		// also uses) divided by SDL_GetWindowPixelDensity.
		int pixW = 0, pixH = 0;
		SDL_GetWindowSizeInPixels(SDLDevice::TheSDLWindow, &pixW, &pixH);
		float density = SDL_GetWindowPixelDensity(SDLDevice::TheSDLWindow);
		if (pixW <= 0 || pixH <= 0 || density <= 0.0f) {
			s_mouseScaleX = 1.0f;
			s_mouseScaleY = 1.0f;
			return;
		}
		const float pointW = static_cast<float>(pixW) / density;
		const float pointH = static_cast<float>(pixH) / density;
		s_mouseScaleX = static_cast<float>(DEFAULT_DISPLAY_WIDTH)  / pointW;
		s_mouseScaleY = static_cast<float>(DEFAULT_DISPLAY_HEIGHT) / pointH;
		DEBUG_LOG(("SDL mouse scale: pixels=%dx%d density=%.3f points=%.1fx%.1f scale=%.5fx%.5f",
			pixW, pixH, density, pointW, pointH, s_mouseScaleX, s_mouseScaleY));
	}

	inline Sint32 scaleX(float x)
	{
		if (!s_mouseScaleSeeded) refreshMouseScale();
		return static_cast<Sint32>(std::lround(x * s_mouseScaleX));
	}
	inline Sint32 scaleY(float y)
	{
		if (!s_mouseScaleSeeded) refreshMouseScale();
		return static_cast<Sint32>(std::lround(y * s_mouseScaleY));
	}

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
				lParam = packMousePos(scaleX(e.motion.x), scaleY(e.motion.y));
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
				lParam = packMousePos(scaleX(e.button.x), scaleY(e.button.y));
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
				SDL_GetMouseState(&mx, &my);
				lParam = packMousePos(scaleX(mx), scaleY(my));
				return true;
			}
			default:
				return false;
		}
	}
}

void SDLGameEngine::serviceWindowsOS()
{
	if (!s_mouseScaleSeeded) refreshMouseScale();

	// SDL3 does not coalesce SDL_EVENT_MOUSE_MOTION; high-Hz mice/trackpads can
	// emit hundreds per second. Forwarding each one would flood Win32Mouse's
	// 256-slot ring buffer during frame stalls and silently drop button DOWN/UP
	// events queued behind them — wedging Mouse::processMouseEvent's edge-
	// triggered click state machine in MBS_Down. Keep only the latest motion.
	SDL_Event lastMotion;
	bool haveMotion = false;

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

			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
			case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
			case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
				refreshMouseScale();
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				if (TheSDLKeyboard)
					TheSDLKeyboard->pushEvent(e);
				break;

			case SDL_EVENT_MOUSE_MOTION:
				lastMotion = e;
				haveMotion = true;
				break;

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
				{
					if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
						s_heldButtons |= SDL_BUTTON_MASK(e.button.button);
					else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
						s_heldButtons &= ~SDL_BUTTON_MASK(e.button.button);
					TheWin32Mouse->addWin32Event(msg, wParam, lParam, e.common.timestamp / 1000000u);
				}
				break;
			}

			default:
				break;
		}
	}

	if (haveMotion && TheWin32Mouse)
	{
		UINT msg = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
		if (translateMouseEvent(lastMotion, msg, wParam, lParam))
			TheWin32Mouse->addWin32Event(msg, wParam, lParam, lastMotion.common.timestamp / 1000000u);
	}

	// Reconcile held-button state with SDL's current view. If we tracked a
	// DOWN but SDL says the button is no longer held (UP was dropped across a
	// focus boundary — Mission Control, Cmd-Tab, dock-show, hot corner),
	// synthesize the missing UP so Mouse::processMouseEvent can resync from
	// MBS_Down to MBS_Up. Without this, the next single-click is silently
	// swallowed by the edge-triggered state machine.
	if (s_heldButtons && TheWin32Mouse)
	{
		float mx = 0.0f, my = 0.0f;
		const Uint32 sdlMask = SDL_GetMouseState(&mx, &my);
		const Uint32 stale = s_heldButtons & ~sdlMask;
		if (stale)
		{
			const LPARAM pos = packMousePos(scaleX(mx), scaleY(my));
			const DWORD ts = static_cast<DWORD>(SDL_GetTicks());
			const struct { Uint32 mask; UINT msg; } table[] = {
				{ SDL_BUTTON_MASK(SDL_BUTTON_LEFT),   WM_LBUTTONUP },
				{ SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE), WM_MBUTTONUP },
				{ SDL_BUTTON_MASK(SDL_BUTTON_RIGHT),  WM_RBUTTONUP },
			};
			for (const auto &row : table)
			{
				if (stale & row.mask)
				{
					std::fprintf(stderr,
						"[InputFix:syncUP] synthesizing UP for mask=0x%X (SDL dropped it across focus boundary)\n",
						row.mask);
					TheWin32Mouse->addWin32Event(row.msg, 0, pos, ts);
					s_heldButtons &= ~row.mask;
				}
			}
		}
	}
}
