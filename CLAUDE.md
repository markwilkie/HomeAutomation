# HomeAutomation

## Environment Map
- `wyse` — Docker host for MCP gateways, Caddy reverse proxy, TripTracker. Secrets live in gitignored `.env` files on the host, NOT in this repo.
- Home Assistant — Tuya + BME280 climate control, Thread border router.
- Network — pfSense (DMZ + port forwarding), DuckDNS for dynamic DNS, Tailscale for private access.

Before proposing a networking or deployment change, state which host it runs on and check whether an existing pattern (Caddy site block, docker-compose service) already covers it.

## Host Access
- `wyse` (hostname `wilkie-home-server`, IP `192.168.15.30`) is directly SSH-reachable as user `mwilkie` via a key already trusted in `~/.ssh/config` — no password prompt. Use it directly (`docker ps`, `docker logs`, `docker exec`, reading `.env`/appdata files, `mosquitto_pub`/`mosquitto_sub` via the `mosquitto` container, etc.) to verify state or diagnose issues, rather than asking the user to paste output.
- Home Assistant, Zigbee2MQTT, and Z-Wave JS UI all run as containers on this host (`homeassistant`, `zigbee2mqtt`, `mosquitto`). Their live state (paired devices, entity registry, recorder history) is queryable by execing into the container rather than guessing from repo config alone — e.g. `docker exec homeassistant python3 -c "..."` against `/config/.storage/*` or `/config/home-assistant_v2.db`, or publishing to `zigbee2mqtt/bridge/request/...` topics via the `mosquitto` container for Zigbee2MQTT bridge commands.

## Networking & DNS Rules
- Public exposure pattern is: DuckDNS hostname → pfSense port forward → Caddy TLS reverse proxy → Docker container. Do not introduce Cloudflare zones or alternate DNS providers.
- Prefer Tailscale for anything that only needs private/device access; only expose publicly when an external service (e.g. Claude connectors, Google OAuth) requires it.
- Never leave placeholder values like `YOURTOKEN` or `example.com` in a command or config — read the real value from the host's `.env` or ask me for it.
- Always echo the exact hostname back to me for confirmation before generating certs or DDNS URLs.

## Deployment Verification
- Never report a service as 'live' until you have: (1) pinned all dependency versions in requirements.txt/package.json, (2) run `docker compose up -d && docker compose logs --tail=50` and shown me the logs, and (3) curled the public endpoint and shown the response.
- If a deploy fails, keep investigating root cause even if I say I'll work around it manually — tell me what you found.

## Interactive Steps
You cannot complete interactive OAuth logins, Windows UAC prompts, or TTY-based installers. When a plan requires one, stop and give me a numbered copy-paste block of exactly what to run, then wait for me to confirm before continuing.
