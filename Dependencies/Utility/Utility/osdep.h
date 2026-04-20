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

// __max / __min are MSVC intrinsic macros (from stdlib.h on Windows).
#ifndef __max
#define __max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef __min
#define __min(a,b) (((a) < (b)) ? (a) : (b))
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
// Note: DWORD/WORD/BYTE are typedef'd in Core/Libraries/Source/WWVegas/WWLib/bittype.h
// (unsigned long / unsigned short / unsigned char). We don't redefine here to avoid
// typedef-redefinition conflicts.
typedef unsigned long* LPDWORD;
typedef unsigned long* PDWORD;
typedef int SOCKET;
#define INVALID_SOCKET -1
#define MAX_PATH 260

// SYSTEMTIME — matches Win32 layout so replay-header serialization stays
// byte-compatible with retail .rep files.
typedef struct _SYSTEMTIME {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} SYSTEMTIME;
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
// Match gamespy's extern-C declarations (build_bgfx/_deps/gamespy-src/include/gamespy/gsplatform.h:427)
#ifdef __cplusplus
extern "C" {
#endif
inline char* strupr(char* s) { for (char* p = s; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p))); return s; }
inline char* _strupr(char* s) { return strupr(s); }
#ifdef __cplusplus
}
#endif

// MSVC case-insensitive string compare aliases.
#include <strings.h>
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

// MSVC _stat / _S_IFDIR aliases (POSIX equivalents).
#include <sys/stat.h>
#ifndef _stat
#define _stat stat
#endif
#ifndef _S_IFDIR
#define _S_IFDIR S_IFDIR
#endif

// GetLocalTime — POSIX fallback filling the SYSTEMTIME-compatible struct
// defined above. Millisecond field is zero on non-Windows (no portable API).
#include <ctime>
#include <sys/time.h>
inline void GetLocalTime(SYSTEMTIME* st) {
    if (!st) return;
    struct timeval tv; gettimeofday(&tv, nullptr);
    struct tm lt; localtime_r(&tv.tv_sec, &lt);
    st->wYear         = static_cast<uint16_t>(lt.tm_year + 1900);
    st->wMonth        = static_cast<uint16_t>(lt.tm_mon + 1);
    st->wDayOfWeek    = static_cast<uint16_t>(lt.tm_wday);
    st->wDay          = static_cast<uint16_t>(lt.tm_mday);
    st->wHour         = static_cast<uint16_t>(lt.tm_hour);
    st->wMinute       = static_cast<uint16_t>(lt.tm_min);
    st->wSecond       = static_cast<uint16_t>(lt.tm_sec);
    st->wMilliseconds = static_cast<uint16_t>(tv.tv_usec / 1000);
}

// CopyFile — std::filesystem fallback. Returns non-zero on success (matches
// Win32's BOOL). failIfExists=TRUE maps to skip_existing behaviour.
#include <filesystem>
#include <system_error>
inline int CopyFile(const char* src, const char* dst, int failIfExists) {
    std::error_code ec;
    auto opts = failIfExists
        ? std::filesystem::copy_options::skip_existing
        : std::filesystem::copy_options::overwrite_existing;
    std::filesystem::copy_file(src, dst, opts, ec);
    return ec ? 0 : 1;
}

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
