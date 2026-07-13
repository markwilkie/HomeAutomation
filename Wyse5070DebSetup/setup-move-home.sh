#!/usr/bin/env bash
#
# setup-move-home.sh
#
# /home lives on the 13GB eMMC root disk (mmcblk0p2) alongside everything
# else -- it isn't its own mount. This host already hit 100% full once from
# Docker/containerd data (see setup-containerd-storage.sh); /home/mwilkie is
# a second, ongoing source of the same problem (~3.2GB and growing --
# ~/.vscode-server alone is 3GB of VS Code Remote server/extension cache).
# This moves /home onto the 234GB SATA drive (/mnt/data) via a bind mount, so
# future growth there doesn't threaten the root disk again.
#
# *** DO NOT RUN THIS FROM A VS CODE REMOTE / CLAUDE CODE SESSION. ***
# Both live under /home/mwilkie. The swap step below (renaming /home aside
# and bind-mounting the new location over it) needs nothing actively holding
# open files/sockets under /home/mwilkie by path at that moment -- an active
# remote session is exactly that. Close VS Code and any other terminals
# logged in as mwilkie, then run this from a fresh plain SSH session (a
# console/IPMI session is even safer, if available). Expect to reconnect
# fresh afterward.
#
# This script does NOT delete anything -- the original /home is renamed to
# /home.pre-migration, not removed. Delete it manually after confirming
# things are stable for a few days:
#   sudo rm -rf /home.pre-migration
#
# Usage:
#   ./setup-move-home.sh

set -euo pipefail

NEW_HOME_ROOT="/mnt/data/home"
OLD_HOME="/home"
BACKUP_HOME="/home.pre-migration"

echo "==> Current /home usage:"
du -sh "${OLD_HOME}"/* 2>/dev/null || true

echo ""
echo "==> Who else is logged in right now (verify this is JUST this session before continuing):"
who

echo ""
read -r -p "Confirm no other active sessions as any /home user, and you are NOT running this through VS Code Remote/Claude Code. Type YES to continue: " CONFIRM
if [ "${CONFIRM}" != "YES" ]; then
  echo "Aborted -- typed confirmation did not match 'YES'."
  exit 1
fi

echo ""
echo "==> First-pass copy (safe to run while things are still active -- just reads):"
sudo mkdir -p "${NEW_HOME_ROOT}"
sudo rsync -aHAX --info=progress2 "${OLD_HOME}/" "${NEW_HOME_ROOT}/"

echo ""
echo "==> Second-pass copy (catches anything that changed during the first pass):"
sudo rsync -aHAX --delete "${OLD_HOME}/" "${NEW_HOME_ROOT}/"

echo ""
echo "==> Swapping in the new location"
sudo mv "${OLD_HOME}" "${BACKUP_HOME}"
sudo mkdir "${OLD_HOME}"

echo "==> Adding bind mount to /etc/fstab"
if ! grep -qF "${NEW_HOME_ROOT}" /etc/fstab; then
  echo "${NEW_HOME_ROOT} ${OLD_HOME} none bind 0 0" | sudo tee -a /etc/fstab > /dev/null
fi

echo "==> Mounting"
sudo mount -a

echo ""
echo "==> Verification:"
df -h "${OLD_HOME}"
ls -la "${OLD_HOME}/mwilkie" | head -5

echo ""
echo "==> Done. /home now lives on the SATA drive via a bind mount."
echo "    Old copy kept at ${BACKUP_HOME} -- once you've reconnected and confirmed"
echo "    everything (SSH keys, VS Code Remote, this repo checkout) works normally"
echo "    for a few days, remove it:"
echo "      sudo rm -rf ${BACKUP_HOME}"
echo ""
echo "    You'll need to reconnect your SSH/VS Code session now -- this shell's own"
echo "    cwd was under the old /home, which no longer exists at that path."
