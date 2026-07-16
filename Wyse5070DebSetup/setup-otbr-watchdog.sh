#!/usr/bin/env bash
#
# setup-otbr-watchdog.sh
#
# otbr-agent (inside the "otbr" container) can die from an RCP/radio
# communication timeout ("Failed to communicate with RCP - no response from
# RCP during initialization") while its container's PID 1 -- the image's own
# docker_entrypoint.sh wrapper -- stays running. Confirmed on real hardware:
# when this happened, `docker ps` still showed the container "Up", but
# `docker exec otbr ps aux` showed no otbr-agent process at all, and
# `ot-ctl state`/`ot-ctl nat64 state` came back "Connection refused". Docker's
# own `--restart unless-stopped` policy never helps here since it only fires
# when a container's PID 1 exits, which it never does in this failure mode.
#
# Separately, ANY otbr-agent (re)start -- this crash, a plain `docker restart
# otbr`, an image update, or a host reboot -- always re-enables OTBR's own
# broken built-in NAT64 translator by default and forgets the manually-added
# Thread Network Data route (`ot-ctl route add ... sn low`), since both are
# in-process runtime state, not persisted to disk. setup-nat64-jool.sh's own
# systemd unit (nat64-jool.service) only reapplies the Jool override once, at
# host boot -- it does nothing for an otbr-agent restart that happens without
# a host reboot, which is exactly the case above.
#
# This script installs a single watchdog service that, on a loop:
#   1. Checks otbr-agent is actually responsive (`ot-ctl state`, not just
#      "container is Up") and restarts the container if not.
#   2. Checks whether OTBR's own NAT64 has drifted back to Active (from a
#      restart this loop just caused, or one that happened some other way --
#      e.g. someone ran a plain `docker restart otbr` by hand) and reapplies
#      setup-nat64-jool.sh's full config if so.
#
# Diagnostics before restarting: confirmed live (2026-07-16) that the
# original RCP/radio-timeout crash this was built for stopped recurring
# after the USB-autosuspend fix (setup-otbr-usb-power.sh) -- but
# otbr-agent still occasionally goes unresponsive with NO crash line in
# its own log and nothing in dmesg (checked via `docker exec otbr dmesg`,
# which works because the container runs privileged). That means it's
# hanging, not crashing -- and a bare `docker restart` destroys the one
# copy of the evidence (process state, D-Bus status) the moment it fires.
# So now, before restarting, this captures a snapshot -- `ot-ctl state`'s
# actual output (not just its exit code), `ps aux` inside the container,
# service statuses, and the container's own recent log -- to a per-incident
# file, so the next occurrence leaves a trail instead of just "it's better
# now".
#
# Usage:
#   ./setup-otbr-watchdog.sh            # install + enable + start the service
#   ./setup-otbr-watchdog.sh --watch    # run the monitoring loop directly
#                                        # (this is what the service actually runs)
#   sudo systemctl status otbr-watchdog.service
#   journalctl -u otbr-watchdog.service -f
#   tail -f /mnt/data/appdata/otbr/watchdog.log          # same log, no sudo needed
#   ls /mnt/data/appdata/otbr/diagnostics/               # per-incident snapshots

set -euo pipefail

CONTAINER_NAME="otbr"
POLL_INTERVAL_SECONDS=30
RESTART_WAIT_TIMEOUT_SECONDS=60
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename "${BASH_SOURCE[0]}")"
NAT64_JOOL_SCRIPT="${SCRIPT_DIR}/setup-nat64-jool.sh"
# Plain-file mirror of the journal log. The service runs as root (no User=
# in the unit) so journalctl -u otbr-watchdog.service requires root/
# systemd-journal group membership to read -- this file is created with the
# default 644 mode so crash history can be checked without sudo.
LOG_FILE="/mnt/data/appdata/otbr/watchdog.log"
DIAG_DIR="/mnt/data/appdata/otbr/diagnostics"
DIAG_RETENTION_COUNT=20

log() {
    local line="[otbr-watchdog] $(date '+%Y-%m-%d %H:%M:%S') $*"
    echo "${line}"
    echo "${line}" >> "${LOG_FILE}" 2>/dev/null || true
}

otbr_responsive() {
    docker exec "${CONTAINER_NAME}" timeout 5 ot-ctl state > /dev/null 2>&1
}

otbr_nat64_drifted() {
    # "drifted" = OTBR's own translator is Active again, which means it's
    # fighting Jool for the same synthesized addresses -- see setup-mg24.sh
    # for why that combination doesn't work.
    docker exec "${CONTAINER_NAME}" sh -c "ot-ctl nat64 state" 2>/dev/null | grep -q "Active"
}

wait_for_otbr_responsive() {
    local waited=0
    while [ "${waited}" -lt "${RESTART_WAIT_TIMEOUT_SECONDS}" ]; do
        if otbr_responsive; then
            return 0
        fi
        sleep 3
        waited=$((waited + 3))
    done
    return 1
}

capture_diagnostics() {
    # The whole point of this function is to run right as otbr-agent is
    # confirmed unresponsive, so several of these (ot-ctl state, service
    # status) are *expected* to fail or time out -- that's the diagnostic
    # signal, not an error. Under this script's global `set -e`, even a
    # guarded `cmd || true` on a command substitution assignment
    # (`x="$(cmd)" || true`) doesn't reliably preserve cmd's real exit
    # status for logging, so instead of guarding each line individually,
    # errexit is turned off for this whole function and restored on the
    # way out -- nothing in here may ever be able to abort the watchdog
    # before it reaches `docker restart` below.
    set +e
    mkdir -p "${DIAG_DIR}" 2>/dev/null
    local diag_file="${DIAG_DIR}/unresponsive-$(date '+%Y%m%d-%H%M%S').log"

    {
        echo "=== $(date '+%Y-%m-%d %H:%M:%S') otbr-agent unresponsive -- pre-restart snapshot ==="
        echo ""
        echo "--- ot-ctl state (the actual failing health check, with real output this time) ---"
        docker exec "${CONTAINER_NAME}" timeout 5 ot-ctl state 2>&1
        echo "exit code: $?"
        echo ""
        echo "--- ps aux inside container (is otbr-agent process even there?) ---"
        docker exec "${CONTAINER_NAME}" ps aux 2>&1
        echo ""
        echo "--- service statuses ---"
        docker exec "${CONTAINER_NAME}" sudo service otbr-agent status 2>&1
        docker exec "${CONTAINER_NAME}" sudo service dbus status 2>&1
        echo ""
        echo "--- docker stats (container CPU/mem at time of detection) ---"
        docker stats "${CONTAINER_NAME}" --no-stream 2>&1
        echo ""
        echo "--- host load/memory ---"
        uptime 2>&1
        free -h 2>&1
        echo ""
        echo "--- last 40 lines of the container's own log ---"
        docker logs "${CONTAINER_NAME}" --tail 40 2>&1
    } > "${diag_file}" 2>&1

    # Retention: keep the newest DIAG_RETENTION_COUNT snapshots.
    local old
    while IFS= read -r old; do
        rm -f "${old}"
    done < <(ls -1t "${DIAG_DIR}"/unresponsive-*.log 2>/dev/null | tail -n "+$((DIAG_RETENTION_COUNT + 1))")
    set -e

    log "Captured pre-restart diagnostics -> ${diag_file}"
}

reapply_jool() {
    log "Reapplying Jool NAT64 override"
    if "${NAT64_JOOL_SCRIPT}"; then
        log "Jool override reapplied successfully"
    else
        log "!! setup-nat64-jool.sh failed -- will retry next loop iteration"
    fi
}

watch_loop() {
    log "Starting otbr watchdog (poll every ${POLL_INTERVAL_SECONDS}s)"
    while true; do
        if ! otbr_responsive; then
            log "otbr-agent unresponsive (container may be 'Up' but the daemon is dead) -- restarting container"
            capture_diagnostics
            docker restart "${CONTAINER_NAME}" > /dev/null
            if wait_for_otbr_responsive; then
                log "otbr-agent responsive again after restart"
                reapply_jool
            else
                log "!! otbr-agent still unresponsive ${RESTART_WAIT_TIMEOUT_SECONDS}s after restart -- will retry next loop iteration"
            fi
        elif otbr_nat64_drifted; then
            log "OTBR's own NAT64 translator is Active (drifted back on since the last check)"
            reapply_jool
        fi
        sleep "${POLL_INTERVAL_SECONDS}"
    done
}

if [ "${1:-}" = "--watch" ]; then
    watch_loop
    exit 0
fi

# ---- Install systemd service ------------------------------------------------
SERVICE_FILE="/etc/systemd/system/otbr-watchdog.service"
echo "==> Installing otbr-watchdog.service"
sudo tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Watch otbr-agent for RCP/radio crashes and reapply the Jool NAT64 override after any restart
After=docker.service
# Deliberately NOT "Requires=docker.service" -- confirmed on real hardware
# that Requires= causes a plain "systemctl stop docker.service" (e.g. for
# host maintenance, like setup-containerd-storage.sh) to cascade-stop this
# watchdog too, and unlike a crash, nothing then restarts it once docker
# comes back -- NAT64 silently drifted to Active/Active for the entire time
# the watchdog was down and unnoticed. After= alone keeps this service
# running through a docker stop/start; its own polling loop already
# tolerates docker being transiently unavailable (a failed docker exec is
# treated the same as otbr being unresponsive, retried next cycle) and
# self-heals once docker is back for real.
#
# NOTE ON THIS HEREDOC: it's intentionally unquoted (<<EOF, not <<'EOF') so
# ${SCRIPT_PATH} below expands -- which also means backticks ANYWHERE in
# this block trigger real command substitution. Confirmed the hard way: an
# earlier draft of the comment above quoted a systemctl command in
# backticks, and bash actually ran that command while writing this file (it
# failed harmlessly only because it lacked a real sudo TTY). Do not put
# backticks anywhere in this heredoc, including in comments -- use plain
# quotes instead.

[Service]
Type=simple
ExecStart=${SCRIPT_PATH} --watch
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable otbr-watchdog.service > /dev/null 2>&1
sudo systemctl restart otbr-watchdog.service

echo ""
echo "==> Done. Check status any time with:"
echo "      sudo systemctl status otbr-watchdog.service"
echo "      journalctl -u otbr-watchdog.service -f"
