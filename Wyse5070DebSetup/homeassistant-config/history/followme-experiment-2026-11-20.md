# Follow-Me spoofing experiment — 2026-11-20

Implementation: `packages/minisplit_followme_experiment.yaml`.

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

(to fill in)

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
