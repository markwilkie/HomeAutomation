#!/usr/bin/env bash
#
# setup-zwave-js-ui.sh
# Idempotent deployment script for Z-Wave JS UI via Docker Compose, using the
# Aeotec Z-Stick Gen5 (ZW090-A, Sigma Designs 0658:0200).
# Designed for wilkie-home-server (Debian 12, Docker data-root on /mnt/data/docker).
#
# Home Assistant integration: unlike Zigbee2MQTT, Z-Wave devices are NOT
# bridged into HA over MQTT here. HA's official "Z-Wave JS" integration talks
# directly to the zwave-js-server websocket this container exposes (port
# 3000) via its own config flow (Settings -> Devices & Services -> Add
# Integration -> Z-Wave JS -> ws://<this host>:3000). No MQTT broker
# involved for this device.
#
# Usage:
#   ./setup-zwave-js-ui.sh
#
# Re-running this script is safe: it will recreate the zwave-js-ui container
# with current settings, but does not touch the persisted Z-Wave network
# store (pairing data, node info) under APPDATA_ROOT/store.

set -euo pipefail

# ---- Config you may want to tweak -----------------------------------------
APPDATA_ROOT="/mnt/data/appdata/zwavejsui"
CONTAINER_NAME="zwavejsui"
IMAGE="zwavejs/zwave-js-ui:latest"
ZWAVE_JS_SERVER_PORT="3000"   # HA's Z-Wave JS integration connects here
WEB_UI_PORT="8091"
SERIAL_BY_ID_GLOB="/dev/serial/by-id/usb-0658_0200-if00*"
# -----------------------------------------------------------------------------

echo "==> Looking for the Aeotec Z-Stick under /dev/serial/by-id/"
SERIAL_DEVICE=$(ls ${SERIAL_BY_ID_GLOB} 2>/dev/null | head -n1 || true)

if [ -z "${SERIAL_DEVICE}" ]; then
  echo "!! Could not find a device matching ${SERIAL_BY_ID_GLOB}"
  echo "   Is the Z-Stick plugged in? Check with: ls /dev/serial/by-id/"
  exit 1
fi
echo "==> Found Z-Stick at: ${SERIAL_DEVICE}"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}/store"

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  zwavejsui:
    image: ${IMAGE}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    network_mode: host
    tty: true
    stop_signal: SIGINT
    environment:
      - SESSION_SECRET=$(openssl rand -hex 32)
      - ZWAVEJS_EXTERNAL_CONFIG=/usr/src/app/store/.config-db
    volumes:
      - ${APPDATA_ROOT}/store:/usr/src/app/store
      - /etc/localtime:/etc/localtime:ro
    devices:
      - ${SERIAL_DEVICE}:/dev/zwave
EOF

# ---- Bring it up ---------------------------------------------------------
echo "==> Starting Z-Wave JS UI via docker compose"
cd "${APPDATA_ROOT}"
docker compose up -d --force-recreate

echo "==> Waiting a few seconds for Z-Wave JS UI to settle..."
sleep 6

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 30

echo ""
echo "==> Done."
echo "    Container:     ${CONTAINER_NAME}"
echo "    Serial device: ${SERIAL_DEVICE}"
echo "    Data store:    ${APPDATA_ROOT}/store"
echo ""
echo "    Web UI (control panel, pairing, logs):"
echo "      http://$(hostname -I | awk '{print $1}'):${WEB_UI_PORT}"
echo ""
echo "    First-time setup in the web UI:"
echo "      Settings -> Z-Wave -> Serial Port -> /dev/zwave -> Save"
echo "      (the container also enables the zwave-js-server on port ${ZWAVE_JS_SERVER_PORT}"
echo "      by default, which is what Home Assistant connects to)"
echo ""
echo "    Then in Home Assistant:"
echo "      Settings -> Devices & Services -> Add Integration -> \"Z-Wave JS\""
echo "      URL: ws://$(hostname -I | awk '{print $1}'):${ZWAVE_JS_SERVER_PORT}"
echo ""
echo "    Check status any time with:"
echo "      docker ps --filter name=${CONTAINER_NAME}"
echo "      docker logs ${CONTAINER_NAME} --tail 30"
