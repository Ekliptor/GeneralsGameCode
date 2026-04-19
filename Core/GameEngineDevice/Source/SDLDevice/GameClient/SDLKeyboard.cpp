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

#include "SDLDevice/GameClient/SDLKeyboard.h"

#include "Common/Debug.h"
#include "GameClient/KeyDefs.h"
#include "SDLDevice/Common/SDLGameEngine.h"

#include <SDL3/SDL.h>
#include <cstring>

extern SDLKeyboard *TheSDLKeyboard;

// Game internal key codes are DirectInput DIK_* scancodes (see KeyDefs.h:44 —
// "These key definitions are currently tied directly to the Direct Input key
// codes"). We translate SDL_Scancode into DIK using a switch so the compiler
// can emit a jump table. Values are taken from the stable PS/2 Set 1 scancode
// map that DIK exposes; they're not going to change.
//
// Returns 0 (KEY_NONE) for scancodes we don't map. That's fine: the game only
// queries codes it knows about and ignores unknown ones.
static UnsignedByte scancodeToDIK(SDL_Scancode sc)
{
	switch (sc)
	{
		// Letters — SDL 4..29, DIK follows QWERTY scan order.
		case SDL_SCANCODE_A: return 0x1E;
		case SDL_SCANCODE_B: return 0x30;
		case SDL_SCANCODE_C: return 0x2E;
		case SDL_SCANCODE_D: return 0x20;
		case SDL_SCANCODE_E: return 0x12;
		case SDL_SCANCODE_F: return 0x21;
		case SDL_SCANCODE_G: return 0x22;
		case SDL_SCANCODE_H: return 0x23;
		case SDL_SCANCODE_I: return 0x17;
		case SDL_SCANCODE_J: return 0x24;
		case SDL_SCANCODE_K: return 0x25;
		case SDL_SCANCODE_L: return 0x26;
		case SDL_SCANCODE_M: return 0x32;
		case SDL_SCANCODE_N: return 0x31;
		case SDL_SCANCODE_O: return 0x18;
		case SDL_SCANCODE_P: return 0x19;
		case SDL_SCANCODE_Q: return 0x10;
		case SDL_SCANCODE_R: return 0x13;
		case SDL_SCANCODE_S: return 0x1F;
		case SDL_SCANCODE_T: return 0x14;
		case SDL_SCANCODE_U: return 0x16;
		case SDL_SCANCODE_V: return 0x2F;
		case SDL_SCANCODE_W: return 0x11;
		case SDL_SCANCODE_X: return 0x2D;
		case SDL_SCANCODE_Y: return 0x15;
		case SDL_SCANCODE_Z: return 0x2C;

		// Number row.
		case SDL_SCANCODE_1: return 0x02;
		case SDL_SCANCODE_2: return 0x03;
		case SDL_SCANCODE_3: return 0x04;
		case SDL_SCANCODE_4: return 0x05;
		case SDL_SCANCODE_5: return 0x06;
		case SDL_SCANCODE_6: return 0x07;
		case SDL_SCANCODE_7: return 0x08;
		case SDL_SCANCODE_8: return 0x09;
		case SDL_SCANCODE_9: return 0x0A;
		case SDL_SCANCODE_0: return 0x0B;

		// Control / punctuation.
		case SDL_SCANCODE_RETURN:       return 0x1C; // DIK_RETURN
		case SDL_SCANCODE_ESCAPE:       return 0x01; // DIK_ESCAPE
		case SDL_SCANCODE_BACKSPACE:    return 0x0E; // DIK_BACK
		case SDL_SCANCODE_TAB:          return 0x0F; // DIK_TAB
		case SDL_SCANCODE_SPACE:        return 0x39; // DIK_SPACE
		case SDL_SCANCODE_MINUS:        return 0x0C; // DIK_MINUS
		case SDL_SCANCODE_EQUALS:       return 0x0D; // DIK_EQUALS
		case SDL_SCANCODE_LEFTBRACKET:  return 0x1A; // DIK_LBRACKET
		case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; // DIK_RBRACKET
		case SDL_SCANCODE_BACKSLASH:    return 0x2B; // DIK_BACKSLASH
		case SDL_SCANCODE_SEMICOLON:    return 0x27; // DIK_SEMICOLON
		case SDL_SCANCODE_APOSTROPHE:   return 0x28; // DIK_APOSTROPHE
		case SDL_SCANCODE_GRAVE:        return 0x29; // DIK_GRAVE
		case SDL_SCANCODE_COMMA:        return 0x33; // DIK_COMMA
		case SDL_SCANCODE_PERIOD:       return 0x34; // DIK_PERIOD
		case SDL_SCANCODE_SLASH:        return 0x35; // DIK_SLASH
		case SDL_SCANCODE_CAPSLOCK:     return 0x3A; // DIK_CAPSLOCK

		// Function row.
		case SDL_SCANCODE_F1:  return 0x3B;
		case SDL_SCANCODE_F2:  return 0x3C;
		case SDL_SCANCODE_F3:  return 0x3D;
		case SDL_SCANCODE_F4:  return 0x3E;
		case SDL_SCANCODE_F5:  return 0x3F;
		case SDL_SCANCODE_F6:  return 0x40;
		case SDL_SCANCODE_F7:  return 0x41;
		case SDL_SCANCODE_F8:  return 0x42;
		case SDL_SCANCODE_F9:  return 0x43;
		case SDL_SCANCODE_F10: return 0x44;
		case SDL_SCANCODE_F11: return 0x57;
		case SDL_SCANCODE_F12: return 0x58;

		// Nav cluster.
		case SDL_SCANCODE_PRINTSCREEN: return 0xB7; // DIK_SYSRQ
		case SDL_SCANCODE_SCROLLLOCK:  return 0x46;
		case SDL_SCANCODE_PAUSE:       return 0xC5;
		case SDL_SCANCODE_INSERT:      return 0xD2;
		case SDL_SCANCODE_HOME:        return 0xC7;
		case SDL_SCANCODE_PAGEUP:      return 0xC9;
		case SDL_SCANCODE_DELETE:      return 0xD3;
		case SDL_SCANCODE_END:         return 0xCF;
		case SDL_SCANCODE_PAGEDOWN:    return 0xD1;
		case SDL_SCANCODE_RIGHT:       return 0xCD;
		case SDL_SCANCODE_LEFT:        return 0xCB;
		case SDL_SCANCODE_DOWN:        return 0xD0;
		case SDL_SCANCODE_UP:          return 0xC8;

		// Keypad.
		case SDL_SCANCODE_NUMLOCKCLEAR:  return 0x45;
		case SDL_SCANCODE_KP_DIVIDE:     return 0xB5;
		case SDL_SCANCODE_KP_MULTIPLY:   return 0x37;
		case SDL_SCANCODE_KP_MINUS:      return 0x4A;
		case SDL_SCANCODE_KP_PLUS:       return 0x4E;
		case SDL_SCANCODE_KP_ENTER:      return 0x9C;
		case SDL_SCANCODE_KP_1:          return 0x4F;
		case SDL_SCANCODE_KP_2:          return 0x50;
		case SDL_SCANCODE_KP_3:          return 0x51;
		case SDL_SCANCODE_KP_4:          return 0x4B;
		case SDL_SCANCODE_KP_5:          return 0x4C;
		case SDL_SCANCODE_KP_6:          return 0x4D;
		case SDL_SCANCODE_KP_7:          return 0x47;
		case SDL_SCANCODE_KP_8:          return 0x48;
		case SDL_SCANCODE_KP_9:          return 0x49;
		case SDL_SCANCODE_KP_0:          return 0x52;
		case SDL_SCANCODE_KP_PERIOD:     return 0x53;

		// Modifiers.
		case SDL_SCANCODE_LCTRL:  return 0x1D;
		case SDL_SCANCODE_LSHIFT: return 0x2A;
		case SDL_SCANCODE_LALT:   return 0x38;
		case SDL_SCANCODE_LGUI:   return 0xDB; // DIK_LWIN
		case SDL_SCANCODE_RCTRL:  return 0x9D;
		case SDL_SCANCODE_RSHIFT: return 0x36;
		case SDL_SCANCODE_RALT:   return 0xB8;
		case SDL_SCANCODE_RGUI:   return 0xDC;

		// Extras.
		case SDL_SCANCODE_NONUSBACKSLASH: return 0x56; // DIK_OEM_102

		default:
			return KEY_NONE;
	}
}

SDLKeyboard::SDLKeyboard()
	: m_nextFreeIndex(0), m_nextGetIndex(0)
{
	std::memset(m_events, 0, sizeof(m_events));
}

SDLKeyboard::~SDLKeyboard()
{
	if (TheSDLKeyboard == this)
		TheSDLKeyboard = nullptr;
}

void SDLKeyboard::init()
{
	Keyboard::init();
	// Seed modifier state from SDL so first-frame caps/shift queries match
	// the OS. KEY_STATE_CAPSLOCK is tracked by the base class.
	if ((SDL_GetModState() & SDL_KMOD_CAPS) != 0)
		m_modifiers |= KEY_STATE_CAPSLOCK;
	else
		m_modifiers &= ~KEY_STATE_CAPSLOCK;
}

void SDLKeyboard::reset()
{
	Keyboard::reset();
}

void SDLKeyboard::update()
{
	Keyboard::update();
}

Bool SDLKeyboard::getCapsState()
{
	return (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
}

void SDLKeyboard::pushEvent(const SDL_Event &event)
{
	if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
		return;

	// Silently drop autorepeats — the base class does its own repeat handling
	// in checkKeyRepeat(), matching DirectInputKeyboard's buffered-edges model.
	if (event.key.repeat)
		return;

	const UnsignedByte dik = scancodeToDIK(event.key.scancode);
	if (dik == KEY_NONE)
		return;

	KeyboardIO &slot = m_events[m_nextFreeIndex];
	// Buffer full — oldest slot still has data; drop this event rather than
	// overwrite a pending record.
	if (slot.key != KEY_NONE)
		return;

	slot.key = dik;
	slot.state = (event.type == SDL_EVENT_KEY_DOWN) ? KEY_STATE_DOWN : KEY_STATE_UP;
	slot.status = KeyboardIO::STATUS_UNUSED;
	slot.keyDownTimeMsec = event.key.timestamp / 1000000u; // ns → ms

	m_nextFreeIndex = (m_nextFreeIndex + 1) % EVENT_QUEUE_SIZE;
}

void SDLKeyboard::getKey(KeyboardIO *key)
{
	DEBUG_ASSERTCRASH(key, ("SDLKeyboard::getKey: null out ptr"));
	key->key = KEY_NONE;

	KeyboardIO &slot = m_events[m_nextGetIndex];
	if (slot.key == KEY_NONE)
		return;

	*key = slot;
	slot.key = KEY_NONE;
	m_nextGetIndex = (m_nextGetIndex + 1) % EVENT_QUEUE_SIZE;
}
