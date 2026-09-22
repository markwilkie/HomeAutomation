#!/usr/bin/env bash
#
# setup-daily-brief.sh
# Deploys the daily-brief script: a small Python program, run by cron
# directly on this host (not a container -- nothing else needs to reach
# it), that pulls from the local MCP gateways already running here
# (mcp-gateway-todo:8600, mcp-gateway-trilium:8601, mcp-gateway-monarch:8602
# -- see README.md's port table) plus Anthropic's hosted web-search tool,
# and emails the result. Also deploys monarch_refresh.py, a tiny companion
# script that just calls the Monarch gateway's refresh_accounts tool --
# meant to run ~30 minutes before brief.py, since Monarch's sync with the
# underlying financial institutions takes a few minutes, not instant.
#
# Runs entirely on this host on purpose: Claude's own cloud-scheduled jobs
# run in Anthropic's sandbox, which has no path to these gateways'
# 127.0.0.1-only bindings (only Caddy's token-gated public paths are
# internet-reachable). Running as a local cron job means the MCP calls to
# 127.0.0.1:8600/8601/8602 are genuinely local -- no token, no public
# exposure, no network boundary to cross.
#
# One-time manual prerequisites (secrets, never committed to this repo):
#   1. An Anthropic API key, provisioned for this script specifically.
#   2. Mail is sent via Microsoft Graph (me/sendMail), reusing the same
#      Azure App Registration the Microsoft To Do MCP gateway uses
#      (d0f1f47d-77ee-494d-a63b-ee3afeb2fad1) with its own independent
#      OAuth grant -- not SMTP. One-time setup:
#        a. portal.azure.com -> Entra ID -> App registrations -> that app
#           -> API permissions -> Add a permission -> Microsoft Graph ->
#           Delegated permissions -> Mail.Send -> Add, then grant admin
#           consent.
#        b. Same app -> Authentication -> Advanced settings -> "Allow
#           public client flows" -> Yes -> Save.
#        c. After config/.env exists (step 3 below), run
#           `venv/bin/python app/graph_login.py` once -- it prints a
#           URL+code to complete in any browser (device code flow, no
#           browser needed on this host) and saves a token cache to
#           config/graph_token_cache.json. brief.py refreshes it
#           automatically after that.
#   3. Create /mnt/data/appdata/daily-brief/config/.env with:
#        ANTHROPIC_API_KEY=<key>
#        EMAIL_TO=<your address>
#        # Optional, for localized web search results (e.g. weather):
#        # WEB_SEARCH_CITY=<city>
#        # WEB_SEARCH_REGION=<state/region>
#        # WEB_SEARCH_COUNTRY=<two-letter ISO country code>
#        # WEB_SEARCH_TIMEZONE=<IANA timezone>
#   4. Create /mnt/data/appdata/daily-brief/config/prompt.md with your
#      actual brief instructions -- what to check (todo lists, notes,
#      finances), what public info to pull, tone/length. Free-form text,
#      used as the system prompt. Edit this file any time without
#      redeploying.
#   Then:
#     ssh mwilkie@<host> mkdir -p /mnt/data/appdata/daily-brief/config
#     scp .env prompt.md mwilkie@<host>:/mnt/data/appdata/daily-brief/config/
#
# Usage:
#   ./setup-daily-brief.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/daily-brief"
APP_DIR="${APPDATA_ROOT}/app"
CONFIG_DIR="${APPDATA_ROOT}/config"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_APP_SRC="${SCRIPT_DIR}/daily-brief"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APP_DIR}" "${CONFIG_DIR}"

if [ ! -f "${CONFIG_DIR}/.env" ] || [ ! -f "${CONFIG_DIR}/prompt.md" ]; then
  echo "!! Missing ${CONFIG_DIR}/.env and/or ${CONFIG_DIR}/prompt.md"
  echo "   These are never committed to this repo -- see the prerequisites"
  echo "   in this script's header comment, create both files, then re-run."
  exit 1
fi

echo "==> Copying brief.py + monarch_refresh.py + graph_login.py + requirements.txt into ${APP_DIR}"
cp "${LOCAL_APP_SRC}/brief.py" "${LOCAL_APP_SRC}/monarch_refresh.py" "${LOCAL_APP_SRC}/graph_login.py" "${LOCAL_APP_SRC}/requirements.txt" "${APP_DIR}/"

echo "==> Creating/updating venv at ${APP_DIR}/venv"
python3 -m venv "${APP_DIR}/venv"
"${APP_DIR}/venv/bin/pip" install --quiet --upgrade pip
"${APP_DIR}/venv/bin/pip" install --quiet -r "${APP_DIR}/requirements.txt"

echo ""
echo "==> Done."
echo "    Script:  ${APP_DIR}/brief.py"
echo "    Config:  ${CONFIG_DIR}/.env, ${CONFIG_DIR}/prompt.md"
echo ""
echo "    If you haven't yet, complete the one-time Graph mail login:"
echo "      ${APP_DIR}/venv/bin/python ${APP_DIR}/graph_login.py"
echo ""
echo "    Test it first (prints the brief instead of emailing it):"
echo "      ${APP_DIR}/venv/bin/python ${APP_DIR}/brief.py --dry-run"
echo ""
echo "    Once that looks right, run it for real once to confirm email delivery:"
echo "      ${APP_DIR}/venv/bin/python ${APP_DIR}/brief.py"
echo ""
echo "    Then add these lines via 'crontab -e': Monarch refresh at 6am (its"
echo "    sync takes a few minutes, so it needs lead time), brief at 6:30am:"
echo "      0 6 * * * ${APP_DIR}/venv/bin/python ${APP_DIR}/monarch_refresh.py >> \$HOME/daily-brief.log 2>&1"
echo "      30 6 * * * ${APP_DIR}/venv/bin/python ${APP_DIR}/brief.py >> \$HOME/daily-brief.log 2>&1"
