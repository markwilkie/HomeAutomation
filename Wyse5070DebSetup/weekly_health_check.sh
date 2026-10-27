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
#   - Memory/CPU: sysstat (sar -r, sar -W, sar -q MEM for PSI stall), 10-min
#     samples, ~7 day retention; OOM kills from the kernel journal.
#   - Reboots: journalctl --list-boots, plus whether each predecessor boot
#     logged a clean shutdown.
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

# Swap-out is informational only, never flagged. On this 3.7 GB box Linux
# routinely pages idle memory (sleeping MCP gateways, TripTracker) out to
# swap, and the daily brief's 6:30-6:50 run adds a burst most mornings --
# the week of 2026-10-11 had 19 samples >=10 pages/s (max ~0.4 MB/s) with
# 0.14% peak memory stall, no OOM kills and >=870 MB always available.
# Counting swap-out samples was flagging that as "worth a look". The real
# signals are below: memory stall time (PSI), available-memory low and
# OOM kills.
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
SWAP_HITS="$(echo "$SWAP_ACTIVITY" | awk '{print $2}')"
SWAP_NOTE="${SWAP_HITS:-0} sample(s) with swap-out >=10 pages/s (informational -- mostly the 6:30 brief run; not a problem on its own)."

# Memory stall (PSI) per 10-min sample: %smem = share of time at least one
# task waited on memory, %fmem = share of time all did. Thresholds are
# far above this box's normal (peak 0.14% the week of 2026-10-11).
PSI_STATS="$(
  for f in $(ls -1 /var/log/sysstat/sa[0-9][0-9] 2>/dev/null | tail -7); do
    LC_ALL=C sar -q MEM -f "$f" 2>/dev/null
  done | awk '
    /^[0-9][0-9]:[0-9][0-9]:[0-9][0-9]/ && !/Average/ && !/RESTART/ && $5 ~ /^[0-9.]+$/ {
      if ($5 > ms) { ms = $5; ts = $1 }
      if ($9 > mf) mf = $9
    }
    END { printf "%.2f %.2f %s", ms+0, mf+0, (ts == "" ? "-" : ts) }
  '
)"
PSI_SOME="$(echo "$PSI_STATS" | awk '{print $1}')"
PSI_FULL="$(echo "$PSI_STATS" | awk '{print $2}')"
if awk -v s="$PSI_SOME" -v f="$PSI_FULL" 'BEGIN { exit !(s >= 10 || f >= 2) }'; then
  PSI_NOTE="**peak ${PSI_SOME}% of a 10-min window stalled (full ${PSI_FULL}%) -- real memory pressure.**"
  ISSUES=$((ISSUES + 1))
else
  PSI_NOTE="OK (peak ${PSI_SOME}% of any 10-min window, full ${PSI_FULL}%)."
fi

# Lowest available memory (sar -r kbavail, 3rd field in 24h C locale).
AVAIL_MIN_MB="$(
  for f in $(ls -1 /var/log/sysstat/sa[0-9][0-9] 2>/dev/null | tail -7); do
    LC_ALL=C sar -r -f "$f" 2>/dev/null
  done | awk '
    /^[0-9][0-9]:[0-9][0-9]:[0-9][0-9]/ && !/Average/ && !/RESTART/ && $3 ~ /^[0-9]+$/ {
      if (min == "" || $3 < min) min = $3
    }
    END { if (min == "") print "NA"; else printf "%.0f", min/1024 }
  '
)"
if [ "$AVAIL_MIN_MB" != "NA" ] && [ "$AVAIL_MIN_MB" -lt 300 ]; then
  AVAIL_NOTE="**low point ${AVAIL_MIN_MB} MB available -- close to running out.**"
  ISSUES=$((ISSUES + 1))
else
  AVAIL_NOTE="OK (low point ${AVAIL_MIN_MB} MB available)."
fi

# OOM kills (kernel log; mwilkie has NOPASSWD sudo for journalctl).
OOM_COUNT="$(sudo -n journalctl -k --since "7 days ago" --no-pager -q 2>/dev/null | grep -ciE 'oom-kill|out of memory' || true)"
if [ "${OOM_COUNT:-0}" -gt 0 ]; then
  OOM_NOTE="**${OOM_COUNT} OOM-killer event(s) this week.**"
  ISSUES=$((ISSUES + 1))
else
  OOM_NOTE="None."
fi

# ---- Reboots -------------------------------------------------------------
# A boot whose predecessor's journal ends without a clean-shutdown marker
# means the box lost power or hard-hung. 2026-10-09 had two (04:10, 05:41)
# with the ISP modem restarting at the same moments (public IP changed
# each time) -- i.e. power blips, not software.
WEEK_AGO_EPOCH="$(date -d '7 days ago' +%s)"
REBOOT_LINES=""
UNCLEAN=0
BOOTS_THIS_WEEK=0
while read -r IDX _ _ SDATE STIME _ _ EDATE ETIME _; do
  [[ "$IDX" =~ ^-?[0-9]+$ ]] || continue
  [ "$(date -d "$SDATE $STIME" +%s)" -ge "$WEEK_AGO_EPOCH" ] || continue
  BOOTS_THIS_WEEK=$((BOOTS_THIS_WEEK + 1))
  PREV=$((IDX - 1))
  if ! sudo -n journalctl -b "$PREV" -n 40 --no-pager -q 2>/dev/null \
      | grep -qE 'Journal stopped|systemd-shutdown|Reached target.*(Shutdown|Power-Off|Reboot)'; then
    UNCLEAN=$((UNCLEAN + 1))
    PREV_END="$(sudo -n journalctl -b "$PREV" -n 1 --no-pager -q -o short-iso 2>/dev/null | awk '{print $1}')"
    REBOOT_LINES="${REBOOT_LINES}- Booted ${SDATE} ${STIME}; previous run stopped ${PREV_END:-?} with no clean shutdown (power loss or hang)
"
  fi
done < <(sudo -n journalctl --list-boots --no-pager -q 2>/dev/null)
if [ "$UNCLEAN" -gt 0 ]; then
  REBOOT_NOTE="**${UNCLEAN} unexpected reboot(s) this week.** If your public IP / modem also reset at the same time, it was a power blip -- a small UPS would ride these out."
  ISSUES=$((ISSUES + 1))
else
  REBOOT_NOTE="No unexpected reboots (${BOOTS_THIS_WEEK} boot(s) this week, all after clean shutdowns)."
fi

# ---- Disk ----------------------------------------------------------------
DISK_LOG="/var/log/disk-usage.log"
DISK_NOW="$(df -h / | awk 'NR==2 {print $3" used / "$2" total ("$4" available, "$5" used)"}')"
if [ -f "$DISK_LOG" ] && [ -s "$DISK_LOG" ]; then
  # logrotate rotates this log weekly at Sunday ~00:30, hours before this
  # report's Sunday 7am run, so the live file alone only ever held today
  # (the trend compared today with today until 2026-10-11). Read last
  # week's rotated file too and take the first sample from >=7 days ago.
  WEEK_AGO="$(date -d '7 days ago' '+%Y-%m-%d %H:%M:%S')"
  DISK_WEEK_AGO_LINE="$(
    { zcat -f "${DISK_LOG}.1.gz" 2>/dev/null || true; cat "$DISK_LOG"; } \
      | awk -v cutoff="$WEEK_AGO" '($1" "$2) >= cutoff { print; exit }'
  )"
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
# Containers whose restarts are expected, not a problem in themselves:
# otbr exits now and then and Docker brings it back (plus otbr-watchdog
# restarts it if otbr-agent hangs). For these, what matters is the *rate*:
# each run records the cumulative RestartCount to RESTART_HISTORY, and the
# week's increase is flagged only if it climbs well above the trailing
# average of the previous (up to) 4 weeks -- more than double it, and at
# least RESTART_RATE_FLOOR, so a quiet baseline doesn't flag on 1-2.
EXPECTED_RESTARTS="otbr"
RESTART_HISTORY="../config/container_restarts.log"  # "<date> <name> <count> <weekly increase, or - if unknown>"
RESTART_RATE_FLOOR=5
CONTAINER_LINES=""
CONTAINER_NOTES=""
CONTAINER_TOTAL=0
CONTAINER_FLAGGED=0
while IFS='|' read -r NAME STATUS; do
  [ -z "$NAME" ] && continue
  CONTAINER_TOTAL=$((CONTAINER_TOTAL + 1))
  RESTARTS="$(docker inspect --format '{{.RestartCount}}' "$NAME" 2>/dev/null || echo "?")"
  if [ "$STATUS" = "running" ] && [ "$RESTARTS" != "?" ] && [[ " ${EXPECTED_RESTARTS} " == *" ${NAME} "* ]]; then
    PREV_COUNT="$(awk -v n="$NAME" '$2 == n {c = $3} END {print c}' "$RESTART_HISTORY" 2>/dev/null)"
    # Trailing average of up to the last 4 weekly increases.
    BASELINE="$(awk -v n="$NAME" '$2 == n && $4 ~ /^[0-9]+$/ {d[++k] = $4} END {s = 0; m = 0; for (i = k; i > 0 && m < 4; i--) {s += d[i]; m++} if (m) printf "%.1f", s / m}' "$RESTART_HISTORY" 2>/dev/null)"
    if [ -z "$PREV_COUNT" ]; then
      WEEK_DELTA=""
    elif [ "$RESTARTS" -ge "$PREV_COUNT" ]; then
      WEEK_DELTA=$((RESTARTS - PREV_COUNT))
    else
      WEEK_DELTA="$RESTARTS"  # container was recreated; count started over
    fi
    if [ "${1:-}" != "--dry-run" ]; then
      echo "${REPORT_DATE} ${NAME} ${RESTARTS} ${WEEK_DELTA:--}" >> "$RESTART_HISTORY"
    fi
    if [ -z "$WEEK_DELTA" ]; then
      CONTAINER_NOTES="${CONTAINER_NOTES}- ${NAME}: restarts=${RESTARTS} (expected; tracking the weekly rate starting now)
"
    elif [ -n "$BASELINE" ] && [ "$WEEK_DELTA" -ge "$RESTART_RATE_FLOOR" ]          && awk -v d="$WEEK_DELTA" -v b="$BASELINE" 'BEGIN {exit !(d > 2 * b)}'; then
      CONTAINER_FLAGGED=$((CONTAINER_FLAGGED + 1))
      CONTAINER_LINES="${CONTAINER_LINES}- **${NAME}: restart rate climbing -- ${WEEK_DELTA} this week vs ${BASELINE}/week average.**
"
    else
      CONTAINER_NOTES="${CONTAINER_NOTES}- ${NAME}: ${WEEK_DELTA} restart(s) this week (expected; average ${BASELINE:-n/a}/week)
"
    fi
    continue
  fi
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
- Memory stall: ${PSI_NOTE}
- Available memory: ${AVAIL_NOTE}
- OOM kills: ${OOM_NOTE}
- Swap activity: ${SWAP_NOTE}

## Reboots
- ${REBOOT_NOTE}
$( [ -n "$REBOOT_LINES" ] && printf '%s' "$REBOOT_LINES" )

## Disk (/)
- Now: ${DISK_NOW}
- Trend: ${DISK_TREND}
- ${DISK_FLAG}

## Containers
- ${CONTAINER_TOTAL} total, ${CONTAINER_FLAGGED} flagged
$( [ "$CONTAINER_FLAGGED" -gt 0 ] && echo "${CONTAINER_LINES}" || echo "- All running, no unexpected restarts since last create." )
$( [ -n "$CONTAINER_NOTES" ] && printf '%s' "$CONTAINER_NOTES" )

---
_Generated automatically every Sunday 7:00 AM from wilkie-home-server. Sources: sysstat, disk-usage.log, docker._
EOF
)"

if [ "${1:-}" = "--dry-run" ]; then
  echo "$REPORT"
else
  echo "$REPORT" | venv/bin/python send_mail.py --subject "Weekly Wyse Health Report - ${REPORT_DATE}"
fi
