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
5. **Day/night setpoint ramp, replacing the flat target** — `input_number.minisplit_target_temp` (a single
   hand-set slider) was retired 2026-08-07 in favor of `input_number.minisplit_day_setpoint` /
   `minisplit_night_setpoint` plus a read-only `sensor.minisplit_computed_setpoint` (`template:` sensor,
   `configuration.yaml`) that ramps between them across the day. Deliberately a template sensor, not another
   input_number: the earlier design had an automation write a computed value into an input_number, and a
   user's own edit would just get silently overwritten within minutes. A template sensor has no `set` service
   at all, so there's nothing to silently overwrite.
6. **BME280 smoothing and a corrected indoor-temp reading** — `sensor.minisplit_bme280_smoothed` (5-minute
   moving average) replaced the raw BME280 sensor as `bme280_now`'s source 2026-08-08, after confirming on
   real hardware that the raw reading swings genuinely (not noise) by several degrees within 15 minutes,
   almost certainly from picking up the mini-split's own blown-air stream — sampled once per 10-minute cycle,
   that swing was what actually drove the setpoint's persistent flip-flopping, not any control-loop bug.
   Same day, `input_number.minisplit_temp_offset` was added (seeded to `-8`, assigned to the `bedroom` area)
   to correct a BME280 placement/calibration bias, with `sensor.minisplit_bme280_corrected` = smoothed +
   offset. **This offset must be applied on the BME280 side only.** It was briefly folded into
   `sensor.minisplit_computed_setpoint`'s day/night values too, on the reasoning that "the target should
   reflect the offset as well" — but `bme280_now - target_temp` is a subtraction, so an identical offset on
   both sides cancels out completely and the offset ended up with zero effect on the setpoint actually
   written to Tuya, only on what the display-facing sensors showed. Reverted the same day: `bme280_now` reads
   `sensor.minisplit_bme280_corrected` (offset-adjusted), `target_temp` reads the un-offset computed setpoint.

New `input_number`/template-sensor entities added via YAML don't get an `area_id` or (for `input_number`) a
real starting value from config alone — both need a one-time `docker stop` / edit `.storage/core.entity_registry`
+ `.storage/core.restore_state` / `docker start` cycle after deploying. Done for `minisplit_day_setpoint`,
`minisplit_night_setpoint` (2026-08-07), and `minisplit_temp_offset` (2026-08-08).

This history — the *why* behind each change — lives in this repo's git log for this directory, not in the
live HA instance itself.
