#!/usr/bin/env bash
#
# setup-openmessage.sh
# Deploys MaxGhenis/openmessage (github.com/MaxGhenis/openmessage), a Go
# server that pairs with Google Messages like messages.google.com does and
# keeps a local SQLite archive of my phone's SMS + RCS, so the daily brief
# and claude.ai can read my texts. Unofficial protocol (mautrix libgm); the
# brief only ever reads, never sends -- OpenMessage itself has no read-only
# mode, so nothing may reach port 7007 except through the read-only proxy.
#
# Three containers:
#   openmessage           the server, 127.0.0.1:7007 (web UI + /api + MCP)
#   texts-mcp             read-only MCP proxy in front of it, 127.0.0.1:8607
#                         (openmessage/mcp-readonly/); Caddy exposes its /mcp
#                         at /texts/<MCP_AUTH_TOKEN>/mcp for claude.ai
#   openmessage-chromium  a Chromium kept signed in to my Google account,
#                         127.0.0.1:3001 (web desktop for the one-time sign-in)
#
# Why the Chromium: OpenMessage is paired through my Google account and
# authenticates with that account's browser cookies. Copied cookies died
# after ~2h on the first try (2026-10-28, copied from a Chrome incognito
# window on the PC). Upstream's fix is a refresh script that re-reads fresh
# cookies from a signed-in local Chrome profile when Google answers 401;
# openmessage/refresh-cookies.py adapts it to this container Chromium
# (basic password store instead of KWallet), and OpenMessage runs it via
# OPENMESSAGE_COOKIE_REFRESH_SCRIPT, then reconnects -- no re-pair.
# Chromium has to stay running so Google keeps that browser session alive.
# Never open messages.google.com in it (or anywhere): Google Messages allows
# one active web client, and that would kick OpenMessage off.
#
# Pinned to upstream commit a455f63 (2026-10-24) and linuxserver/chromium
# 2dba585f-ls60, plus the patches in openmessage/patches/ (applied before
# the build; an upgrade that no longer applies them fails loudly). To
# upgrade, change the pins, drop any patch upstream has since fixed, re-run.
#   0001-retry-unanswered-requests: a request the phone doesn't answer within
#     libgm's 35s is retried after OPENMESSAGES_UNANSWERED_RETRY_DELAYS
#     (set below) before the run gives up, instead of aborting at once.
#
# Data: ${APPDATA_ROOT}/data (session.json = the pairing, messages.db = the
# archive, unencrypted -- keep it on this host). It was first created in a
# docker volume (openmessage_openmessage-data) by a hand-run trial; the
# first run of this script copies it over, later runs leave it alone.
#
# Google sign-in (first run, and again if I sign out or change my Google
# password; the 401s then persist and the status shows google_auth_expired):
#   1. From the PC: ssh -L 3001:127.0.0.1:3001 mwilkie@192.168.15.30
#   2. Open https://localhost:3001 (accept the self-signed cert warning).
#   3. In that Chromium, sign in at accounts.google.com with the account my
#      phone's Google Messages uses. Leave it on myaccount.google.com.
#
# Pairing the phone (only if session.json is lost, or the phone unpairs it):
#   ssh -t 192.168.15.30 ~/github/HomeAutomation/Wyse5070DebSetup/openmessage/pair.sh
#   then tap the emoji it prints in Google Messages on the phone.
#
# When it breaks -- what /api/status shows, and the exact fix. The read-only
# MCP proxy puts the matching steps at the top of every tool response while
# the problem lasts (keep openmessage/mcp-readonly/server.py in step with
# this table), so the brief and claude.ai relay them verbatim:
#   status unreachable          -> server down:
#       ssh 192.168.15.30 'cd /mnt/data/appdata/openmessage && docker compose up -d'
#   google.paired false, or     -> pairing gone: run pair.sh (above), tap
#   google.needs_pairing true      the emoji on the phone
#   google.connected false and  -> Google sign-in expired and Chromium's
#   supervisor.state "blocked"     couldn't fix it: do the Google sign-in
#                                  steps above, then
#       ssh 192.168.15.30 'cd /mnt/data/appdata/openmessage && docker compose restart openmessage'
#   google.phone_responding     -> phone offline or asleep: unlock it, open
#   false                          Messages, check it has a connection; if it
#                                  persists, Messages -> Device pairing should
#                                  still list this computer (else pair.sh)
#   freshness.stale true        -> archive behind (texts may be missing since
#                                  freshness.newest_ms); same checks as above,
#                                  then repair-gap.sh from that date
#   offline > 3h (texts-mcp's   -> gap: texts-mcp backfills it by itself
#   gap.json)                      every 10 min until clean; manual way is
#       ssh 192.168.15.30 ~/github/HomeAutomation/Wyse5070DebSetup/openmessage/repair-gap.sh YYYY-MM-DD
#
# Outages: up to an hour or two, OpenMessage's own catch-up on reconnect is
# enough (50 most recent conversations, newest-first down to what's stored,
# max 120 messages each). Longer, it misses messages for good -- busy
# threads past 120, quieter ones pushed out of the top 50 -- hence texts-mcp
# watching for it and running a window backfill (all folders, every
# conversation active since the date, down to that date).
#
# Full-history backfill (best with the phone plugged in; a dozing Android
# misses libgm's 35s timeout now and then -- each miss is retried for up to
# ~10 min, and only a phone silent that long aborts the run):
#   docker exec openmessage wget -qO- --post-data "" http://localhost:7007/api/backfill
#   docker exec openmessage wget -qO- http://localhost:7007/api/backfill/status
#
# Usage:
#   ./setup-openmessage.sh

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/openmessage"
BUILD_DIR="${APPDATA_ROOT}/build"
DATA_DIR="${APPDATA_ROOT}/data"
CHROMIUM_DIR="${APPDATA_ROOT}/chromium"
MCP_STATE_DIR="${APPDATA_ROOT}/texts-mcp"
REPO_URL="https://github.com/MaxGhenis/openmessage.git"
PINNED_REF="a455f63"
BASE_IMAGE="openmessage-upstream:${PINNED_REF}"
CHROMIUM_IMAGE="lscr.io/linuxserver/chromium:2dba585f-ls60"
LISTEN_PORT="7007"
MCP_PORT="8607"
CHROMIUM_PORT="3001"
TRIAL_VOLUME="openmessage_openmessage-data"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Creating directory structure under ${APPDATA_ROOT}"
mkdir -p "${APPDATA_ROOT}" "${DATA_DIR}" "${CHROMIUM_DIR}" "${MCP_STATE_DIR}"
chmod 700 "${DATA_DIR}" "${CHROMIUM_DIR}"

echo "==> Fetching ${REPO_URL} at ${PINNED_REF}"
rm -rf "${BUILD_DIR}"
git clone --quiet "${REPO_URL}" "${BUILD_DIR}"
git -C "${BUILD_DIR}" checkout --quiet "${PINNED_REF}"
for patch in "${SCRIPT_DIR}"/openmessage/patches/*.patch; do
  echo "==> Applying $(basename "${patch}")"
  git -C "${BUILD_DIR}" apply "${patch}"
done

echo "==> Building ${BASE_IMAGE}"
docker build -q -t "${BASE_IMAGE}" --build-arg VERSION="${PINNED_REF}" "${BUILD_DIR}"

# One-time move of the trial's data out of its docker volume. The trial
# container (same name) has to stop first so the database copy is consistent.
if [ ! -f "${DATA_DIR}/session.json" ] && docker volume inspect "${TRIAL_VOLUME}" >/dev/null 2>&1; then
  echo "==> Migrating data from docker volume ${TRIAL_VOLUME}"
  docker rm -f openmessage >/dev/null 2>&1 || true
  docker run --rm -v "${TRIAL_VOLUME}:/from:ro" -v "${DATA_DIR}:/to" alpine:3.20 \
    sh -c "cp -a /from/. /to/ && chown -R $(id -u):$(id -g) /to"
fi

# ---- docker-compose.yml ------------------------------------------------
# Both run as this host user so the bind mounts stay owned by mwilkie and
# OpenMessage can read Chromium's profile (mounted read-only).
COMPOSE_FILE="${APPDATA_ROOT}/docker-compose.yml"
echo "==> Writing ${COMPOSE_FILE}"
tee "${COMPOSE_FILE}" > /dev/null <<EOF
services:
  openmessage:
    build:
      context: ${SCRIPT_DIR}/openmessage
      additional_contexts:
        upstream: ${BUILD_DIR}
      args:
        BASE: ${BASE_IMAGE}
    image: openmessage:${PINNED_REF}
    container_name: openmessage
    restart: unless-stopped
    user: "$(id -u):$(id -g)"
    # Naming any transport turns the rest off, so the web/API side is named too.
    command: ["serve", "--web", "--mcp-sse"]
    ports:
      - "127.0.0.1:${LISTEN_PORT}:7007"
    environment:
      OPENMESSAGES_DATA_DIR: /data
      OPENMESSAGES_HOST: 0.0.0.0
      OPENMESSAGES_PORT: "7007"
      # Startup only catches up on recent messages; full history is a
      # manual backfill (see header).
      OPENMESSAGES_STARTUP_BACKFILL: shallow
      # Local patch 0001: retry a request the phone didn't answer.
      OPENMESSAGES_UNANSWERED_RETRY_DELAYS: "30s,2m,5m"
      OPENMESSAGE_COOKIE_REFRESH_SCRIPT: /opt/openmessage/refresh-cookies
      OPENMESSAGE_CHROME_PROFILE: /chromium/.config/chromium/Default
      OPENMESSAGE_SESSION_PATH: /data/session.json
    volumes:
      - ${DATA_DIR}:/data
      - ${CHROMIUM_DIR}:/chromium:ro
    healthcheck:
      test: ["CMD-SHELL", "wget -qO- http://localhost:7007/api/status >/dev/null || exit 1"]
      interval: 60s
      timeout: 5s
      retries: 3

  texts-mcp:
    build: ${SCRIPT_DIR}/openmessage/mcp-readonly
    image: texts-mcp:local
    container_name: texts-mcp
    restart: unless-stopped
    user: "$(id -u):$(id -g)"
    environment:
      OPENMESSAGE_URL: http://openmessage:7007
      TZ: America/Los_Angeles
    ports:
      - "127.0.0.1:${MCP_PORT}:8607"
    # gap.json: when OpenMessage was last connected, so an outage (even a
    # whole-host one) is noticed and backfilled once it's back.
    volumes:
      - ${MCP_STATE_DIR}:/state

  openmessage-chromium:
    image: ${CHROMIUM_IMAGE}
    container_name: openmessage-chromium
    restart: unless-stopped
    environment:
      PUID: "$(id -u)"
      PGID: "$(id -g)"
      TZ: America/Los_Angeles
      CHROME_CLI: --password-store=basic https://myaccount.google.com/
    ports:
      - "127.0.0.1:${CHROMIUM_PORT}:3001"
    volumes:
      - ${CHROMIUM_DIR}:/config
    shm_size: 1gb
    security_opt:
      - seccomp:unconfined
EOF

echo "==> Building and starting openmessage + texts-mcp + openmessage-chromium"
cd "${APPDATA_ROOT}"
docker compose up -d --build

echo "==> Waiting a few seconds for them to settle..."
sleep 10

echo ""
echo "==> Container status:"
docker ps --filter "name=openmessage" --filter "name=texts-mcp"

echo ""
echo "==> Recent logs:"
docker logs openmessage --tail 15
docker logs openmessage-chromium --tail 5
docker logs texts-mcp --tail 5

echo ""
echo "==> Done."
echo "    Data:     ${DATA_DIR}"
echo "    Status:   docker exec openmessage wget -qO- http://localhost:${LISTEN_PORT}/api/status"
echo "    MCP:      http://127.0.0.1:${MCP_PORT}/mcp (read-only; claude.ai via Caddy /texts/<token>/mcp)"
echo "    Sign-in:  ssh -L ${CHROMIUM_PORT}:127.0.0.1:${CHROMIUM_PORT} mwilkie@192.168.15.30, then https://localhost:${CHROMIUM_PORT}"
