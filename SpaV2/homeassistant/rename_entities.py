"""Give the Spa Monitor's Matter entities stable entity ids.

HA names Matter entities by type and endpoint ("sensor.spa_monitor_humidity_3"),
and the exact ids depend on naming history, so the templates in spa_package.yaml
refer to stable ids instead. This maps each entity by its Matter endpoint number
(from the unique_id, "...-MatterNodeDevice-<endpoint>-<Kind>-...") to the ids in
MAP below, and hides the raw/plumbing entities. Safe to re-run.

Run on wyse after commissioning (needs HA_TOKEN in the HA .env):
  T=$(sed -n 's/^HA_TOKEN=//p' /mnt/data/appdata/homeassistant/.env)
  docker exec -i -e HA_TOKEN="$T" homeassistant python3 - < rename_entities.py
"""
import asyncio
import os
import re
import sys

import aiohttp

DEVICE_NAMES = {"Spa Monitor"}          # node label / product name in matter_device.cpp

# (endpoint, kind) -> (entity_id, friendly name, hidden)
# Endpoint table: SpaV2/firmware/src/matter_device.cpp.
MAP = {
    (1, "TemperatureSensor"): ("sensor.spa_water_temperature_raw", "Spa water temperature (raw)", True),
    (2, "HumiditySensor"): ("sensor.spa_ph_raw", "Spa pH (raw)", True),
    (3, "HumiditySensor"): ("sensor.spa_orp_raw", "Spa ORP (raw)", True),
    (4, "OccupancySensor"): ("binary_sensor.spa_readings_live", "Spa pH/ORP readings live", False),
    (5, "OccupancySensor"): ("binary_sensor.spa_pump_running", "Spa pump running", False),
    (6, "HumiditySensor"): ("sensor.spa_pump_current_raw", "Spa pump current (raw)", True),
    (7, "HumiditySensor"): ("sensor.spa_smarterspa_state_raw", "SmarterSpa state (raw)", True),
    (8, "HumiditySensor"): ("sensor.spa_electrode_hours_raw", "Spa electrode hours (raw)", True),
    (9, "HumiditySensor"): ("sensor.spa_calibration_status_raw", "Spa calibration status (raw)", True),
    (10, "MatterPlug"): ("switch.spa_calibrate_ph_7", "Calibrate pH 7", False),
    (11, "MatterPlug"): ("switch.spa_calibrate_ph_4", "Calibrate pH 4", False),
    (12, "MatterPlug"): ("switch.spa_calibrate_ph_10", "Calibrate pH 10", False),
    (13, "MatterPlug"): ("switch.spa_calibrate_orp", "Calibrate ORP", False),
    (14, "MatterPlug"): ("switch.spa_calibrate_clear", "Clear pH/ORP calibration", False),
}
# Per-endpoint plumbing HA adds that means nothing here.
HIDE_KINDS = ("IdentifyButton", "MatterStartUpOnOff")

UID = re.compile(r"MatterNodeDevice-(\d+)-([A-Za-z]+)-")


async def main():
    token = os.environ.get("HA_TOKEN")
    if not token:
        sys.exit("HA_TOKEN not set")
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect("http://127.0.0.1:8123/api/websocket") as ws:
            await ws.receive_json()
            await ws.send_json({"type": "auth", "access_token": token})
            if (await ws.receive_json()).get("type") != "auth_ok":
                sys.exit("HA auth failed")
            n = 0

            async def call(msg):
                nonlocal n
                n += 1
                await ws.send_json({"id": n, **msg})
                while True:
                    r = await ws.receive_json()
                    if r.get("id") == n:
                        if not r.get("success"):
                            raise RuntimeError(f"{msg['type']}: {r.get('error')}")
                        return r.get("result")

            devices = await call({"type": "config/device_registry/list"})
            ids = [d["id"] for d in devices
                   if (d.get("name_by_user") or d.get("name")) in DEVICE_NAMES or d.get("model") in DEVICE_NAMES]
            if len(ids) != 1:
                sys.exit(f"Expected exactly one Spa Monitor device, found {len(ids)}")
            entities = [e for e in await call({"type": "config/entity_registry/list"})
                        if e.get("device_id") == ids[0]]

            done = set()
            for e in entities:
                m = UID.search(e["unique_id"])
                if not m:
                    continue
                key = (int(m.group(1)), m.group(2))
                if key in MAP:
                    new_id, name, hidden = MAP[key]
                    upd = {"type": "config/entity_registry/update", "entity_id": e["entity_id"],
                           "name": name, "hidden_by": "user" if hidden else None}
                    if e["entity_id"] != new_id:
                        upd["new_entity_id"] = new_id
                    await call(upd)
                    done.add(key)
                    print(f"{e['entity_id']:55} -> {new_id}{'  (hidden)' if hidden else ''}")
                elif m.group(2) in HIDE_KINDS and e.get("hidden_by") is None:
                    await call({"type": "config/entity_registry/update", "entity_id": e["entity_id"],
                                "hidden_by": "user"})
                    print(f"{e['entity_id']:55}    hidden")
            missing = [MAP[k][0] for k in MAP if k not in done]
            if missing:
                print("NOT FOUND (endpoint layout changed?):", ", ".join(missing))
                sys.exit(1)
            print("All Spa Monitor entities mapped.")


asyncio.run(main())
