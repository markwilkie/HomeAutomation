#!/usr/bin/env bash
#
# setup-mcp-gateway-monarch.sh
# Deploys a persistent, network-reachable Monarch Money MCP server, for
# remote clients (e.g. Claude mobile, via Caddy) that can't spawn a local
# stdio process the way Claude Desktop does.
#
# Upstream: robcerda/monarch-mcp-server (github.com/robcerda/monarch-mcp-server)
# -- Python, stdio-only, not published to PyPI (git clone + `pip install .`
# in the Dockerfile, same pattern as setup-mcp-gateway-todo.sh's clone of
# microsoft-todo-mcp-server). Chosen over the several other Monarch MCP
# forks out there for being by far the most active/used (295 stars / 123
# forks / pushed within the last two months, vs. low double-digit stars for
# the alternatives). Wrapped here with `supergateway`
# (github.com/supercorp-ai/supergateway) to expose it over Streamable HTTP,
# same as the other two gateways.
#
# Auth is unlike the other two gateways: there's no email/password/MFA env
# var story here (robcerda's server doesn't support one -- see its
# login_setup.py). Auth is a session token, normally persisted to the OS
# keyring, with an automatic fallback to a plaintext file
# (~/.monarch-mcp-server/token) when no keyring backend is present. A
# container has no keyring backend, so that fallback is exactly what we
# want -- but it means the *interactive* one-time login has to happen
# inside a container too (this image, specifically: it needs the
# >=3.12,<3.14 Python this package requires, which Debian 12 -- this host's
# OS -- doesn't ship by default; and running it on a machine that DOES have
# a real keyring, like a Mac or Windows box, would save the token there
# instead of to the file this gateway needs).
#
# One-time manual prerequisite (secret, never committed to this repo):
#   After this script's first run builds the image (it will refuse to
#   start the gateway and stop here if no token is present yet), run the
#   login setup INTERACTIVELY, from an SSH session on this host, reusing
#   the same image/volumes already defined in the compose file:
#     cd /mnt/data/appdata/mcp-gateway-monarch
#     docker compose run --rm --entrypoint python mcp-gateway-monarch login_setup.py
#   Recommended: the browser-cookie method (log into app.monarch.com in any
#   browser, DevTools -> Network -> copy the `Cookie:` header off any XHR
#   request to app.monarch.com, paste when prompted) -- it sidesteps
#   Monarch's Cloudflare CAPTCHA gate on programmatic email/password login
#   and works for SSO accounts. Email/password + MFA works too if you'd
#   rather not fish a cookie out of DevTools.
#   This saves the session token straight to
#   /mnt/data/appdata/mcp-gateway-monarch/config/token (mounted as
#   /root/.monarch-mcp-server inside the container). Re-run this script
#   afterward to start the gateway for real.
#
#   Monarch sessions are long-lived but not eternal -- if the gateway
#   starts failing tool calls with an auth error, re-run the same
#   `docker compose run --rm --entrypoint python ...` command above to
#   refresh the token, no need to touch this script.
#
# Network exposure: this container listens on 127.0.0.1 only (see the
# "127.0.0.1:" prefix on the port mapping below) -- it is NOT reachable from
# the LAN or WAN directly, only from Caddy running on this same host. Note
# supergateway's HTTP server mode has no built-in inbound auth of its own;
# Caddy is expected to be the thing enforcing access control in front of it.
#
# Usage:
#   ./setup-mcp-gateway-monarch.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/mcp-gateway-monarch"
APP_DIR="${APPDATA_ROOT}/app"
CONFIG_DIR="${APPDATA_ROOT}/config"
CONTAINER_NAME="mcp-gateway-monarch"
REPO_URL="https://github.com/robcerda/monarch-mcp-server.git"
LISTEN_PORT="8602"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${CONFIG_DIR}"

# The upstream server's secure_session module rmdir()s the token directory
# whenever it's empty, including at the start of every login_setup.py run
# (via delete_token(), to clear any stale session first). Since this
# directory is a bind mount here, rmdir()ing it hits EBUSY (can't rmdir an
# active mount point) instead of the silent no-op it'd be on a normal
# empty directory, and login_setup.py dies immediately with "Device or
# resource busy" before it even asks how you want to log in. A placeholder
# file keeps the directory permanently non-empty so that branch never
# fires.
touch "${CONFIG_DIR}/.keep"

if [ -d "${APP_DIR}/.git" ]; then
  echo "==> ${APP_DIR} already cloned, pulling latest"
  git -C "${APP_DIR}" pull
else
  echo "==> Cloning ${REPO_URL}"
  git clone "${REPO_URL}" "${APP_DIR}"
fi

# ---- Dockerfile: upstream server (Python) + supergateway (Node) on top -----
echo "==> Writing ${APP_DIR}/Dockerfile"
tee "${APP_DIR}/Dockerfile" > /dev/null <<'EOF'
FROM python:3.12-slim
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends \
      git curl ca-certificates gnupg \
    && curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*
RUN npm install -g supergateway
COPY . .
# monarch-mcp-server's pyproject.toml declares mcp[cli]>=1.10.0 with no
# upper bound. mcp 2.0.0 (a breaking rewrite -- drops mcp.server.fastmcp,
# which this server imports) exists on PyPI now, so an unconstrained
# install grabs it and the server fails at startup with
# "ModuleNotFoundError: No module named 'mcp.server.fastmcp'". Pin below
# 2.0 first so the later install of "." is satisfied by this version and
# leaves it alone.
RUN pip install --no-cache-dir "mcp[cli]<2" \
    && pip install --no-cache-dir .
ENTRYPOINT ["npx", "supergateway", \
  "--stdio", "monarch-mcp-server", \
  "--outputTransport", "streamableHttp", \
  "--stateful", \
  "--sessionTimeout", "3600000", \
  "--streamableHttpPath", "/mcp", \
  "--port", "8602"]
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
    volumes:
      - ${CONFIG_DIR}:/root/.monarch-mcp-server
    ports:
      - "127.0.0.1:${LISTEN_PORT}:${LISTEN_PORT}"
EOF

echo "==> Building ${CONTAINER_NAME} image"
cd "${APPDATA_ROOT}"
docker compose build

if [ ! -f "${CONFIG_DIR}/token" ]; then
  echo ""
  echo "!! No session token found at ${CONFIG_DIR}/token"
  echo "   Run the one-time interactive login now (see this script's header"
  echo "   comment for details):"
  echo "     cd ${APPDATA_ROOT} && docker compose run --rm --entrypoint python ${CONTAINER_NAME} login_setup.py"
  echo "   Then re-run this script to start the gateway."
  exit 1
fi

echo "==> Starting ${CONTAINER_NAME}"
docker compose up -d

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
