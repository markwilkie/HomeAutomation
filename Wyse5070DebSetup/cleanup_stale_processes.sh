#!/usr/bin/env bash
#
# cleanup_stale_processes.sh
# Kills leftover ad-hoc processes: the `ssh wyse 'bash -c ...'` wait loops,
# log tails and watchers that outlive the SSH session that started them.
# Found 2026-10-27: three such loops had run for up to two weeks, one
# re-reading Home Assistant's entire container log every 5 seconds.
#
# A process tree is killed only if its root is ALL of:
#   - owned by this user
#   - orphaned (parent is PID 1 -- its SSH session is gone)
#   - older than MAX_AGE_HOURS
#   - an ad-hoc shell command or watcher (bash -c, sh -c, docker logs,
#     tail -f, watch, sleep)
# Containers, systemd services and cron jobs never match: their parents are
# containerd-shim, systemd --user or cron, not PID 1 under this user. To
# keep something long-running on purpose, put KEEP_RUNNING anywhere in its
# command line (e.g. `bash -c '# KEEP_RUNNING ...'`).
#
# Installed by setup-stale-process-cleanup.sh (daily cron).
#
# Usage:
#   ./cleanup_stale_processes.sh            # kills, logs what it killed
#   ./cleanup_stale_processes.sh --dry-run  # only lists what it would kill

set -euo pipefail

MAX_AGE_HOURS="${MAX_AGE_HOURS:-24}"
DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1

STALE_CMD_RE='^(/usr/bin/|/bin/)?(bash|sh) -c|^docker logs|^tail -[fF]|^watch |^sleep '

descendants() {
  local child
  for child in $(pgrep -P "$1"); do
    descendants "$child"
    echo "$child"
  done
}

MAX_AGE_SECS=$((MAX_AGE_HOURS * 3600))
FOUND=0
while read -r PID PARENT AGE CMD; do
  [ "$PARENT" = "1" ] || continue
  [ "$AGE" -ge "$MAX_AGE_SECS" ] || continue
  [[ "$CMD" =~ $STALE_CMD_RE ]] || continue
  [[ "$CMD" == *KEEP_RUNNING* ]] && continue

  FOUND=$((FOUND + 1))
  # Freeze the root first so a loop can't spawn its next child (a new
  # `sleep`, a new `docker logs`) between listing the tree and killing it.
  [ "$DRY_RUN" -eq 0 ] && kill -STOP "$PID" 2>/dev/null
  TREE="$(descendants "$PID") $PID"
  echo "[$(date -Iseconds)] stale: pid=${PID} age=$((AGE / 3600))h procs=$(echo $TREE | wc -w) cmd=$(printf '%s' "$CMD" | tr '\n' ' ' | cut -c1-200)"
  if [ "$DRY_RUN" -eq 0 ]; then
    kill -9 $TREE 2>/dev/null || true
  fi
done < <(ps -u "$(id -u)" -o pid=,ppid=,etimes=,args=)

if [ "$DRY_RUN" -eq 1 ]; then
  echo "[$(date -Iseconds)] dry run: ${FOUND} stale process tree(s) found, nothing killed"
elif [ "$FOUND" -gt 0 ]; then
  echo "[$(date -Iseconds)] killed ${FOUND} stale process tree(s)"
fi
