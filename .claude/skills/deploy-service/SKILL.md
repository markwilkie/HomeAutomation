---
name: deploy-service
description: Containerize and expose any Dockerized service on wyse behind Caddy + DuckDNS
---
Given a service name, source location, and upstream port:
1. Write a Dockerfile (or reuse an existing one) with PINNED versions for every dependency — no floating majors.
2. Add a service to the appropriate docker-compose.yml on wyse, matching existing service patterns already in that stack.
3. Add a Caddy site block for <name>.<duckdns-host>; echo the exact hostname back to me for confirmation BEFORE generating certs.
4. Never emit placeholder tokens (YOURTOKEN, CHANGEME, example.com) — read real values from the host's gitignored `.env` or ask me for them.
5. Deploy: `docker compose up -d && docker compose logs --tail=50 <name>` and show me the logs.
6. Verify: curl the public HTTPS endpoint with real cert validation and show me the response.
Do not report the service as live until steps 5 and 6 both pass with no errors.
