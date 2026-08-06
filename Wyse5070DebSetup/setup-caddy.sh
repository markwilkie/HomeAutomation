#!/usr/bin/env bash
#
# setup-caddy.sh
# Deploys Caddy as the single internet-facing entry point on
# wilkie-home-server, terminating TLS (automatic Let's Encrypt via
# wilkiefamily.duckdns.org) and reverse-proxying by path to the MCP
# gateways deployed by setup-mcp-gateway-todo.sh / setup-mcp-gateway-trilium.sh.
#
# Runs via Docker (network_mode: host, matching this repo's convention for
# most services) rather than an OS package install -- this host's operator
# account doesn't have passwordless sudo, and host networking is required
# anyway so Caddy can reach the two gateways on 127.0.0.1:8600/8601 (which
# are deliberately bound to the host's loopback only, not reachable via
# container-to-container networking or a bridge).
#
# Auth: supergateway's HTTP server mode has no inbound authentication of
# its own, and both gateways would otherwise expose read/write access to
# personal notes (Trilium) and tasks (Microsoft To Do) to anyone who finds
# the URL. Caddy checks a static bearer token (generated below, stored only
# in .env, never committed) on both paths before proxying through.
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
DOMAIN="wilkefamily.duckdns.org"

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
	@todo path /todo/*
	handle @todo {
		@authorized header Authorization "Bearer {\$MCP_AUTH_TOKEN}"
		handle @authorized {
			uri strip_prefix /todo
			reverse_proxy 127.0.0.1:8600
		}
		handle {
			respond "Unauthorized" 401
		}
	}

	@trilium path /trilium/*
	handle @trilium {
		@authorized2 header Authorization "Bearer {\$MCP_AUTH_TOKEN}"
		handle @authorized2 {
			uri strip_prefix /trilium
			reverse_proxy 127.0.0.1:8601
		}
		handle {
			respond "Unauthorized" 401
		}
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
echo "==> Done."
echo "    https://${DOMAIN}/todo/mcp     -> mcp-gateway-todo (127.0.0.1:8600)"
echo "    https://${DOMAIN}/trilium/mcp  -> mcp-gateway-trilium (127.0.0.1:8601)"
echo "    Both require: Authorization: Bearer <token>"
echo "    Token is in ${ENV_FILE} (not committed, not printed to logs)."
echo ""
echo "    Requires ports 80 + 443 forwarded from pfSense to 192.168.15.30."
