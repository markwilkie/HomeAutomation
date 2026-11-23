#!/usr/bin/env bash
#
# setup-disk-usage-log.sh
# Logs root filesystem usage (used/avail/%) every 10 minutes, so "disk
# pressure over time" has a real answer instead of only a point-in-time df.
#
# Why a separate mechanism instead of sysstat: sysstat/sar tracks disk I/O
# activity (throughput, IOPS) but does NOT track filesystem space usage at
# all -- there's no sar option for "% used" the way there is for CPU/memory.
# This fills that specific gap with the same 10-minute cadence sysstat uses.
#
# Usage (needs sudo -- run interactively, not from an unattended script):
#   sudo bash ./setup-disk-usage-log.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Must run as root (sudo bash $0)" >&2
  exit 1
fi

SCRIPT="/usr/local/sbin/log-disk-usage.sh"
CRON_FILE="/etc/cron.d/disk-usage-log"
LOGROTATE_FILE="/etc/logrotate.d/disk-usage-log"
LOG_FILE="/var/log/disk-usage.log"

echo "==> Writing ${SCRIPT}"
tee "${SCRIPT}" > /dev/null <<'EOF'
#!/usr/bin/env bash
echo "$(date '+%Y-%m-%d %H:%M:%S') $(df -h / | awk 'NR==2 {print "used="$3, "avail="$4, "pct="$5}')" >> /var/log/disk-usage.log
EOF
chmod 755 "${SCRIPT}"

echo "==> Writing ${CRON_FILE}"
tee "${CRON_FILE}" > /dev/null <<EOF
# Logs root filesystem usage every 10 minutes -- see setup-disk-usage-log.sh
*/10 * * * * root ${SCRIPT}
EOF

echo "==> Writing ${LOGROTATE_FILE}"
tee "${LOGROTATE_FILE}" > /dev/null <<EOF
${LOG_FILE} {
    weekly
    rotate 8
    compress
    missingok
    notifempty
}
EOF

touch "${LOG_FILE}"

echo ""
echo "==> Done. First entry lands within 10 minutes. Check with:"
echo "      tail -f ${LOG_FILE}"
