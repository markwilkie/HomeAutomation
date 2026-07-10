#!/usr/bin/env bash
#
# setup-otbr.sh
# Idempotent deployment script for OpenThread Border Router (OTBR) via Docker,
# using the SONOFF Dongle Plus MG24 flashed with OpenThread RCP firmware.
# Designed for wilkie-home-server (Debian 12, Docker data-root on /mnt/data/docker).
#
# PREREQUISITE: the MG24 dongle must already be flashed with "OpenThread RCP"
# firmware via the SONOFF web flasher (https://dongle.sonoff.tech/sonoff-dongle-flasher/)
# BEFORE plugging it into this server. This script does not flash firmware.
#
# Usage:
#   ./setup-otbr.sh
#
# Re-running this script is safe: it will remove and recreate the otbr
# container and the web-forward service with the current settings, but does
# not touch your Thread network data (persisted under APPDATA_ROOT).

set -euo pipefail

# ---- Config you may want to tweak -----------------------------------------
APPDATA_ROOT="/mnt/data/appdata/otbr"
CONTAINER_NAME="otbr"
IMAGE="openthread/otbr:latest"
BACKBONE_INTERFACE="enp1s0"   # <-- your LAN interface; check with `ip a` if unsure
WEB_FORWARD_PORT="8080"        # externally reachable port for the OTBR web UI
SERIAL_BY_ID_GLOB="/dev/serial/by-id/usb-SONOFF_SONOFF_Dongle_Plus_MG24_*"
# -----------------------------------------------------------------------------

echo "==> Looking for the SONOFF MG24 dongle under /dev/serial/by-id/"
SERIAL_DEVICE=$(ls ${SERIAL_BY_ID_GLOB} 2>/dev/null | head -n1 || true)

if [ -z "${SERIAL_DEVICE}" ]; then
  echo "!! Could not find a device matching ${SERIAL_BY_ID_GLOB}"
  echo "   Is the dongle plugged in? Check with: ls /dev/serial/by-id/"
  exit 1
fi
echo "==> Found dongle at: ${SERIAL_DEVICE}"

echo "==> Creating appdata directory: ${APPDATA_ROOT}"
sudo mkdir -p "${APPDATA_ROOT}"

# ---- Kernel modules required for OTBR's firewall (ip6tables/ipset) --------
# Without these, otbr-agent fails at startup with:
#   "ip6tables v1.6.1: can't initialize ip6tables table `filter'"
echo "==> Ensuring required kernel modules are loaded (ip6table_filter, ip6_tables)"
sudo modprobe ip6table_filter || true
sudo modprobe ip6_tables || true

# Persist across reboots if not already listed
for mod in ip6table_filter ip6_tables; do
  if ! grep -qx "${mod}" /etc/modules 2>/dev/null; then
    echo "${mod}" | sudo tee -a /etc/modules > /dev/null
    echo "    Added ${mod} to /etc/modules for persistence"
  fi
done

# ---- Remove any existing container so this script is safely re-runnable ---
if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
  echo "==> Removing existing ${CONTAINER_NAME} container"
  docker rm -f "${CONTAINER_NAME}" > /dev/null
fi

# ---- Start OTBR -------------------------------------------------------
# NOTE on BACKBONE_INTERFACE: the default OTBR image env var is "eth0",
# which does not exist on this hardware (interface is named enp1s0 here).
# Both the -e env var AND the --backbone-ifname flag are set below since
# some image builds only honor one or the other reliably.
#
# NOTE on NAT64=1: without this, OTBR's own startup script (/app/script/_nat64,
# nat64_start()) silently no-ops instead of starting NAT44/iptables MASQUERADE
# for Thread-originated traffic -- no error, no log line, it just returns 0.
# Result: Thread devices can join the mesh fine (radio/MLE layer works) but
# can never reach any IPv4 internet host (NTP, cloud APIs, etc.) since there's
# no translation path out. Confirmed on real hardware: a Matter/Thread device
# joined the mesh successfully, then crashed on its first NTP time-sync
# attempt because this was never enabled since this script's original
# deployment -- the MASQUERADE rule existed (from otbr-firewall) but matched
# 0 packets, and no nat44 process was ever running.
#
# NOTE on INFRA_IF_NAME: this is a SEPARATE env var from BACKBONE_INTERFACE
# and is easy to miss. otbr-agent's own border-routing (RA/OMR prefix
# advertising) reads BACKBONE_INTERFACE (passed via --backbone-ifname below),
# but the _nat64/_nat44 shell script that builds the NAT44 iptables rules
# reads INFRA_IF_NAME instead (defaulting to "eth0" if unset -- baked into
# the image). Without this set to the real interface, the FORWARD-chain
# ACCEPT rules get bound to the literal, non-existent interface "eth0", so
# NAT64-translated packets are silently dropped by the default FORWARD
# policy -- MASQUERADE and the userspace NAT64 translator both work fine
# (a mapping gets created), but no reply ever comes back. Confirmed on real
# hardware: a Matter/Thread device's DNS query for pool.ntp.org (via the
# NAT64-synthesized 8.8.8.8 address) went unanswered for 60+ seconds even
# though NAT64=1 was set correctly and OTBR logged a translator mapping.
echo "==> Starting OTBR container"
# NOTE on the /var/lib/thread mount: this is where otbr-agent ACTUALLY
# persists the Thread dataset/keys -- NOT under /data. The /data mount below
# is otbr-agent's working directory but isn't where its dataset storage
# lives. Confirmed the hard way on real hardware: recreating this container
# without the /var/lib/thread mount silently lost the entire Thread network
# (dataset, keys, everything) even though /data looked correctly persisted.
mkdir -p "${APPDATA_ROOT}/thread"
docker run -d \
  --name "${CONTAINER_NAME}" \
  --restart unless-stopped \
  --network host \
  --privileged \
  -v "${SERIAL_DEVICE}:/dev/ttyUSB0" \
  -v "${APPDATA_ROOT}:/data" \
  -v "${APPDATA_ROOT}/thread:/var/lib/thread" \
  -e BACKBONE_INTERFACE="${BACKBONE_INTERFACE}" \
  -e INFRA_IF_NAME="${BACKBONE_INTERFACE}" \
  -e NAT64=1 \
  "${IMAGE}" \
  --radio-url spinel+hdlc+uart:///dev/ttyUSB0 \
  --backbone-ifname "${BACKBONE_INTERFACE}"

echo "==> Waiting for OTBR to initialize..."
sleep 6

# ---- NAT64: handled by setup-nat64-jool.sh, not OTBR's own translator -----
# otbr-agent's built-in userspace NAT64 translator (Nat64Translator) has an
# unresolved bug in this deployment: translated packets are correctly parsed
# and marked in netfilter's mangle PREROUTING chain (confirmed via matching
# packet counters), then vanish with zero trace anywhere else in the IP
# stack -- no forwarding-counter increase, no drop counter, no martian-source
# log (even with log_martians enabled), no conntrack entry. Ruled out on real
# hardware: rp_filter, accept_local, a "local" route for the translator's
# internal 192.168.255.0/24 pool, and policy routing via a dummy interface --
# none of it got translated packets to actually leave the host, even though
# `ot-ctl nat64 counters` reported them as successfully translated with zero
# errors. A synthetic packet sent via a normal socket (bypassing otbr-agent's
# tun-device injection path) with the exact same source address worked
# perfectly, proving the bug is specific to otbr-agent's translator/tun
# interaction, not the surrounding routing/NAT config.
#
# setup-nat64-jool.sh replaces it with Jool, a standard kernel-level NAT64
# module that translates via netfilter hooks instead of a tun device --
# confirmed working end-to-end on real hardware (DNS + NTP + HTTPS to
# Tuya's cloud API all succeeded). It disables OTBR's own translator via
# `ot-ctl nat64 disable` and manually re-advertises the identical prefix, so
# already-commissioned devices relying on it via DNS64 don't need to change.
echo "==> Configuring Jool as the NAT64 gateway (replaces OTBR's own translator)"
"$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/setup-nat64-jool.sh"

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 20

# ---- Web UI forward (OTBR binds its web UI to 127.0.0.1:80 only) ----------
# We use socat + a systemd service so the forward survives reboots.
echo ""
echo "==> Setting up persistent web UI port forward (127.0.0.1:80 -> 0.0.0.0:${WEB_FORWARD_PORT})"

if ! command -v socat &> /dev/null; then
  echo "==> Installing socat"
  sudo apt-get update -qq
  sudo apt-get install -y socat
fi

SERVICE_FILE="/etc/systemd/system/otbr-web-forward.service"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Forward port ${WEB_FORWARD_PORT} to OTBR web UI on localhost:80
After=docker.service
Requires=docker.service

[Service]
Type=simple
ExecStart=/usr/bin/socat TCP-LISTEN:${WEB_FORWARD_PORT},fork,reuseaddr TCP:127.0.0.1:80
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable otbr-web-forward > /dev/null 2>&1
sudo systemctl restart otbr-web-forward

echo ""
echo "==> Done."
echo "    OTBR container: ${CONTAINER_NAME}"
echo "    Serial device:  ${SERIAL_DEVICE}"
echo "    Backbone iface: ${BACKBONE_INTERFACE}"
echo "    Data dir:       ${APPDATA_ROOT}"
echo ""
echo "    Web UI:  http://$(hostname -I | awk '{print $1}'):${WEB_FORWARD_PORT}"
echo "    REST API (localhost only): http://127.0.0.1:8081"
echo ""
echo "    Check status any time with:"
echo "      docker ps --filter name=${CONTAINER_NAME}"
echo "      docker logs ${CONTAINER_NAME} --tail 30"
echo "      sudo systemctl status otbr-web-forward"