---
name: otbr-stability-check
description: Check OTBR (Thread border router) crash/hang stability on wyse since the last recorded baseline
---
Host: wyse (192.168.15.30, SSH as `mwilkie`). Container: `otbr`.

1. Find the last baseline in memory (`otbr-watchdog-crash-check`) — the date/time of the last check and its incident counts. If no baseline exists, say so and treat the full retained log as the baseline window.
2. Confirm the running image/uptime hasn't silently changed underneath the comparison:
   `docker inspect otbr --format '{{.Config.Image}}  started={{.State.StartedAt}}  restarts={{.RestartCount}}'`
   If the image differs from the last migration recorded in memory (`otbr-production-image-migration` / `otbr-matter-thread14-update`), treat this as a hard reset point — don't diff hang frequency across an image change as apples-to-apples.
3. Pull watchdog history since the baseline date:
   `awk '/<baseline-date>/{f=1} f' /mnt/data/appdata/otbr/watchdog.log`
   Don't pipe through `tail -N` before counting — get the full range first, then trim for display.
4. Classify each line, don't just count total lines:
   - `otbr-agent unresponsive ... restarting container` = a real incident (hang). Note the restart-to-responsive gap (single fast recovery vs. a cascade of several restarts in a few minutes — cascades are the bad pattern).
   - `radio tx timeout` / `Failed to communicate with RCP` = the other known failure mode (RCP/USB crash), distinct from a hang — call out which signature is present, don't lump them together.
   - `NAT64 translator is Active (drifted back on)` / `Reapplying Jool NAT64 override` = expected image behavior, not instability. Exclude from incident counts.
5. **Timezone trap**: `docker inspect` timestamps (`StartedAt`, etc.) are UTC. `watchdog.log` and `journalctl` are host-local (PDT, UTC-7). Convert before treating a `docker inspect` timestamp as an unexplained/unlogged restart — subtract 7 hours and check it against watchdog.log again before calling it a mystery.
6. Check `ls -la /mnt/data/appdata/otbr/diagnostics/` — retention is capped (~20 files). If the oldest file on disk is newer than the baseline date, say so rather than assuming zero incidents occurred in the gap.
7. Compare the new incident count/rate against the baseline's, and flag any time-of-day clustering (e.g. repeated incidents within the same hour across different days) as a hypothesis worth investigating, not a conclusion.
8. Report: incident count and signature breakdown since baseline, image/uptime confirmation, and whether this represents an improvement, regression, or no change vs. the baseline.
9. Update the `otbr-watchdog-crash-check` memory with the new baseline (date checked, counts, signature, any open hypotheses) so the next check diffs against this one.
