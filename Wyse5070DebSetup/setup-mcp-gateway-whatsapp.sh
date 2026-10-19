#!/usr/bin/env bash
#
# setup-mcp-gateway-whatsapp.sh
# Deploys Sealjay/mcp-whatsapp (github.com/Sealjay/mcp-whatsapp), a single Go
# binary wrapping whatsmeow, so the daily brief can summarize my personal
# WhatsApp chats and Claude chat can answer questions about them. It links
# to the account as a "Linked Device" (like WhatsApp Web) -- unofficial, so
# against WhatsApp's terms with a small ban risk; the brief only ever reads,
# never sends.
#
# Unlike the other gateways it's native Streamable HTTP (no supergateway),
# and it must run continuously: it only captures messages while `serve` is
# up (after downtime whatsmeow backfills within WhatsApp's own retention).
#
# Network exposure: host networking with the binary bound to 127.0.0.1, so
# not reachable from the LAN directly. The daily brief uses it on this
# host; claude.ai reaches it through Caddy at /whatsapp/<MCP_AUTH_TOKEN>/mcp
# (setup-caddy.sh), which forwards /mcp only -- the /pair page stays
# loopback-only. Host networking (rather than a 127.0.0.1 port mapping) is
# what lets it stay loopback-only without its -allow-remote + bearer-token
# mode, which claude.ai's connector form couldn't send anyway.
#
# Pinned to v0.6.0 (commit 078ac8c). To upgrade, change PINNED_REF and
# re-run.
#
# Pairing (first run, and again whenever WhatsApp rotates the session --
# roughly every 20 days; the brief says so when it's unpaired):
#   1. From the PC: ssh -L 8606:127.0.0.1:8606 mwilkie@192.168.15.30
#   2. Open http://127.0.0.1:8606/pair in a browser.
#   3. Phone: WhatsApp -> Settings -> Linked Devices -> Link a Device, scan.
#
# Usage:
#   ./setup-mcp-gateway-whatsapp.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/mcp-gateway-whatsapp"
BUILD_DIR="${APPDATA_ROOT}/build"
STORE_DIR="${APPDATA_ROOT}/store"
CONTAINER_NAME="mcp-gateway-whatsapp"
LISTEN_PORT="8606"
REPO_URL="https://github.com/Sealjay/mcp-whatsapp.git"
PINNED_REF="v0.6.0"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}" "${STORE_DIR}"
chmod 700 "${STORE_DIR}"

echo "==> Fetching ${REPO_URL} at ${PINNED_REF}"
rm -rf "${BUILD_DIR}"
git clone --quiet --depth 1 --branch "${PINNED_REF}" "${REPO_URL}" "${BUILD_DIR}"

# ---- docker-compose.yml ------------------------------------------------
# Runs as this host user (not the image's uid 10001) so the bind-mounted
# store stays owned by mwilkie, like the rest of /mnt/data/appdata.
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    build:
      context: ${BUILD_DIR}
      args:
        VERSION: ${PINNED_REF}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    user: "$(id -u):$(id -g)"
    network_mode: host
    volumes:
      - ${STORE_DIR}:/home/app/store
    command: ["serve", "-addr", "127.0.0.1:${LISTEN_PORT}"]
EOF

echo "==> Building and starting ${CONTAINER_NAME}"
cd "${APPDATA_ROOT}"
docker compose up -d --build

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
sleep 3
docker logs "${CONTAINER_NAME}" --tail 20

echo ""
echo "==> Done."
echo "    Local-only endpoint: http://127.0.0.1:${LISTEN_PORT}/mcp"
[ -s "${STORE_DIR}/whatsapp.db" ] || echo "    !! Not paired yet -- see Pairing in this script's header comment."
