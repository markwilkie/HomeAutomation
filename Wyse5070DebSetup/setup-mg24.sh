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
echo "==> Starting OTBR container"
docker run -d \
  --name "${CONTAINER_NAME}" \
  --restart unless-stopped \
  --network host \
  --privileged \
  -v "${SERIAL_DEVICE}:/dev/ttyUSB0" \
  -v "${APPDATA_ROOT}:/data" \
  -e BACKBONE_INTERFACE="${BACKBONE_INTERFACE}" \
  "${IMAGE}" \
  --radio-url spinel+hdlc+uart:///dev/ttyUSB0 \
  --backbone-ifname "${BACKBONE_INTERFACE}"

echo "==> Waiting for OTBR to initialize..."
sleep 6

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