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
# It runs from wyse's clone of this repo (~/github/HomeAutomation, pulled
# first), not copied from here: commit and push the patch before running it.
set -uo pipefail

HOST="${WYSE_HOST:-mwilkie@192.168.15.30}"
patch="${1:?usage: patch-storage.sh <patch.py>}"
[ -f "$patch" ] || { echo "no such file: $patch" >&2; exit 1; }
rel=$(git ls-files --full-name --error-unmatch "$patch" 2>/dev/null) \
    || { echo "$patch isn't committed -- commit and push it first." >&2; exit 1; }
git diff --quiet HEAD -- "$patch" || { echo "$patch has uncommitted changes -- commit and push first." >&2; exit 1; }
git fetch -q && git merge-base --is-ancestor HEAD '@{u}' \
    || { echo "This checkout has commits not on GitHub -- push first." >&2; exit 1; }
remote_patch="/home/mwilkie/github/HomeAutomation/$rel"

ssh "$HOST" "git -C ~/github/HomeAutomation pull -q --ff-only" || exit 1
[[ "$(ssh "$HOST" "cat $remote_patch" | sha256sum)" == "$(sha256sum < "$patch")" ]] \
    || { echo "wyse's clone doesn't match $patch after pulling (local branch behind GitHub?)." >&2; exit 1; }
ssh "$HOST" "
    docker stop homeassistant >/dev/null
    docker run --rm -v /mnt/data/appdata/homeassistant/config:/config -v $remote_patch:/patch.py python:3-slim python3 /patch.py
    rc=\$?
    docker start homeassistant >/dev/null
    until docker exec homeassistant python3 -c \"import urllib.request; urllib.request.urlopen('http://localhost:8123/manifest.json', timeout=3)\" 2>/dev/null; do sleep 5; done
    echo \"Home Assistant up (patch exit code \$rc)\"
    exit \$rc
"
