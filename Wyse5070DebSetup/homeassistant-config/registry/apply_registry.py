"""Make Home Assistant's floors, areas, device/entity settings and default dashboard
match registry/registry.yaml.

HA keeps these in .storage (no YAML support), so this file is their source of truth:
deploy.sh copies it and this script into the homeassistant container and runs it
after every deploy. Fully declarative -- anything set live that the file doesn't
say is reset (an area or name added in the UI, a hidden entity), and every change
is printed. Integration-set state (hidden_by/disabled_by "integration") is left
alone.

    python3 apply_registry.py registry.yaml           apply
    python3 apply_registry.py registry.yaml --check   print the drift, change nothing
                                                      (exit 1 if there is any)

Needs HA_TOKEN (a long-lived access token) in the environment and HA on
localhost:8123 -- i.e. run it inside the homeassistant container.
"""
import asyncio
import os
import sys

import aiohttp
import yaml

CHECK = "--check" in sys.argv
PATH = next(a for a in sys.argv[1:] if not a.startswith("--"))
changes = []


def say(msg):
    changes.append(msg)
    print(("would: " if CHECK else "") + msg)


async def main():
    want = yaml.safe_load(open(PATH, encoding="utf-8"))
    async with aiohttp.ClientSession() as s, s.ws_connect("http://localhost:8123/api/websocket") as ws:
        await ws.receive_json()
        await ws.send_json({"type": "auth", "access_token": os.environ["HA_TOKEN"]})
        if (await ws.receive_json())["type"] != "auth_ok":
            sys.exit("auth failed")
        n = [0]

        async def q(t, write=False, **k):
            if write and CHECK:
                return None
            n[0] += 1
            await ws.send_json({"id": n[0], "type": t, **k})
            r = await ws.receive_json()
            if not r.get("success"):
                raise SystemExit(f"{t} {k} failed: {r.get('error')}")
            return r["result"]

        # --- floors
        floors = {f["floor_id"]: f for f in await q("config/floor_registry/list")}
        for fid, w in want.get("floors", {}).items():
            fields = {"name": w["name"], "level": w.get("level"), "icon": w.get("icon")}
            if fid not in floors:
                say(f"create floor {fid}")
                r = await q("config/floor_registry/create", write=True, **{k: v for k, v in fields.items() if v is not None})
                if r and r["floor_id"] != fid:
                    print(f"  note: HA named it {r['floor_id']}, rename the key in registry.yaml")
                continue
            diff = {k: v for k, v in fields.items() if floors[fid].get(k) != v}
            if diff:
                say(f"floor {fid}: {diff}")
                await q("config/floor_registry/update", write=True, floor_id=fid, **diff)

        # --- areas
        areas = {a["area_id"]: a for a in await q("config/area_registry/list")}
        for aid, w in want.get("areas", {}).items():
            fields = {"name": w["name"], "floor_id": w.get("floor"), "icon": w.get("icon"),
                      "aliases": sorted(w.get("aliases", []))}
            if aid not in areas:
                say(f"create area {aid}")
                r = await q("config/area_registry/create", write=True, **{k: v for k, v in fields.items() if v})
                if r and r["area_id"] != aid:
                    print(f"  note: HA named it {r['area_id']}, rename the key in registry.yaml")
                continue
            live = dict(areas[aid], aliases=sorted(areas[aid].get("aliases") or []))
            diff = {k: v for k, v in fields.items() if live.get(k) != v}
            if diff:
                say(f"area {aid}: {diff}")
                await q("config/area_registry/update", write=True, area_id=aid, **diff)

        # --- devices
        devs = [d for d in await q("config/device_registry/list") if d.get("entry_type") != "service"]
        wanted_dev = {}
        for w in want.get("devices", []):
            if w.get("id"):
                dom, _, ident = w["id"].partition(":")
                hits = [d for d in devs if [dom, ident] in d["identifiers"]]
            else:
                hits = [d for d in devs if d.get("name") == w["name"]]
            if len(hits) != 1:
                print(f"WARNING device {w['name']!r}: {len(hits)} matches -- add/fix its id:, skipped")
                continue
            wanted_dev[hits[0]["id"]] = w
        for d in devs:
            w = wanted_dev.get(d["id"], {})
            fields = {"area_id": w.get("area"), "name_by_user": w.get("rename"),
                      "disabled_by": "user" if w.get("disabled") else None}
            live = {"area_id": d.get("area_id"), "name_by_user": d.get("name_by_user"),
                    "disabled_by": d.get("disabled_by") if d.get("disabled_by") == "user" else None}
            diff = {k: v for k, v in fields.items() if live[k] != v}
            if diff:
                say(f"device {d.get('name')!r}: {diff}")
                await q("config/device_registry/update", write=True, device_id=d["id"], **diff)

        # --- entities
        wanted_ent = want.get("entities", {}) or {}
        for e in await q("config/entity_registry/list"):
            w = wanted_ent.get(e["entity_id"], {}) or {}
            fields = {"area_id": w.get("area"), "name": w.get("name"),
                      "hidden_by": "user" if w.get("hidden") else None}
            live = {"area_id": e.get("area_id"), "name": e.get("name"),
                    "hidden_by": e.get("hidden_by") if e.get("hidden_by") == "user" else None}
            diff = {k: v for k, v in fields.items() if live[k] != v}
            if e.get("hidden_by") == "integration" and "hidden_by" in diff and diff["hidden_by"] is None:
                diff.pop("hidden_by")
            if diff:
                say(f"entity {e['entity_id']}: {diff}")
                await q("config/entity_registry/update", write=True, entity_id=e["entity_id"], **diff)
        known = {e["entity_id"] for e in await q("config/entity_registry/list")}
        for eid in wanted_ent:
            if eid not in known:
                print(f"WARNING entity {eid} in registry.yaml doesn't exist")

        # --- undeclared areas / floors (after devices/entities have moved off them)
        for aid in areas:
            if aid not in want.get("areas", {}):
                say(f"delete area {aid}")
                await q("config/area_registry/delete", write=True, area_id=aid)
        for fid in floors:
            if fid not in want.get("floors", {}):
                say(f"delete floor {fid}")
                await q("config/floor_registry/delete", write=True, floor_id=fid)

        # --- default dashboard (system-wide)
        dp = (want.get("frontend") or {}).get("default_panel")
        cur = ((await q("frontend/get_system_data", key="core")) or {}).get("value") or {}
        if cur.get("default_panel") != dp:
            say(f"default dashboard: {cur.get('default_panel')} -> {dp}")
            val = dict(cur)
            if dp:
                val["default_panel"] = dp
            else:
                val.pop("default_panel", None)
            await q("frontend/set_system_data", write=True, key="core", value=val)

    print(f"registry: {len(changes)} {'difference' if CHECK else 'change'}(s)")
    if CHECK and changes:
        sys.exit(1)

asyncio.run(main())
