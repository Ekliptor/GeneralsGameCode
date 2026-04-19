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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/RegistryStore.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#endif

#if RTS_PLATFORM_SDL
#include <SDL3/SDL.h>
#endif

RegistryStore *TheRegistryStore = nullptr;

namespace
{
	// Resolve the directory used for the RegistrySettings.ini file. Keeps
	// TheGlobalData out of the path — TheRegistryStore is first hit by
	// CommandLine parsing before TheGlobalData is constructed.
	AsciiString resolveConfigDir()
	{
#if RTS_PLATFORM_SDL
		if (char *p = SDL_GetPrefPath("EA", "Generals"))
		{
			AsciiString out(p);
			SDL_free(p);
			return out;
		}
#endif
#ifdef _WIN32
		PWSTR wpath = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &wpath)))
		{
			char buf[MAX_PATH] = {0};
			WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf, sizeof(buf), nullptr, nullptr);
			CoTaskMemFree(wpath);
			AsciiString out(buf);
#if RTS_GENERALS
			out.concat("\\Command and Conquer Generals Data\\");
#else
			out.concat("\\Command and Conquer Generals Zero Hour Data\\");
#endif
			CreateDirectoryA(out.str(), nullptr);
			return out;
		}
#endif
		return AsciiString("");
	}
}

#ifdef _WIN32
// Retail installations write these keys to HKLM (installer) or HKCU (user).
// On first launch after the swap we read them once and seed the INI so the
// new build picks up where the retail install left off. After the initial
// seeding the registry is ignored.
static Bool readLegacyRegistryString(AsciiString path, AsciiString name, AsciiString &val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#else
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#endif
	if (!path.isEmpty())
		fullPath.concat(path);

	const HKEY roots[2] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for (HKEY root : roots)
	{
		HKEY handle = nullptr;
		if (RegOpenKeyEx(root, fullPath.str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
			continue;
		unsigned char buffer[256] = {0};
		unsigned long size = sizeof(buffer);
		unsigned long type = 0;
		LONG rc = RegQueryValueEx(handle, name.str(), nullptr, &type, buffer, &size);
		RegCloseKey(handle);
		if (rc == ERROR_SUCCESS)
		{
			val = reinterpret_cast<const char *>(buffer);
			return TRUE;
		}
	}
	return FALSE;
}

static Bool readLegacyRegistryUInt(AsciiString path, AsciiString name, UnsignedInt &val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#else
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#endif
	if (!path.isEmpty())
		fullPath.concat(path);

	const HKEY roots[2] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for (HKEY root : roots)
	{
		HKEY handle = nullptr;
		if (RegOpenKeyEx(root, fullPath.str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
			continue;
		unsigned char buffer[4] = {0};
		unsigned long size = sizeof(buffer);
		unsigned long type = 0;
		LONG rc = RegQueryValueEx(handle, name.str(), nullptr, &type, buffer, &size);
		RegCloseKey(handle);
		if (rc == ERROR_SUCCESS)
		{
			val = *reinterpret_cast<UnsignedInt *>(buffer);
			return TRUE;
		}
	}
	return FALSE;
}
#endif // _WIN32

RegistryStore::RegistryStore()
{
	load(AsciiString("RegistrySettings.ini"));
}

RegistryStore::~RegistryStore() = default;

Bool RegistryStore::load(AsciiString fname)
{
	m_filename = resolveConfigDir();
	m_filename.concat(fname);

	FILE *fp = std::fopen(m_filename.str(), "r");
	if (!fp)
		return FALSE;

	char buf[2048];
	while (std::fgets(buf, sizeof(buf), fp) != nullptr)
	{
		AsciiString line(buf);
		line.trim();
		if (line.isEmpty() || line.getCharAt(0) == ';' || line.getCharAt(0) == '#')
			continue;
		AsciiString key, val;
		line.nextToken(&key, "=");
		val = line.str() + 1; // skip the '=' that nextToken left behind
		key.trim();
		val.trim();
		if (key.isEmpty() || val.isEmpty())
			continue;
		(*this)[key] = val;
	}
	std::fclose(fp);
	return TRUE;
}

RegistryStore *RegistryStore::get()
{
	if (!TheRegistryStore)
		TheRegistryStore = NEW RegistryStore;
	return TheRegistryStore;
}

AsciiString RegistryStore::makeKey(AsciiString path, AsciiString name) const
{
	AsciiString out = path;
	if (!out.isEmpty() && !name.isEmpty())
		out.concat("\\");
	out.concat(name);
	return out;
}

Bool RegistryStore::getString(AsciiString path, AsciiString name, AsciiString &val)
{
	const AsciiString key = makeKey(path, name);
	iterator it = find(key);
	if (it != end())
	{
		val = it->second;
		return TRUE;
	}
#ifdef _WIN32
	if (readLegacyRegistryString(path, name, val))
	{
		(*this)[key] = val;
		write();
		return TRUE;
	}
#endif
	return FALSE;
}

Bool RegistryStore::getUnsignedInt(AsciiString path, AsciiString name, UnsignedInt &val)
{
	const AsciiString key = makeKey(path, name);
	iterator it = find(key);
	if (it != end())
	{
		val = static_cast<UnsignedInt>(std::strtoul(it->second.str(), nullptr, 10));
		return TRUE;
	}
#ifdef _WIN32
	if (readLegacyRegistryUInt(path, name, val))
	{
		AsciiString asText;
		asText.format("%u", val);
		(*this)[key] = asText;
		write();
		return TRUE;
	}
#endif
	return FALSE;
}

Bool RegistryStore::setString(AsciiString path, AsciiString name, AsciiString val)
{
	(*this)[makeKey(path, name)] = val;
	return write();
}

Bool RegistryStore::setUnsignedInt(AsciiString path, AsciiString name, UnsignedInt val)
{
	AsciiString asText;
	asText.format("%u", val);
	(*this)[makeKey(path, name)] = asText;
	return write();
}
