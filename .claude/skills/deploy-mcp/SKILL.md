---
name: deploy-mcp
description: Containerize and expose an MCP server on wyse behind Caddy + DuckDNS
---
Given an MCP server name and upstream command:
0. Source comes from GitHub, never copied from the laptop (no scp/rsync): commit and push, then on wyse clone/`git pull --ff-only` into `~/github/<repo>` and build from that clone. The deploy script should do the pull itself (see personal-shopper's `deploy/setup-mcp-gateway-shopper.sh`). Only secrets go over SSH, into the host's gitignored `.env`.
1. Write a Dockerfile with PINNED versions for every dependency (especially `mcp`).
2. Write a checked-in `Wyse5070DebSetup/setup-mcp-gateway-<name>.sh` that writes `/mnt/data/appdata/mcp-gateway-<name>/docker-compose.yml`, bound to 127.0.0.1 on the next free 86xx port (see README's port table), matching the existing gateway scripts.
3. Add a `handle_path /<name>/{$MCP_AUTH_TOKEN}/*` route to `setup-caddy.sh` on wilkiefamily.duckdns.org (already has a cert). Only if a NEW hostname is needed, echo it back to me for confirmation BEFORE generating certs.
4. Never emit placeholder tokens — read real values from the host .env.
5. Deploy: `docker compose up -d && docker compose logs --tail=50 <name>`
6. Verify: POST an MCP `initialize` to `https://wilkiefamily.duckdns.org/<name>/<token>/mcp` and show me the response.
7. Add it to the dashboard (`Wyse5070DebSetup/dashboard/index.html`, run `setup-dashboard.sh` on wyse) and to `Wyse5070DebSetup/README.md`'s port table, and make sure its compose service has `restart: unless-stopped` so it survives a reboot.
Do not report success until steps 5, 6 and 7 are all done.
