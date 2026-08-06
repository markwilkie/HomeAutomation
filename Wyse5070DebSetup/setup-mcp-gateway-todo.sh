#!/usr/bin/env bash
#
# setup-mcp-gateway-todo.sh
# Deploys a persistent, network-reachable copy of the Microsoft To Do MCP
# server, for remote clients (e.g. Claude mobile, via Caddy) that can't spawn
# a local stdio process the way Claude Desktop does.
#
# This is a SEPARATE deployment from setup-microsoft-todo-mcp.sh, on purpose:
#
# - setup-microsoft-todo-mcp.sh: stdio, spawned fresh per Claude Desktop
#   session over SSH, using /mnt/data/appdata/microsoft-todo-mcp/config/.
# - This script: an always-on container wrapping the same upstream server
#   with `supergateway` (github.com/supercorp-ai/supergateway) to expose it
#   over Streamable HTTP, using its OWN config dir with its OWN OAuth grant.
#
# Why a separate OAuth grant instead of sharing tokens.json: Microsoft's
# refresh tokens rotate on use. Two independent long-running consumers
# (Desktop's per-session spawn and this always-on gateway) refreshing from
# the same tokens.json race and can invalidate each other's access token.
# Two separate logins avoid that entirely -- each gets its own independent
# refresh-token family that Azure AD tracks separately.
#
# Network exposure: this container listens on 127.0.0.1 only (see the
# "127.0.0.1:" prefix on the port mapping below) -- it is NOT reachable from
# the LAN or WAN directly, only from Caddy running on this same host. Note
# supergateway's HTTP server mode has no built-in inbound auth of its own;
# Caddy is expected to be the thing enforcing access control in front of it.
#
# One-time manual prerequisites (secrets, never committed to this repo):
#   1. Same Azure App Registration as setup-microsoft-todo-mcp.sh can be
#      reused (same CLIENT_ID/TENANT_ID) -- you're just minting a second,
#      independent token grant against it, not registering a new app.
#   2. On a machine with a browser (NOT this headless box): clone
#      https://github.com/jordanburke/microsoft-todo-mcp-server, put the
#      Azure app's CLIENT_ID/CLIENT_SECRET/TENANT_ID in a .env file, run
#      `pnpm run auth` and complete the browser login. This produces a
#      *second* tokens.json, independent from the one already used by
#      setup-microsoft-todo-mcp.sh.
#   3. Copy those two files here, then run this script:
#        scp .env tokens.json mwilkie@<host>:/mnt/data/appdata/mcp-gateway-todo/config/
#
# Usage:
#   ./setup-mcp-gateway-todo.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/mcp-gateway-todo"
APP_DIR="${APPDATA_ROOT}/app"
CONFIG_DIR="${APPDATA_ROOT}/config"
CONTAINER_NAME="mcp-gateway-todo"
REPO_URL="https://github.com/jordanburke/microsoft-todo-mcp-server.git"
LISTEN_PORT="8600"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${CONFIG_DIR}"

if [ -d "${APP_DIR}/.git" ]; then
  echo "==> ${APP_DIR} already cloned, pulling latest"
  git -C "${APP_DIR}" pull
else
  echo "==> Cloning ${REPO_URL}"
  git clone "${REPO_URL}" "${APP_DIR}"
fi

if [ ! -f "${CONFIG_DIR}/.env" ] || [ ! -f "${CONFIG_DIR}/tokens.json" ]; then
  echo "!! Missing ${CONFIG_DIR}/.env and/or ${CONFIG_DIR}/tokens.json"
  echo "   These are secrets and are never committed to this repo -- see the"
  echo "   prerequisites in this script's header comment (a SEPARATE OAuth"
  echo "   login from the one used by setup-microsoft-todo-mcp.sh), then"
  echo "   copy both files into ${CONFIG_DIR} and re-run."
  exit 1
fi

# ---- Dockerfile: upstream server + supergateway on top ---------------------
echo "==> Writing ${APP_DIR}/Dockerfile"
tee "${APP_DIR}/Dockerfile" > /dev/null <<'EOF'
FROM node:22-alpine
WORKDIR /app
RUN npm install -g pnpm supergateway
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
RUN pnpm install --frozen-lockfile
COPY . .
RUN pnpm run build
ENTRYPOINT ["npx", "supergateway", \
  "--stdio", "node dist/cli.js", \
  "--outputTransport", "streamableHttp", \
  "--stateful", \
  "--sessionTimeout", "3600000", \
  "--streamableHttpPath", "/mcp", \
  "--port", "8600"]
EOF

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    build: ${APP_DIR}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    env_file:
      - ${CONFIG_DIR}/.env
    environment:
      HOME: /data
    volumes:
      - ${CONFIG_DIR}:/data/.config/microsoft-todo-mcp
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
