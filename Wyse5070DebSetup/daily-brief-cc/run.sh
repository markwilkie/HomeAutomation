#!/usr/bin/env bash
#
# run.sh
# Generates the brief via Claude Code CLI (subscription auth, not a metered
# API key), then emails it via send_mail.py (Microsoft Graph, same as the
# API-key-based daily-brief project, but its own independent OAuth grant --
# see graph_login.py).
#
# Auth: a persisted `claude login` session (~/.claude/.credentials.json on
# this host), NOT the CLAUDE_CODE_OAUTH_TOKEN env var (still in ../config/.env
# as a backup/rollback value, but deliberately unset below) -- confirmed by
# testing that CLAUDE_CODE_OAUTH_TOKEN auth cannot see claude.ai Connectors
# (Google Calendar, Microsoft 365) at all ("it can't ... fetch claude.ai
# connectors" per `claude`'s own docs), which section 8 of prompt.md needs.
# `claude login` was completed once, interactively, directly on this host.
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
# See header comment: unset so the persisted `claude login` session (with
# claude.ai Connectors) is used instead of token auth (which hides them).
unset CLAUDE_CODE_OAUTH_TOKEN

TODAY="$(date +%Y-%m-%d)"
WEEKDAY="$(date +%A)"

echo "[$(date -Iseconds)] Generating brief via Claude Code CLI..."

# No --bare: confirmed by testing that it forces API-key-only auth and
# explicitly disables OAuth token auth ("OAuth and keychain are never
# read" per `claude --help`) -- exactly the subscription auth this project
# exists to use. Safe to omit here since this directory has no stray
# CLAUDE.md/hooks/skills for --bare to have protected us from anyway.
#
# --output-format json, not text: `text` runs the response through a
# terminal-oriented renderer that was observed reflowing long bullet lists
# (Concerts, To Do) into run-on lines joined by " - " instead of real
# newlines, breaking markdown list rendering in the email. JSON's `result`
# field carries the model's raw text untouched -- see parse_claude_result.py.
#
# set +e/-e around this call: the CLAUDE_CODE_OAUTH_TOKEN is a one-year
# token with no auto-renewal (see setup-daily-brief-cc.sh's prerequisites).
# When it expires, `claude` fails before the model ever runs, so prompt.md's
# own instructions can't help -- this script has to detect that itself and
# email a fix directly, or a silent cron-log failure could go unnoticed for
# a long time.
set +e
CLAUDE_OUTPUT="$(claude -p "Generate today's brief. Today is ${TODAY}, a ${WEEKDAY}." \
  --append-system-prompt-file prompt.md \
  --mcp-config mcp.json \
  --allowedTools "mcp__todo__get-task-lists,mcp__todo__get-task-lists-organized,mcp__todo__get-tasks,mcp__trilium__search_notes,mcp__trilium__list_children_notes,mcp__trilium__get_note,mcp__trilium__resolve_note_id,mcp__monarch__get_accounts,mcp__monarch__get_transactions,mcp__monarch__search_transactions,mcp__claude_ai_Google_Calendar__list_calendars,mcp__claude_ai_Google_Calendar__list_events,mcp__claude_ai_Google_Calendar__search_events,mcp__claude_ai_Microsoft_365__outlook_email_search,mcp__claude_ai_Microsoft_365__get_me,WebSearch" \
  --permission-mode dontAsk \
  --output-format json)"
CLAUDE_EXIT=$?
BRIEF="$(printf '%s' "${CLAUDE_OUTPUT}" | python3 parse_claude_result.py 2>/tmp/daily-brief-cc-parse-error.log)"
PARSE_EXIT=$?
set -e

if [ "${CLAUDE_EXIT}" -ne 0 ] || [ "${PARSE_EXIT}" -ne 0 ]; then
  PARSE_ERROR="$(cat /tmp/daily-brief-cc-parse-error.log 2>/dev/null || true)"
  BRIEF="${PARSE_ERROR:-${CLAUDE_OUTPUT}}"
  echo "[$(date -Iseconds)] Claude Code CLI failed (exit=${CLAUDE_EXIT}, parse_exit=${PARSE_EXIT}): ${BRIEF}" >&2
  FALLBACK="Today's brief could not be generated -- Claude Code CLI failed, likely an expired login session.

To fix:
1. SSH to wyse: ssh -t mwilkie@192.168.15.30
2. Run: export NVM_DIR=\$HOME/.nvm && . \"\$NVM_DIR/nvm.sh\" && claude login
3. Follow the URL/code prompt to re-authenticate.

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
