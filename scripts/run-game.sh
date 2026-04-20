#!/usr/bin/env bash
# Stages original C&C Generals / Zero Hour assets into the freshly built
# .app bundle's Contents/Resources/ (which SDL_GetBasePath() chdirs to at
# startup) and then launches the game.
#
# Usage:
#   scripts/run-game.sh [--assets /path/to/game] [options]
#
# Options:
#   --assets  DIR   Path to the original extracted game directory.
#                   (Must contain *.big files and/or a Data/ subfolder.)
#                   Optional after the first run: if Contents/Resources/
#                   already holds staged assets, staging is skipped.
#   --restage       Force re-staging even if assets look already present.
#   --target  NAME  Which build to run: "zh" (Zero Hour, default) or
#                   "generals" (vanilla Generals).
#   --mode    M     How to stage files: "symlink" (default), "copy", "move".
#                   Symlink is fastest and uses no extra disk; move is
#                   destructive on the source tree.
#   --build   DIR   CMake build directory (default: build_bgfx).
#   --no-run        Stage assets but do not launch the game.
#   -h | --help     Show this help.
#
# Environment overrides:
#   ASSETS_DIR, TARGET, MODE, BUILD_DIR — same as the flags above.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ASSETS_DIR="${ASSETS_DIR:-}"
TARGET="${TARGET:-zh}"
MODE="${MODE:-symlink}"
BUILD_DIR="${BUILD_DIR:-build_bgfx}"
RUN_AFTER_STAGE=1
FORCE_RESTAGE=0

usage() { sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while (($#)); do
    case "$1" in
        --assets)   ASSETS_DIR="$2"; shift 2 ;;
        --target)   TARGET="$2";     shift 2 ;;
        --mode)     MODE="$2";       shift 2 ;;
        --build)    BUILD_DIR="$2";  shift 2 ;;
        --no-run)   RUN_AFTER_STAGE=0; shift ;;
        --restage)  FORCE_RESTAGE=1; shift ;;
        -h|--help)  usage; exit 0 ;;
        *)
            if [[ -z "${ASSETS_DIR}" && -d "$1" ]]; then
                ASSETS_DIR="$1"; shift
            else
                echo "Unknown argument: $1" >&2; usage; exit 2
            fi ;;
    esac
done

if [[ -n "${ASSETS_DIR}" && ! -d "${ASSETS_DIR}" ]]; then
    echo "Error: assets directory does not exist: ${ASSETS_DIR}" >&2
    exit 2
fi

case "${TARGET}" in
    zh|ZH|zerohour)    APP_REL="${BUILD_DIR}/GeneralsMD/generalszh.app"; APP_BIN="generalszh" ;;
    generals|vanilla)  APP_REL="${BUILD_DIR}/Generals/generalsv.app";    APP_BIN="generalsv"  ;;
    *) echo "Error: --target must be 'zh' or 'generals' (got '${TARGET}')." >&2; exit 2 ;;
esac

APP_PATH="${REPO_ROOT}/${APP_REL}"
if [[ ! -d "${APP_PATH}" ]]; then
    echo "Error: built .app not found at: ${APP_PATH}" >&2
    echo "Build it first, e.g.:  cmake --build ${BUILD_DIR} --target ${APP_BIN}" >&2
    exit 1
fi

RESOURCES_DIR="${APP_PATH}/Contents/Resources"
mkdir -p "${RESOURCES_DIR}"

already_staged() {
    [[ -d "${RESOURCES_DIR}/Data" ]] && return 0
    shopt -s nullglob
    local bigs=( "${RESOURCES_DIR}"/*.[bB][iI][gG] )
    shopt -u nullglob
    (( ${#bigs[@]} > 0 ))
}

stage_entry() {
    local src="$1"
    local name
    name="$(basename -- "${src}")"
    local dst="${RESOURCES_DIR}/${name}"

    if [[ -e "${dst}" || -L "${dst}" ]]; then
        rm -rf -- "${dst}"
    fi

    case "${MODE}" in
        symlink) ln -s -- "${src}" "${dst}" ;;
        copy)    cp -R  -- "${src}" "${dst}" ;;
        move)    mv     -- "${src}" "${dst}" ;;
        *) echo "Error: --mode must be symlink|copy|move (got '${MODE}')." >&2; exit 2 ;;
    esac
}

NEED_STAGE=1
if [[ -z "${ASSETS_DIR}" ]]; then
    if already_staged; then
        NEED_STAGE=0
        echo "Assets already staged in ${RESOURCES_DIR} — skipping."
    else
        echo "Error: no --assets DIR given and ${RESOURCES_DIR} is empty." >&2
        usage
        exit 2
    fi
elif (( ! FORCE_RESTAGE )) && already_staged; then
    NEED_STAGE=0
    echo "Assets already staged in ${RESOURCES_DIR} — skipping (use --restage to force)."
fi

if (( NEED_STAGE )); then
    echo "Staging assets"
    echo "  source : ${ASSETS_DIR}"
    echo "  dest   : ${RESOURCES_DIR}"
    echo "  mode   : ${MODE}"

    shopt -s dotglob nullglob
    cd -- "${ASSETS_DIR}"

    STAGED=0
    for entry in "${ASSETS_DIR}"/*; do
        stage_entry "${entry}"
        STAGED=$((STAGED + 1))
    done
    echo "Staged ${STAGED} top-level entries."
fi

if (( ! RUN_AFTER_STAGE )); then
    echo "Skipping launch (--no-run)."
    exit 0
fi

EXEC="${APP_PATH}/Contents/MacOS/${APP_BIN}"
if [[ ! -x "${EXEC}" ]]; then
    echo "Error: executable missing or not runnable: ${EXEC}" >&2
    exit 1
fi

echo "Launching ${EXEC}"
exec "${EXEC}"
