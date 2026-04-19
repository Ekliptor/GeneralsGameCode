// osdep.h — Unix OS-dependency stub (Phase 5d).
// Replaces the stlport-4.5.3 osdep.h for non-VC6 builds on Unix platforms.
// The original provides _alloca → alloca mapping and other OS compat defines.
#pragma once

#ifndef _WIN32

#include <cstdint>
#include <cstdlib>

// _alloca → alloca mapping (used in math headers under _UNIX).
#ifdef __has_include
#if __has_include(<alloca.h>)
#include <alloca.h>
#endif
#endif
#ifndef _alloca
#define _alloca alloca
#endif

// __forceinline is MSVC-specific.
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

// MSVC integer types — must be macros so 'unsigned __int64' expands correctly.
#ifndef __int64
#define __int64 long long
#endif
#ifndef _int64
#define _int64 long long
#endif

// Win32 types that leak through debug/profile headers.
#ifndef _WIN32_TYPES_COMPAT
#define _WIN32_TYPES_COMPAT
typedef void* HANDLE;
typedef void* HMODULE;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef unsigned long* LPDWORD;
typedef unsigned long* PDWORD;
typedef int SOCKET;
#define INVALID_SOCKET -1
#define MAX_PATH 260
#endif

// MSVC string function aliases.
#include <cstring>
#include <cctype>
#include <cmath>
#ifndef _strdup
#define _strdup strdup
#endif
#ifndef _isnan
#define _isnan std::isnan
#endif
inline char* strupr(char* s) { for (char* p = s; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p))); return s; }
inline char* _strupr(char* s) { return strupr(s); }

// MSVC case-insensitive string compare aliases.
#include <strings.h>
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

// Win32 string function aliases.
#define lstrcpy  strcpy
#define lstrcpyn strncpy
#define lstrcmpi strcasecmp
#define lstrlen  strlen
#define ZeroMemory(p,n) memset((p),0,(n))

// Additional Win32 types.
typedef void* HBITMAP;
typedef void* HFONT;
typedef void* HKEY;

// SUCCEEDED/FAILED macros for HRESULT.
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) ((hr) < 0)
#endif

#endif // !_WIN32
