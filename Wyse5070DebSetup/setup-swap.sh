#!/usr/bin/env bash
#
# setup-swap.sh
#
# Found while investigating the root disk being 100% full: swap itself lives
# on its own dedicated partition (/dev/mmcblk0p3, ~800MB, carved out at OS
# install time) -- separate from the root filesystem, so it wasn't part of
# the disk-space problem (see setup-containerd-storage.sh for that). But
# `free -h` showed swap at 789Mi/789Mi used (essentially exhausted) on a host
# with only 3.7GB total RAM, which is a real, independent problem: this host
# runs otbr/matter-server/homeassistant/mosquitto/zigbee2mqtt (~867MB
# combined per `docker stats`) plus whatever dev tooling (VS Code Remote,
# Claude Code) is connected at the time (measured at ~1.2GB during this
# investigation -- more than all the production containers combined).
#
# Two independent reasons to move swap off the eMMC and onto the SATA drive:
#   1. Capacity: 800MB of swap is tiny for a host this loaded. A swapfile on
#      /mnt/data can be sized generously (this script uses 4GB) since there's
#      222GB free there.
#   2. Endurance: eMMC has meaningfully lower write endurance than a SATA
#      SSD/HDD. Swap under real memory pressure means sustained, repetitive
#      writes -- exactly the wear pattern eMMC handles worst. Moving that
#      I/O to the SATA drive protects the eMMC's remaining lifespan.
#
# This disables the old partition swap entirely (rather than running both)
# and replaces it with a single 4GB swapfile at /mnt/data/swapfile. The old
# partition is left alone (not repurposed/repartitioned) -- ~800MB of dead
# space on a 13GB eMMC disk that's otherwise idle isn't worth the risk of
# repartitioning to reclaim.
#
# Usage:
#   ./setup-swap.sh

set -euo pipefail

NEW_SWAPFILE="/mnt/data/swapfile"
NEW_SWAP_SIZE_MB=4096

echo "==> Current swap:"
free -h
cat /proc/swaps

echo ""
echo "==> Creating ${NEW_SWAP_SIZE_MB}MB swapfile at ${NEW_SWAPFILE}"
sudo fallocate -l "${NEW_SWAP_SIZE_MB}M" "${NEW_SWAPFILE}"
sudo chmod 600 "${NEW_SWAPFILE}"
sudo mkswap "${NEW_SWAPFILE}"

echo "==> Disabling old partition swap (/dev/mmcblk0p3)"
sudo swapoff -a

echo "==> Enabling new swapfile"
sudo swapon "${NEW_SWAPFILE}"

echo "==> Updating /etc/fstab: commenting out the old partition swap line, adding the new swapfile"
sudo sed -i '/UUID=.*none.*swap.*sw/ s/^/#/' /etc/fstab
if ! grep -qF "${NEW_SWAPFILE}" /etc/fstab; then
  echo "${NEW_SWAPFILE} none swap sw 0 0" | sudo tee -a /etc/fstab > /dev/null
fi

echo ""
echo "==> Swap now:"
free -h
cat /proc/swaps

echo ""
echo "==> Done. The old /dev/mmcblk0p3 partition is untouched (just no longer"
echo "    used as swap) -- fine to leave as-is."
