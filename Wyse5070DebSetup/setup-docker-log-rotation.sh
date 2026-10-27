#!/usr/bin/env bash
#
# setup-docker-log-rotation.sh
# Makes every container's log rotate. Docker's default json-file driver
# never rotates unless told to, and until 2026-10-27 nothing on this host
# set it -- all 18 containers had unbounded logs (otbr's, at debug level,
# had grown since 2026-08-08). This sets a daemon-wide default in
# /etc/docker/daemon.json (keeping whatever else is there, e.g. data-root
# from setup-containerd-storage.sh), restarts Docker, then recreates every
# compose-managed container so it picks the default up -- log options are
# fixed when a container is created, so a restart alone isn't enough.
#
# otbr is not compose-managed: setup-mg24-production.sh sets its own
# (larger) rotation on its `docker run` and recreates it.
#
# Restarting Docker briefly stops every container (they come back via their
# restart policies), and recreating a container discards its old log.
#
# Usage:
#   ./setup-docker-log-rotation.sh   # prompts for sudo once

set -euo pipefail

DAEMON_JSON="/etc/docker/daemon.json"
MAX_SIZE="20m"
MAX_FILE="5"

echo "==> Setting log rotation default (${MAX_FILE} x ${MAX_SIZE}) in ${DAEMON_JSON}"
NEW_JSON="$(sudo cat "${DAEMON_JSON}" 2>/dev/null | python3 -c '
import json, sys
text = sys.stdin.read().strip()
config = json.loads(text) if text else {}
config["log-driver"] = "json-file"
config.setdefault("log-opts", {}).update({"max-size": sys.argv[1], "max-file": sys.argv[2]})
print(json.dumps(config, indent=2))
' "${MAX_SIZE}" "${MAX_FILE}")"
echo "${NEW_JSON}"
echo "${NEW_JSON}" | sudo tee "${DAEMON_JSON}" > /dev/null

echo "==> Restarting Docker"
sudo systemctl restart docker
docker info --format 'Log driver: {{.LoggingDriver}}'

echo "==> Recreating compose-managed containers so they pick up the default"
docker ps -a --format '{{.Label "com.docker.compose.project.working_dir"}}' | sort -u | while read -r DIR; do
  [ -z "${DIR}" ] && continue
  echo "--> ${DIR}"
  (cd "${DIR}" && docker compose up -d --force-recreate)
done

echo ""
echo "==> Log options per container (otbr shows its own from setup-mg24-production.sh):"
docker ps -a --format '{{.Names}}' | while read -r NAME; do
  echo "  ${NAME}: $(docker inspect "${NAME}" --format '{{json .HostConfig.LogConfig.Config}}')"
done
