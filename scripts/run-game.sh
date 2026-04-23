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
#   --generals-assets DIR
#                   Path to a vanilla Generals install (folder with INI.big).
#                   Zero Hour needs vanilla Generals' base data at runtime
#                   to resolve Data\\INI\\Default\\GameData.ini and friends.
#                   If --assets points at .../Install/ZeroHour, the sibling
#                   .../Install/Generals is auto-detected when this flag is
#                   omitted. Ignored for --target generals.
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
# Any arguments after a `--` separator are forwarded verbatim to the game
# executable. Example:
#   scripts/run-game.sh -- -screenshot /tmp/menu.tga -win
#
# Environment overrides:
#   ASSETS_DIR, GENERALS_ASSETS_DIR, TARGET, MODE, BUILD_DIR
#   — same as the flags above.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

# Load repo-root .env (gitignored) so ASSETS_DIR / GENERALS_ASSETS_DIR and
# friends can be persisted locally without re-typing them every run. Values
# already present in the shell environment win over .env; CLI flags still
# override both. Supports KEY=VALUE lines with optional single/double quotes.
if [[ -f "${REPO_ROOT}/.env" ]]; then
    while IFS= read -r __env_line || [[ -n "${__env_line}" ]]; do
        [[ "${__env_line}" =~ ^[[:space:]]*(#|$) ]] && continue
        [[ "${__env_line}" =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]] || continue
        __env_key="${BASH_REMATCH[2]}"
        __env_val="${BASH_REMATCH[3]}"
        if [[ "${__env_val}" =~ ^\"(.*)\"$ ]] || [[ "${__env_val}" =~ ^\'(.*)\'$ ]]; then
            __env_val="${BASH_REMATCH[1]}"
        fi
        if [[ -z "${!__env_key:-}" ]]; then
            printf -v "${__env_key}" '%s' "${__env_val}"
            export "${__env_key}"
        fi
    done < "${REPO_ROOT}/.env"
    unset __env_line __env_key __env_val
fi

ASSETS_DIR="${ASSETS_DIR:-}"
GENERALS_ASSETS_DIR="${GENERALS_ASSETS_DIR:-}"
TARGET="${TARGET:-zh}"
MODE="${MODE:-symlink}"
BUILD_DIR="${BUILD_DIR:-build_bgfx}"
RUN_AFTER_STAGE=1
FORCE_RESTAGE=0
GAME_ARGS=()

usage() { sed -n '2,31p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while (($#)); do
    case "$1" in
        --assets)            ASSETS_DIR="$2";          shift 2 ;;
        --generals-assets)   GENERALS_ASSETS_DIR="$2"; shift 2 ;;
        --target)            TARGET="$2";              shift 2 ;;
        --mode)              MODE="$2";                shift 2 ;;
        --build)             BUILD_DIR="$2";           shift 2 ;;
        --no-run)            RUN_AFTER_STAGE=0;        shift ;;
        --restage)           FORCE_RESTAGE=1;          shift ;;
        -h|--help)           usage; exit 0 ;;
        --)                  shift; GAME_ARGS=("$@");  break ;;
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
if [[ -n "${GENERALS_ASSETS_DIR}" && ! -d "${GENERALS_ASSETS_DIR}" ]]; then
    echo "Error: --generals-assets dir does not exist: ${GENERALS_ASSETS_DIR}" >&2
    exit 2
fi

case "${TARGET}" in
    zh|ZH|zerohour)
        APP_REL="${BUILD_DIR}/GeneralsMD/generalszh.app"; APP_BIN="generalszh"
        # INIZH.big is unique to a Zero Hour install. INI.big (vanilla's
        # counterpart) is absent from ZH, so it doubles as a "wrong target"
        # tell when we see it in a folder that should have been ZH.
        SIG_FILE="INIZH.big";  OTHER_SIG="INI.big";    OTHER_NAME="vanilla Generals"
        ;;
    generals|vanilla)
        APP_REL="${BUILD_DIR}/Generals/generalsv.app";    APP_BIN="generalsv"
        SIG_FILE="INI.big";    OTHER_SIG="INIZH.big";  OTHER_NAME="Zero Hour"
        ;;
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

case_insensitive_find() {
    # find "$1" in directory "$2" case-insensitively; print the actual
    # on-disk name. Works for regular files and symlinks. No recursion.
    local want="$1" dir="$2" entry
    shopt -s nullglob nocaseglob
    for entry in "${dir}"/"${want}"; do
        if [[ -e "${entry}" || -L "${entry}" ]]; then
            basename -- "${entry}"
            shopt -u nullglob nocaseglob
            return 0
        fi
    done
    shopt -u nullglob nocaseglob
    return 1
}

already_staged() {
    # The target's signature .big being present in Resources/ is what
    # "already staged and correct" means. A bare Data/ dir or arbitrary
    # .big files aren't enough — they might be leftovers from staging
    # the wrong folder (e.g. CD images or the opposite target's install).
    case_insensitive_find "${SIG_FILE}" "${RESOURCES_DIR}" >/dev/null
}

validate_assets_dir() {
    # Confirm the given folder actually looks like the expected target's
    # installed game (not the game's parent dir, not the other target's
    # install, not a CD-image dump).
    local dir="$1"

    if case_insensitive_find "${SIG_FILE}" "${dir}" >/dev/null; then
        return 0
    fi

    echo "Error: --assets dir does not look like a ${TARGET} install." >&2
    echo "  expected signature file: ${SIG_FILE}" >&2
    echo "  searched directory     : ${dir}" >&2

    if case_insensitive_find "${OTHER_SIG}" "${dir}" >/dev/null; then
        echo "  detected ${OTHER_SIG} — this looks like a ${OTHER_NAME} install." >&2
        echo "  hint: re-run with --target $([[ "${OTHER_NAME}" == "Zero Hour" ]] && echo zh || echo generals)" >&2
    else
        # Common mistake: user passed the top-level "Game/" folder that
        # contains Install/ZeroHour (or Install/Generals) one level down.
        local nested
        if [[ -d "${dir}/Install/ZeroHour" || -d "${dir}/Install/Generals" ]]; then
            nested="${dir}/Install/$([[ "${TARGET}" == "zh" || "${TARGET}" == "ZH" || "${TARGET}" == "zerohour" ]] && echo ZeroHour || echo Generals)"
            if [[ -d "${nested}" ]] && case_insensitive_find "${SIG_FILE}" "${nested}" >/dev/null; then
                echo "  hint: the installed game lives one level deeper:" >&2
                echo "        --assets \"${nested}\"" >&2
            fi
        fi
    fi
    exit 2
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
        echo "Error: no --assets DIR given and ${RESOURCES_DIR} does not contain ${SIG_FILE}." >&2
        shopt -s nullglob
        existing=( "${RESOURCES_DIR}"/* )
        shopt -u nullglob
        if (( ${#existing[@]} > 0 )); then
            echo "       (${#existing[@]} entries already staged — but none is ${SIG_FILE};" >&2
            echo "        the previous --assets pointed at the wrong folder." >&2
            echo "        Re-run with --assets DIR --restage to replace them.)" >&2
        fi
        usage
        exit 2
    fi
elif (( ! FORCE_RESTAGE )) && already_staged; then
    NEED_STAGE=0
    echo "Assets already staged in ${RESOURCES_DIR} — skipping (use --restage to force)."
else
    validate_assets_dir "${ASSETS_DIR}"
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

upsert_registry_install_path() {
    # Seeds ~/Library/Application Support/EA/Generals/RegistrySettings.ini with
    # an "InstallPath=<abs>" entry. The ZH engine reads this via
    # GetStringFromGeneralsRegistry("", "InstallPath", …) and uses it to load
    # vanilla Generals BIG files (INI.big holds Data\INI\Default\GameData.ini
    # and other base-game INIs that ZH extends).
    local abs_path="$1"
    local reg_dir="$HOME/Library/Application Support/EA/Generals"
    local reg_file="${reg_dir}/RegistrySettings.ini"
    mkdir -p "${reg_dir}"

    local tmp="${reg_file}.tmp.$$"
    if [[ -f "${reg_file}" ]]; then
        grep -v -E '^[[:space:]]*InstallPath[[:space:]]*=' -- "${reg_file}" > "${tmp}" || true
    else
        : > "${tmp}"
    fi
    printf 'InstallPath=%s\n' "${abs_path}" >> "${tmp}"
    mv -f "${tmp}" "${reg_file}"
    echo "  wrote  : ${reg_file}"
    echo "         : InstallPath=${abs_path}"
}

resolve_generals_install() {
    # Only ZH needs this. Returns the chosen dir on stdout, empty string if
    # none found. Order: explicit --generals-assets, auto-sibling of the ZH
    # assets dir, then user's install tree under ./Install/Generals.
    if [[ "${TARGET}" != "zh" && "${TARGET}" != "ZH" && "${TARGET}" != "zerohour" ]]; then
        return 0
    fi

    if [[ -n "${GENERALS_ASSETS_DIR}" ]]; then
        if ! case_insensitive_find "INI.big" "${GENERALS_ASSETS_DIR}" >/dev/null; then
            echo "Error: --generals-assets dir does not contain INI.big: ${GENERALS_ASSETS_DIR}" >&2
            exit 2
        fi
        printf '%s\n' "${GENERALS_ASSETS_DIR}"
        return 0
    fi

    if [[ -n "${ASSETS_DIR}" ]]; then
        local parent sibling
        parent="$(cd -- "${ASSETS_DIR}/.." && pwd 2>/dev/null || true)"
        sibling="${parent}/Generals"
        if [[ -d "${sibling}" ]] && case_insensitive_find "INI.big" "${sibling}" >/dev/null; then
            printf '%s\n' "${sibling}"
            return 0
        fi
    fi

    return 0
}

# Zero Hour needs vanilla Generals data staged or pointed-to via the registry
# store. Do this after staging so we can even fall back to a sibling path.
GEN_INSTALL="$(resolve_generals_install)"
if [[ -n "${GEN_INSTALL}" ]]; then
    GEN_INSTALL_ABS="$(cd -- "${GEN_INSTALL}" && pwd)"
    echo "Vanilla Generals data"
    echo "  source : ${GEN_INSTALL_ABS}"
    upsert_registry_install_path "${GEN_INSTALL_ABS}"
elif [[ "${TARGET}" == "zh" || "${TARGET}" == "ZH" || "${TARGET}" == "zerohour" ]]; then
    echo "Warning: no vanilla Generals install located. ZH init will throw" >&2
    echo "         'Uncaught Exception' while loading Data\\INI\\Default\\GameData.ini." >&2
    echo "         Pass --generals-assets DIR (folder containing INI.big) to fix." >&2
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

if ((${#GAME_ARGS[@]})); then
    echo "Launching ${EXEC} ${GAME_ARGS[*]}"
    exec "${EXEC}" "${GAME_ARGS[@]}"
else
    echo "Launching ${EXEC}"
    exec "${EXEC}"
fi
