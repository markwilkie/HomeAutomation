#!/usr/bin/env bash
#
# setup-homeassistant.sh
#
# Deploys Home Assistant (Container install, NOT HAOS/Supervised) via Docker
# Compose on the Wyse 5070 home automation server.
#
# Persistent config lives on the SSD data drive, not the eMMC boot drive:
#   /mnt/data/appdata/homeassistant/config
#
# Networking: host mode. HA is bound directly to the host's network stack
# (no port mapping needed) so mDNS/SSDP-based auto-discovery works for
# HomeKit, Chromecast, Sonos, and similar integrations. This intentionally
# differs from the bridge+socat pattern used for OTBR, and from Mosquitto's
# explicit port mapping — host networking is what HA's own docs recommend,
# and discovery is expected to matter once real devices get migrated over.
#
# Unlike Mosquitto/OTBR, the official Home Assistant image runs as root
# inside the container, so there's no host-side UID chown step required
# for the bind-mounted config directory.
#
# Run this on the server itself (e.g. via SSH or VS Code Remote-SSH), not
# from this workstation.

set -euo pipefail

APPDATA_DIR="/mnt/data/appdata/homeassistant"
CONFIG_DIR="${APPDATA_DIR}/config"
HOST_IP="192.168.15.30"

if ! command -v docker &> /dev/null; then
  echo "ERROR: docker not found. Is Docker installed on this host?" >&2
  exit 1
fi

echo "==> Creating Home Assistant config directory on the SSD data drive..."
mkdir -p "${CONFIG_DIR}"

echo "==> Writing docker-compose.yml to ${APPDATA_DIR}..."
cat > "${APPDATA_DIR}/docker-compose.yml" <<EOF
services:
  homeassistant:
    container_name: homeassistant
    image: ghcr.io/home-assistant/home-assistant:stable
    volumes:
      - ${CONFIG_DIR}:/config
      - /etc/localtime:/etc/localtime:ro
    restart: unless-stopped
    network_mode: host
EOF

echo "==> Pulling image and starting Home Assistant..."
cd "${APPDATA_DIR}"
docker compose pull
docker compose up -d

echo ""
echo "==> Done."
echo "    Home Assistant takes 1-2 minutes to finish its first boot."
echo "    Web UI (once ready): http://${HOST_IP}:8123"
echo ""
echo "    Notes:"
echo "    - Host networking means there's no container port mapping to check;"
echo "      if the UI isn't reachable, verify the host's own firewall (if any)"
echo "      allows inbound tcp/8123."
echo "    - Timezone/location aren't set via env vars here - configure them"
echo "      during the HA onboarding wizard on first login."
echo "    - Zigbee2MQTT and Z-Wave JS UI will run as their own separate"
echo "      containers later, talking to HA through Mosquitto/MQTT and their"
echo "      own web UIs - this HA container does not need direct USB device"
echo "      passthrough for those, so it isn't run privileged."