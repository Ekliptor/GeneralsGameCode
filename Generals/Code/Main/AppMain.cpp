/*
**	Command & Conquer Generals(tm)
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

// FILE: AppMain.cpp //////////////////////////////////////////////////////////
//
// Phase 2 — Cross-platform entry point used when RTS_PLATFORM=sdl. Mirrors
// WinMain.cpp's startup sequence (memory manager, command line, single-
// instance check) but creates the window and pumps events through SDL3.
//
// On Windows, the SDL-owned HWND is fed back into the legacy ApplicationHWnd
// global so DX8 init, cursor clipping, and window-title updates keep working
// without code changes elsewhere.
///////////////////////////////////////////////////////////////////////////////

#if RTS_PLATFORM_SDL

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>
#endif

#include "Lib/BaseType.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/Debug.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/StackDump.h"
#include "Common/version.h"
#include "GameClient/ClientInstance.h"

#include "SDLDevice/Common/SDLGameEngine.h"
#include "SDLDevice/Common/SDLGlobals.h"

#include "BuildVersion.h"
#include "GeneratedVersion.h"

// Process-wide globals expected by the rest of the engine. Win32-typed on
// Windows so DX8 / cursor / title-update call sites compile unchanged; SDL
// populates them after window creation.
#ifdef _WIN32
HINSTANCE ApplicationHInstance = nullptr;
HWND ApplicationHWnd = nullptr;
#endif
class Win32Mouse;
Win32Mouse *TheWin32Mouse = nullptr;
DWORD TheMessageTime = 0;

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = "";

namespace
{
	enum { DEFAULT_DISPLAY_WIDTH = 800, DEFAULT_DISPLAY_HEIGHT = 600 };
	bool s_appActive = true;

	// Critical sections used by string/memory subsystems. Same allocation
	// pattern as WinMain.cpp:751 — file-scope statics tied to TheXXXCriticalSection.
	CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;
}

GameEngine *CreateGameEngine()
{
	SDLGameEngine *engine = NEW SDLGameEngine;
	engine->setIsActive(s_appActive);
	return engine;
}

extern Int GameMain(int argc = 0, char *argv[] = nullptr);

int main(int argc, char *argv[])
{
	int exitcode = 1;
	try
	{
		TheAsciiStringCriticalSection = &critSec1;
		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		initMemoryManager();

#ifdef RTS_DEBUG
		int tmpFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
		tmpFlag |= (_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);
		tmpFlag &= ~_CRTDBG_CHECK_CRT_DF;
		_CrtSetDbgFlag(tmpFlag);
#endif

		// Match retail behavior: run from the directory the executable lives in
		// so relative asset paths (data\, etc.) resolve.
		if (const char *base = SDL_GetBasePath())
		{
#ifdef _WIN32
			SetCurrentDirectoryA(base);
#else
			(void)chdir(base);
#endif
		}

		CommandLine::parseCommandLineForStartup();

		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		{
			DEBUG_LOG(("SDL_Init failed: %s", SDL_GetError()));
			shutdownMemoryManager();
			return exitcode;
		}

		if (!TheGlobalData->m_headless)
		{
			const Bool windowed = TheGlobalData->m_windowed;
			Uint32 flags = SDL_WINDOW_HIDDEN;
			if (!windowed)
				flags |= SDL_WINDOW_FULLSCREEN;
			// macOS Retina: without this flag SDL configures the NSView for
			// point-sized backing, which leaves the bgfx drawable at half the
			// real pixel count and collapses the render into the top-left
			// quadrant of the display. With the flag, SDL_GetWindowSizeInPixels
			// returns the true physical pixel count and bgfx fills the entire
			// surface.
			flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

			SDLDevice::TheSDLWindow = SDL_CreateWindow(
				"Command and Conquer Generals",
				DEFAULT_DISPLAY_WIDTH,
				DEFAULT_DISPLAY_HEIGHT,
				flags);

			if (!SDLDevice::TheSDLWindow)
			{
				DEBUG_LOG(("SDL_CreateWindow failed: %s", SDL_GetError()));
				SDL_Quit();
				shutdownMemoryManager();
				return exitcode;
			}

#ifdef _WIN32
			// Hand the SDL-owned HWND back to legacy DX8 / cursor / title code.
			ApplicationHWnd = static_cast<HWND>(SDLDevice::getNativeWindowHandle());
			ApplicationHInstance = GetModuleHandle(nullptr);
#endif

			SDL_ShowWindow(SDLDevice::TheSDLWindow);
		}

		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		if (!rts::ClientInstance::initialize())
		{
			DEBUG_LOG(("Generals is already running...Bail!"));
			delete TheVersion;
			TheVersion = nullptr;
			if (SDLDevice::TheSDLWindow)
				SDL_DestroyWindow(SDLDevice::TheSDLWindow);
			SDL_Quit();
			shutdownMemoryManager();
			return exitcode;
		}

		exitcode = GameMain(argc, argv);

		delete TheVersion;
		TheVersion = nullptr;

#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
#endif
#if defined(RTS_DEBUG)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
#endif

		shutdownMemoryManager();

		if (SDLDevice::TheSDLWindow)
		{
			SDL_DestroyWindow(SDLDevice::TheSDLWindow);
			SDLDevice::TheSDLWindow = nullptr;
		}
		SDL_Quit();
	}
	catch (...)
	{
	}

	TheAsciiStringCriticalSection = nullptr;
	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;

	return exitcode;
}

#endif // RTS_PLATFORM_SDL
