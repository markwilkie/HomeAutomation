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
| **8085** | Dashboard | Yes — quick links to every service below + top-level repo docs | (none) | `dashboard` |
| 8095 | RaceTimes | Yes, but via Caddy only: **https://mayhem-racing.duckdns.org/** (public, basic auth); the port is bound to 127.0.0.1. Mayhem's ORC time owed/received per boat — separate repo [markwilkie/RaceTimes](https://github.com/markwilkie/RaceTimes), deployed and documented there | (none) | `racetimes` |
| 8600 | MCP gateway: Microsoft To Do | No — Streamable HTTP, `/mcp` path, 127.0.0.1-only | (none) | `mcp-gateway-todo` |
| 8601 | MCP gateway: Trilium | No — Streamable HTTP, `/mcp` path, 127.0.0.1-only | (none) | `mcp-gateway-trilium` |
| 8602 | MCP gateway: Monarch Money | No — Streamable HTTP, `/mcp` path, 127.0.0.1-only | (none) | `mcp-gateway-monarch` |

## Quick-links dashboard

**http://192.168.15.30:8085/** — a static "quick links" landing page
(`setup-dashboard.sh`, source in `dashboard/index.html`) linking to every
browsable service in the table above plus this repo's top-level
README/CLAUDE.md docs on GitHub. LAN/Tailscale only, same as everything
else in this table except the Caddy-fronted MCP gateways below and
RaceTimes (`mayhem-racing.duckdns.org`) — not in
the Caddyfile, not reachable via `wilkiefamily.duckdns.org`.

## The three dongle admin UIs, side by side

These are the ones actually worth telling apart at a glance — each manages a
completely different wireless network, with completely separate paired
devices:

- **http://192.168.15.30:8080/** — **OTBR** — Thread mesh (the MiniSplit
  bridge and anything else on Thread). Shows Thread topology, node roles,
  NAT64 state. As of 2026-08-08 this runs `openthread/border-router`, the
  OpenThread project's actual versioned production image (see
  `setup-mg24-production.sh`) — `openthread/otbr` (used previously, still
  the image `setup-mg24.sh` deploys) turned out to be their CI test image,
  published only as a continuously-rebuilt `latest` tag with no release
  channel at all. Internally the production container serves the web UI on
  port 8082 (moved off its own default of 8080, which collided with the
  external forward below); a systemd unit (`otbr-web-forward.service`,
  installed by `setup-mg24-production.sh`) forwards host port 8080 ->
  `127.0.0.1:8082` so it's reachable from the LAN. Rollback path and full
  migration notes: `IMAGE=openthread/otbr:pre-thread14-20260721
  ./setup-mg24.sh`.
- **http://192.168.15.30:8099/** — **Zigbee2MQTT** — Zigbee mesh. Pairing
  ("Permit join"), device list, per-device diagnostics. Bridges to Home
  Assistant over MQTT, not Matter.
- **http://192.168.15.30:8091/** — **Z-Wave JS UI** — Z-Wave mesh. Pairing,
  device list, network health/logs. Home Assistant talks to this one
  directly over the websocket on :3000 (its own separate "Z-Wave JS"
  integration), not MQTT.

### Zigbee temp/humidity sensors (SONOFF SNZB-02P) — reporting is factory-coarse by default

Out of the box, a newly-paired SNZB-02P only sends a temperature update on a
full 1.0°C change (or once per hour regardless). This is Z2M's built-in
default reporting config (`min 10s / max 3600s / change 100` raw units,
i.e. 1.0°C) — nothing in `configuration.yaml`'s `devices:` block controls
it, and there's no schema key for it. It has to be pushed to the physical
device's Zigbee cluster directly, after pairing, while the device happens to
be awake (it's a battery/sleepy end device, so the bind can time out if sent
between check-ins — retry right after you see it report).

`minisplit_followme_temp` (`0x18690afffe5602f1`) has had this tightened to
0.1°C so it's responsive enough to drive the MiniSplit follow-me
automations (see `automations.yaml`). Any *new* SNZB-02P — including
`Upstairs Hallway` (`0x70d07efffea47190`), tightened 2026-09-09 — needs the
same treatment manually. To do it: SSH to `192.168.15.30`, then publish to
the `zigbee2mqtt` bridge over the `mosquitto` container:

```
mosquitto_pub -h localhost -p 1883 -u mwilkie -P <password from setup-mosquitto.sh> \
  -t 'zigbee2mqtt/bridge/request/device/reporting/configure' \
  -f reporting_payload.json
```

where `reporting_payload.json` is:

```json
{
  "id": "<friendly_name>",
  "endpoint": "1",
  "cluster": "msTemperatureMeasurement",
  "attribute": "measuredValue",
  "minimum_report_interval": 10,
  "maximum_report_interval": 3600,
  "reportable_change": 10
}
```

Watch `docker logs zigbee2mqtt` for `Configured reporting for '<name>',
'msTemperatureMeasurement.measuredValue'` to confirm it landed — a bind
timeout just means it missed the device's wake window, retry. Humidity
(`msRelativeHumidity`) is left at the 1.0% factory default on both sensors
today; there's been no need to tighten it yet, but the same request shape
works with `"cluster": "msRelativeHumidity"` if that changes.

This configuration lives only on the physical device's firmware (and Z2M's
local cache of it) — there's no YAML record. A factory reset or re-pair
silently reverts it to the 1.0°C default with nothing in git to catch the
drift, so re-check `configured_reportings` in `zigbee2mqtt/bridge/devices`
after any re-pairing.

## Adding a new Matter-over-Thread device

**Don't commission Thread devices from the phone.** There are two separate
Thread networks in the house, OTBR's `WilkieMatterNet` (the one Home
Assistant uses) and the SmartThings Hub's `ST-3011356111`. The HA Companion
app hands the Bluetooth step to Google Play Services, which gives the device
whichever network *Google* prefers. On 2026-10-02 that was the SmartThings
one, and after clearing Play Services' data the phone stopped commissioning
at all ("Failed to generate credentials"). Full investigation:
`../ThreadRouter/CLAUDE.md`.

Instead, commission from wyse itself. `matter-server` has its own Bluetooth
radio and joins the device straight to OTBR's network and into HA.

1. **Power the device and put it in pairing mode.** A brand-new device is
   already in pairing mode. For a previously-paired one, factory-reset it
   (for one of our own ESP32 boards: `idf.py -p <COMx> erase-flash` then
   `idf.py -p <COMx> flash`). It needs to be within Bluetooth range of
   wyse; the same room works.
2. **Get its pairing code.** Use the 11-digit "manual pairing code" on the
   label or box, or the `MT:...` string encoded in its QR code. Our own
   ESP32 boards print both on the serial console at boot (`Manual pairing
   code: [...]`). ThreadRouter's is `34970112332`.
3. **Update wyse's repo checkout** so it has the current script:
   ```
   ssh mwilkie@192.168.15.30 git -C github/HomeAutomation pull
   ```
4. **Check prerequisites** (OTBR up, matter-server Bluetooth enabled,
   OTBR's network loaded into matter-server):
   ```
   ssh mwilkie@192.168.15.30 github/HomeAutomation/Wyse5070DebSetup/commission-thread-device.sh --check
   ```
   Expect `Prerequisites OK.`
5. **Commission:**
   ```
   ssh mwilkie@192.168.15.30 github/HomeAutomation/Wyse5070DebSetup/commission-thread-device.sh 34970112332
   ```
   This takes 1-2 minutes. On success it prints the Matter node ID, the
   device's name once HA has added it, and OTBR's router/child tables.
6. **Verify it's on the right network.** The device should show up in
   OTBR's child table, or its router table after about 2 minutes if it's
   router-capable. The script prints OTBR's partition as e.g. `758061476
   (0x2d2f19a4)`. If you can see the device's own logs, they must show
   `Partition ID 0x2d2f19a4`. `0x4d6f48d3` means it joined the SmartThings
   network.
7. **Rename/assign an area in HA** under Settings -> Devices & services ->
   Matter, same as any other device.

If step 5 fails partway, the device's pairing window may have closed; power
cycle it and rerun. The script exits non-zero with matter-server's error
code on failure.

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

## Scheduled jobs (cron, not a container)

- **daily-brief-cc** (active) — pulls from `mcp-gateway-todo`/`-trilium`/
  `-monarch` on `127.0.0.1` (no token needed -- it runs on this same host)
  plus Claude Code CLI's built-in web search, generated by **Claude Code
  CLI authenticated with a Pro/Max subscription token** (`claude
  setup-token`, stored as `CLAUDE_CODE_OAUTH_TOKEN` -- not a metered API
  key), then emails the result via Microsoft Graph (`me/sendMail`) using
  its own independent OAuth grant against the same Azure App Registration
  the Microsoft To Do MCP gateway uses (see `graph_login.py`) -- not SMTP.
  Deployed by `setup-daily-brief-cc.sh` to
  `/mnt/data/appdata/daily-brief-cc/{app,config}`; runs via crontab
  (`run.sh` at 6:30am; a companion `monarch_refresh.sh` at 6am, since
  Monarch's account sync takes a few minutes of lead time). Runs on this
  host specifically so it never depends on Claude's own cloud-scheduled
  jobs, which run in Anthropic's sandbox and have no path to these
  gateways' `127.0.0.1`-only bindings. **Important:** invokes `claude`
  *without* `--bare` -- `--bare` forces API-key-only auth and explicitly
  disables OAuth token auth, which defeats the entire point of this
  project (confirmed by testing, not just docs). The Node.js/npm this
  needs is nvm-managed (`~/.nvm`), not the apt `nodejs` package (whose
  `npm` requires enabling corepack separately) -- `run.sh` and
  `monarch_refresh.sh` both source `~/.nvm/nvm.sh` since cron doesn't load
  `.bashrc`. Log: `~/daily-brief-cc.log`. Secrets and the actual brief
  prompt live in `config/.env` / `app/prompt.md` /
  `config/graph_token_cache.json`, never committed. See the script's
  header comment for one-time setup.
- **daily-brief** (disabled 2026-09-22, kept for reference/rollback) — the
  original implementation: same idea, but driven by the Anthropic API
  directly (a metered `ANTHROPIC_API_KEY`, Sonnet 5, manual prompt-caching)
  instead of Claude Code CLI. Superseded by daily-brief-cc once subscription
  auth was confirmed working end-to-end, to avoid the per-token API billing
  (~$12/month at the tuned/cached usage this had reached). Its crontab
  lines are commented out, not removed; its files under
  `/mnt/data/appdata/daily-brief/` are untouched. See
  `setup-daily-brief.sh`'s header comment if reviving it.

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
connect to. `mcp-gateway-todo`, `mcp-gateway-trilium`, and
`mcp-gateway-monarch` are always-on Docker containers that wrap upstream
stdio MCP servers (Microsoft To Do, Trilium, and
[robcerda/monarch-mcp-server](https://github.com/robcerda/monarch-mcp-server)
respectively) with [`supergateway`](https://github.com/supercorp-ai/supergateway)
to expose them over Streamable HTTP instead, without modifying any
upstream server's code.

All three gateways bind to `127.0.0.1` only — not reachable from the LAN or
WAN, only from Caddy running on this same host, which is expected to be the
thing enforcing access control in front of them (`supergateway`'s HTTP
server mode has no built-in inbound auth of its own).

The Microsoft To Do gateway uses its **own, separate OAuth grant** — not the
one `microsoft-todo-mcp`/Claude Desktop uses — since two independent
long-running consumers refreshing from the same `tokens.json` would race
and invalidate each other's access token. Desktop's existing config is
untouched by either gateway. The Monarch gateway's one-time login has to
run *inside* its own container too (needs the Python version the upstream
package requires, and needs an environment with no OS keyring so the
session token falls back to a plaintext file the container can read). See
each script's header comment for the one-time setup needed before first
run.

## Internet-facing entry point: Caddy

`setup-caddy.sh` deploys Caddy (Docker, `network_mode: host`) as the only
thing this host exposes to the internet — automatic Let's Encrypt TLS for
`wilkiefamily.duckdns.org`, reverse-proxying by path to the MCP gateways
above (`/todo/*` → `mcp-gateway-todo`, `/trilium/*` →
`mcp-gateway-trilium`, `/monarch/*` → `mcp-gateway-monarch`). Requires
ports 80 and 443 forwarded from pfSense to this host (`192.168.15.30`) —
80 for Let's Encrypt's renewal challenge, not just 443.

Since none of the gateways authenticate inbound requests on their own,
Caddy gates every path on a static token (auto-generated on first run into
`/mnt/data/appdata/caddy/.env`, never committed) — embedded as a URL path
segment rather than a header, since Claude's custom-connector UI only
offers a single URL field (plus optional OAuth client ID/secret), with no
way to attach a custom header. External URLs (see `setup-caddy.sh`'s
output for the actual token):

- `https://wilkiefamily.duckdns.org/todo/<token>/mcp`
- `https://wilkiefamily.duckdns.org/trilium/<token>/mcp`
- `https://wilkiefamily.duckdns.org/monarch/<token>/mcp`

The Caddyfile also carries a second site, `mayhem-racing.duckdns.org`
(RaceTimes, basic auth), in a block between `# BEGIN racetimes` /
`# END racetimes` markers that RaceTimes' own `racetimes-users.sh`
regenerates. `setup-caddy.sh` rewrites the file but keeps any such marked
block, so re-running it doesn't take RaceTimes offline. That container is
also firewalled off from the LAN and this host by `racetimes-egress.service`
(iptables `RT-EGRESS` / `DOCKER-USER` / `INPUT` rules) — see the RaceTimes
README before touching those chains.

## Setup scripts, for reference

Each service above is deployed by the correspondingly-named script in this
directory (`setup-homeassistant.sh`, `setup-mg24.sh` for OTBR,
`setup-zigbee2mqtt.sh`, `setup-zwave-js-ui.sh`, `setup-matter-server.sh`,
`setup-mosquitto.sh`, `setup-trilium.sh`, `setup-mcp-gateway-todo.sh`,
`setup-mcp-gateway-trilium.sh`, `setup-mcp-gateway-monarch.sh`,
`setup-caddy.sh`, `setup-dashboard.sh`, `setup-daily-brief.sh`,
`setup-daily-brief-cc.sh`).
Re-running any of them is safe/idempotent and will recreate that one
container (or, for the daily-brief scripts, that one venv) with current
settings.
