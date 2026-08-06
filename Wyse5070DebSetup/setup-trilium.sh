#!/usr/bin/env bash
#
# setup-trilium.sh
# Idempotent deployment script for Trilium Notes (TriliumNext) via Docker
# Compose. Designed for wilkie-home-server (Debian 12, Docker data-root on
# /mnt/data/docker).
#
# Port 8090, not 8080: 8080 is already used by OTBR's web-forward service
# (see README.md's port table). 8090 was confirmed free against a live
# `ss -tln` on this host before picking it.
#
# TRILIUM_NO_UPLOAD_LIMIT=true: Trilium's default 250 MiB upload cap rejects
# large ENEX notebook exports (Evernote -> Trilium migration hit this with a
# 304 MiB notebook). Left on permanently since large attachments can recur.
#
# Data (document.db etc.) was migrated once from the Windows laptop this was
# originally set up on (~\evernote-migration\data\trilium) via rsync into
# APPDATA_ROOT/data before this script's first run. Re-running this script is
# safe: it only (re)creates the container and never touches that data.
#
# Usage:
#   ./setup-trilium.sh

set -euo pipefail

# ---- Config you may want to tweak -----------------------------------------
APPDATA_ROOT="/mnt/data/appdata/trilium"
CONTAINER_NAME="trilium"
IMAGE="triliumnext/notes:latest"
WEB_UI_PORT="8090"
# -----------------------------------------------------------------------------

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}/data"

# ---- docker-compose.yml ------------------------------------------------
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  trilium:
    image: ${IMAGE}
    container_name: ${CONTAINER_NAME}
    restart: unless-stopped
    environment:
      TRILIUM_ETAPI_ENABLED: "true"
      TRILIUM_NO_UPLOAD_LIMIT: "true"
    ports:
      - "${WEB_UI_PORT}:8080"
    volumes:
      - ${APPDATA_ROOT}/data:/home/node/trilium-data
EOF

# ---- Bring it up ---------------------------------------------------------
echo "==> Starting Trilium via docker compose"
cd "${APPDATA_ROOT}"
docker compose up -d --force-recreate

echo "==> Waiting a few seconds for Trilium to settle..."
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
echo "    Data:      ${APPDATA_ROOT}/data"
echo ""
echo "    Web UI (notes, ETAPI token under Options -> ETAPI):"
echo "      http://$(hostname -I | awk '{print $1}'):${WEB_UI_PORT}"
echo ""
echo "    Check status any time with:"
echo "      docker ps --filter name=${CONTAINER_NAME}"
echo "      docker logs ${CONTAINER_NAME} --tail 30"
