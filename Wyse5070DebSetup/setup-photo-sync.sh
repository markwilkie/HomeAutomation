#!/usr/bin/env bash
#
# setup-photo-sync.sh
#
# Two nightly jobs that keep the X-T5 photo library safe (plan: cam_copy/IMPROVEMENT_PLAN.md):
#
#   01:00  edit saver  -- asks Adobe's Lightroom API what changed and saves a full-size JPEG of every
#                         edited or Pick photo to \\192.168.15.10\photo\Xt5\Edited
#                         (code: lightroom/lightroom_tool/edit_saver.py, command `save-edits`)
#   02:00  backup      -- rclone sync of \\192.168.15.10\photo\Xt5 to the work OneDrive
#                         (remote onedrive_backup, see TripTracker/scripts/setup_onedrive_remote.sh).
#                         Anything deleted or replaced on the NAS is moved to
#                         Backups/Photos/_deleted/YYYY-MM-DD/ and kept 90 days, not lost.
#
# Both write status JSON to /mnt/data/appdata/photo-sync/ (read by the daily brief's `photos` check
# and the weekly health check) and copy it into the dashboard's html dir as photo-status.json
# (the dashboard card shows it live).
#
# NAS access: DSM user `photo-sync` -- read-only on the `photo` share, write only to Xt5/Edited.
# Its password lives in /mnt/data/appdata/photo-sync/.env (NAS_USER, NAS_PASS; chmod 600), written by
# hand, never in git. The rclone remote `nas` (type smb) is (re)created from it on every install.
#
# Lightroom sign-in: /mnt/data/appdata/photo-sync/lightroom-tokens.json (chmod 600), copied from the
# PC by lightroom/Renew-LightroomSignIn.ps1. Adobe makes it last 14 days; the edit saver reports the
# expiry and the daily brief warns 3 days ahead. ADOBE_CLIENT_ID is in lightroom.env (not a secret).
#
# No sudo: runs as mwilkie from crontab (like TripTracker's backup). Re-running is safe.
#
# Usage:
#   ./setup-photo-sync.sh                 # install/refresh: venv, rclone remote, crontab, first status
#   ./setup-photo-sync.sh --run-edits     # run the edit saver now (what cron calls at 01:00)
#   ./setup-photo-sync.sh --run-backup    # run the backup now (what cron calls at 02:00)
#   tail -f ~/photo-sync.log

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename "${BASH_SOURCE[0]}")"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_DIR="/mnt/data/appdata/photo-sync"
VENV="${APP_DIR}/venv"
RCLONE="${RCLONE:-$HOME/bin/rclone}"
NAS_HOST="192.168.15.10"
SRC="nas:photo/Xt5"
DST="onedrive_backup:Backups/Photos/Xt5"
DELETED_ROOT="onedrive_backup:Backups/Photos/_deleted"
KEEP_DELETED_DAYS=90
DASH_HTML="/mnt/data/appdata/dashboard/html"
LOG="${HOME}/photo-sync.log"

log() { echo "[photo-sync] $(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "${LOG}"; }

publish_status() {
    # Merge both status files into one JSON the dashboard card can fetch.
    python3 - "${APP_DIR}" "${DASH_HTML}/photo-status.json" <<'PY' || true
import json, os, sys
app, out = sys.argv[1], sys.argv[2]
def load(n):
    try: return json.load(open(os.path.join(app, n)))
    except Exception: return None
data = {"backup": load("backup-status.json"), "edits": load("edit-saver-status.json")}
if os.path.isdir(os.path.dirname(out)):
    tmp = out + ".tmp"; json.dump(data, open(tmp, "w")); os.replace(tmp, out)
PY
}

write_backup_status() {  # ok error files bytes not_in_onedrive
    python3 - "${APP_DIR}/backup-status.json" "$@" <<'PY'
import json, sys, datetime, os
path, ok, error, files, size, missing = sys.argv[1:7]
now = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
try: prev = json.load(open(path))
except Exception: prev = {}
s = dict(prev, last_attempt=now, ok=(ok == "1"), error=error or None,
         files=int(files or 0), bytes=int(size or 0), not_in_onedrive=int(missing or 0))
if ok == "1": s["last_success"] = now
tmp = path + ".tmp"; json.dump(s, open(tmp, "w"), indent=1); os.replace(tmp, path)
PY
}

run_backup() {
    log "Backup starting: ${SRC} -> ${DST}"
    local today; today="$(date +%F)"
    local err=""
    if ! "${RCLONE}" sync "${SRC}" "${DST}" \
            --backup-dir "${DELETED_ROOT}/${today}" \
            --exclude ".incoming/**" --exclude "**/.incoming/**" \
            --transfers 4 --checkers 8 --retries 3 --low-level-retries 10 \
            --stats-one-line --stats 0 >> "${LOG}" 2>&1; then
        err="rclone sync failed (see ~/photo-sync.log)"
    fi
    # Size check of everything on the NAS against OneDrive (smb has no hashes; sizes catch truncation).
    local missing=0
    if [[ -z "${err}" ]]; then
        local bad="${APP_DIR}/not-in-onedrive.txt"
        : > "${bad}"
        "${RCLONE}" check "${SRC}" "${DST}" --one-way --size-only \
            --exclude ".incoming/**" --exclude "**/.incoming/**" \
            --missing-on-dst "${bad}" --differ "${bad}.differ" >> "${LOG}" 2>&1 || true
        cat "${bad}.differ" >> "${bad}" 2>/dev/null || true
        rm -f "${bad}.differ"
        missing="$(grep -c . "${bad}" || true)"
        [[ "${missing}" != "0" ]] && err="${missing} files differ or are missing in OneDrive after sync (list: ${bad})"
    fi
    "${RCLONE}" delete --min-age "${KEEP_DELETED_DAYS}d" "${DELETED_ROOT}" >> "${LOG}" 2>&1 || true
    "${RCLONE}" rmdirs --leave-root "${DELETED_ROOT}" >> "${LOG}" 2>&1 || true
    local size_json; size_json="$("${RCLONE}" size "${DST}" --json 2>/dev/null || echo '{}')"
    local files bytes
    files="$(echo "${size_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("count",0))' 2>/dev/null || echo 0)"
    bytes="$(echo "${size_json}" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("bytes",0))' 2>/dev/null || echo 0)"
    if [[ -z "${err}" ]]; then
        write_backup_status 1 "" "${files}" "${bytes}" 0
        log "Backup OK: ${files} files, $((bytes / 1048576)) MB in OneDrive"
    else
        write_backup_status 0 "${err}" "${files}" "${bytes}" "${missing}"
        log "Backup FAILED: ${err}"
    fi
    publish_status
    [[ -z "${err}" ]]
}

run_edits() {
    log "Edit saver starting"
    set -a; source "${APP_DIR}/lightroom.env"; set +a
    local rc=0
    (cd "${REPO_DIR}/lightroom" && "${VENV}/bin/python" -m lightroom_tool save-edits \
        --rclone-remote "${SRC}/Edited" --rclone "${RCLONE}" --state-dir "${APP_DIR}") >> "${LOG}" 2>&1 || rc=$?
    log "Edit saver finished (exit ${rc}: 0 = all saved, 2 = some missing, 1 = could not run)"
    publish_status
    return 0
}

install() {
    [[ -f "${APP_DIR}/.env" ]] || { echo "Missing ${APP_DIR}/.env (NAS_USER/NAS_PASS). See the header."; exit 1; }
    [[ -x "${RCLONE}" ]] || { echo "rclone not found at ${RCLONE}"; exit 1; }
    chmod 600 "${APP_DIR}/.env"
    # shellcheck disable=SC1091
    set -a; source "${APP_DIR}/.env"; set +a

    echo "==> rclone remote 'nas' (smb ${NAS_HOST}, user ${NAS_USER})"
    "${RCLONE}" config delete nas >/dev/null 2>&1 || true
    "${RCLONE}" config create nas smb host "${NAS_HOST}" user "${NAS_USER}" \
        pass "$("${RCLONE}" obscure "${NAS_PASS}")" >/dev/null
    if ! "${RCLONE}" lsf --max-depth 1 nas:photo/Xt5 >/dev/null 2>&1; then
        echo "!! ${NAS_USER} can't read photo/Xt5. In DSM File Station: photo/Xt5 > Properties > Permission >"
        echo "   add ${NAS_USER} with Read (this folder, child folders and files), and Read+Write on Xt5/Edited."
        exit 1
    fi
    echo "    can read photo/Xt5"

    echo "==> Python venv for the edit saver (${VENV})"
    [[ -d "${VENV}" ]] || python3 -m venv "${VENV}"
    "${VENV}/bin/pip" install -q --upgrade pip
    "${VENV}/bin/pip" install -q -r "${REPO_DIR}/lightroom/requirements.txt"

    if [[ ! -f "${APP_DIR}/lightroom.env" ]]; then
        echo "==> Writing ${APP_DIR}/lightroom.env (edit ADOBE_CLIENT_ID if it changes)"
        cat > "${APP_DIR}/lightroom.env" <<EOF
ADOBE_CLIENT_ID=2313a09372e54462a51e72c027350eb6
ADOBE_REDIRECT_URI=http://localhost:8080/callback
LIGHTROOM_TOKEN_FILE=${APP_DIR}/lightroom-tokens.json
EOF
    fi
    [[ -f "${APP_DIR}/lightroom-tokens.json" ]] || echo "    NOTE: no Lightroom sign-in yet: run lightroom/Renew-LightroomSignIn.ps1 on the PC"

    echo "==> crontab: edit saver 01:00, backup 02:00"
    { crontab -l 2>/dev/null | grep -v -e "setup-photo-sync.sh" -e "^# photo-sync:" || true
      echo "# photo-sync: Lightroom edit saver + NAS->OneDrive photo backup (Wyse5070DebSetup/setup-photo-sync.sh)"
      echo "0 1 * * * ${SCRIPT_PATH} --run-edits >/dev/null 2>&1"
      echo "0 2 * * * ${SCRIPT_PATH} --run-backup >/dev/null 2>&1"
    } | crontab -
    crontab -l | grep -A2 "^# photo-sync:"
    publish_status
    echo "==> Done. Log: ${LOG}"
}

mkdir -p "${APP_DIR}"
case "${1:-}" in
    --run-edits)  run_edits ;;
    --run-backup) run_backup ;;
    "")           install ;;
    *) echo "unknown option: $1"; exit 2 ;;
esac
