#!/usr/bin/env bash
#
# setup-stale-process-cleanup.sh
# Deploys cleanup_stale_processes.sh (see its header for exactly what it
# kills) and schedules it daily at 4:00am in this user's crontab. No sudo
# needed: it only ever touches this user's own processes.
#
# Re-running is safe: it re-copies the script and replaces its own crontab
# line rather than adding a second one.
#
# Usage:
#   ./setup-stale-process-cleanup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="/mnt/data/appdata/stale-process-cleanup"
LOG="\$HOME/stale-process-cleanup.log"
CRON_LINE="0 4 * * * ${APP_DIR}/cleanup_stale_processes.sh >> ${LOG} 2>&1"

echo "==> Copying cleanup_stale_processes.sh into ${APP_DIR}"
mkdir -p "${APP_DIR}"
cp "${SCRIPT_DIR}/cleanup_stale_processes.sh" "${APP_DIR}/"
chmod +x "${APP_DIR}/cleanup_stale_processes.sh"

echo "==> Scheduling it daily at 4:00am"
CRON_COMMENT="# stale-process-cleanup: kills orphaned ad-hoc loops/watchers older than 24h (setup-stale-process-cleanup.sh)"
{ crontab -l 2>/dev/null | grep -v -e "cleanup_stale_processes.sh" -e "^# stale-process-cleanup:" || true
  echo "${CRON_COMMENT}"
  echo "${CRON_LINE}"
} | crontab -
crontab -l | grep -A1 "^# stale-process-cleanup:"

echo ""
echo "==> Dry run (what it would kill right now):"
"${APP_DIR}/cleanup_stale_processes.sh" --dry-run
