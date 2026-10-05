#!/usr/bin/env bash
#
# commission-thread-device.sh
#
# Commissions a new Matter-over-Thread device into Home Assistant directly
# from matter-server over Bluetooth, using OTBR's Thread network
# (WilkieMatterNet). The phone is not involved at all.
#
# WHY NOT THE PHONE: there are two Thread networks in this house -- OTBR's
# (WilkieMatterNet) and the SmartThings Hub's (ST-3011356111). Commissioning
# from the HA Companion app hands the BLE step to Google Play Services, which
# provisions whatever Thread network *Google* prefers, not HA's. On
# 2026-10-02 that was the SmartThings network, so devices landed there. See
# ../ThreadRouter/CLAUDE.md for the full story.
#
# Run ON WYSE (ssh mwilkie@192.168.15.30). The device must be powered,
# advertising for commissioning (fresh or factory reset), and within
# Bluetooth range of wyse.
#
# Usage:
#   ./commission-thread-device.sh <manual-pairing-code>   # e.g. 34970112332
#   ./commission-thread-device.sh --check                 # prerequisites only
#
# QR-code strings (MT:...) also work in place of the 11-digit manual code.

set -euo pipefail

CODE="${1:-}"
if [[ -z "$CODE" ]]; then
    echo "usage: $0 <manual-pairing-code | MT:qr-payload | --check>" >&2
    exit 2
fi

DATASET=$(docker exec otbr ot-ctl dataset active -x | head -1 | tr -d '\r')
if [[ ! "$DATASET" =~ ^[0-9a-fA-F]+$ ]]; then
    echo "ERROR: could not read OTBR's active dataset (got: '$DATASET'). Is the otbr container up?" >&2
    exit 1
fi
NETNAME=$(docker exec otbr ot-ctl networkname | head -1 | tr -d '\r')
echo "OTBR network: $NETNAME (dataset ${#DATASET} hex chars)"

# matter-server is a Node image with no python; HA's container has aiohttp and
# both use host networking, so borrow HA's interpreter to talk to ws://:5580.
NODE_ID=$(docker exec -i homeassistant python3 - "$DATASET" "$CODE" <<'EOF'
import sys, asyncio, aiohttp

dataset, code = sys.argv[1], sys.argv[2]
log = lambda *a: print(*a, file=sys.stderr, flush=True)

async def call(ws, mid, command, args):
    await ws.send_json({"message_id": mid, "command": command, "args": args})
    while True:
        m = await ws.receive_json()
        if m.get("message_id") == mid:
            return m

async def main():
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect("ws://127.0.0.1:5580/ws", timeout=600, receive_timeout=600) as ws:
            info = await ws.receive_json()
            if not info.get("bluetooth_enabled"):
                log("ERROR: matter-server reports bluetooth_enabled=false -- see setup-matter-server.sh / setup-bluetooth.sh")
                sys.exit(1)
            r = await call(ws, "ds", "set_thread_dataset", {"dataset": dataset})
            if "error_code" in r:
                log("ERROR: set_thread_dataset failed:", r); sys.exit(1)
            log("matter-server: bluetooth enabled, OTBR dataset loaded")
            if code == "--check":
                log("Prerequisites OK.")
                return
            log("Commissioning over BLE (usually 1-2 minutes)...")
            r = await call(ws, "cm", "commission_with_code", {"code": code, "network_only": False})
            if "error_code" in r:
                log("ERROR: commissioning failed:", r.get("error_code"), r.get("details", ""))
                sys.exit(1)
            print(r["result"]["node_id"])

asyncio.run(main())
EOF
)

if [[ "$CODE" == "--check" ]]; then
    exit 0
fi

NODE_HEX=$(printf '%016X' "$NODE_ID")
echo "Commissioned as Matter node $NODE_ID (0x$NODE_HEX)."

echo "Waiting for the device to appear in Home Assistant..."
for _ in $(seq 1 12); do
    NAME=$(docker exec homeassistant python3 -c "
import json
d = json.load(open('/config/.storage/core.device_registry'))
for x in d['data']['devices']:
    for i in x.get('identifiers', []):
        if i[0] == 'matter' and '-$NODE_HEX-' in i[1].upper():
            print(x.get('name_by_user') or x.get('name'))
" 2>/dev/null || true)
    [[ -n "$NAME" ]] && break
    sleep 5
done
if [[ -n "$NAME" ]]; then
    echo "Home Assistant device: \"$NAME\""
else
    echo "WARNING: not in HA's device registry yet -- check Settings -> Devices & services -> Matter."
fi

cat <<EOF

Thread topology (the new device starts as a child and is usually promoted to
router within ~2 minutes if it's router-capable). Partition must be OTBR's.
EOF
PID=$(docker exec otbr ot-ctl partitionid | head -1 | tr -d '\r')
printf 'OTBR partition: %s (0x%08x) -- device logs must show this hex value, not 0x4d6f48d3 (SmartThings)\n' "$PID" "$PID"
docker exec otbr ot-ctl router table
docker exec otbr ot-ctl child table
