#!/usr/bin/env bash
#
# run.sh
# Generates the brief on Azure OpenAI (brief.py) and emails it via
# send_mail.py (shared with daily-brief-cc, using this project's own Graph
# grant). On failure, emails what went wrong instead of failing silently in
# the cron log.
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
  echo "[$(date -Iseconds)] brief.py failed (exit=${EXIT_CODE})" >&2
  FALLBACK="Today's OpenAI brief could not be generated (brief.py exit ${EXIT_CODE}).

Last log lines:
$(tail -20 "${ERR_LOG}")"
  if [ "${1:-}" = "--dry-run" ]; then
    echo "${FALLBACK}"
  else
    echo "${FALLBACK}" | venv/bin/python send_mail.py --subject "Daily Brief (OpenAI) FAILED"
  fi
  exit 1
fi

echo "[$(date -Iseconds)] Generated brief: ${#BRIEF} chars"

if [ "${1:-}" = "--dry-run" ]; then
  echo ""
  echo "----- BRIEF (dry run, not emailed) -----"
  echo ""
  echo "$BRIEF"
else
  echo "$BRIEF" | venv/bin/python send_mail.py --subject "Daily Brief (OpenAI)"
fi
