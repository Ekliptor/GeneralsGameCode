# libcurl — cross-platform HTTP/FTP/HTTPS client (Phase 4 — see docs/CrossPlatformPort-Plan.md).
# Replaces the hand-rolled Winsock FTP client in WWDownload. Always on; networking
# is not a selectable subsystem.

# Prefer CURL's CONFIG package (vcpkg exports CURLConfig.cmake). Fall back to
# the FindCURL module (Homebrew's libcurl only ships pkg-config, not CMake
# config). Both modes define the CURL::libcurl imported target.
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
    find_package(CURL REQUIRED)
endif()
