#!/usr/bin/env python3
#
# smartthings-inventory.py
# Pulls every device on the SmartThings account and prints a grouped report:
# protocol, live online/offline state, real last-activity timestamp, and a
# recommended Home Assistant migration path for each.
#
# Requires a SmartThings Personal Access Token (create one at
# https://account.smartthings.com/tokens — no scopes beyond device read needed).
#
# Usage:
#   SMARTTHINGS_PAT=xxxx ./smartthings-inventory.py
#   ./smartthings-inventory.py --token xxxx
#
# Notes on the two traps this script deliberately avoids:
#   - The /health endpoint's lastUpdatedDate is when the ONLINE/OFFLINE *flag*
#     last flipped, not when the device last actually did anything. We instead
#     scan /status for the newest per-attribute timestamp.
#   - Within /status, the healthCheck capability's own attributes (e.g.
#     DeviceWatch-DeviceStatus) are stamped when the *platform* notices a
#     connectivity change, not when the device reports data. Left in, a device
#     that just went offline looks like it was "active" the moment it died.
#     We exclude that capability from the last-activity scan.

import argparse
import json
import os
import sys
import time

try:
    import requests
except ImportError:
    sys.exit("This script needs the 'requests' package: pip3 install requests")

API_ROOT = "https://api.smartthings.com/v1"

PROTOCOL_ORDER = ["ZIGBEE", "MATTER", "ZWAVE", "VIPER", "OCF", "LAN",
                  "EDGE_CHILD", "VIRTUAL", "ENDPOINT_APP", "HUB"]

VIPER_OEM_ALT = {
    "arlo": "Use native Arlo integration",
    "august": "Use native August integration",
    "yale": "Use native Yale/August integration",
    "tp-link": "Use native TP-Link/Kasa integration",
    "tplink": "Use native TP-Link/Kasa integration",
}


def get(session, path):
    r = session.get(f"{API_ROOT}{path}", timeout=15)
    r.raise_for_status()
    return r.json()


def max_activity_timestamp(status_json):
    """Newest attribute timestamp in /status, excluding the healthCheck
    capability (which reflects platform monitoring, not device activity)."""
    best = None
    for component in status_json.get("components", {}).values():
        for cap_name, cap in component.items():
            if cap_name == "healthCheck" or not isinstance(cap, dict):
                continue
            for attr in cap.values():
                if isinstance(attr, dict):
                    ts = attr.get("timestamp")
                    if isinstance(ts, str) and (best is None or ts > best):
                        best = ts
    return best


def classify(dtype, name, viper_manufacturer=None):
    """Returns (category, path, note)."""
    n = name.lower()
    if dtype == "ZIGBEE":
        return ("migrate", "Re-pair to Zigbee2MQTT",
                "Local Zigbee coordinator already running -- join it directly, no cloud hop.")
    if dtype == "MATTER":
        return ("migrate", "Commission via HA Matter + OTBR",
                "Thread border router already up -- commission straight into HA's Matter server.")
    if dtype == "ZWAVE":
        return ("hardware", "Needs a Z-Wave USB stick",
                "No local Z-Wave radio yet -- add one, then HA's Z-Wave JS integration.")
    if dtype == "VIPER":
        oem = (viper_manufacturer or "").lower()
        for key, alt in VIPER_OEM_ALT.items():
            if key in oem:
                return ("alt-integration", alt,
                        f"viper.manufacturerName reports '{viper_manufacturer}' -- talk to its own cloud/local API directly, skip SmartThings.")
        label = viper_manufacturer or "unknown OEM"
        return ("cloud-only", "HA SmartThings integration",
                f"OEM is '{label}' -- no known native HA integration found; SmartThings bridge is the fallback.")
    if dtype == "OCF":
        return ("cloud-only", "HA SmartThings integration",
                "Samsung appliance cloud (OCF) -- no local or direct manufacturer path.")
    if dtype == "VIRTUAL":
        return ("trivial", "Recreate as HA input_boolean",
                "Pure virtual switch -- no device behind it, rebuild natively in HA.")
    if dtype == "LAN":
        if any(k in n for k in ["creator", "requestor", "admin device", "shade checker"]):
            return ("retire", "Retire -- ST helper/glue device",
                    "Community Edge-driver utility (virtual MQTT bridge/admin helper) -- unneeded once devices publish to MQTT directly.")
        return ("migrate", "Point firmware straight at Mosquitto",
                "Likely your own ESP-based sensor/driver -- publish directly to your MQTT broker instead of through ST's LAN edge driver.")
    if dtype == "EDGE_CHILD":
        return ("migrate", "Migrates with its LAN parent",
                "Child device of a custom LAN driver -- follows once the parent re-hosts to MQTT.")
    if dtype == "ENDPOINT_APP":
        return ("investigate", "Investigate -- unclear backing",
                "Custom endpoint app device -- confirm what's actually behind it before planning a path.")
    if dtype == "HUB":
        return ("retire", "Decommission after Zigbee/Z-Wave move",
                "Only needed while Zigbee/Z-Wave devices still pair through it.")
    return ("investigate", "Investigate", "Unclassified device type.")


def rel_time(iso):
    if not iso:
        return "unknown"
    then = time.strptime(iso.split(".")[0], "%Y-%m-%dT%H:%M:%S")
    then_epoch = time.mktime(then) - time.timezone
    days = (time.time() - then_epoch) / 86400
    if days < 1:
        return "today"
    if days < 2:
        return "1 day ago"
    if days < 30:
        return f"{int(days)} days ago"
    if days < 730:
        return f"{int(days / 30)} mo ago"
    return f"{int(days / 365)} yr ago"


COLOR = {
    "green": "\033[32m", "red": "\033[31m", "yellow": "\033[33m",
    "dim": "\033[2m", "bold": "\033[1m", "reset": "\033[0m",
}


def c(text, color, use_color):
    return f"{COLOR[color]}{text}{COLOR['reset']}" if use_color else text


def main():
    parser = argparse.ArgumentParser(description="Report SmartThings devices and their HA migration path.")
    parser.add_argument("--token", help="SmartThings Personal Access Token (defaults to $SMARTTHINGS_PAT)")
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI color output")
    args = parser.parse_args()

    token = args.token or os.environ.get("SMARTTHINGS_PAT")
    if not token:
        sys.exit("Set SMARTTHINGS_PAT in the environment or pass --token.\n"
                  "Create a token at https://account.smartthings.com/tokens")

    use_color = not args.no_color and sys.stdout.isatty()

    session = requests.Session()
    session.headers["Authorization"] = f"Bearer {token}"

    print("Fetching device list...", file=sys.stderr)
    devices = get(session, "/devices")["items"]

    records = []
    for i, dev in enumerate(devices, 1):
        did = dev["deviceId"]
        name = dev.get("label") or dev.get("name")
        dtype = dev.get("type")
        print(f"  [{i}/{len(devices)}] {name}", file=sys.stderr, end="\r")

        try:
            health = get(session, f"/devices/{did}/health")
            state = health.get("state", "UNKNOWN")
        except requests.RequestException:
            state = "UNKNOWN"

        try:
            status = get(session, f"/devices/{did}/status")
            last = max_activity_timestamp(status)
        except requests.RequestException:
            last = None

        viper_manufacturer = None
        if dtype == "VIPER":
            try:
                detail = get(session, f"/devices/{did}")
                viper_manufacturer = detail.get("viper", {}).get("manufacturerName") \
                    or detail.get("deviceManufacturerCode")
            except requests.RequestException:
                pass

        category, path, note = classify(dtype, name, viper_manufacturer)
        records.append({
            "name": name, "type": dtype, "state": state,
            "last": last, "category": category, "path": path, "note": note,
        })
        time.sleep(0.05)

    print(" " * 60, file=sys.stderr, end="\r")

    total = len(records)
    online = sum(1 for r in records if r["state"] == "ONLINE")
    offline = sum(1 for r in records if r["state"] == "OFFLINE")
    ready = sum(1 for r in records if r["category"] == "migrate")

    print()
    print(c(f"SmartThings device manifest -- {total} devices", "bold", use_color))
    print(f"  online: {c(online, 'green', use_color)}   offline: {c(offline, 'red', use_color)}   "
          f"ready to migrate now: {c(ready, 'green', use_color)}")
    print()

    by_type = {}
    for r in records:
        by_type.setdefault(r["type"], []).append(r)

    order = PROTOCOL_ORDER + sorted(set(by_type) - set(PROTOCOL_ORDER))

    for dtype in order:
        if dtype not in by_type:
            continue
        group = sorted(by_type[dtype], key=lambda r: r["name"])
        print(c(f"-- {dtype} ({len(group)}) " + "-" * max(1, 50 - len(dtype)), "dim", use_color))
        for r in group:
            state_color = "green" if r["state"] == "ONLINE" else ("red" if r["state"] == "OFFLINE" else "yellow")
            state_str = c(f"{r['state']:<8}", state_color, use_color)
            last_str = f"{rel_time(r['last']):<12}"
            print(f"  {r['name']:<28} {state_str} {last_str} {c(r['category'], 'yellow', use_color):<28} {r['path']}")
            print(f"    {c(r['note'], 'dim', use_color)}")
        print()


if __name__ == "__main__":
    main()
