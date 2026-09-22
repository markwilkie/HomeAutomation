#!/usr/bin/env bash
#
# run.sh
# Generates the brief via Claude Code CLI (subscription auth, not a metered
# API key -- see CLAUDE_CODE_OAUTH_TOKEN in ../config/.env, created with
# `claude setup-token`), then emails it via send_mail.py (Microsoft Graph,
# same as the API-key-based daily-brief project, but its own independent
# OAuth grant -- see graph_login.py).
#
# Runs entirely on this host, same reasoning as the other daily-brief
# project: Claude's own cloud-scheduled jobs can't reach these gateways'
# 127.0.0.1-only bindings, so this runs as a local cron job instead.
#
# Usage:
#   ./run.sh            # generates and emails
#   ./run.sh --dry-run  # generates and prints instead of emailing

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# cron doesn't source .bashrc, so `claude` (installed via nvm-managed npm)
# isn't on PATH without this -- see setup-daily-brief-cc.sh's notes.
export NVM_DIR="$HOME/.nvm"
# shellcheck disable=SC1091
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"

set -a
source ../config/.env
set +a

TODAY="$(date +%Y-%m-%d)"
WEEKDAY="$(date +%A)"

echo "[$(date -Iseconds)] Generating brief via Claude Code CLI..."

# No --bare: confirmed by testing that it forces API-key-only auth and
# explicitly disables OAuth token auth ("OAuth and keychain are never
# read" per `claude --help`) -- exactly the subscription auth this project
# exists to use. Safe to omit here since this directory has no stray
# CLAUDE.md/hooks/skills for --bare to have protected us from anyway.
#
# set +e/-e around this call: the CLAUDE_CODE_OAUTH_TOKEN is a one-year
# token with no auto-renewal (see setup-daily-brief-cc.sh's prerequisites).
# When it expires, `claude` fails before the model ever runs, so prompt.md's
# own instructions can't help -- this script has to detect that itself and
# email a fix directly, or a silent cron-log failure could go unnoticed for
# a long time.
set +e
BRIEF="$(claude -p "Generate today's brief. Today is ${TODAY}, a ${WEEKDAY}." \
  --append-system-prompt-file prompt.md \
  --mcp-config mcp.json \
  --allowedTools "mcp__todo__get-task-lists,mcp__todo__get-task-lists-organized,mcp__todo__get-tasks,mcp__trilium__search_notes,mcp__trilium__list_children_notes,mcp__trilium__get_note,mcp__trilium__resolve_note_id,mcp__monarch__get_accounts,mcp__monarch__get_transactions,mcp__monarch__search_transactions,WebSearch" \
  --permission-mode dontAsk \
  --output-format text)"
CLAUDE_EXIT=$?
set -e

if [ "${CLAUDE_EXIT}" -ne 0 ] || [ -z "${BRIEF}" ] || printf '%s' "${BRIEF}" | grep -qi "not logged in\|please run.*login"; then
  echo "[$(date -Iseconds)] Claude Code CLI failed (exit=${CLAUDE_EXIT}): ${BRIEF}" >&2
  FALLBACK="Today's brief could not be generated -- Claude Code CLI failed, likely an expired subscription token.

To fix:
1. On any machine with Claude Code CLI and a browser, run: claude setup-token
2. Update CLAUDE_CODE_OAUTH_TOKEN in /mnt/data/appdata/daily-brief-cc/config/.env on wyse with the new token.

Raw output from claude (exit code ${CLAUDE_EXIT}):
${BRIEF}"
  if [ "${1:-}" = "--dry-run" ]; then
    echo ""
    echo "----- FALLBACK (dry run, not emailed) -----"
    echo ""
    echo "${FALLBACK}"
  else
    echo "${FALLBACK}" | venv/bin/python send_mail.py --subject "Daily Brief FAILED - action needed"
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
  echo "$BRIEF" | venv/bin/python send_mail.py --subject "Daily Brief (Claude Code)"
fi
