#!/usr/bin/env bash
#
# run_events.sh
# The weekly "Weekend & Ahead" events email (events.md), to Mark and Jennie:
#   Thursday 5:00am  FULL run: deep search -- this weekend, next week, plan
#                    ahead (2-6 weeks), concerts. Builds on last Saturday's
#                    email for the "Still running" line.
#   Saturday 8:00am  REFRESH run: Thursday's email plus the Nextdoor digest
#                    (arrives Thu ~8am, after the full run), cancellations,
#                    today's weather; Friday dropped. Jennie reads this one.
# Each email's markdown is kept in ../state/events-<date>.md for the next run.
# If brief.py fails, falls back to Claude Code (daily-brief/claude/run.sh, deployed under daily-brief-cc, with the
# same events prompt), like run.sh does for the daily brief.
#
# Usage:
#   ./run_events.sh                 # mode from the weekday (Thu full, Sat refresh)
#   ./run_events.sh full|refresh    # force a mode
#   ./run_events.sh full --dry-run  # print instead of emailing

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

MODE="${1:-}"
DRY="${2:-}"
if [ "${MODE}" = "--dry-run" ]; then MODE=""; DRY="--dry-run"; fi
if [ -z "${MODE}" ]; then
  case "$(date +%u)" in
    4) MODE=full ;;
    6) MODE=refresh ;;
    *) echo "Not Thursday or Saturday; pass full or refresh" >&2; exit 2 ;;
  esac
fi

STATE_DIR="../state"
mkdir -p "${STATE_DIR}"
TODAY="$(date +%Y-%m-%d)"

# The email to build on: refresh needs this week's Thursday (within 3 days) or
# it does a full run; full uses the newest one from the last 8 days, if any.
PREVIOUS=""
if [ "${MODE}" = "refresh" ]; then
  THU="$(date -d 'last thursday' +%Y-%m-%d)"
  [ -f "${STATE_DIR}/events-${THU}.md" ] && PREVIOUS="${STATE_DIR}/events-${THU}.md"
  if [ -z "${PREVIOUS}" ]; then
    echo "[$(date -Iseconds)] No Thursday events email found; doing a full run instead" >&2
    MODE=full
  fi
fi
if [ "${MODE}" = "full" ]; then
  PREVIOUS="$(find "${STATE_DIR}" -name 'events-*.md' -mtime -8 ! -name "events-${TODAY}.md" | sort | tail -1)"
fi

if [ "${MODE}" = "full" ]; then
  SUBJECT="Weekend & Ahead: $(date +'%a %b %-d')"
else
  SUBJECT="Weekend & Ahead (updated): $(date +'%a %b %-d')"
fi

# Replies to earlier emails first, so they shape this one. Never blocks the email.
venv/bin/python replies.py ${DRY} || echo "[$(date -Iseconds)] replies.py failed; continuing" >&2

echo "[$(date -Iseconds)] Events email (${MODE}) via Azure OpenAI; previous: ${PREVIOUS:-none}"

set +e
EMAIL="$(venv/bin/python brief.py --events "${MODE}" ${PREVIOUS:+--previous "${PREVIOUS}"})"
EXIT_CODE=$?
set -e

if [ "${EXIT_CODE}" -ne 0 ]; then
  echo "[$(date -Iseconds)] brief.py failed (exit=${EXIT_CODE}); falling back to Claude Code (daily-brief-cc)" >&2
  REQUEST="$(venv/bin/python -c 'import sys, brief; from pathlib import Path; p = sys.argv[2]; print(brief.events_request(sys.argv[1], Path(p) if p else None))' "${MODE}" "${PREVIOUS}")"
  export BRIEF_PROMPT_FILE=events.md BRIEF_REQUEST="${REQUEST}" BRIEF_SUBJECT="${SUBJECT}" \
         BRIEF_SAVE_TO="$(pwd)/${STATE_DIR}/events-${TODAY}.md"
  exec /mnt/data/appdata/daily-brief-cc/app/run.sh ${DRY}
fi

echo "[$(date -Iseconds)] Generated events email: ${#EMAIL} chars"

if [ "${DRY}" = "--dry-run" ]; then
  echo ""
  echo "----- EVENTS EMAIL (dry run, not emailed or saved) -----"
  echo ""
  echo "${EMAIL}"
else
  echo "${EMAIL}" > "${STATE_DIR}/events-${TODAY}.md"
  # Footer only in the sent email, not the saved copy Saturday builds on.
  FOOTER="_To adjust these picks, reply with one command per line: **skip** thing · **more** thing · **undo** · **show**. For example: “skip ice skating”. Bigger changes are made directly, not by reply._"
  printf '%s\n\n---\n\n%s\n' "${EMAIL}" "${FOOTER}" | venv/bin/python send_mail.py --subject "${SUBJECT}" --also-to jennie@wilkiefamily.net
fi
