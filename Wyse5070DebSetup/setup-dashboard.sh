#!/usr/bin/env bash
#
# setup-dashboard.sh
# Deploys a static "quick links" landing page for wilkie-home-server: one
# page with links to every browsable service on this host (Home Assistant,
# the three radio-dongle admin UIs, Trilium, TripTracker) plus links to
# this repo's top-level README/CLAUDE.md docs on GitHub.
#
# LAN/Tailscale only, by design -- this is an internal ops page, not a
# service an external client needs, so it is NOT added to Caddy's
# Caddyfile and is NOT reachable via wilkiefamily.duckdns.org. See
# CLAUDE.md: "Prefer Tailscale for anything that only needs private/device
# access; only expose publicly when an external service ... requires it."
#
# Port 8085: confirmed free against a live `ss -tln` on this host before
# picking it (see README.md's port table for what's already taken).
#
# The page itself (dashboard/index.html) lives in this repo and is bind-
# mounted read-only into a plain nginx container -- no build step, no
# backend. To update it: edit dashboard/index.html, commit, then re-run
# this script (it re-syncs the file to the host and recreates the
# container; nginx serves the new file immediately, no rebuild needed).
#
# Usage:
#   ./setup-dashboard.sh

set -euo pipefail

# ---- Config you may want to tweak -----------------------------------------
APPDATA_ROOT="/mnt/data/appdata/dashboard"
CONTAINER_NAME="dashboard"
IMAGE="nginx:1.27-alpine"
WEB_UI_PORT="8085"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_HTML="${SCRIPT_DIR}/dashboard/index.html"
# -----------------------------------------------------------------------------

if [ ! -f "${LOCAL_HTML}" ]; then
  echo "ERROR: ${LOCAL_HTML} not found." >&2
  exit 1
fi

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}/html"

echo "==> Copying dashboard/index.html into ${APPDATA_ROOT}/html/"
cp "${LOCAL_HTML}" "${APPDATA_ROOT}/html/index.html"

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  ${CONTAINER_NAME}:
    image: ${IMAGE}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    ports:
      - "${WEB_UI_PORT}:80"
    volumes:
      - ${APPDATA_ROOT}/html:/usr/share/nginx/html:ro
EOF

# ---- Bring it up ---------------------------------------------------------
echo "==> Starting dashboard via docker compose"
cd "${APPDATA_ROOT}"
docker compose up -d --force-recreate

echo "==> Waiting a couple seconds for nginx to settle..."
sleep 2

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 20

echo ""
echo "==> Done."
echo "    Container: ${CONTAINER_NAME}"
echo "    Page:      ${APPDATA_ROOT}/html/index.html"
echo ""
echo "    http://$(hostname -I | awk '{print $1}'):${WEB_UI_PORT}"
echo ""
echo "    Check status any time with:"
echo "      docker ps --filter name=${CONTAINER_NAME}"
echo "      docker logs ${CONTAINER_NAME} --tail 30"
