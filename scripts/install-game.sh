#!/usr/bin/env bash
# Drive CrossOver to run the original C&C Generals / Zero Hour InstallShield
# installers, then copy the installed data files into a destination directory
# that scripts/run-game.sh --assets DIR can consume.
#
# Usage:
#   scripts/install-game.sh [flags]
#
# Flags:
#   --target {generals|zh|both}   Which title(s) to install (default: both).
#   --bottle NAME                 CrossOver bottle name  (default: CnCGenerals).
#   --dest   DIR                  Destination base dir   (default: NAS Install/).
#   --generals-cd1 DIR            Extracted Generals CD1 (default: NAS).
#   --generals-cd2 DIR            Extracted Generals CD2 (default: NAS).
#   --zh-cd1       DIR            Extracted ZH CD1       (default: NAS).
#   --zh-cd2       DIR            Extracted ZH CD2       (default: NAS).
#   --skip-install                Do not run setup.exe; only extract from bottle.
#   --skip-extract                Run setup.exe; do not rsync out of the bottle.
#   -h | --help                   Show this help.
#
# The installer wizard is interactive: click through Next / Install in the
# CrossOver window that appears. The script blocks until the wizard exits.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

NAS_BASE="/Users/.../Games/Command and Conquer Generals/Game"

TARGET="both"
BOTTLE="CnCGenerals"
DEST="${NAS_BASE}/Install"
GEN_CD1="${NAS_BASE}/Command_and_Conquer_Generals_CD1"
GEN_CD2="${NAS_BASE}/Command_and_Conquer_Generals_CD2"
ZH_CD1="${NAS_BASE}/Zero Hour/cc_zh_cd1"
ZH_CD2="${NAS_BASE}/Zero Hour/cc_zh_cd2"
DO_INSTALL=1
DO_EXTRACT=1

CX_BIN_DIR="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin"
WINE="${CX_BIN_DIR}/wine"
CXBOTTLE="${CX_BIN_DIR}/cxbottle"
BOTTLES_DIR="${HOME}/Library/Application Support/CrossOver/Bottles"

usage() { sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while (($#)); do
    case "$1" in
        --target)        TARGET="$2";        shift 2 ;;
        --bottle)        BOTTLE="$2";        shift 2 ;;
        --dest)          DEST="$2";          shift 2 ;;
        --generals-cd1)  GEN_CD1="$2";       shift 2 ;;
        --generals-cd2)  GEN_CD2="$2";       shift 2 ;;
        --zh-cd1)        ZH_CD1="$2";        shift 2 ;;
        --zh-cd2)        ZH_CD2="$2";        shift 2 ;;
        --skip-install)  DO_INSTALL=0;       shift ;;
        --skip-extract)  DO_EXTRACT=0;       shift ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

case "${TARGET}" in
    generals|zh|both) ;;
    *) echo "Error: --target must be generals|zh|both (got '${TARGET}')." >&2; exit 2 ;;
esac

if [[ ! -x "${WINE}" || ! -x "${CXBOTTLE}" ]]; then
    cat >&2 <<EOF
Error: CrossOver CLI tools not found at:
  ${CX_BIN_DIR}
Install CrossOver from https://www.codeweavers.com/crossover or adjust CX_BIN_DIR in the script.
EOF
    exit 1
fi

ensure_bottle() {
    local bottle_path="${BOTTLES_DIR}/${BOTTLE}"
    if [[ -d "${bottle_path}" ]]; then
        echo "[bottle] reusing '${BOTTLE}' at ${bottle_path}"
        return
    fi
    echo "[bottle] creating '${BOTTLE}' (win7 template)"
    "${CXBOTTLE}" --create --bottle "${BOTTLE}" --description "CnC Generals / Zero Hour" --template win7
    if [[ ! -d "${bottle_path}" ]]; then
        echo "Error: bottle creation did not produce ${bottle_path}" >&2
        exit 1
    fi
}

merge_cds() {
    local cd1="$1" cd2="$2" tmp="$3"
    shopt -s dotglob nullglob
    for entry in "${cd1}"/*; do
        ln -s -- "${entry}" "${tmp}/$(basename -- "${entry}")"
    done
    for entry in "${cd2}"/*; do
        local name
        name="$(basename -- "${entry}")"
        [[ -e "${tmp}/${name}" || -L "${tmp}/${name}" ]] && continue
        ln -s -- "${entry}" "${tmp}/${name}"
    done
    shopt -u dotglob nullglob
}

run_installer() {
    local label="$1" cd1="$2" cd2="$3"
    if [[ ! -f "${cd1}/setup.exe" ]]; then
        echo "Error: ${label}: no setup.exe at: ${cd1}" >&2
        return 1
    fi
    local tmp
    tmp="$(mktemp -d -t "cnc-${label,,}-XXXXXX")"

    echo "[install:${label}] merging CD1+CD2 into ${tmp}"
    merge_cds "${cd1}" "${cd2}" "${tmp}"

    echo "[install:${label}] launching setup.exe in bottle '${BOTTLE}' — click through the wizard"
    local rc=0
    "${WINE}" --bottle "${BOTTLE}" -- "${tmp}/setup.exe" || rc=$?
    rm -rf -- "${tmp}"
    if (( rc != 0 )); then
        echo "[install:${label}] setup.exe exited with rc=${rc}" >&2
        return "${rc}"
    fi
    echo "[install:${label}] installer exited cleanly"
}

locate_bottle_install() {
    local candidates=("$@")
    local root prefix subdir
    for root in "drive_c/Program Files" "drive_c/Program Files (x86)"; do
        prefix="${BOTTLES_DIR}/${BOTTLE}/${root}/EA Games"
        for subdir in "${candidates[@]}"; do
            if [[ -d "${prefix}/${subdir}" ]]; then
                printf '%s' "${prefix}/${subdir}"
                return 0
            fi
        done
    done
    return 1
}

extract_assets() {
    local label="$1" dest_subdir="$2"; shift 2
    local candidates=("$@")
    local src dst
    if ! src="$(locate_bottle_install "${candidates[@]}")"; then
        echo "Error: ${label}: installed dir not found under bottle '${BOTTLE}'." >&2
        echo "       tried (under Program Files/EA Games/): ${candidates[*]}" >&2
        return 1
    fi
    dst="${DEST}/${dest_subdir}"
    mkdir -p -- "${dst}"
    echo "[extract:${label}] rsync ${src}/ -> ${dst}/" >&2
    rsync -a --info=progress2 \
        --exclude='*.exe' --exclude='*.dll' \
        --exclude='unins*' --exclude='Uninstall*' \
        -- "${src}/" "${dst}/" >&2
    echo "[extract:${label}] done -> ${dst}" >&2
    printf '%s' "${dst}"
}

GENERALS_OUT=""
ZH_OUT=""

do_title() {
    local label="$1" cd1="$2" cd2="$3" dest_subdir="$4"; shift 4
    local candidates=("$@")
    if (( DO_INSTALL )); then
        run_installer "${label}" "${cd1}" "${cd2}"
    else
        echo "[install:${label}] skipped (--skip-install)"
    fi
    if (( DO_EXTRACT )); then
        case "${label}" in
            Generals) GENERALS_OUT="$(extract_assets "${label}" "${dest_subdir}" "${candidates[@]}")" ;;
            ZeroHour) ZH_OUT="$(extract_assets "${label}" "${dest_subdir}" "${candidates[@]}")" ;;
        esac
    else
        echo "[extract:${label}] skipped (--skip-extract)"
    fi
}

ensure_bottle

if [[ "${TARGET}" == "generals" || "${TARGET}" == "both" ]]; then
    do_title "Generals" "${GEN_CD1}" "${GEN_CD2}" "Generals" \
        "Command and Conquer Generals" \
        "Command & Conquer Generals"
fi

if [[ "${TARGET}" == "zh" || "${TARGET}" == "both" ]]; then
    do_title "ZeroHour" "${ZH_CD1}" "${ZH_CD2}" "ZeroHour" \
        "Command and Conquer Generals Zero Hour" \
        "Command & Conquer Generals Zero Hour"
fi

echo
echo "done."
[[ -n "${GENERALS_OUT}" ]] && echo "  Generals : ${GENERALS_OUT}"
[[ -n "${ZH_OUT}"       ]] && echo "  ZeroHour : ${ZH_OUT}"
echo
echo "Next step, e.g.:"
[[ -n "${ZH_OUT}"       ]] && echo "  scripts/run-game.sh --assets \"${ZH_OUT}\""
[[ -n "${GENERALS_OUT}" ]] && echo "  scripts/run-game.sh --assets \"${GENERALS_OUT}\" --target generals"