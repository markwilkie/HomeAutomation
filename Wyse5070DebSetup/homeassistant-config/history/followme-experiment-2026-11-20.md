# Follow-Me spoofing experiment — 2026-11-20

Implementation: `packages/minisplit_followme_experiment.yaml`, renamed `packages/minisplit_followme_band.yaml` when made permanent.

## Baseline (2026-11-18/19, daytime Heat, target 69.5F, real relay)

- Compressor on at room ~68.7F (Follow-Me 20), off at ~72.0-72.5F (Follow-Me 22
  for a few minutes), peak after stopping 72.7-73.0F.
- ~4F peak to peak, ~1 cycle/hour, 12-18 min runs.
- Overnight (target 65.5F) worse: 65.1 -> 70.3F.
- Reading: the unit acts like a fixed thermostat on whole C around its ~20.5C
  setpoint — 20 = heat, 21 = hold, 22 = stop — so its 2C (3.6F) deadband plus
  the 3-minute heartbeat is the swing.

## Schedule (PT)

| Time | Phase |
|---|---|
| until 10:02 | baseline (real relay) |
| 10:02-12:06 | band: heat (19) at <= target-0.3F, stop (22) at >= target+0.5F, else hold (21); 8-min min run, 10-min min off |
| 12:06-17:15 | band, same but 6-min min run (adaptive 6-10 from ~12:15); max 3 starts per rolling hour (guard added 12:04) |
| 17:15-18:45 | end baseline (real relay) |
| 18:45 | hard stop before the 19:00 night ramp |

Changes during the day:
- First start at 09:58 used fixed 20/21/22 and a 67F safety floor; the floor
  stopped it at once (room 66.6F after the morning ramp) and the unit sat at
  20 for an hour without heating -- its band is setpoint +/- 1C, and today's
  setpoint was 20.5C (69F), not yesterday's 21C. Fixed to setpoint-relative
  values and a target +/- 4F safety band; restarted 10:01, compressor on 10:03.
- The planned soft_hold arm (14:00-17:15) was dropped at ~12:00: band already
  sends hold during the 8-min minimum run and the room rose at the baseline
  rate (0.25-0.33F/min), so the inverter doesn't throttle on a small gap.
  Shorter minimum runs (5/6 min, est. ~1.6-2F swing at ~2.5-3 starts/hour)
  were also considered and rejected to avoid extra compressor stress.
- Open question: the normal relay was sending 19 from ~09:36 but the unit
  didn't act on it until the experiment's own write at 10:01.

Early read (2 cycles, 10:35-11:50): 68.4-71.1F (~2.7F, -33%), ~1.6
starts/hour, 12-13 min runs.

Success: swing down >= 30% (<= 2.8F), <= 2.5 compressor starts/hour, no run
under 6 min.

## Results

(to fill in tonight)

Mid-afternoon read (6-min min run, 12:31-14:10): starts 12:37, 13:11, 13:46
(34-35 min gaps, ~1.7 starts/hour), runs 9-10 min, room 68.4-70.5F (~2.2F,
~-45% vs baseline; 8-min cycles were ~2.7F). Adaptive min run stayed at 6
(gaps in the 24-40 min "leave it" range). Control outage 12:04-12:14
(TupleWrapper split error, unit held stop) -- fixed, no errors since.

Minimum run 8 -> 6 min at ~12:10 (owner's call, keeping <= 3 starts/hour):
8-min cycles ran ~1.6 starts/hour (34-40 min periods, room rises ~0.29F/min,
falls ~0.1F/min, ~4 min from stop sent to compressor off). Estimate for 6
min: ~10 min runs, peak ~70.4F, ~2.0F swing, ~2.0 starts/hour (~2.3-2.5 on
a colder day); 5 min was rejected (runs under 10 min, ~2.6-2.8 starts/hour
when cold, for ~0.2F more).

Adaptive minimum run (deployed ~12:15, owner's suggestion): instead of
relying on the 3-starts/hour guard (which holds the room off below the start
line -- worst on cold days), each start during an arm adjusts the minimum run
by the gap since the previous start: < 24 min -> +1, 24-40 -> unchanged,
40-90 -> -1, bounds 6-10 min, 90+ min gaps ignored. Expected not to move
this afternoon (~30 min gaps at 6 min); it's there for colder conditions.

Rest of the 6-min arm: starts 14:23, 15:01, 15:42, 16:26, 17:13 (gaps
37-47 min, lengthening into late afternoon), runs 9-10 min, room
68.4-70.7F. Whole 6-min arm: 8 starts in ~4.6 h (~1.7/hour).

End baseline (17:15 real relay): the 17:13 start ran 13 min under the
real reading and peaked 71.4F (vs 70.5-70.7F with band), then coasted
into the 19:00+ night ramp with no restart -- the unit's heat line
(setpoint - 1C) stayed below the room as the ramp stepped the setpoint
down. Only one comparable cycle.

Fix for next run: latch heat/stop. At the start line the sensor's 0.18F
steps (68.54/68.72 around 68.7) flip heat/hold and send 19/21/19/21 within
a minute (same at the stop line, 69.44/69.62); starts then lag 5-7 min
instead of ~2, deepening the lows ~0.2F. Once heat is sent, keep it until
the compressor starts; once stop is sent, keep it until it stops.

## Made permanent (2026-11-20, ~22:30, owner's call)

Package renamed `packages/minisplit_followme_band.yaml`; entity IDs keep
the "experiment" names so history carries on. Changes from the trial:
- Runs whenever band is selected, the unit is in Heat, there's no manual
  override and the sensors are up (`binary_sensor.minisplit_follow_me_band_active`,
  2-min delay_off). Otherwise it pauses: the real reading is sent again and
  the normal relay takes over. No schedule; soft_hold removed.
- Heat/stop latch added (above).
- The setpoint automation no longer stands down; it's forced onto "direct"
  (Desired = target, rounded) while band is active, so the unit's own
  setpoint still follows the day/night ramps.
- Safety stop now only for real failures: room > target+4F, or < target-4F
  with the compressor off 30+ min. Mode change / override just pause.
- Known gap (same as the plain relay): if HA or the Zigbee sensor is down,
  the bridge keeps sending the last value. A Follow-Me staleness timeout in
  the bridge firmware would close it.

## First night permanent (2026-11-20/21)

- 22:07 band took over (room 66.4F, target 65F, 32F outside overnight).
- 23:04 and 06:22 the unit's setpoint slipped 65 -> 64F on its own (owner
  confirmed: not the remote). The bridge adopted each as a remote change
  and HA held the schedule -- and band -- off until 06:00 / 19:00. Same
  thing 2026-11-18 18:04 (70->71), 22:21 (66->67), 2026-11-19 17:03
  (70->71): always exactly half a degree C, before band control existed.
  Mechanism: the unit reports a value one step off; the bridge's heartbeat
  is built from the reported status so it re-sends it; the next poll
  "confirms" it and the bridge adopts it into Desired. Suspected trigger:
  the unit misreading the IR half-degree bit (state[12] 0x04) -- unconfirmed
  without the bridge's serial log.
- Fixes 2026-11-21: HA puts a non-HA change of <= 1F back to the schedule
  instead of holding it (logged as source "slip" in overrides.csv);
  firmware 1.3.2 resends a 1F move instead of adopting it. Remote changes
  of 2F+ still count as overrides.
- 04:52-05:07 (relay, override active): 63.3 -> 67.5F, the old ~4F swing.
- 06:13 band start overshot: 64.4 -> 68.0F (target 65.5, stop line 66),
  ~0.5F/min vs ~0.29F/min the day before. Heat had been stepped 17 -> 16C
  after 6 min "ignored", but values take 3-6 min to reach the unit (it got
  17 at ~06:13), so the step fired on lag and may have pushed full output.
  Heat step-down removed 2026-11-21. Still open: the 6-min minimum run on
  fast cold-morning heat-ups (early stop at target+1.5F after 4 min was
  floated, not built -- watch a few more cold mornings first).

## For cooling (not built)

Start log, 3-starts/hour guard and adaptive min run carry over unchanged.
Needs mode-awareness: run at room >= target+0.3F, stop at <= target-0.5F;
start value setpoint+1C, stop value setpoint-1C; step-up direction reversed;
safety stop must allow Cool. Verify the unit's cool band first (assumed
mirror of heat's setpoint +/- 1C). Use longer min runs (10-15 min) and a
slightly wider band for dehumidification.
