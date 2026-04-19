/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// Phase 4: cross-platform replacement for the Win32-only SNMP/MIB-II walk
// that used to determine which local interface the kernel would route to
// a given chat server. The portable trick is to connect() a throwaway UDP
// socket (no packets are actually transmitted) and read back the kernel's
// chosen source address via getsockname().

#pragma once

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"

Bool GetLocalChatConnectionAddress(AsciiString serverName,
                                   UnsignedShort serverPort,
                                   UnsignedInt& localIP);
