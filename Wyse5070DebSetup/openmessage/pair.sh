#!/usr/bin/env bash
#
# pair.sh
# Re-pairs OpenMessage with my phone's Google Messages, using the Google
# session of the signed-in openmessage-chromium (no copying cookies out of a
# browser). Needed only when the pairing itself is gone: the phone unpaired
# it (Messages -> Device pairing), or session.json was lost. Expired Google
# cookies don't need this -- OpenMessage refreshes those from Chromium itself.
#
# Prints an emoji: tap the same one in Google Messages on the phone (the
# prompt appears there within a few seconds; phone unlocked, Messages open).
# Then the server restarts with the new pairing.
#
# If it fails with "missing required cookies", Chromium isn't signed in to
# Google any more: do the sign-in steps in setup-openmessage.sh, then re-run.
#
# Usage (from the PC):
#   ssh -t 192.168.15.30 ~/github/HomeAutomation/Wyse5070DebSetup/openmessage/pair.sh

set -euo pipefail

COMPOSE=(docker compose -f /mnt/data/appdata/openmessage/docker-compose.yml)

echo "==> Reading Google cookies from openmessage-chromium"
cookies="$("${COMPOSE[@]}" run --rm -T --no-deps --entrypoint /opt/openmessage/refresh-cookies openmessage --print-json)"

echo "==> Stopping openmessage for the pairing"
"${COMPOSE[@]}" stop openmessage

echo "==> Pairing -- tap the emoji below in Google Messages on the phone"
status=0
printf '%s' "${cookies}" | "${COMPOSE[@]}" run --rm -T --no-deps openmessage pair --google || status=$?

echo "==> Starting openmessage"
"${COMPOSE[@]}" start openmessage
exit "${status}"
