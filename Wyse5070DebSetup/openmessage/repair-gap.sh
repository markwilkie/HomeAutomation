#!/usr/bin/env bash
#
# repair-gap.sh
# Fills a gap in the texts archive after OpenMessage was offline for a long
# time (hours to weeks): a window backfill re-reads every conversation with
# activity since SINCE, all the way down to SINCE, in every folder. The
# quick catch-up on reconnect can't do that -- it only checks the 50 most
# recent conversations, at most 120 messages each, and never revisits a
# hole it leaves. texts-mcp runs this same backfill by itself after an
# outage over 3h; this is the manual way, e.g. when its automatic tries keep
# failing because the phone is asleep.
#
# Needs the phone awake: unlock it and open Google Messages first.
#
# Usage (from the PC; SINCE is a date like 2026-11-03, or an RFC 3339 time):
#   ssh 192.168.15.30 ~/github/HomeAutomation/Wyse5070DebSetup/openmessage/repair-gap.sh SINCE

set -euo pipefail

SINCE="${1:?usage: repair-gap.sh SINCE  (e.g. 2026-11-03)}"
status() { docker exec openmessage wget -qO- http://localhost:7007/api/backfill/status; }

until status | grep -q '"running":false'; do
  echo "==> Another sync is running; waiting..."
  sleep 15
done

echo "==> Backfilling texts since ${SINCE}"
docker exec openmessage wget -qO- --header "Content-Type: application/json" \
  --post-data "{\"since\":\"${SINCE}\"}" http://localhost:7007/api/backfill
echo

sleep 5
while status | grep -q '"running":true'; do
  sleep 15
done

result="$(status)"
echo "${result}"
if echo "${result}" | grep -q '"errors":0'; then
  echo "==> Done: gap filled."
else
  echo "==> Finished with errors (usually the phone was asleep). Wake the phone and run this again."
  exit 1
fi
