#!/usr/bin/env bash
#
# setup-matter-server.sh
#
# Deploys python-matter-server via Docker Compose on the Wyse 5070 home
# automation server. This is the Matter *controller/commissioning* stack --
# it is separate from OTBR (setup-otbr.sh), which only provides Thread
# network connectivity. HA's Container install does not bundle Matter
# support, so this runs as its own container that HA's Matter integration
# connects to over a websocket.
#
# PREREQUISITE: Home Assistant must already be running (see
# setup-homeassistant.sh). After this script finishes, add the integration
# in HA: Settings -> Devices & Services -> Add Integration -> Matter, using
# ws://localhost:5580/ws (both containers use host networking, so
# "localhost" resolves correctly from HA's process to this container).
#
# BLE commissioning: WiFi-based Matter devices (like the MiniSplit bridge)
# are still commissioned over BLE -- the WiFi credentials are handed off via
# BLE before the device joins the network. This container needs access to
# the host's Bluetooth adapter via D-Bus/BlueZ for that to work. If this
# host has no Bluetooth hardware, on-network/QR commissioning of new devices
# won't be possible from this server (existing devices already on Matter
# fabrics are unaffected).
#
# Usage:
#   ./setup-matter-server.sh
#
# Re-running this script is safe: it recreates the container with current
# settings but does not touch persisted fabric/device data under
# APPDATA_ROOT.

set -euo pipefail

# ---- Config you may want to tweak -----------------------------------------
APPDATA_ROOT="/mnt/data/appdata/matter-server"
COMPOSE_DIR="${APPDATA_ROOT}"
CONTAINER_NAME="matter-server"
IMAGE="ghcr.io/matter-js/python-matter-server:stable"
WEBSOCKET_PORT="5580"
# -----------------------------------------------------------------------------

if ! command -v docker &> /dev/null; then
  echo "ERROR: docker not found. Is Docker installed on this host?" >&2
  exit 1
fi

echo "==> Checking for a Bluetooth adapter (needed for BLE commissioning)..."
if ! command -v bluetoothctl &> /dev/null && [ ! -S /run/dbus/system_bus_socket ]; then
  echo "!! No bluetoothd/D-Bus system socket found at /run/dbus/system_bus_socket."
  echo "   The container will still start, but BLE commissioning of new devices"
  echo "   will not work until Bluetooth is available on this host."
fi

echo "==> Creating appdata directory: ${APPDATA_ROOT}/data"
mkdir -p "${APPDATA_ROOT}/data"

echo "==> Writing docker-compose.yml to ${COMPOSE_DIR}..."
cat > "${COMPOSE_DIR}/docker-compose.yml" <<EOF
services:
  ${CONTAINER_NAME}:
    container_name: ${CONTAINER_NAME}
    image: ${IMAGE}
    restart: unless-stopped
    network_mode: host
    security_opt:
      - apparmor:unconfined
    # --bluetooth-adapter 0 is required for BLE commissioning -- mounting
    # /run/dbus alone is not enough. Without this flag the server starts
    # fine but self-reports "bluetooth_enabled": false over its websocket
    # API and silently refuses to do any BLE-based commissioning (Thread or
    # WiFi). Confirmed on real hardware: this was missing here even though
    # D-Bus/BlueZ were otherwise working correctly on the host.
    #
    # IMPORTANT: this "command:" REPLACES the image's default command
    # entirely rather than appending to it -- so --storage-path and
    # --paa-root-cert-dir must be repeated explicitly here too, or the
    # server silently stops using the /data volume mount below (falls back
    # to an ephemeral path inside the container and resets all state on
    # every recreate). Confirmed on real hardware: a first attempt with
    # just "--bluetooth-adapter 0" alone caused exactly that.
    command: --storage-path /data --paa-root-cert-dir /data/credentials --bluetooth-adapter 0
    volumes:
      - ${APPDATA_ROOT}/data:/data
      - /run/dbus:/run/dbus:ro
      - /etc/localtime:/etc/localtime:ro
EOF

echo "==> Starting Matter Server via docker compose"
cd "${COMPOSE_DIR}"
docker compose pull
docker compose up -d --force-recreate

echo "==> Waiting for Matter Server to initialize..."
sleep 6

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 20

echo ""
echo "==> Done."
echo "    Container: ${CONTAINER_NAME}"
echo "    Data dir:  ${APPDATA_ROOT}/data"
echo "    Websocket: ws://$(hostname -I | awk '{print $1}'):${WEBSOCKET_PORT}/ws"
echo ""
echo "    Next step: in Home Assistant, go to Settings -> Devices & Services"
echo "    -> Add Integration -> Matter, and point it at ws://localhost:5580/ws"
echo "    (HA and this container both use host networking, so localhost works"
echo "    even though they're separate containers)."
echo ""
echo "    Check status any time with:"
echo "      docker ps --filter name=${CONTAINER_NAME}"
echo "      docker logs ${CONTAINER_NAME} --tail 30"
