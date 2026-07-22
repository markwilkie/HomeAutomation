#!/usr/bin/env bash
#
# setup-dongle-flasher.sh
#
# Deploys ewelink/sonoff-dongle-flasher (https://hub.docker.com/r/ewelink/sonoff-dongle-flasher)
# -- a standalone, non-Supervisor container version of iHost's "SONOFF Dongle Flasher" app -- to
# check/flash firmware on the SONOFF Dongle Plus MG24 used by setup-mg24.sh.
#
# Investigated as an alternative to the Supervisor-only hassio-ihost-sonoff-dongle-flasher add-on,
# which cannot run on this host: Home Assistant here is a Container install, not HAOS/Supervised
# (see setup-homeassistant.sh), and that add-on hard-depends on Supervisor's own API
# (hassio_api/homeassistant_api: true in its config.json) and ingress proxy, neither of which
# exist outside Supervisor. This image is maintained separately by eWeLink specifically to be run
# with a plain `docker run` instead.
#
# WHY THIS SCRIPT STOPS/RESTARTS OTBR: the dongle's serial port can only be held open by one
# process at a time, and the otbr container currently owns it (setup-mg24.sh mounts
# `${SERIAL_DEVICE}:/dev/ttyUSB0` into it) -- so the flasher can't reach the dongle while otbr is
# running. otbr-watchdog also has to be stopped first: the moment otbr goes down, the watchdog
# notices "otbr-agent unresponsive" on its next 30s poll and calls `docker restart otbr` right
# back, fighting this script for the port. Rather than leave that as a manual two-step dance to
# remember (and get wrong under pressure), this script IS the up/down lifecycle for the swap.
#
# RESTORING THE RIGHT IMAGE: `up` records whichever otbr image is actually running (via `docker
# inspect`) to a state file before stopping it. `down` reads that back and passes it as IMAGE= to
# setup-mg24.sh, so otbr comes back on the SAME image it was running before -- not
# setup-mg24.sh's own default (openthread/otbr:latest). This matters concretely: as of 2026-07-21
# otbr was rolled back to openthread/otbr:pre-thread14-20260721 after the Thread 1.4 update caused
# an RCP-timeout regression (see otbr-watchdog.log history) -- if `down` silently recreated otbr
# from :latest instead, it would undo that rollback.
#
# Usage:
#   ./setup-dongle-flasher.sh up      # stop otbr + watchdog, start the flasher container
#   ./setup-dongle-flasher.sh down    # stop/remove the flasher container, restore otbr + watchdog
#
# While "up": open http://<host>:8324 in a browser to use the flasher's web UI. This script does
# NOT drive the actual flash -- picking/confirming a firmware version is a manual, human-in-the-
# loop step by design (an automatic wrong choice here can brick the dongle).
#
# The Thread network is OFFLINE for as long as "up" state persists -- always run "down" when
# finished, even if you only checked firmware versions without flashing anything.

set -euo pipefail

APPDATA_ROOT="/mnt/data/appdata/dongle-flasher"
STATE_FILE="${APPDATA_ROOT}/otbr-image-before-flash.txt"
CONTAINER_NAME="sonoff-dongle-flasher"
IMAGE="${IMAGE:-ewelink/sonoff-dongle-flasher:latest}"
WEB_PORT="8324"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  echo "Usage: $0 {up|down}" >&2
  exit 1
}

[ $# -eq 1 ] || usage

case "$1" in
  up)
    echo "==> Recording the currently-running otbr image (so 'down' can restore exactly this)"
    CURRENT_OTBR_IMAGE="$(docker inspect otbr --format '{{.Config.Image}}' 2>/dev/null || true)"
    if [ -z "${CURRENT_OTBR_IMAGE}" ]; then
      echo "!! Could not find a running otbr container to record an image from." >&2
      echo "   Refusing to proceed -- 'down' would have no way to know what to restore." >&2
      exit 1
    fi
    mkdir -p "${APPDATA_ROOT}/data"
    echo "${CURRENT_OTBR_IMAGE}" > "${STATE_FILE}"
    echo "    Recorded: ${CURRENT_OTBR_IMAGE}"

    echo "==> Stopping otbr-watchdog.service (so it doesn't restart otbr out from under this)"
    sudo systemctl stop otbr-watchdog.service 2>/dev/null || true

    echo "==> Stopping otbr container (frees the dongle's serial port)"
    docker stop otbr > /dev/null 2>&1 || true

    if docker ps -a --format '{{.Names}}' | grep -qx "${CONTAINER_NAME}"; then
      echo "==> Removing existing ${CONTAINER_NAME} container"
      docker rm -f "${CONTAINER_NAME}" > /dev/null
    fi

    echo "==> Starting ${CONTAINER_NAME}"
    docker run -d \
      --name "${CONTAINER_NAME}" \
      --restart unless-stopped \
      -p "${WEB_PORT}:${WEB_PORT}" \
      --privileged \
      -v "${APPDATA_ROOT}/data:/workspace/data" \
      -v /run/udev:/run/udev:ro \
      "${IMAGE}"

    echo ""
    echo "==> Done. otbr is stopped -- the Thread network is OFFLINE until you run: $0 down"
    echo "    Flasher web UI: http://$(hostname -I | awk '{print $1}'):${WEB_PORT}"
    ;;

  down)
    echo "==> Removing ${CONTAINER_NAME}"
    docker rm -f "${CONTAINER_NAME}" > /dev/null 2>&1 || true

    if [ ! -f "${STATE_FILE}" ]; then
      echo "!! No recorded pre-flash otbr image found at ${STATE_FILE}." >&2
      echo "   Restoring with setup-mg24.sh's own default (openthread/otbr:latest) instead --" >&2
      echo "   if that's not what should be running, Ctrl-C now and set IMAGE= explicitly." >&2
      "${SCRIPT_DIR}/setup-mg24.sh"
    else
      RESTORE_IMAGE="$(cat "${STATE_FILE}")"
      echo "==> Restoring otbr to its pre-flash image: ${RESTORE_IMAGE}"
      IMAGE="${RESTORE_IMAGE}" "${SCRIPT_DIR}/setup-mg24.sh"
      rm -f "${STATE_FILE}"
    fi

    echo ""
    echo "==> Done. Thread network restored (otbr-watchdog reinstalled/restarted by setup-mg24.sh)."
    ;;

  *)
    usage
    ;;
esac
