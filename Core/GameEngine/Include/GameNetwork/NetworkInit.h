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

// Phase 4: consolidated Winsock init/cleanup. On Windows wraps a single
// WSAStartup(MAKEWORD(2,2)) behind std::once_flag so the 4 historical
// init sites (IPEnumeration, Transport, PingThread, GameResultsThread)
// share the same handshake. On POSIX every call is a no-op.

#pragma once

namespace NetworkInit
{
    bool ensureStarted();
    void shutdownOnce();
}
