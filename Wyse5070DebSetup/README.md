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
| **8090** | Trilium Notes | Yes — notes, migrated from Evernote | (none) | `trilium` |
| 8600 | MCP gateway: Microsoft To Do | No — Streamable HTTP, `/mcp` path, 127.0.0.1-only | (none) | `mcp-gateway-todo` |
| 8601 | MCP gateway: Trilium | No — Streamable HTTP, `/mcp` path, 127.0.0.1-only | (none) | `mcp-gateway-trilium` |

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

## On-demand services (no port, no daemon)

- **microsoft-todo-mcp** — `microsoft-todo-mcp:latest` Docker image, built by
  `setup-microsoft-todo-mcp.sh`. Not a running container — it's a stdio MCP
  server that Claude Desktop spawns fresh (`docker run --rm -i`) over SSH per
  session, so there's nothing to see in `docker ps` between calls. Config/
  tokens (secrets, never committed) live in
  `/mnt/data/appdata/microsoft-todo-mcp/config/`.
  **Windows client gotcha:** Claude Desktop's `mcpServers` config must invoke
  Git for Windows' `ssh.exe`, not `System32\OpenSSH\ssh.exe` — the native one
  dies silently under Electron's spawn (~100ms, never reaches this box). See
  the fix (a `.bat` wrapper) in `setup-microsoft-todo-mcp.sh`'s header
  comment.

## Remote-reachable MCP gateways (for Claude mobile, via Caddy)

`microsoft-todo-mcp` and `triliumnext-mcp` (see "On-demand services" above)
are both **stdio**-transport MCP servers, spawned locally per-session by
Claude Desktop — there's nothing for a remote client like Claude mobile to
connect to. `mcp-gateway-todo` and `mcp-gateway-trilium` are always-on
Docker containers that wrap those same upstream servers with
[`supergateway`](https://github.com/supercorp-ai/supergateway) to expose
them over Streamable HTTP instead, without modifying either upstream
server's code.

Both gateways bind to `127.0.0.1` only — not reachable from the LAN or WAN,
only from Caddy running on this same host, which is expected to be the
thing enforcing access control in front of them (`supergateway`'s HTTP
server mode has no built-in inbound auth of its own).

The Microsoft To Do gateway uses its **own, separate OAuth grant** — not the
one `microsoft-todo-mcp`/Claude Desktop uses — since two independent
long-running consumers refreshing from the same `tokens.json` would race
and invalidate each other's access token. Desktop's existing config is
untouched by either gateway. See each script's header comment for the
one-time setup needed before first run.

## Internet-facing entry point: Caddy

`setup-caddy.sh` deploys Caddy (Docker, `network_mode: host`) as the only
thing this host exposes to the internet — automatic Let's Encrypt TLS for
`wilkiefamily.duckdns.org`, reverse-proxying by path to the two MCP
gateways above (`/todo/*` → `mcp-gateway-todo`, `/trilium/*` →
`mcp-gateway-trilium`). Requires ports 80 and 443 forwarded from pfSense to
this host (`192.168.15.30`) — 80 for Let's Encrypt's renewal challenge, not
just 443.

Since neither gateway authenticates inbound requests on its own, Caddy
gates both paths on a static token (auto-generated on first run into
`/mnt/data/appdata/caddy/.env`, never committed) — embedded as a URL path
segment rather than a header, since Claude's custom-connector UI only
offers a single URL field (plus optional OAuth client ID/secret), with no
way to attach a custom header. External URLs (see `setup-caddy.sh`'s
output for the actual token):

- `https://wilkiefamily.duckdns.org/todo/<token>/mcp`
- `https://wilkiefamily.duckdns.org/trilium/<token>/mcp`

## Setup scripts, for reference

Each service above is deployed by the correspondingly-named script in this
directory (`setup-homeassistant.sh`, `setup-mg24.sh` for OTBR,
`setup-zigbee2mqtt.sh`, `setup-zwave-js-ui.sh`, `setup-matter-server.sh`,
`setup-mosquitto.sh`, `setup-trilium.sh`, `setup-mcp-gateway-todo.sh`,
`setup-mcp-gateway-trilium.sh`, `setup-caddy.sh`). Re-running any of them is
safe/idempotent and will recreate that one container with current settings.
