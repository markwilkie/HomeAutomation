#!/usr/bin/env bash
#
# weekly_health_check.sh
# Gathers memory/disk/container health for the past week and emails a
# report via the same send_mail.py used by daily-brief-cc (its own
# independent Graph OAuth grant, EMAIL_TO from ../config/.env).
#
# Deliberately NOT a Claude Code CLI job like run.sh: this is a mechanical
# report over deterministic data (sar, disk-usage.log, docker ps) with no
# synthesis or judgment call an LLM would add value to, and it avoids
# depending on the `claude login` session's one-year token (see run.sh's
# header comment on that fragility) for something that should just work.
# send_mail.py's own header makes the same point: "mail delivery isn't
# something to delegate to the model, it's a deterministic last step" --
# the same reasoning applies to the data-gathering and formatting here too.
#
# Data sources (see wyse repo history for how each was set up):
#   - Memory/CPU: sysstat (sar -r, sar -W), 10-min samples, ~7 day retention.
#   - Disk: /var/log/disk-usage.log (setup-disk-usage-log.sh), 10-min samples.
#   - Containers: docker ps -a / docker inspect, live state only (no history
#     -- RestartCount is cumulative since each container's last
#     creation/recreation, not scoped to this week specifically).
#
# Usage:
#   ./weekly_health_check.sh            # generates and emails
#   ./weekly_health_check.sh --dry-run  # generates and prints instead

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ISSUES=0
REPORT_DATE="$(date '+%Y-%m-%d')"

# ---- Memory ------------------------------------------------------------
MEM_NOW="$(free -h | awk '/^Mem:/ {printf "%s used / %s total (%s available)", $3, $2, $7}')"

# %memused is always the 8th-from-last field on a sar -r data line,
# regardless of 12h/24h time format (AM/PM adds a column) -- see this
# script's dev notes if extending. Lines are filtered to those starting
# with a timestamp (HH:MM:SS), excluding each file's own "Average:" row,
# so multiple days' files can be concatenated and re-averaged here.
MEM_STATS="$(
  for f in $(ls -1 /var/log/sysstat/sa[0-9][0-9] 2>/dev/null | tail -7); do
    LC_ALL=C sar -r -f "$f" 2>/dev/null
  done | awk '
    /^[0-9][0-9]:[0-9][0-9]:[0-9][0-9]/ && !/Average/ && !/RESTART/ && NF >= 12 {
      val = $(NF-7)
      if (val ~ /^[0-9.]+$/) {
        sum += val; n++
        if (min == "" || val < min) min = val
        if (val > max) max = val
      }
    }
    END {
      if (n > 0) printf "%.0f %.0f %.0f", min, sum/n, max
      else print "NA NA NA"
    }
  '
)"
MEM_MIN="$(echo "$MEM_STATS" | awk '{print $1}')"
MEM_AVG="$(echo "$MEM_STATS" | awk '{print $2}')"
MEM_MAX="$(echo "$MEM_STATS" | awk '{print $3}')"

# Rate of pswpout/s samples that clear a noise floor (10 pages/s): idle
# Linux touches swap for trivial sub-1-page/s background reasons
# constantly, which is not pressure and was swamping a >0 threshold in
# testing. A handful of double-digit blips lining up with cron/rebuild
# activity is normal (see this week's memory-pressure investigation);
# many of them, or large ones, would indicate real sustained pressure.
SWAP_ACTIVITY="$(
  for f in $(ls -1 /var/log/sysstat/sa[0-9][0-9] 2>/dev/null | tail -7); do
    LC_ALL=C sar -W -f "$f" 2>/dev/null
  done | awk '
    /^[0-9][0-9]:[0-9][0-9]:[0-9][0-9]/ && !/Average/ && !/RESTART/ && NF >= 3 {
      val = $NF
      if (val ~ /^[0-9.]+$/ && val+0 >= 10) { sum += val; hits++ }
    }
    END { printf "%.0f %d", sum+0, hits+0 }
  '
)"
SWAP_SUM="$(echo "$SWAP_ACTIVITY" | awk '{print $1}')"
SWAP_HITS="$(echo "$SWAP_ACTIVITY" | awk '{print $2}')"
if [ "${SWAP_HITS:-0}" -gt 10 ]; then
  SWAP_NOTE="**${SWAP_HITS} samples this week with notable swap-out (>=10 pages/s, sum ${SWAP_SUM}) -- worth a look.**"
  ISSUES=$((ISSUES + 1))
else
  SWAP_NOTE="Normal (${SWAP_HITS:-0} notable blip(s) this week, typically aligned with cron/rebuild activity)."
fi

# ---- Disk ----------------------------------------------------------------
DISK_LOG="/var/log/disk-usage.log"
DISK_NOW="$(df -h / | awk 'NR==2 {print $3" used / "$2" total ("$4" available, "$5" used)"}')"
if [ -f "$DISK_LOG" ] && [ -s "$DISK_LOG" ]; then
  DISK_WEEK_AGO_LINE="$(head -1 "$DISK_LOG")"
  DISK_WEEK_AGO_PCT="$(echo "$DISK_WEEK_AGO_LINE" | grep -oE 'pct=[0-9]+%' | grep -oE '[0-9]+')"
  DISK_WEEK_AGO_DATE="$(echo "$DISK_WEEK_AGO_LINE" | awk '{print $1}')"
  DISK_NOW_PCT="$(df -h / | awk 'NR==2 {print $5}' | tr -d '%')"
  if [ -n "${DISK_WEEK_AGO_PCT:-}" ]; then
    DISK_DELTA=$((DISK_NOW_PCT - DISK_WEEK_AGO_PCT))
    DISK_TREND="${DISK_NOW_PCT}% now vs ${DISK_WEEK_AGO_PCT}% on ${DISK_WEEK_AGO_DATE} (change: ${DISK_DELTA} pts)"
  else
    DISK_TREND="not enough history yet to show a trend"
  fi
else
  DISK_TREND="no history yet (log just started)"
fi
if [ "${DISK_NOW_PCT:-0}" -ge 85 ]; then
  ISSUES=$((ISSUES + 1))
  DISK_FLAG="**${DISK_NOW_PCT}% used -- getting tight.**"
else
  DISK_FLAG="OK."
fi

# ---- Containers ------------------------------------------------------------
CONTAINER_LINES=""
CONTAINER_TOTAL=0
CONTAINER_FLAGGED=0
while IFS='|' read -r NAME STATUS; do
  [ -z "$NAME" ] && continue
  CONTAINER_TOTAL=$((CONTAINER_TOTAL + 1))
  RESTARTS="$(docker inspect --format '{{.RestartCount}}' "$NAME" 2>/dev/null || echo "?")"
  if [ "$STATUS" != "running" ] || { [ "$RESTARTS" != "?" ] && [ "$RESTARTS" -gt 0 ]; }; then
    CONTAINER_FLAGGED=$((CONTAINER_FLAGGED + 1))
    CONTAINER_LINES="${CONTAINER_LINES}- **${NAME}**: status=${STATUS}, restarts=${RESTARTS} (cumulative since last create)
"
  fi
done < <(docker ps -a --format '{{.Names}}|{{.State}}')
if [ "$CONTAINER_FLAGGED" -gt 0 ]; then
  ISSUES=$((ISSUES + 1))
fi

# ---- Compose report ------------------------------------------------------
if [ "$ISSUES" -eq 0 ]; then
  STATUS_LINE="**Status: OK** -- nothing needs attention"
else
  STATUS_LINE="**Status: NEEDS ATTENTION** (${ISSUES} item(s) flagged below)"
fi

REPORT="$(cat <<EOF
# Weekly Wyse Health Report -- ${REPORT_DATE}

${STATUS_LINE}

## Memory
- Now: ${MEM_NOW}
- Past week: min ${MEM_MIN}% / avg ${MEM_AVG}% / max ${MEM_MAX}% used
- Swap pressure: ${SWAP_NOTE}

## Disk (/)
- Now: ${DISK_NOW}
- Trend: ${DISK_TREND}
- ${DISK_FLAG}

## Containers
- ${CONTAINER_TOTAL} total, ${CONTAINER_FLAGGED} flagged
$( [ "$CONTAINER_FLAGGED" -gt 0 ] && echo "${CONTAINER_LINES}" || echo "- All running, no restarts since last create." )

---
_Generated automatically every Sunday 7:00 AM from wilkie-home-server. Sources: sysstat, disk-usage.log, docker._
EOF
)"

if [ "${1:-}" = "--dry-run" ]; then
  echo "$REPORT"
else
  echo "$REPORT" | venv/bin/python send_mail.py --subject "Weekly Wyse Health Report - ${REPORT_DATE}"
fi
