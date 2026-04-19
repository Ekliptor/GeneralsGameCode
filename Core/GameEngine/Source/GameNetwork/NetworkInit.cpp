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

#include "PreRTS.h"

#include "GameNetwork/NetworkInit.h"

#include <mutex>

#ifdef _WIN32
#include <winsock.h>
#endif

namespace
{
    std::once_flag s_startupFlag;
    bool s_started = false;
}

namespace NetworkInit
{
    bool ensureStarted()
    {
#ifdef _WIN32
        std::call_once(s_startupFlag, []() {
            WSADATA wsadata;
            const WORD verReq = MAKEWORD(2, 2);
            const int err = ::WSAStartup(verReq, &wsadata);
            if (err != 0)
            {
                s_started = false;
                return;
            }
            if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2)
            {
                ::WSACleanup();
                s_started = false;
                return;
            }
            s_started = true;
        });
        return s_started;
#else
        s_started = true;
        return true;
#endif
    }

    void shutdownOnce()
    {
#ifdef _WIN32
        if (s_started)
        {
            ::WSACleanup();
            s_started = false;
        }
#endif
    }
}
