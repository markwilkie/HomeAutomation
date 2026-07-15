# wilkie-home-server — service endpoints

Host: `wilkie-home-server` (`192.168.15.30`), Debian 12, Wyse 5070 thin client.
Every service below runs in its own Docker container (`network_mode: host`
unless noted), so ports map directly onto the host IP.

There are **three separate radio dongles**, each with its own admin UI. It's
easy to mix these up since they're all just "the web UI on some port" —
this table exists specifically so that doesn't happen:

| Port | Service | Web UI? | Radio / dongle | Container |
|---|---|---|---|---|
| **8123** | Home Assistant | Yes — main dashboard | (none — the hub) | `homeassistant` |
| **8080** | OTBR (Thread Border Router) | Yes — Thread network status/topology | SONOFF Dongle Plus MG24 (`usb-SONOFF_SONOFF_Dongle_Plus_MG24_...`) | `otbr` |
| **8099** | Zigbee2MQTT | Yes — Zigbee pairing, device list, logs | SONOFF Zigbee 3.0 USB Dongle Plus V2 (`usb-Itead_Sonoff_Zigbee_3.0...`) | `zigbee2mqtt` |
| **8091** | Z-Wave JS UI | Yes — Z-Wave pairing, device list, logs | Aeotec Z-Stick Gen5 (`usb-0658_0200-if00`) | `zwavejsui` |
| 5580 | Matter Server | No — WebSocket only, for HA's Matter integration | (Thread, via OTBR's border routing — no dongle of its own) | `matter-server` |
| 3000 | Z-Wave JS server | No — WebSocket only, for HA's Z-Wave JS integration | Aeotec Z-Stick Gen5 (same dongle as :8091, different port) | `zwavejsui` |
| 1883 | Mosquitto (MQTT) | No — broker only | (none) | `mosquitto` |
| 9001 | Mosquitto (MQTT over WebSockets) | No — broker only | (none) | `mosquitto` |

## The three dongle admin UIs, side by side

These are the ones actually worth telling apart at a glance — each manages a
completely different wireless network, with completely separate paired
devices:

- **http://192.168.15.30:8080/** — **OTBR** — Thread mesh (the MiniSplit
  bridge and anything else on Thread). Shows Thread topology, node roles,
  NAT64 state. Internally the container serves this on port 80; a systemd
  unit (`otbr-web-forward.service`, installed by `setup-mg24.sh`) forwards
  host port 8080 -> `127.0.0.1:80` so it's reachable from the LAN.
- **http://192.168.15.30:8099/** — **Zigbee2MQTT** — Zigbee mesh. Pairing
  ("Permit join"), device list, per-device diagnostics. Bridges to Home
  Assistant over MQTT, not Matter.
- **http://192.168.15.30:8091/** — **Z-Wave JS UI** — Z-Wave mesh. Pairing,
  device list, network health/logs. Home Assistant talks to this one
  directly over the websocket on :3000 (its own separate "Z-Wave JS"
  integration), not MQTT.

## Non-browsable endpoints

`ws://192.168.15.30:5580/ws` (Matter Server) and
`ws://192.168.15.30:3000` (Z-Wave JS server) are not meant to be opened in a
browser — they're the raw protocol endpoints Home Assistant's own Matter and
Z-Wave JS integrations connect to (Settings -> Devices & Services -> Add
Integration -> "Matter" / "Z-Wave JS", pointing at the `ws://` URL above).
Visiting them directly in a browser will just show a WebSocket handshake
error, not a UI.

## Background services (no endpoint at all)

- `nat64-jool.service` — one-shot at boot, configures Jool NAT64 for the
  Thread network. See `setup-nat64-jool.sh`.
- `otbr-watchdog.service` — polls otbr-agent every 30s and restarts it on
  crash. Log: `/mnt/data/appdata/otbr/watchdog.log` (plain-text mirror of the
  journal, world-readable, no `sudo` needed to `tail -f` it).

## Setup scripts, for reference

Each service above is deployed by the correspondingly-named script in this
directory (`setup-homeassistant.sh`, `setup-mg24.sh` for OTBR,
`setup-zigbee2mqtt.sh`, `setup-zwave-js-ui.sh`, `setup-matter-server.sh`,
`setup-mosquitto.sh`). Re-running any of them is safe/idempotent and will
recreate that one container with current settings.
