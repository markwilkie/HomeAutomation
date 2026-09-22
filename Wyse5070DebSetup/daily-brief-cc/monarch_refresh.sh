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

echo "[$(date -Iseconds)] Requesting Monarch account refresh..."

# Same reasoning as run.sh's set +e/-e block: CLAUDE_CODE_OAUTH_TOKEN is a
# one-year token with no auto-renewal, and this call fails before the model
# ever runs if it's expired -- catch that here too (30 min before run.sh
# would otherwise be the first to notice) rather than let it fail silently
# into the cron log.
set +e
RESULT="$(claude -p "Call the monarch refresh_accounts tool with no arguments (refreshes all accounts). Report only whether it succeeded or failed, in one short line." \
  --mcp-config mcp.json \
  --allowedTools "mcp__monarch__refresh_accounts" \
  --permission-mode dontAsk \
  --output-format text)"
CLAUDE_EXIT=$?
set -e

echo "${RESULT}"

if [ "${CLAUDE_EXIT}" -ne 0 ] || printf '%s' "${RESULT}" | grep -qi "not logged in\|please run.*login"; then
  echo "[$(date -Iseconds)] Claude Code CLI failed (exit=${CLAUDE_EXIT}). Sending fallback reminder email." >&2
  FALLBACK="Monarch account refresh could not run -- Claude Code CLI failed, likely an expired subscription token.

To fix:
1. On any machine with Claude Code CLI and a browser, run: claude setup-token
2. Update CLAUDE_CODE_OAUTH_TOKEN in /mnt/data/appdata/daily-brief-cc/config/.env on wyse with the new token.

Raw output from claude (exit code ${CLAUDE_EXIT}):
${RESULT}"
  echo "${FALLBACK}" | venv/bin/python send_mail.py --subject "Monarch Refresh FAILED - action needed"
  exit 1
fi
