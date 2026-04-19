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

// FILE: SDLKeyboard.h ///////////////////////////////////////////////////////
//
// Phase 2 — Cross-platform keyboard via SDL3. Mirrors DirectInputKeyboard's
// public surface; translates SDL_EVENT_KEY_DOWN/UP records into the game's
// DIK-valued KeyboardIO stream. SDL's event pump feeds pushEvent() once per
// event; getKey() pops one record at a time as Keyboard::updateKeys expects.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/Keyboard.h"

union SDL_Event;

class SDLKeyboard : public Keyboard
{
public:
	SDLKeyboard();
	virtual ~SDLKeyboard() override;

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override;
	virtual Bool getCapsState() override;

	// Feeder called from the SDL event pump for SDL_EVENT_KEY_DOWN / KEY_UP.
	void pushEvent(const SDL_Event &event);

protected:
	virtual void getKey(KeyboardIO *key) override;

private:
	enum { EVENT_QUEUE_SIZE = 256 };
	KeyboardIO m_events[EVENT_QUEUE_SIZE];
	UnsignedInt m_nextFreeIndex;
	UnsignedInt m_nextGetIndex;
};
