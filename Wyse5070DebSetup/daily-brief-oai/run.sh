#!/usr/bin/env bash
#
# run.sh
# Generates the brief on Azure OpenAI (brief.py) and emails it via
# send_mail.py (shared with daily-brief-cc, using this project's own Graph
# grant). If brief.py fails (e.g. Azure credit exhausted -> 401), falls back
# to daily-brief-cc's run.sh (Claude Code CLI on the Claude subscription),
# which emails its own brief, or its own failure notice if that fails too.
#
# Usage:
#   ./run.sh            # generates and emails
#   ./run.sh --dry-run  # generates and prints instead of emailing

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

echo "[$(date -Iseconds)] Generating brief via Azure OpenAI..."

ERR_LOG="$(mktemp)"
trap 'rm -f "${ERR_LOG}"' EXIT

set +e
BRIEF="$(venv/bin/python brief.py 2> >(tee "${ERR_LOG}" >&2))"
EXIT_CODE=$?
set -e

if [ "${EXIT_CODE}" -ne 0 ]; then
  echo "[$(date -Iseconds)] brief.py failed (exit=${EXIT_CODE}); falling back to Claude Code (daily-brief-cc)" >&2
  # Logs from here on go to this log, not daily-brief-cc.log.
  exec /mnt/data/appdata/daily-brief-cc/app/run.sh "$@"
fi

echo "[$(date -Iseconds)] Generated brief: ${#BRIEF} chars"

if [ "${1:-}" = "--dry-run" ]; then
  echo ""
  echo "----- BRIEF (dry run, not emailed) -----"
  echo ""
  echo "$BRIEF"
else
  echo "$BRIEF" | venv/bin/python send_mail.py --subject "Daily Brief (OpenAI)" --also-to jennie@wilkiefamily.net
fi
