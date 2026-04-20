#!/usr/bin/env bash
# Configure (if needed), build, and ad-hoc codesign the macOS .app bundles
# for Command & Conquer Generals (g_generals) and Zero Hour (z_generals).
#
# Usage:
#   scripts/build-osx.sh [flags]
#
# Flags:
#   --target {zh|generals|both}   What to build (default: zh).
#                                 NOTE: "generals" (vanilla) and "both" currently
#                                 fail to link — base-Generals WW3D2 sources
#                                 still reference Win32 APIs (ddraw.h, windows.h)
#                                 not yet ported. Zero Hour is the supported
#                                 target on macOS.
#   --build   DIR                 CMake binary dir (default: build_bgfx).
#   --config  CFG                 Release|Debug|RelWithDebInfo (default: Release).
#   --sign-id ID                  codesign identity (default: "-" ad-hoc).
#                                 Use "Developer ID Application: ..." for a real cert.
#   --no-sign                     Skip codesigning.
#   --reconfigure                 Force re-run of cmake configure.
#   --clean                       Wipe the build dir before configuring.
#   -j N                          Parallel build jobs (default: all cores).
#   -h | --help                   Show this help.
#
# Ad-hoc signing (-s -) produces a locally-valid Mach-O signature. It's
# enough for Gatekeeper to let the app run on the machine that signed it
# and avoids the "killed: 9" fault on Apple Silicon when binaries are
# unsigned. It does NOT enable distribution — that needs a Developer ID
# cert + notarization, which is out of scope here.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

TARGET="zh"
BUILD_DIR="build_bgfx"
CONFIG="Release"
SIGN_ID="-"
DO_SIGN=1
RECONFIGURE=0
CLEAN=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

usage() { sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while (($#)); do
    case "$1" in
        --target)       TARGET="$2";       shift 2 ;;
        --build)        BUILD_DIR="$2";    shift 2 ;;
        --config)       CONFIG="$2";       shift 2 ;;
        --sign-id)      SIGN_ID="$2";      shift 2 ;;
        --no-sign)      DO_SIGN=0;         shift ;;
        --reconfigure)  RECONFIGURE=1;     shift ;;
        --clean)        CLEAN=1;           shift ;;
        -j)             JOBS="$2";         shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Error: build-osx.sh is macOS-only (uname=$(uname -s))." >&2
    exit 1
fi

TARGETS=()
APPS=()
case "${TARGET}" in
    both)
        TARGETS=(g_generals z_generals)
        APPS=("${BUILD_DIR}/Generals/generalsv.app" "${BUILD_DIR}/GeneralsMD/generalszh.app") ;;
    generals)
        TARGETS=(g_generals)
        APPS=("${BUILD_DIR}/Generals/generalsv.app") ;;
    zh|ZH|zerohour)
        TARGETS=(z_generals)
        APPS=("${BUILD_DIR}/GeneralsMD/generalszh.app") ;;
    *) echo "Error: --target must be zh|generals|both (got '${TARGET}')." >&2; exit 2 ;;
esac

cd -- "${REPO_ROOT}"

if (( CLEAN )) && [[ -d "${BUILD_DIR}" ]]; then
    echo "[clean] removing ${BUILD_DIR}"
    rm -rf -- "${BUILD_DIR}"
fi

if (( RECONFIGURE )) || [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "[configure] ${BUILD_DIR} (${CONFIG})"
    cmake -S . -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${CONFIG}" \
        -DRTS_PLATFORM=sdl \
        -DRTS_RENDERER=bgfx \
        -DRTS_VIDEO=ffmpeg \
        -DRTS_AUDIO=openal
else
    echo "[configure] reusing existing cache in ${BUILD_DIR}"
fi

echo "[build] targets: ${TARGETS[*]} (jobs=${JOBS})"
cmake --build "${BUILD_DIR}" --config "${CONFIG}" --target "${TARGETS[@]}" -j "${JOBS}"

sign_app() {
    local app="$1"
    if [[ ! -d "${app}" ]]; then
        echo "Error: expected bundle missing: ${app}" >&2
        return 1
    fi
    local bin_dir="${app}/Contents/MacOS"
    if [[ -z "$(ls -A "${bin_dir}" 2>/dev/null)" ]]; then
        echo "Error: bundle has no executable in Contents/MacOS: ${app}" >&2
        return 1
    fi
    echo "[sign] ${app}  (id=${SIGN_ID})"
    codesign --force --deep --timestamp=none --sign "${SIGN_ID}" -- "${app}"
    codesign --verify --verbose=2 -- "${app}"
}

if (( DO_SIGN )); then
    for app in "${APPS[@]}"; do
        sign_app "${REPO_ROOT}/${app}"
    done
else
    echo "[sign] skipped (--no-sign)"
fi

echo
echo "done."
for app in "${APPS[@]}"; do
    echo "  ${REPO_ROOT}/${app}"
done