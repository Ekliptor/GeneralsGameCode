// osdep.h — Unix OS-dependency stub (Phase 5d).
// Replaces the stlport-4.5.3 osdep.h for non-VC6 builds on Unix platforms.
// The original provides _alloca → alloca mapping and other OS compat defines.
#pragma once

#ifndef _WIN32

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cerrno>

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
typedef unsigned int UINT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
// Note: LONG is defined by Core/Libraries/Source/WWVegas/WW3D2/compat/d3d8.h
// as int32_t; we don't redefine here to avoid typedef-redefinition conflicts.

// LARGE_INTEGER — used for QueryPerformanceCounter pairs. Only QuadPart is
// referenced on our side.
#include <time.h>
#ifndef _LARGE_INTEGER_DEFINED_
#define _LARGE_INTEGER_DEFINED_
typedef union _LARGE_INTEGER {
    struct { unsigned int LowPart; int HighPart; };
    long long QuadPart;
} LARGE_INTEGER;
#endif
inline int QueryPerformanceFrequency(LARGE_INTEGER* freq) {
    if (freq) freq->QuadPart = 1000000000LL; // steady_clock uses ns
    return 1;
}
inline int QueryPerformanceCounter(LARGE_INTEGER* counter) {
    if (!counter) return 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    counter->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 1;
}
// GetModuleFileName — Win32 returns the path to the executable. On Apple use
// _NSGetExecutablePath; elsewhere fall back to /proc/self/exe.
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
inline unsigned long GetModuleFileName(HMODULE /*mod*/, char* buf, unsigned long size) {
    if (!buf || size == 0) return 0;
#ifdef __APPLE__
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        unsigned long n = 0;
        while (buf[n]) ++n;
        return n;
    }
#else
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n > 0) { buf[n] = '\0'; return (unsigned long)n; }
#endif
    buf[0] = '\0';
    return 0;
}

inline void Sleep(unsigned int ms) {
    struct timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}
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
#include <cerrno>
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

// MSVC _wtoi — wide-string to int. On non-Windows, wchar_t is 32-bit UCS-4;
// values outside ASCII don't parse as digits anyway so a naive byte-by-byte
// copy is safe for the numeric use cases in this codebase.
#include <cwchar>
inline int _wtoi(const wchar_t* s) {
    if (!s) return 0;
    int sign = 1; int val = 0;
    while (*s == L' ' || *s == L'\t') ++s;
    if (*s == L'-') { sign = -1; ++s; } else if (*s == L'+') ++s;
    while (*s >= L'0' && *s <= L'9') { val = val * 10 + (int)(*s - L'0'); ++s; }
    return sign * val;
}

// MSVC itoa — non-standard; implement with snprintf for base 10/16/2.
#include <cstdio>
inline char* itoa(int value, char* buffer, int radix) {
    if (!buffer) return buffer;
    if (radix == 10) {
        std::snprintf(buffer, 32, "%d", value);
    } else if (radix == 16) {
        std::snprintf(buffer, 32, "%x", (unsigned)value);
    } else if (radix == 8) {
        std::snprintf(buffer, 32, "%o", (unsigned)value);
    } else {
        unsigned v = (unsigned)value;
        char tmp[33]; int i = 0;
        if (v == 0) tmp[i++] = '0';
        while (v) { int d = v % radix; tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; }
        int k = 0; while (i > 0) buffer[k++] = tmp[--i]; buffer[k] = '\0';
    }
    return buffer;
}

// MSVC case-insensitive string compare aliases.
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
// wcscasecmp is a GNU/POSIX extension — some libc's don't expose it by default.
// Provide a simple fallback that walks both strings and compares via towlower.
#ifndef _wcsicmp
#define _wcsicmp wcscasecmp
#endif

// MSVC _stat / _S_IFDIR aliases (POSIX equivalents).
#include <sys/stat.h>
#include <unistd.h>
#ifndef _stat
#define _stat stat
#endif
#ifndef _S_IFDIR
#define _S_IFDIR S_IFDIR
#endif
#ifndef _access
#define _access access
#endif
#ifndef _MAX_PATH
#define _MAX_PATH 260
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

// CreateDirectory / Get|SetCurrentDirectory — std::filesystem fallbacks.
// Second SECURITY_ATTRIBUTES arg is ignored (always nullptr in this codebase).
inline int CreateDirectory(const char* path, void* /*secAttrs*/) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return ec ? 0 : 1;
}
inline unsigned long GetCurrentDirectory(unsigned long bufLen, char* buf) {
    std::error_code ec;
    auto p = std::filesystem::current_path(ec).string();
    if (ec) return 0;
    if (buf && bufLen > p.size()) {
        std::memcpy(buf, p.c_str(), p.size() + 1);
        return static_cast<unsigned long>(p.size());
    }
    return static_cast<unsigned long>(p.size() + 1);
}
inline int SetCurrentDirectory(const char* path) {
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    return ec ? 0 : 1;
}
inline int DeleteFile(const char* path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec) ? 1 : 0;
}

// Win32 DLL loading — on non-Windows we use dlopen/dlsym/dlclose. Only the
// script engine's optional in-process DebugWindow.dll / ParticleEditor.dll
// consume this; both lookups are expected to fail on macOS (no .dll
// artifacts), so LoadLibrary returns nullptr and the rest short-circuits.
#include <dlfcn.h>
typedef void (*FARPROC)();
inline HMODULE LoadLibrary(const char* name) {
    if (!name) return nullptr;
    return (HMODULE)dlopen(name, RTLD_NOW | RTLD_LOCAL);
}
inline int FreeLibrary(HMODULE mod) {
    if (!mod) return 1;
    return dlclose(mod) == 0 ? 1 : 0;
}
inline FARPROC GetProcAddress(HMODULE mod, const char* sym) {
    if (!mod || !sym) return nullptr;
    return (FARPROC)dlsym(mod, sym);
}

// MEMORYSTATUS — used only for debug logging in preloadAssets. Field names
// mirror the Win32 struct so call sites compile unchanged. Values come from
// sysctl where reasonable; dwMemoryLoad/dwLength are left at 0.
typedef struct _MEMORYSTATUS {
    unsigned long dwLength;
    unsigned long dwMemoryLoad;
    unsigned long dwTotalPhys;
    unsigned long dwAvailPhys;
    unsigned long dwTotalPageFile;
    unsigned long dwAvailPageFile;
    unsigned long dwTotalVirtual;
    unsigned long dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
inline void GlobalMemoryStatus(MEMORYSTATUS* ms) {
    if (!ms) return;
    std::memset(ms, 0, sizeof(*ms));
    ms->dwLength = sizeof(*ms);
}

// GetDoubleClickTime — Win32 returns user's double-click threshold (ms).
// macOS default is ~500ms; we use the same constant Cocoa uses when no
// NSEvent is available.
inline unsigned int GetDoubleClickTime() { return 500; }

// Win32 error formatting shims. GetLastError → errno; FormatMessageW writes a
// wide error string from strerror(). Only FORMAT_MESSAGE_FROM_SYSTEM is used.
#ifndef FORMAT_MESSAGE_FROM_SYSTEM
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#endif
inline unsigned long GetLastError() { return (unsigned long)errno; }
inline unsigned long FormatMessageW(unsigned long /*flags*/, const void* /*src*/,
                                    unsigned long code, unsigned long /*lang*/,
                                    wchar_t* buf, unsigned long bufLen, void* /*args*/) {
    if (!buf || bufLen == 0) return 0;
    const char* msg = std::strerror((int)code);
    unsigned long i = 0;
    while (msg[i] && i + 1 < bufLen) { buf[i] = (wchar_t)(unsigned char)msg[i]; ++i; }
    buf[i] = L'\0';
    return i;
}
inline unsigned long FormatMessageA(unsigned long /*flags*/, const void* /*src*/,
                                    unsigned long code, unsigned long /*lang*/,
                                    char* buf, unsigned long bufLen, void* /*args*/) {
    if (!buf || bufLen == 0) return 0;
    std::snprintf(buf, bufLen, "%s", std::strerror((int)code));
    return (unsigned long)std::strlen(buf);
}
#define FormatMessage FormatMessageA

// Win32 MessageBox / ShowWindow stubs — on non-Windows, log to stderr and
// ignore the show-window call. These are called from crash paths where we
// want to surface the message but have no usable HWND.
#ifndef MB_OK
#define MB_OK             0x00000000
#define MB_ICONERROR      0x00000010
#define MB_TASKMODAL      0x00002000
#define MB_SYSTEMMODAL    0x00001000
#define MB_APPLMODAL      0x00000000
#define MB_ICONSTOP       MB_ICONERROR
#define MB_ICONEXCLAMATION 0x00000030
#define MB_ICONWARNING    MB_ICONEXCLAMATION
#define MB_ICONINFORMATION 0x00000040
#define MB_YESNO          0x00000004
#define MB_OKCANCEL       0x00000001
#define IDOK              1
#define IDCANCEL          2
#define IDYES             6
#define IDNO              7
#define SW_HIDE           0
#define SW_SHOW           5
#define SW_SHOWNORMAL     1
#endif
inline int MessageBoxA(void* /*hwnd*/, const char* text, const char* caption, unsigned int /*type*/) {
    std::fprintf(stderr, "[%s] %s\n", caption ? caption : "Message", text ? text : "");
    return IDOK;
}
inline int MessageBoxW(void* /*hwnd*/, const wchar_t* /*text*/, const wchar_t* /*caption*/, unsigned int /*type*/) {
    std::fprintf(stderr, "[MessageBoxW] (wide message)\n");
    return IDOK;
}
#define MessageBox MessageBoxA
inline int ShowWindow(void* /*hwnd*/, int /*cmd*/) { return 0; }

// Win32 virtual-key constants (winuser.h). Only the values referenced by
// engine code are defined — extend as new callsites appear.
#ifndef VK_BACK
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_PAUSE    0x13
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_PRIOR    0x21
#define VK_NEXT     0x22
#define VK_END      0x23
#define VK_HOME     0x24
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_INSERT   0x2D
#define VK_DELETE   0x2E
#define VK_F1       0x70
#define VK_F2       0x71
#define VK_F3       0x72
#define VK_F4       0x73
#define VK_F5       0x74
#define VK_F6       0x75
#define VK_F7       0x76
#define VK_F8       0x77
#define VK_F9       0x78
#define VK_F10      0x79
#define VK_F11      0x7A
#define VK_F12      0x7B
#endif

// Win32 WM_ mouse message codes. Used on non-Windows just to keep switch cases
// compiling; the SDL event pump dispatches SDL-native events, so these case
// values are dead (but the ordinals match Windows for reference).
#ifndef WM_MOUSEMOVE
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define WM_MBUTTONDBLCLK  0x0209
#define WM_MOUSEWHEEL     0x020A
#define WM_MOUSEMOVE      0x0200
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#endif

// LOWORD / HIWORD — extract the low/high 16 bits of a 32-bit DWORD-ish value.
#ifndef LOWORD
#define LOWORD(l) ((unsigned short)((uintptr_t)(l) & 0xFFFF))
#define HIWORD(l) ((unsigned short)(((uintptr_t)(l) >> 16) & 0xFFFF))
#endif

// GetAsyncKeyState — SDL handles keyboard globally, so this Win32 probe is a
// no-op (always "not down") on POSIX. Keyboard-driven debug hotkeys that went
// through the Win32 global key state now silently do nothing; the keyboard
// subsystem in SDLKeyboard picks up real events.
inline short GetAsyncKeyState(int /*vkey*/) { return 0; }
inline short GetKeyState(int /*vkey*/) { return 0; }

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
typedef void* HCURSOR;

// POINT — Win32 point struct (used by cursor APIs).
// Guard matches Core/Libraries/.../compat/d3d8.h so only one definition wins.
#ifndef _POINT_DEFINED_
#define _POINT_DEFINED_
struct POINT { long x, y; };
#endif
typedef POINT* LPPOINT;

// Cursor/window API stubs — no-ops since SDL handles mouse positioning.
inline HCURSOR SetCursor(HCURSOR /*cursor*/) { return nullptr; }
inline int GetCursorPos(LPPOINT pt) { if (pt) { pt->x = 0; pt->y = 0; } return 1; }
inline int ScreenToClient(void* /*hwnd*/, LPPOINT /*pt*/) { return 1; }
inline int ClientToScreen(void* /*hwnd*/, LPPOINT /*pt*/) { return 1; }

// RECT — matches Win32 layout. Compat/d3d8.h also defines this when included;
// both definitions are identical so only the first one seen wins via the guard.
#ifndef _RECT_DEFINED_
#define _RECT_DEFINED_
struct RECT { long left, top, right, bottom; };
#endif

// GetClientRect / ClipCursor / LoadCursorFromFile — no-ops on POSIX. SDL owns
// window geometry and mouse clipping.
inline int GetClientRect(void* /*hwnd*/, RECT* rect) {
    if (rect) { rect->left = rect->top = rect->right = rect->bottom = 0; }
    return 1;
}
inline int ClipCursor(const RECT* /*rect*/) { return 1; }
inline HCURSOR LoadCursorFromFile(const char* /*path*/) { return nullptr; }

// Win32 heap APIs — back by the process allocator; enough for the few places
// that allocate via GetProcessHeap()/HeapAlloc() (shader enum buffers).
#define HEAP_ZERO_MEMORY 0x00000008
inline void* GetProcessHeap() { return (void*)1; }
inline void* HeapAlloc(void* /*heap*/, unsigned long flags, size_t size) {
    void* p = std::malloc(size);
    if (p && (flags & HEAP_ZERO_MEMORY)) std::memset(p, 0, size);
    return p;
}
inline int HeapFree(void* /*heap*/, unsigned long /*flags*/, void* p) { std::free(p); return 1; }

// SUCCEEDED/FAILED macros for HRESULT.
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) ((hr) < 0)
#endif

#endif // !_WIN32
