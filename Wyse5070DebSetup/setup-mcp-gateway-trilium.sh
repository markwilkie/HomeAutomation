#!/usr/bin/env bash
#
# setup-mcp-gateway-trilium.sh
# Deploys a persistent, network-reachable copy of triliumnext-mcp, for
# remote clients (e.g. Claude mobile, via Caddy) that can't spawn a local
# stdio process the way Claude Desktop does.
#
# Unlike the Microsoft To Do gateway, this one is purely additive: Trilium's
# ETAPI token is a static credential (no OAuth rotation), so running this
# always-on copy alongside Claude Desktop's existing local
# `npx triliumnext-mcp` config carries no token-contention risk. Desktop's
# config is untouched by this script.
#
# triliumnext-mcp itself (github.com/TriliumNext/triliumnext-mcp, npm package
# `triliumnext-mcp`) is stdio-only, same as the To Do server -- it's wrapped
# here with `supergateway` (github.com/supercorp-ai/supergateway) to expose
# it over Streamable HTTP.
#
# Network exposure: this container listens on 127.0.0.1 only (see the
# "127.0.0.1:" prefix on the port mapping below) -- it is NOT reachable from
# the LAN or WAN directly, only from Caddy running on this same host. Note
# supergateway's HTTP server mode has no built-in inbound auth of its own;
# Caddy is expected to be the thing enforcing access control in front of it.
#
# Reaches Trilium's ETAPI via this host's own LAN IP (192.168.15.30:8090),
# same as how Claude Desktop's existing config reaches it over Tailscale --
# just a different IP for a different network path to the same container.
#
# One-time manual prerequisite (secret, never committed to this repo):
#   Trilium Options -> ETAPI -> create a new ETAPI token (a fresh one,
#   distinct from whatever token Desktop's local config already uses is
#   fine but not required -- ETAPI tokens don't rotate, so reusing the same
#   one is also safe). Put it in CONFIG_DIR/.env as shown below, then run
#   this script:
#     ssh mwilkie@<host> mkdir -p /mnt/data/appdata/mcp-gateway-trilium/config
#     scp .env mwilkie@<host>:/mnt/data/appdata/mcp-gateway-trilium/config/
#
# Usage:
#   ./setup-mcp-gateway-trilium.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/mcp-gateway-trilium"
BUILD_DIR="${APPDATA_ROOT}/build"
CONFIG_DIR="${APPDATA_ROOT}/config"
CONTAINER_NAME="mcp-gateway-trilium"
LISTEN_PORT="8601"
TRILIUM_HOST_IP="192.168.15.30"
TRILIUM_PORT="8090"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${BUILD_DIR}" "${CONFIG_DIR}"

if [ ! -f "${CONFIG_DIR}/.env" ]; then
  echo "!! Missing ${CONFIG_DIR}/.env"
  echo "   Expected contents (see this script's header comment):"
  echo "     TRILIUM_API_URL=http://${TRILIUM_HOST_IP}:${TRILIUM_PORT}/etapi"
  echo "     TRILIUM_API_TOKEN=<your ETAPI token>"
  echo "     PERMISSIONS=READ;WRITE"
  echo "   Create it, then re-run."
  exit 1
fi

# ---- Dockerfile: triliumnext-mcp + supergateway on top ---------------------
echo "==> Writing ${BUILD_DIR}/Dockerfile"
tee "${BUILD_DIR}/Dockerfile" > /dev/null <<'EOF'
FROM node:22-alpine
RUN npm install -g triliumnext-mcp supergateway
ENTRYPOINT ["npx", "supergateway", \
  "--stdio", "triliumnext-mcp", \
  "--outputTransport", "streamableHttp", \
  "--stateful", \
  "--sessionTimeout", "3600000", \
  "--streamableHttpPath", "/mcp", \
  "--port", "8601"]
EOF

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    build: ${BUILD_DIR}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    env_file:
      - ${CONFIG_DIR}/.env
    ports:
      - "127.0.0.1:${LISTEN_PORT}:${LISTEN_PORT}"
EOF

echo "==> Building and starting ${CONTAINER_NAME}"
cd "${APPDATA_ROOT}"
docker compose up -d --build

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 20

echo ""
echo "==> Done."
echo "    Local-only endpoint: http://127.0.0.1:${LISTEN_PORT}/mcp"
echo "    (Not reachable from the LAN/WAN -- Caddy proxies to this from here.)"
