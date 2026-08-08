#!/usr/bin/env bash
#
# setup-mg24-production.sh
# Deploys OTBR via the OFFICIAL PRODUCTION image (openthread/border-router),
# replacing openthread/otbr:*, which is explicitly the OpenThread project's
# own TEST image (built from etc/docker/test/Dockerfile in ot-br-posix) --
# no versioned releases, continuously rebuilt off `main`. Migrated 2026-08-08
# after tracing repeated otbr-agent watchdog recoveries to that instability
# and confirming openthread/border-router publishes actual versioned tags
# (v2026.07.0 etc.) intended "for real-world deployments."
#
# THIS IS NOT A DROP-IN IMAGE SWAP for setup-mg24.sh -- the production image
# uses a completely different init system (s6-overlay's /init, not a shell
# entrypoint script) and a different config surface:
#   - No --radio-url / --backbone-ifname CLI args (entrypoint ignores them)
#   - OT_RCP_DEVICE replaces the --radio-url arg
#   - OT_INFRA_IF replaces BACKBONE_INTERFACE / INFRA_IF_NAME
#   - Only a single /data volume is needed -- the image's own otbr-agent
#     service script does `mkdir -p /data/thread && ln -sft /var/lib
#     /data/thread` at startup. Do NOT also bind-mount something at
#     /var/lib/thread like setup-mg24.sh does: that `ln -f` cannot unlink an
#     existing bind mount, so a second explicit mount there breaks startup.
#     Mounting only APPDATA_ROOT:/data is sufficient AND correctly picks up
#     the existing Thread dataset, since APPDATA_ROOT/thread (where the old
#     dual-mount setup actually stored it) lands at /data/thread here too --
#     no data migration step needed, just point at the same APPDATA_ROOT.
#   - otbr-web's default listen port is 8080, not 80 (the old image's
#     default) -- which collides with the external-facing socat forward also
#     using 8080 (0.0.0.0:8080 claims the loopback address too, so otbr-web's
#     own bind to 127.0.0.1:8080 fails with EADDRINUSE). Confirmed on real
#     hardware 2026-08-08: otbr-web crash-looped with "Address already in
#     use (errno 98)" until OT_WEB_LISTEN_PORT was moved to 8082 (below) and
#     the socat target updated to match, keeping the external-facing port at
#     8080 for continuity with the old bookmarked URL.
#     REST API default port (8081) is unchanged, so nothing else downstream
#     (Jool prefix discovery, the watchdog, matter-server if it talks to the
#     REST API) needs to change.
#   - This image's otbr-agent has its own NAT64 translator Active by default
#     too (confirmed via `ot-ctl nat64 state` immediately after first start
#     on 2026-08-08) -- setup-nat64-jool.sh must be re-run after this script
#     every time, exactly as with the old image.
#   - This image's otbr-web startup unconditionally runs its own
#     ip6tables/iptables forwarding setup (no NAT64=1-style opt-in toggle
#     visible) -- verify with `ot-ctl nat64 state` after startup whether
#     OTBR's own translator is enabled by default here too and needs the
#     same setup-nat64-jool.sh override as the old image did. Don't assume;
#     check.
#
# CONTAINER_NAME is kept as "otbr" (same as setup-mg24.sh) since
# setup-nat64-jool.sh and setup-otbr-watchdog.sh both hardcode that name and
# were not modified by this migration -- they should keep working unchanged
# against this image as long as `ot-ctl state` / `ot-ctl nat64 state` behave
# the same, which must be verified live (see rollback note below).
#
# ROLLBACK: setup-mg24.sh itself was left completely unmodified by this
# migration. To roll back to the test image at any time:
#   IMAGE=openthread/otbr:pre-thread14-20260721 ./setup-mg24.sh
# This recreates the "otbr" container from the known-good pinned test image
# against the same APPDATA_ROOT (Thread dataset format is unaffected by
# which image reads it, so no data migration is needed in either
# direction). A pre-migration data snapshot also exists at
# /mnt/data/appdata/otbr-backup-20260808-124935 if the data dir itself ever
# needs restoring (should not normally be necessary -- the image swap does
# not touch on-disk dataset format).
#
# Usage:
#   ./setup-mg24-production.sh
#   IMAGE=openthread/border-router:v2026.07.0 ./setup-mg24-production.sh   # (default)

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/otbr"
CONTAINER_NAME="otbr"
IMAGE="${IMAGE:-openthread/border-router:v2026.07.0}"
BACKBONE_INTERFACE="enp1s0"
WEB_FORWARD_PORT="8080"
SERIAL_BY_ID_GLOB="/dev/serial/by-id/usb-SONOFF_SONOFF_Dongle_Plus_MG24_*"

echo "==> Looking for the SONOFF MG24 dongle under /dev/serial/by-id/"
SERIAL_DEVICE=$(ls ${SERIAL_BY_ID_GLOB} 2>/dev/null | head -n1 || true)

if [ -z "${SERIAL_DEVICE}" ]; then
  echo "!! Could not find a device matching ${SERIAL_BY_ID_GLOB}"
  echo "   Is the dongle plugged in? Check with: ls /dev/serial/by-id/"
  exit 1
fi
echo "==> Found dongle at: ${SERIAL_DEVICE}"

echo "==> Ensuring required kernel modules are loaded (ip6table_filter, ip6_tables)"
sudo modprobe ip6table_filter || true
sudo modprobe ip6_tables || true

if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
  echo "==> Removing existing ${CONTAINER_NAME} container"
  docker rm -f "${CONTAINER_NAME}" > /dev/null
fi

echo "==> Starting OTBR (production image: ${IMAGE})"
docker run -d \
  --name "${CONTAINER_NAME}" \
  --restart unless-stopped \
  --network host \
  --privileged \
  -v "${SERIAL_DEVICE}:/dev/ttyUSB0" \
  -v "${APPDATA_ROOT}:/data" \
  -e OT_RCP_DEVICE="spinel+hdlc+uart:///dev/ttyUSB0" \
  -e OT_INFRA_IF="${BACKBONE_INTERFACE}" \
  -e OT_WEB_LISTEN_PORT="8082" \
  "${IMAGE}"

echo "==> Waiting for OTBR to initialize..."
sleep 8

echo ""
echo "==> Container status:"
docker ps --filter "name=${CONTAINER_NAME}"

echo ""
echo "==> Recent logs:"
docker logs "${CONTAINER_NAME}" --tail 40

echo ""
echo "==> ot-ctl state:"
docker exec "${CONTAINER_NAME}" timeout 5 ot-ctl state 2>&1 || echo "!! ot-ctl state failed"

echo ""
echo "==> ot-ctl nat64 state (OTBR's own translator is Active by default on this image too):"
docker exec "${CONTAINER_NAME}" timeout 5 ot-ctl nat64 state 2>&1 || echo "!! ot-ctl nat64 state failed"

echo ""
echo "==> Reapplying the Jool NAT64 override (disables OTBR's own translator)"
"$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/setup-nat64-jool.sh"

echo ""
echo "==> Setting up persistent web UI port forward (0.0.0.0:${WEB_FORWARD_PORT} -> 127.0.0.1:8082)"
if ! command -v socat &> /dev/null; then
  sudo apt-get update -qq
  sudo apt-get install -y socat
fi

SERVICE_FILE="/etc/systemd/system/otbr-web-forward.service"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Forward port ${WEB_FORWARD_PORT} to OTBR web UI on localhost:8082
After=docker.service
Requires=docker.service

[Service]
Type=simple
ExecStart=/usr/bin/socat TCP-LISTEN:${WEB_FORWARD_PORT},fork,reuseaddr TCP:127.0.0.1:8082
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable otbr-web-forward > /dev/null 2>&1
sudo systemctl restart otbr-web-forward

echo ""
echo "==> Done. NEXT STEPS (not automated by this script -- verify manually):"
echo "    1. Check ot-ctl nat64 state above -- if 'Active' or similar, re-run"
echo "       setup-nat64-jool.sh to reapply the Jool override."
echo "    2. Confirm Thread devices reattach (check otbr web UI / matter-server)."
echo "    3. Confirm actual IPv4 cloud reachability from a Thread device"
echo "       (e.g. mini-split's Tuya connectivity), not just container health."
echo ""
echo "    Rollback: IMAGE=openthread/otbr:pre-thread14-20260721 ./setup-mg24.sh"
