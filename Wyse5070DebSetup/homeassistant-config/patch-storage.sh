#!/usr/bin/env bash
# Run a Python patch against Home Assistant's .storage files on wyse with HA
# stopped -- the stop/edit/start cycle that YAML can't do (entity registry
# area/labels, restore_state seeding, dashboard storage, removing orphans).
# HA is ALWAYS started again afterwards, even if the patch fails: on
# 2026-10-05 a failing patch in a `stop && patch && start` chain left HA down
# for ~5 minutes.
#
#   ./patch-storage.sh my_patch.py
#
# The patch runs in a throwaway python:3-slim container with /config
# mounted; it should back up any file it writes (copy to <file>.bak-<tag>).
set -uo pipefail

HOST="${WYSE_HOST:-mwilkie@192.168.15.30}"
patch="${1:?usage: patch-storage.sh <patch.py>}"
remote_patch="/tmp/ha-storage-patch-$$.py"

scp -q "$patch" "$HOST:$remote_patch" || exit 1
ssh "$HOST" "
    docker stop homeassistant >/dev/null
    docker run --rm -v /mnt/data/appdata/homeassistant/config:/config -v $remote_patch:/patch.py python:3-slim python3 /patch.py
    rc=\$?
    docker start homeassistant >/dev/null
    rm -f $remote_patch
    until docker exec homeassistant python3 -c \"import urllib.request; urllib.request.urlopen('http://localhost:8123/manifest.json', timeout=3)\" 2>/dev/null; do sleep 5; done
    echo \"Home Assistant up (patch exit code \$rc)\"
    exit \$rc
"
