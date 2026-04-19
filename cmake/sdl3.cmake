# SDL3 cross-platform window/input/timing backend (Phase 2 — see docs/CrossPlatformPort-Plan.md).
# Provides: window creation, input events, timing, clipboard (unused this phase) on all hosts.
# Only loaded when RTS_PLATFORM=sdl. The Win32 legacy path (WinMain + DirectInput) is unaffected.
if(NOT RTS_PLATFORM_LOWER STREQUAL "sdl")
    return()
endif()

# Prefer SDL3's CONFIG package (vcpkg, Homebrew >=3.x, upstream build all export SDL3Config.cmake).
find_package(SDL3 CONFIG REQUIRED)
# Downstream targets link via SDL3::SDL3 (imported target defined by SDL3Config.cmake).
