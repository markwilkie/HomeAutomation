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
| until ~10:30 | baseline (real relay) |
| ~10:30-14:00 | band: 20 at <= target-0.3F, 22 at >= target+0.5F, else 21 |
| 14:00-17:15 | soft_hold: 20 at <= target-0.3F, 22 at >= target+1.5F, else 21 |
| 17:15-18:45 | end baseline (real relay) |
| 18:45 | hard stop before the 19:00 night ramp |

Arm B kill criterion: if after 2 cycles the room still rises at the baseline
~0.28F/min while it's at 21, switch back to band until 17:15.

Success: swing down >= 30% (<= 2.8F), <= 2.5 compressor starts/hour, no run
under 6 min.

## Results

(to fill in)
