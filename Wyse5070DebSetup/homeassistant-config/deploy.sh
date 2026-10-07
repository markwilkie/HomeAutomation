#!/usr/bin/env bash
# Deploy configuration.yaml / automations.yaml from this directory to the
# live Home Assistant on wyse, the way it's been done by hand: refuse if the
# live file was edited outside this script, back up, copy in, config-check
# (rolling back on failure), then reload -- or restart with --restart.
#
#   ./deploy.sh            deploy changed files, reload YAML via the HA API
#   ./deploy.sh --restart  same, but full container restart (needed for new
#                          entity types/platforms, e.g. a new template block)
#   ./deploy.sh --force    deploy even if the live file has drifted
#
# Drift detection: each deploy records the deployed file's sha256 under
# /config/.deployed/; a live file that no longer matches was edited by
# hand on the box. Reload needs HA_TOKEN in /mnt/data/appdata/homeassistant/.env
# (a long-lived access token); without it this falls back to a restart.
set -euo pipefail

HOST="${WYSE_HOST:-mwilkie@192.168.15.30}"
FILES=(configuration.yaml automations.yaml)
RESTART=0
FORCE=0
for arg in "$@"; do
    case "$arg" in
        --restart) RESTART=1 ;;
        --force) FORCE=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done
cd "$(dirname "$0")"

remote() { ssh "$HOST" "$@"; }
sha() { sha256sum | cut -d' ' -f1; }

stamp=$(date +%Y%m%d-%H%M%S)
changed=()
for f in "${FILES[@]}"; do
    live_sha=$(remote "docker exec homeassistant cat /config/$f" | sha)
    local_sha=$(sha < "$f")
    if [[ "$live_sha" == "$local_sha" ]]; then
        echo "$f: unchanged"
        continue
    fi
    recorded=$(remote "docker exec homeassistant cat /config/.deployed/$f.sha256 2>/dev/null || true")
    if [[ -z "$recorded" ]]; then
        recorded=$(git show "HEAD:./$f" 2>/dev/null | sha || true)
    fi
    if [[ "$live_sha" != "$recorded" && $FORCE -eq 0 ]]; then
        echo "$f: live copy was edited on the box since the last deploy -- diff (live vs local):" >&2
        diff <(remote "docker exec homeassistant cat /config/$f") "$f" >&2 || true
        echo "Pull those edits into this repo first, or rerun with --force." >&2
        exit 1
    fi
    changed+=("$f")
done

if [[ ${#changed[@]} -eq 0 ]]; then
    echo "Nothing to deploy."
    exit 0
fi

for f in "${changed[@]}"; do
    scp -q "$f" "$HOST:/tmp/$f.deploy"
    remote "docker exec homeassistant cp /config/$f /config/$f.bak-$stamp \
        && docker cp /tmp/$f.deploy homeassistant:/config/$f && rm -f /tmp/$f.deploy"
    echo "$f: deployed (backup: $f.bak-$stamp)"
done

# check_config exits 0 even on "Incorrect config" (an invalid platform just
# gets dropped -- "Successful config (partial)"), so judge by its output too.
# Found 2026-10-06 when a bad statistics sensor deployed without rollback.
check_out=$(remote "docker exec homeassistant python3 -m homeassistant --script check_config -c /config" 2>&1)
check_rc=$?
echo "$check_out"
if [[ $check_rc -ne 0 ]] || grep -qE "Incorrect config|Invalid config|partial\)" <<<"$check_out"; then
    echo "Config check FAILED -- rolling back." >&2
    for f in "${changed[@]}"; do
        remote "docker exec homeassistant cp /config/$f.bak-$stamp /config/$f"
    done
    exit 1
fi

for f in "${changed[@]}"; do
    remote "docker exec homeassistant sh -c 'mkdir -p /config/.deployed && sha256sum /config/$f | cut -d\" \" -f1 > /config/.deployed/$f.sha256'"
done

# The token stays on the host: read and used in the same remote shell.
read_token='T=$(sed -n "s/^HA_TOKEN=//p" /mnt/data/appdata/homeassistant/.env 2>/dev/null)'
if [[ $RESTART -eq 0 ]] && remote "$read_token; test -n \"\$T\""; then
    remote "$read_token; curl -sf -X POST -H \"Authorization: Bearer \$T\" -H 'Content-Type: application/json' \
        http://localhost:8123/api/services/homeassistant/reload_all >/dev/null"
    echo "Reloaded YAML (homeassistant.reload_all)."
else
    [[ $RESTART -eq 0 ]] && echo "No HA_TOKEN on the host -- restarting instead of reloading."
    remote "docker restart homeassistant >/dev/null; until docker exec homeassistant python3 -c \
        \"import urllib.request; urllib.request.urlopen('http://localhost:8123/manifest.json', timeout=3)\" \
        2>/dev/null; do sleep 5; done"
    echo "Home Assistant restarted."
fi
