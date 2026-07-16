#!/usr/bin/env bash
#
# setup-homeassistant-backup.sh
#
# Home Assistant (see setup-homeassistant.sh) is a Container install with no
# Supervisor, so the UI's "Automatic backups" schedule isn't available, and
# as of writing nothing else backs up /mnt/data/appdata/homeassistant/config
# either -- no cron, no systemd timer, no backup: integration configured.
# Losing the disk today would mean rebuilding every automation/entity from
# scratch and losing all recorder history.
#
# This installs a daily local backup: a systemd timer runs this script
# (--run), which tars up the HA config directory to
# /mnt/data/appdata/homeassistant-backups/ and prunes old ones. It is a
# LOCAL backup only -- same physical disk as the live config (this host has
# exactly one data drive, /dev/sda, serving both /home and /mnt/data; see
# setup-move-home.sh) -- so it protects against bad config pushes, a botched
# automation edit, or database corruption, but NOT against that drive
# failing. If that matters later, point BACKUP_DIR at a different disk (or
# add an off-host copy step) rather than changing anything else here.
#
# The recorder database (home-assistant_v2.db) is written in WAL mode while
# HA is running, so a plain cp/tar mid-write risks a torn, inconsistent
# copy. Python's sqlite3 module (stdlib, no extra install needed) exposes
# SQLite's own online backup API via Connection.backup(), which takes a
# correct, consistent snapshot without stopping the container or blocking
# writers -- used here instead of a raw file copy of the .db/-wal/-shm
# files.
#
# Runs as root: .storage/auth, auth_provider.homeassistant, core.config,
# core.uuid, and onboarding are 600 root:root (they hold real credentials/
# tokens), unlike the rest of .storage which is world-readable -- confirmed
# live, a plain-user rsync fails on exactly those five files. Root is
# correct here anyway: the resulting archives contain the same credentials,
# so root-only ownership on the backups is the more secure default, not
# just a workaround.
#
# Usage:
#   ./setup-homeassistant-backup.sh          # install + enable the daily timer
#   sudo ./setup-homeassistant-backup.sh --run    # run one backup now (what the timer calls)
#   sudo systemctl status homeassistant-backup.timer
#   journalctl -u homeassistant-backup.service
#   tail -f /mnt/data/appdata/homeassistant-backups/backup.log   # same log, no sudo needed

set -euo pipefail

CONFIG_DIR="/mnt/data/appdata/homeassistant/config"
BACKUP_DIR="/mnt/data/appdata/homeassistant-backups"
RETENTION_COUNT=14
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename "${BASH_SOURCE[0]}")"
LOG_FILE="${BACKUP_DIR}/backup.log"

log() {
    local line="[ha-backup] $(date '+%Y-%m-%d %H:%M:%S') $*"
    echo "${line}"
    echo "${line}" >> "${LOG_FILE}" 2>/dev/null || true
}

run_backup() {
    mkdir -p "${BACKUP_DIR}"
    local timestamp work_dir archive
    timestamp="$(date '+%Y%m%d-%H%M%S')"
    work_dir="$(mktemp -d)"
    archive="${BACKUP_DIR}/ha-backup-${timestamp}.tar.gz"

    log "Starting backup -> ${archive}"

    # Consistent snapshot of the live recorder DB via SQLite's own backup API.
    if ! python3 - "${CONFIG_DIR}/home-assistant_v2.db" "${work_dir}/home-assistant_v2.db" <<'PYEOF'
import sqlite3, sys
src_path, dst_path = sys.argv[1], sys.argv[2]
src = sqlite3.connect(src_path)
dst = sqlite3.connect(dst_path)
with dst:
    src.backup(dst)
dst.close()
src.close()
PYEOF
    then
        log "!! sqlite3 backup of home-assistant_v2.db failed -- aborting this run"
        rm -rf "${work_dir}"
        return 1
    fi

    # Everything else: config, .storage/ (entity registry, input_number
    # values, auth), secrets, blueprints. Skip the live db files (already
    # captured above via the consistent snapshot), logs, and the
    # regenerable/large .cache + deps + tts dirs (brand icons cache, Python
    # deps, TTS audio cache -- none needed to restore config or history).
    rsync -a \
        --exclude='home-assistant_v2.db' \
        --exclude='home-assistant_v2.db-wal' \
        --exclude='home-assistant_v2.db-shm' \
        --exclude='home-assistant.log*' \
        --exclude='.ha_run.lock' \
        --exclude='.cache' \
        --exclude='deps' \
        --exclude='tts' \
        "${CONFIG_DIR}/" "${work_dir}/"

    tar czf "${archive}" -C "${work_dir}" .
    rm -rf "${work_dir}"

    local size
    size="$(du -h "${archive}" | cut -f1)"
    log "Backup complete: ${archive} (${size})"

    # Retention: keep the newest RETENTION_COUNT archives.
    local pruned=0
    while IFS= read -r old; do
        rm -f "${old}"
        pruned=$((pruned + 1))
    done < <(ls -1t "${BACKUP_DIR}"/ha-backup-*.tar.gz 2>/dev/null | tail -n "+$((RETENTION_COUNT + 1))")
    if [ "${pruned}" -gt 0 ]; then
        log "Pruned ${pruned} backup(s) beyond retention (${RETENTION_COUNT})"
    fi
}

if [ "${1:-}" = "--run" ]; then
    run_backup
    exit 0
fi

# ---- Install systemd service + timer ---------------------------------------
SERVICE_FILE="/etc/systemd/system/homeassistant-backup.service"
TIMER_FILE="/etc/systemd/system/homeassistant-backup.timer"

echo "==> Installing homeassistant-backup.service"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Back up Home Assistant config + recorder DB to local disk
After=docker.service

[Service]
Type=oneshot
ExecStart=${SCRIPT_PATH} --run
EOF

echo "==> Installing homeassistant-backup.timer (daily at 03:15, catches up if missed)"
sudo tee "${TIMER_FILE}" > /dev/null <<'EOF'
[Unit]
Description=Daily Home Assistant backup

[Timer]
OnCalendar=*-*-* 03:15:00
Persistent=true

[Install]
WantedBy=timers.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable homeassistant-backup.timer > /dev/null 2>&1
sudo systemctl restart homeassistant-backup.timer

echo ""
echo "==> Done. Check status any time with:"
echo "      sudo systemctl status homeassistant-backup.timer"
echo "      systemctl list-timers homeassistant-backup.timer"
echo "      journalctl -u homeassistant-backup.service"
echo ""
echo "    Run a backup right now with: ${SCRIPT_PATH} --run"
