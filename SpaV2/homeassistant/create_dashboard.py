"""Create or update the "Spa" sidebar dashboard from spa_dashboard.yaml.

Overwrites the dashboard's config every run: spa_dashboard.yaml is the source
of truth, so edits made in HA's dashboard editor are lost on the next run.

Run on wyse (needs HA_TOKEN in the HA .env). The YAML arrives on stdin because
the repo isn't mounted in the container:
  cd ~/github/HomeAutomation/SpaV2/homeassistant
  T=$(sed -n 's/^HA_TOKEN=//p' /mnt/data/appdata/homeassistant/.env)
  docker exec -i -e HA_TOKEN="$T" homeassistant python3 -c "$(cat create_dashboard.py)" < spa_dashboard.yaml
"""
import asyncio
import os
import sys

import aiohttp
import yaml

URL_PATH = "spa-dashboard"      # HA requires a hyphen in a dashboard url_path


async def main():
    token = os.environ.get("HA_TOKEN") or sys.exit("HA_TOKEN not set")
    config = yaml.safe_load(sys.stdin)
    title = config.pop("title", "Spa")
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect("http://127.0.0.1:8123/api/websocket") as ws:
            await ws.receive_json()
            await ws.send_json({"type": "auth", "access_token": token})
            if (await ws.receive_json()).get("type") != "auth_ok":
                sys.exit("auth failed")
            n = 0

            async def call(msg):
                nonlocal n
                n += 1
                await ws.send_json({"id": n, **msg})
                while True:
                    r = await ws.receive_json()
                    if r.get("id") == n:
                        if not r.get("success"):
                            sys.exit(f"{msg['type']} failed: {r.get('error')}")
                        return r.get("result")

            boards = await call({"type": "lovelace/dashboards/list"})
            if not any(b["url_path"] == URL_PATH for b in boards):
                await call({"type": "lovelace/dashboards/create", "url_path": URL_PATH, "title": title,
                            "icon": "mdi:hot-tub", "mode": "storage", "show_in_sidebar": True,
                            "require_admin": False})
                print(f"created dashboard /{URL_PATH}")
            await call({"type": "lovelace/config/save", "url_path": URL_PATH, "config": config})
            print(f"saved /{URL_PATH} from spa_dashboard.yaml")


asyncio.run(main())
