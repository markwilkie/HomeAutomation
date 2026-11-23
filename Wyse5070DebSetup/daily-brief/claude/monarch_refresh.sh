#!/usr/bin/env bash
#
# monarch_refresh.sh
# Claude Code CLI equivalent of the API-key-based project's
# monarch_refresh.py: kicks off a Monarch Money account refresh via the
# local MCP gateway. Run at 6am, 30 minutes before run.sh's 6:30am brief,
# so Monarch's sync with the underlying financial institutions (which
# takes a few minutes, not instant) has finished in time.
#
# This is a trivial single tool call with no real reasoning involved, so
# routing it through the LLM is purely for auth-story consistency (one
# less thing depending on the metered API key) -- see run.sh's header
# comment for why this whole project runs via Claude Code CLI + subscription
# auth instead.
#
# Usage:
#   ./monarch_refresh.sh

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

export NVM_DIR="$HOME/.nvm"
# shellcheck disable=SC1091
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"

set -a
source ../config/.env
set +a
# Same auth as run.sh: a persisted `claude login` session, not the
# CLAUDE_CODE_OAUTH_TOKEN env var -- see run.sh's header comment for why.
unset CLAUDE_CODE_OAUTH_TOKEN

echo "[$(date -Iseconds)] Requesting Monarch account refresh..."

# --output-format json + parse_claude_result.py, same reasoning as run.sh:
# `text` output goes through a terminal-oriented renderer, and the JSON
# result field is more reliable to check for failure than string-matching.
#
# Same set +e/-e reasoning as run.sh: CLAUDE_CODE_OAUTH_TOKEN is a one-year
# token with no auto-renewal, and this call fails before the model ever
# runs if it's expired -- catch that here too (30 min before run.sh would
# otherwise be the first to notice) rather than let it fail silently into
# the cron log.
set +e
CLAUDE_OUTPUT="$(claude -p "Call the monarch refresh_accounts tool with no arguments (refreshes all accounts). Report only whether it succeeded or failed, in one short line." \
  --mcp-config mcp.json \
  --allowedTools "mcp__monarch__refresh_accounts" \
  --permission-mode dontAsk \
  --output-format json)"
CLAUDE_EXIT=$?
RESULT="$(printf '%s' "${CLAUDE_OUTPUT}" | python3 parse_claude_result.py 2>/tmp/daily-brief-cc-parse-error.log)"
PARSE_EXIT=$?
set -e

if [ "${CLAUDE_EXIT}" -ne 0 ] || [ "${PARSE_EXIT}" -ne 0 ]; then
  PARSE_ERROR="$(cat /tmp/daily-brief-cc-parse-error.log 2>/dev/null || true)"
  RESULT="${PARSE_ERROR:-${CLAUDE_OUTPUT}}"
  echo "[$(date -Iseconds)] Claude Code CLI failed (exit=${CLAUDE_EXIT}, parse_exit=${PARSE_EXIT}). Sending fallback reminder email." >&2
  FALLBACK="Monarch account refresh could not run -- Claude Code CLI failed, likely an expired login session.

To fix:
1. SSH to wyse: ssh -t mwilkie@192.168.15.30
2. Run: export NVM_DIR=\$HOME/.nvm && . \"\$NVM_DIR/nvm.sh\" && claude login
3. Follow the URL/code prompt to re-authenticate.

Raw output from claude (exit code ${CLAUDE_EXIT}):
${RESULT}"
  echo "${FALLBACK}" | venv/bin/python send_mail.py --subject "Monarch Refresh FAILED - action needed"
  exit 1
fi

echo "${RESULT}"
