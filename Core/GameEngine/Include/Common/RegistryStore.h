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

// FILE: RegistryStore.h /////////////////////////////////////////////////////
//
// Phase 3 — Cross-platform backing for the Registry.h public API. Values
// live in a flat INI file under the user-data directory (same location
// UserPreferences uses). On Windows, first-read for a key that the INI
// doesn't contain falls back to the legacy HKLM/HKCU registry once and
// seeds the INI, letting retail installations migrate without a tool.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/UserPreferences.h"

class RegistryStore : public UserPreferences
{
public:
	RegistryStore();
	virtual ~RegistryStore() override;

	// Overrides UserPreferences::load to compute its own user-data path via
	// SDL_GetPrefPath / SHGetKnownFolderPath rather than going through
	// TheGlobalData — registry is queried during command-line parsing before
	// TheGlobalData is constructed.
	virtual Bool load(AsciiString fname) override;

	// Compound key format is "<path>\\<name>" — matches the retail registry
	// layout. `path` may be empty for top-level values (Language, SKU, etc.).
	Bool getString(AsciiString path, AsciiString name, AsciiString &val);
	Bool getUnsignedInt(AsciiString path, AsciiString name, UnsignedInt &val);
	Bool setString(AsciiString path, AsciiString name, AsciiString val);
	Bool setUnsignedInt(AsciiString path, AsciiString name, UnsignedInt val);

	// Singleton accessor — constructs on first call, survives until shutdown.
	static RegistryStore *get();

private:
	AsciiString makeKey(AsciiString path, AsciiString name) const;
};

extern RegistryStore *TheRegistryStore;
