#!/usr/bin/env bash
#
# setup-matter-server.sh
#
# Deploys matterjs-server via Docker Compose on the Wyse 5070 home
# automation server. This is the Matter *controller/commissioning* stack --
# it is separate from OTBR (setup-otbr.sh), which only provides Thread
# network connectivity. HA's Container install does not bundle Matter
# support, so this runs as its own container that HA's Matter integration
# connects to over a websocket.
#
# python-matter-server (what this used to deploy) was archived by Home
# Assistant on 2026-06-23; version 8.1.2 (what we were running) is its final
# release and will never be updated again. HA replaced it with matterjs-server
# (a TypeScript/matter.js rewrite, shipped alongside HA Core 2026.7) as a
# same-WebSocket-API drop-in, specifically for "greater stability... fewer
# bugs, and faster start-up and recovery" -- directly targeting the same
# multi-hour secondary-endpoint staleness this project hit after otbr blips
# (see ARCHITECTURE.md / MiniSplit's HA-presentation notes). Migrated here on
# 2026-07-12; existing /data is reused and auto-migrated on first start.
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
# PERMISSIONS: unlike python-matter-server (which ran as root by default),
# matterjs-server runs as an unprivileged UID 1000 inside the container. Its
# /data files must be readable/writable by UID 1000 on the host, or the
# server will fail to read the migrated fabric/credentials data. This script
# chowns APPDATA_ROOT/data to 1000:1000 before starting -- needs sudo the
# first time it touches files still owned by the old root-run container.
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
IMAGE="ghcr.io/matter-js/matterjs-server:stable"
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

echo "==> Ensuring ${APPDATA_ROOT}/data is owned by UID 1000 (matterjs-server runs unprivileged)"
sudo chown -R 1000:1000 "${APPDATA_ROOT}/data"

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
    # BLUETOOTH_ADAPTER is what actually enables BLE support in matterjs-server
    # itself (matches python-matter-server's old --bluetooth-adapter flag
    # 1:1) -- without it, BLE stays off entirely regardless of NOBLE_BINDINGS.
    # NOBLE_BINDINGS=dbus separately tells the underlying `noble` BLE library
    # to talk to the adapter via BlueZ/D-Bus instead of a raw HCI socket
    # (which would otherwise require running this container as root).
    # Confirmed on real hardware: NOBLE_BINDINGS alone left the server
    # logging "BLE is disabled" / "BLE is not enabled on this platform" even
    # with D-Bus and a powered-on adapter working fine on the host.
    #
    # Unlike python-matter-server's old command override, STORAGE_PATH
    # already defaults to /data -- no need to repeat it here.
    environment:
      - NOBLE_BINDINGS=dbus
      - BLUETOOTH_ADAPTER=0
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
