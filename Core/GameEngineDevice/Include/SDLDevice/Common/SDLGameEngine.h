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

// FILE: SDLGameEngine.h /////////////////////////////////////////////////////
//
// Phase 2 — SDL3 game-engine subclass. Replaces the Win32 message pump in
// serviceWindowsOS() with SDL_PollEvent, translating SDL events into the
// keyboard/mouse subsystems. All other factories and services inherit from
// Win32GameEngine unchanged; this lives on top of the existing Windows-only
// engine scaffolding and will be hoisted further when macOS ships in Phase 6.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Win32Device/Common/Win32GameEngine.h"

class SDLGameEngine : public Win32GameEngine
{
public:
	SDLGameEngine();
	virtual ~SDLGameEngine() override;

	virtual void serviceWindowsOS() override;
};

extern class SDLKeyboard *TheSDLKeyboard;
