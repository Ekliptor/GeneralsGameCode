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

// Minimal HRESULT/MAKE_HRESULT compat for code that historically relied on
// <windows.h> + <winerror.h> for these types. On Windows include the real
// headers for exact parity; on POSIX we only need the bits WWDownload uses.

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <winerror.h>
#else

#include <Utility/string_compat.h>   // LPCSTR / LPSTR

typedef long HRESULT;

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef SEVERITY_ERROR
#define SEVERITY_ERROR 1
#endif
#ifndef FACILITY_ITF
#define FACILITY_ITF 4
#endif
#ifndef MAKE_HRESULT
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((unsigned long)(sev) << 31) | \
               ((unsigned long)(fac) << 16) | \
               ((unsigned long)(code))))
#endif

#endif // _WIN32
