#!/usr/bin/env bash
# Launches the freshly built .app under lldb for debugging.
# Assumes assets are already staged in Contents/Resources/ — use
# scripts/run-game.sh first (or with --no-run) to stage them.
#
# Usage:
#   scripts/debug-run.sh [options] [-- <args passed to the game>]
#
# Options:
#   --target  NAME  Which build to run: "zh" (Zero Hour, default) or
#                   "generals" (vanilla Generals).
#   --build   DIR   CMake build directory (default: build_bgfx).
#   --bt-on-crash   Non-interactive: auto-run, print "bt all" on crash/exit,
#                   then quit. Good for CI-style capture.
#   --batch         Alias for --bt-on-crash.
#   -h | --help     Show this help.
#
# Interactive mode (default): drops into the lldb prompt after launch so
# you can set breakpoints, Ctrl-C to break into a hang, etc.
#
# Environment overrides:
#   TARGET, BUILD_DIR — same as the flags above.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

TARGET="${TARGET:-zh}"
BUILD_DIR="${BUILD_DIR:-build_bgfx}"
BATCH=0
GAME_ARGS=()

usage() { sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while (($#)); do
    case "$1" in
        --target)        TARGET="$2";    shift 2 ;;
        --build)         BUILD_DIR="$2"; shift 2 ;;
        --bt-on-crash|--batch) BATCH=1;  shift ;;
        -h|--help)       usage; exit 0 ;;
        --)              shift; GAME_ARGS=( "$@" ); break ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

case "${TARGET}" in
    zh|ZH|zerohour)    APP_REL="${BUILD_DIR}/GeneralsMD/generalszh.app"; APP_BIN="generalszh" ;;
    generals|vanilla)  APP_REL="${BUILD_DIR}/Generals/generalsv.app";    APP_BIN="generalsv"  ;;
    *) echo "Error: --target must be 'zh' or 'generals' (got '${TARGET}')." >&2; exit 2 ;;
esac

APP_PATH="${REPO_ROOT}/${APP_REL}"
EXEC="${APP_PATH}/Contents/MacOS/${APP_BIN}"
if [[ ! -x "${EXEC}" ]]; then
    echo "Error: executable missing or not runnable: ${EXEC}" >&2
    echo "Build it first, e.g.:  cmake --build ${BUILD_DIR} --target ${APP_BIN}" >&2
    exit 1
fi

if ! command -v lldb >/dev/null 2>&1; then
    echo "Error: lldb not found on PATH. Install Xcode command line tools." >&2
    exit 1
fi

echo "Debugging ${EXEC}"
(( ${#GAME_ARGS[@]} )) && echo "  args : ${GAME_ARGS[*]}"

if (( BATCH )); then
    # Non-interactive: launch, auto-print backtrace of all threads on
    # stop/crash, then quit. SIGINT from the terminal still breaks in.
    exec lldb \
        -o "settings set auto-confirm true" \
        -o "settings set target.process.stop-on-exec false" \
        -o "process launch --" \
        -k "thread backtrace all" \
        -k "quit" \
        -- "${EXEC}" "${GAME_ARGS[@]}"
else
    # Interactive: launch and leave the lldb prompt open.
    # Ctrl-C inside the session breaks into the running process
    # (useful when the game hangs); 'bt all' then 'c' or 'q'.
    exec lldb \
        -o "settings set auto-confirm true" \
        -o "process launch --" \
        -- "${EXEC}" "${GAME_ARGS[@]}"
fi