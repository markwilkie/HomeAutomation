#!/usr/bin/env bash
#
# setup-caddy.sh
# Deploys Caddy as the single internet-facing entry point on
# wilkie-home-server, terminating TLS (automatic Let's Encrypt via
# wilkiefamily.duckdns.org) and reverse-proxying by path to the MCP
# gateways deployed by setup-mcp-gateway-todo.sh / setup-mcp-gateway-trilium.sh
# / setup-mcp-gateway-monarch.sh.
#
# Runs via Docker (network_mode: host, matching this repo's convention for
# most services) rather than an OS package install -- this host's operator
# account doesn't have passwordless sudo, and host networking is required
# anyway so Caddy can reach the three gateways on 127.0.0.1:8600/8601/8602
# (which are deliberately bound to the host's loopback only, not reachable
# via container-to-container networking or a bridge).
#
# Auth: supergateway's HTTP server mode has no inbound authentication of
# its own, and these gateways would otherwise expose read/write access to
# personal notes (Trilium), tasks (Microsoft To Do), and financial data
# (Monarch Money) to anyone who finds the URL. A static token (generated
# below, stored only in .env, never committed) gates every path -- embedded
# as a URL path segment rather than a header, because Claude's
# custom-connector UI only exposes a single
# "Remote MCP server URL" field (plus optional OAuth client ID/secret) --
# there's nowhere to attach a custom header from that form. Same trade-off
# as an iCal share link or webhook URL: still TLS-encrypted in transit, but
# more likely than a header to end up in a log line or browser history
# somewhere along the way.
#
# Ports 80 and 443 must be forwarded from pfSense to this host's IP
# (192.168.15.30) -- see the repo README / pfSense port-forward rules.
# Port 80 is required for Let's Encrypt's HTTP-01 renewal challenge, not
# just 443.
#
# Usage:
#   ./setup-caddy.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/caddy"
CONTAINER_NAME="caddy"
DOMAIN="wilkiefamily.duckdns.org"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}/data" "${APPDATA_ROOT}/config"

# ---- Auth token: generate once, keep stable across re-runs -----------------
ENV_FILE="${APPDATA_ROOT}/.env"
if [ ! -f "${ENV_FILE}" ]; then
  echo "==> Generating MCP_AUTH_TOKEN (first run only)"
  echo "MCP_AUTH_TOKEN=$(openssl rand -hex 32)" > "${ENV_FILE}"
  chmod 600 "${ENV_FILE}"
else
  echo "==> Reusing existing MCP_AUTH_TOKEN from ${ENV_FILE}"
fi

# ---- Caddyfile --------------------------------------------------------
CADDYFILE="${APPDATA_ROOT}/Caddyfile"
echo "==> Writing ${CADDYFILE}"
tee "${CADDYFILE}" > /dev/null <<EOF
${DOMAIN} {
	handle_path /todo/{\$MCP_AUTH_TOKEN}/* {
		reverse_proxy 127.0.0.1:8600
	}

	handle_path /trilium/{\$MCP_AUTH_TOKEN}/* {
		reverse_proxy 127.0.0.1:8601
	}

	handle_path /monarch/{\$MCP_AUTH_TOKEN}/* {
		reverse_proxy 127.0.0.1:8602
	}

	handle {
		respond "Not found" 404
	}
}
EOF

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    image: caddy:2-alpine
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    network_mode: host
    env_file:
      - ${ENV_FILE}
    volumes:
      - ${CADDYFILE}:/etc/caddy/Caddyfile:ro
      - ${APPDATA_ROOT}/data:/data
      - ${APPDATA_ROOT}/config:/config
EOF

echo "==> Starting Caddy"
cd "${APPDATA_ROOT}"
docker compose up -d --force-recreate

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 30

echo ""
TOKEN=$(grep MCP_AUTH_TOKEN "${ENV_FILE}" | cut -d= -f2)
echo "==> Done."
echo "    https://${DOMAIN}/todo/${TOKEN}/mcp"
echo "    https://${DOMAIN}/trilium/${TOKEN}/mcp"
echo "    https://${DOMAIN}/monarch/${TOKEN}/mcp"
echo ""
echo "    Requires ports 80 + 443 forwarded from pfSense to 192.168.15.30."
