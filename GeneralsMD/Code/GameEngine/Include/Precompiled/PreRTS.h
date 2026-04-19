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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// This file contains all the header files that shouldn't change frequently.
// Be careful what you stick in here, because putting files that change often in here will
// tend to cheese people's goats.

#pragma once

//-----------------------------------------------------------------------------
// srj sez: this must come first, first, first.
#define _STLP_USE_NEWALLOC					1
//#define _STLP_USE_CUSTOM_NEWALLOC		STLSpecialAlloc
class STLSpecialAlloc;


// We actually don't use Windows for much other than timeGetTime, but it was included in 40
// different .cpp files, so I bit the bullet and included it here.
// PLEASE DO NOT ABUSE WINDOWS OR IT WILL BE REMOVED ENTIRELY. :-)
//--------------------------------------------------------------------------------- System Includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// Phase 3: atlbase.h / atl_compat.h retired from the game precompiled header —
// the only consumer was the embedded IE browser (CComObject<W3DWebBrowser>),
// which has been deleted. Tools that still use MFC/ATL include their own.
#include <windows.h>
#endif // _WIN32

#include <assert.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#include <excpt.h>
#endif
#include <float.h>
#include <Utility/fstream_adapter.h>
#ifdef _WIN32
#include <imagehlp.h>
#endif
#ifdef _WIN32
#include <io.h>
#endif
#include <limits.h>
#ifdef _WIN32
#include <lmcons.h>
#if defined(_MSC_VER) && _MSC_VER < 1300
#include <mapicode.h>
#endif
#endif // _WIN32
#include <math.h>
#include <cstring>
#ifdef _WIN32
#include <mmsystem.h>
#include <objbase.h>
#include <ocidl.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <snmp.h>
#endif // _WIN32
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <sys/timeb.h>
#endif
#include <sys/types.h>
#ifdef _WIN32
#include <tchar.h>
#endif
#include <time.h>
#ifdef _WIN32
#include <vfw.h>
#include <winerror.h>
#include <wininet.h>
#include <winreg.h>
#else
#include "osdep.h"
#endif

#ifdef _WIN32
#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION	0x800
#endif
#include <dinput.h>
#endif

//------------------------------------------------------------------------------------ STL Includes
// srj sez: no, include STLTypesdefs below, instead, thanks
//#include <algorithm>
//#include <bitset>
//#include <hash_map>
//#include <list>
//#include <map>
//#include <queue>
//#include <set>
//#include <stack>
//#include <string>
//#include <vector>

//------------------------------------------------------------------------------------ RTS Includes
// Icky. These have to be in this order.
#include "Lib/BaseType.h"
#include "Common/STLTypedefs.h"
#include "Common/Errors.h"
#include "Common/Debug.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"

// You might not want Kindof in here because it seems like it changes frequently, but the problem
// is that Kindof is included EVERYWHERE, so it might as well be precompiled.
#include "Common/INI.h"
#include "Common/KindOf.h"
#include "Common/DisabledTypes.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/ClientRandomValue.h"
#include "GameLogic/LogicRandomValue.h"
#include "Common/ObjectStatusTypes.h"

#include "Common/Thing.h"
#include "Common/UnicodeString.h"

#if defined(__GNUC__) && defined(_WIN32)
    #pragma GCC diagnostic pop
#endif
