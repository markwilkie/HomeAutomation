---
name: deploy-mcp
description: Containerize and expose an MCP server on wyse behind Caddy + DuckDNS
---
Given an MCP server name and upstream command:
0. Source comes from GitHub, never copied from the laptop (no scp/rsync): commit and push, then on wyse clone/`git pull --ff-only` into `~/github/<repo>` and build from that clone. The deploy script should do the pull itself (see personal-shopper's `deploy/setup-mcp-gateway-shopper.sh`). Only secrets go over SSH, into the host's gitignored `.env`.
1. Write a Dockerfile with PINNED versions for every dependency (especially `mcp`).
2. Add a service to /opt/stacks/mcp/docker-compose.yml matching the existing supergateway pattern.
3. Add a Caddy site block for <name>.<duckdns-host>; echo the exact hostname back to me for confirmation BEFORE generating certs.
4. Never emit placeholder tokens — read real values from the host .env.
5. Deploy: `docker compose up -d && docker compose logs --tail=50 <name>`
6. Verify: `curl -sS https://<name>.<duckdns-host>/health` and show me output.
Do not report success until steps 5 and 6 both pass.
