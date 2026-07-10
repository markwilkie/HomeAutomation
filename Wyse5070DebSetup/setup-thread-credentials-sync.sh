#!/usr/bin/env bash
#
# setup-thread-credentials-sync.sh
#
# Pushes the OTBR Thread network's active operational dataset into
# matter-server so it can commission new Thread devices.
#
# WHY THIS EXISTS: matter-server's own `thread_credentials_set` flag
# (visible in its websocket server-info message) resets to false on every
# container restart -- confirmed on real hardware: it was `true` after
# being pushed once, then `false` again immediately after a plain `docker
# restart matter-server`, with no persisted copy surviving anywhere in its
# own /data storage. This does NOT affect already-commissioned devices
# (Thread network operation is entirely between the device and OTBR, which
# persists its own dataset under /var/lib/thread -- see setup-mg24.sh) but
# it silently blocks commissioning any NEW Thread device after a
# matter-server restart or host reboot until this is re-pushed.
#
# This script fetches the current active dataset from OTBR (`ot-ctl dataset
# active -x`) and pushes it to matter-server's own websocket API
# (`set_thread_dataset`), executed via `docker exec matter-server python3`
# since that container already ships aiohttp -- no extra image/install
# needed.
#
# PREREQUISITE: both the otbr and matter-server containers must already be
# running (setup-mg24.sh and setup-matter-server.sh).
#
# Usage:
#   ./setup-thread-credentials-sync.sh          # apply now (also installs a
#                                                # systemd service for boot)
#   sudo systemctl status thread-credentials-sync.service

set -euo pipefail

OTBR_CONTAINER="otbr"
MATTER_SERVER_CONTAINER="matter-server"

echo "==> Waiting for otbr to have an active Thread dataset..."
DATASET_HEX=""
for i in $(seq 1 30); do
  DATASET_HEX=$(docker exec "${OTBR_CONTAINER}" sh -c "ot-ctl dataset active -x" 2>/dev/null | head -1 | tr -d '\r')
  if [ -n "${DATASET_HEX}" ] && [ "${DATASET_HEX}" != "Done" ]; then
    break
  fi
  DATASET_HEX=""
  sleep 2
done

if [ -z "${DATASET_HEX}" ]; then
  echo "!! Timed out waiting for an active Thread dataset from ${OTBR_CONTAINER}." >&2
  exit 1
fi
echo "==> Got active dataset (${#DATASET_HEX} hex chars)"

echo "==> Waiting for matter-server's websocket API to be reachable..."
for i in $(seq 1 30); do
  if docker exec "${MATTER_SERVER_CONTAINER}" python3 -c "
import socket
s = socket.create_connection(('127.0.0.1', 5580), timeout=2)
s.close()
" 2>/dev/null; then
    break
  fi
  sleep 2
done

echo "==> Pushing dataset to matter-server"
docker exec "${MATTER_SERVER_CONTAINER}" python3 -c "
import asyncio, json, aiohttp, sys

DATASET_HEX = '${DATASET_HEX}'

async def main():
    async with aiohttp.ClientSession() as session:
        async with session.ws_connect('ws://127.0.0.1:5580/ws') as ws:
            await ws.receive_json()  # server info / hello
            await ws.send_json({'message_id': '1', 'command': 'set_thread_dataset', 'args': {'dataset': DATASET_HEX}})
            resp = await ws.receive_json()
            if resp.get('error_code') is not None:
                print(f'matter-server rejected dataset: {resp}', file=sys.stderr)
                sys.exit(1)

asyncio.run(main())
"

echo "==> Verifying thread_credentials_set is now true"
docker exec "${MATTER_SERVER_CONTAINER}" python3 -c "
import asyncio, json, aiohttp

async def main():
    async with aiohttp.ClientSession() as session:
        async with session.ws_connect('ws://127.0.0.1:5580/ws') as ws:
            info = await ws.receive_json()
            print(json.dumps(info, indent=2))
            assert info['thread_credentials_set'] is True, 'thread_credentials_set is still false!'

asyncio.run(main())
"

# ---- Install systemd service for boot-time persistence --------------------
# thread_credentials_set is in-memory server state, not written to disk, so
# this must re-run after every matter-server restart (including a host
# reboot) -- hence the systemd unit below, ordered after docker.service with
# its own poll/retry loop for both containers to actually be ready.
SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
SERVICE_FILE="/etc/systemd/system/thread-credentials-sync.service"
echo ""
echo "==> Installing systemd service for boot-time persistence"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Push OTBR's active Thread dataset into matter-server
After=docker.service
Requires=docker.service

[Service]
Type=oneshot
ExecStart=${SCRIPT_PATH}
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable thread-credentials-sync.service > /dev/null 2>&1

echo ""
echo "==> Done."
echo "    Check status any time with:"
echo "      sudo systemctl status thread-credentials-sync.service"
echo "      (or just re-run this script -- it's idempotent)"
