#!/usr/bin/env bash
#
# setup-containerd-storage.sh
#
# The Wyse 5070's root disk (mmcblk0p2, eMMC) is only 13GB and was found at
# 100% full. Docker's own `data-root` was already correctly pointed at
# /mnt/data/docker (the 234GB SATA drive) in /etc/docker/daemon.json -- but
# that only relocates Docker's own metadata/volumes. The actual image and
# container filesystem layers are managed by containerd (the runtime
# underneath Docker), which has its OWN separate config and was never
# migrated: `mount` showed every container's overlayfs layers still backed
# by /var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/... on the
# root disk. `docker system df` confirmed ~6GB of images+containers living
# there -- almost exactly the gap between the OS's own footprint and the
# disk being full. This is why /mnt/data/docker looked nearly empty despite
# genuinely being Docker's configured data-root: it was never wrong, it just
# isn't where the actual bulk data lives.
#
# This script moves containerd's `root` (image/container storage; NOT
# `state`, which is small runtime socket/pid data appropriate to leave under
# /run) to /mnt/data/containerd, and restarts containerd + docker so all
# existing containers keep running from the same data, just off the tiny
# eMMC disk.
#
# BLAST RADIUS: this stops docker.service (and therefore every container --
# otbr, matter-server, homeassistant, mosquitto, zigbee2mqtt) for the
# duration of the data copy. Expect a real outage window, not an instant
# blip -- size the expectation on however large /var/lib/containerd turns
# out to be (check with `sudo du -sh /var/lib/containerd` first; this repo
# saw ~6GB, which copies in well under a minute on the SATA drive, but
# confirm rather than assume).
#
# SAFETY: the old /var/lib/containerd is renamed (not deleted) to
# /var/lib/containerd.pre-migration once the new location is verified
# working, so there's a fallback if something doesn't come back up clean.
# Delete it manually once you've confirmed everything is stable.
#
# Usage:
#   ./setup-containerd-storage.sh

set -euo pipefail

NEW_CONTAINERD_ROOT="/mnt/data/containerd"
OLD_CONTAINERD_ROOT="/var/lib/containerd"
CONFIG_FILE="/etc/containerd/config.toml"

echo "==> Current containerd data size:"
sudo du -sh "${OLD_CONTAINERD_ROOT}"

echo ""
echo "==> Stopping docker (and containerd) -- every container goes down until this script finishes"
sudo systemctl stop docker.service docker.socket containerd.service

echo "==> Copying ${OLD_CONTAINERD_ROOT} -> ${NEW_CONTAINERD_ROOT} (preserving ownership/permissions/xattrs)"
sudo mkdir -p "${NEW_CONTAINERD_ROOT}"
sudo rsync -aHAX "${OLD_CONTAINERD_ROOT}/" "${NEW_CONTAINERD_ROOT}/"

echo "==> Pointing containerd at the new root in ${CONFIG_FILE}"
if grep -qE '^root\s*=' "${CONFIG_FILE}" 2>/dev/null; then
  sudo sed -i "s#^root\s*=.*#root = \"${NEW_CONTAINERD_ROOT}\"#" "${CONFIG_FILE}"
else
  # Default config.toml has no explicit, uncommented "root =" line (relies
  # on the built-in default). TOML keys belong to whichever [section] header
  # precedes them, so this must be PREPENDED, not appended -- appending
  # would land it under whatever section happens to be last in the file
  # instead of at the top level.
  # mktemp here must run AS ROOT (via sudo), not as the invoking user -- this
  # host appears to give sudo's root session a different view of /tmp (PAM
  # per-user namespacing), so a plain unprivileged mktemp produces a path
  # `sudo tee` then can't see/write to ("Permission denied" even though root
  # normally bypasses file permission checks entirely). Confirmed on real
  # hardware.
  TMP_CONFIG="$(sudo mktemp)"
  { echo "root = \"${NEW_CONTAINERD_ROOT}\""; cat "${CONFIG_FILE}"; } | sudo tee "${TMP_CONFIG}" > /dev/null
  # mktemp defaults to mode 600 -- restore the original config's normal,
  # world-readable permissions rather than leaving it root-only (containerd
  # itself doesn't care, but it's an unnecessary, surprising perms regression
  # for anyone -- including this script's own verification step below --
  # reading the file as a non-root user afterward).
  sudo chmod 644 "${TMP_CONFIG}"
  sudo mv "${TMP_CONFIG}" "${CONFIG_FILE}"
fi

echo "==> Starting containerd, then docker"
sudo systemctl start containerd.service
sudo systemctl start docker.socket docker.service

echo "==> Waiting for docker to come back up..."
for i in $(seq 1 30); do
  if docker info > /dev/null 2>&1; then
    break
  fi
  sleep 1
done

echo ""
echo "==> New containerd root (set in ${CONFIG_FILE}, not shown by 'docker info'):"
sudo grep '^root' "${CONFIG_FILE}"

echo ""
echo "==> Container status (all 5 should be Up):"
docker ps --format '{{.Names}}\t{{.Status}}'

echo ""
echo "==> Disk usage now:"
df -h / /mnt/data

echo ""
echo "==> If everything above looks healthy, retire the old copy once you're confident:"
echo "      sudo mv ${OLD_CONTAINERD_ROOT} ${OLD_CONTAINERD_ROOT}.pre-migration"
echo "    (kept as a fallback for now -- delete it manually after a few stable days)"
