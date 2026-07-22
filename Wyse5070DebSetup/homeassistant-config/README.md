# Home Assistant config (tracked copy)

Home Assistant itself runs as a Container install (see [../setup-homeassistant.sh](../setup-homeassistant.sh)),
with its live config at `/mnt/data/appdata/homeassistant/config` on the server — outside git, and with no
backup of any kind (no `backup:` integration configured, no Supervisor, no cron/systemd job covering it).

This directory is a **manually-updated copy** of the two files most worth having history and documentation
for: `automations.yaml` and `configuration.yaml`. It is not live-synced — editing here does nothing to the
running instance, and editing the live files does nothing here. After changing either live file, re-copy it
here and commit, so the change and its rationale (commit message) are preserved.

```bash
cp /mnt/data/appdata/homeassistant/config/automations.yaml automations.yaml
cp /mnt/data/appdata/homeassistant/config/configuration.yaml configuration.yaml
```

This is documentation and change history, **not a backup** — it doesn't cover the recorder database
(`home-assistant_v2.db`, i.e. all sensor/state history), `.storage/` (entity registry, area/device
registry, auth, `input_number`/`input_boolean` current values), `secrets.yaml`, or any other integration's
config. Losing the server today would mean rebuilding entities/automations from these files but losing all
history and any state (like the MiniSplit cascade automation's learned `input_number.minisplit_adaptive_gain`)
that isn't captured in the YAML itself.

## MiniSplit cascade setpoint correction

The most involved automation here is `MiniSplit BME280 cascade setpoint correction` in `automations.yaml`
(the only one of the three MiniSplit automations currently enabled — see the automation's own `description:`
field for the full mechanism, kept in sync with the code since that's what actually runs). Summary of how it
got to its current form, in order:

1. **Base cascade control** — every 10 minutes, bias-corrects Tuya's onboard setpoint from the BME280 (true
   room) reading, projected forward by the measured BME280-behind-Tuya lag, gain-scaled up on hot days via
   outdoor temp. Replaced an earlier `generic_thermostat` + template switch approach that only toggled
   `hvac_mode` and never touched Tuya's actual setpoint.
2. **Asymmetric step cap** — downward moves (more cooling) capped at 1°F/cycle; upward moves (backing off an
   overshoot) allowed up to 4°F/cycle, added 2026-07-15 after the symmetric 1°F cap left the setpoint chasing
   an overshoot for several cycles.
3. **Ramping-guard bypass on upward moves** — the anti-short-cycle "demand changed in the last 3 minutes"
   guard now only blocks downward (more-cooling) corrections; an upward overshoot-correction is let through
   even mid-ramp, since relieving cooling demand doesn't fight the compressor the way adding more would.
4. **Adaptive gain** — the fixed `base_gain: 1.0` became a persisted, self-adjusting value
   (`input_number.minisplit_adaptive_gain` + `input_number.minisplit_last_correction_sign` in
   `configuration.yaml`, since automation templates carry no state between triggers). After every cycle that
   writes a correction, a same-direction repeat eases gain up 5%; a direction flip (oscillation) cuts it 15%;
   clamped to `[0.3, 1.2]`. Added 2026-07-16 in response to an overnight comparison showing BME280 stdev 1.45
   vs. 1.24 the prior night and 27 setpoint reversals >1.5°F, once a chart of BME280/setpoint/compressor state
   made the oscillation visible.

   **Cold-start caveat:** since these two `input_number` helpers are brand new with nothing to restore, HA's
   first-ever boot defaulted them to their configured `min` (`0.3` and `-1`) rather than a neutral `1.0`/`0`.
   It self-corrects over ~20-25 consistent-direction cycles (a few hours), bounded the whole time by the step
   cap above. Not repeated on subsequent restarts — from the second boot onward these restore whatever value
   was last learned (same "no `initial:`" reasoning as `input_number.minisplit_target_temp`).

This history — the *why* behind each change — lives in this repo's git log for this directory, not in the
live HA instance itself.
