# MiniSplit predictive setpoint automation — design history

Moved out of the automation's `description:` in automations.yaml on 2026-10-05; the description keeps only the current behavior. Dated entries below are as originally written.

(The cascade automation this history keeps referring to was deleted
2026-10-05 -- see git history.)

Alternative to "MiniSplit BME280 cascade setpoint correction" --
created 2026-08-26 to try anticipating a BME280 drift instead of reacting
to it, after tracing a real overnight incident where the trend sensor
turned unambiguously positive at 5:06am but no correction landed until
5:40am (34 minutes later): the cascade automation's own anti-short-cycle
guards were both satisfied the whole time, so the real blocker was its
damped adaptive gain (0.46 that night) shrinking a real, sustained
correction below its fixed "must move >=1 degree F" threshold on every
intervening 10-minute cycle, with no memory of those near-misses carried
forward. Live/enabled as of the 2026-08-26 cutover (initial `initial_state:
false` + a one-shot bootstrap automation handled the first turn-on and
have both been removed now that it's the active controller -- `initial_state`
turned out to re-apply on every restart, not just the first, which is why
a later restart silently left this off with no bootstrap left to flip it
back; removing the key lets normal on/off state persist across restarts
like every other automation here). Both this
and the cascade automation write to climate.mini_split_ac_bridge's
setpoint and will fight each other if both run, so only enable one at a
time. Gain/last-correction-sign/trend-sign-since state lives in its own
input_number/input_datetime helpers (configuration.yaml,
"predictive"/"trend" prefixed), entirely separate from the cascade
automation's, so switching between the two for comparison doesn't
cross-contaminate either one's learned state.

One change from the cascade automation: persistent-trend threshold
override. input_number.minisplit_trend_last_sign and
input_datetime.minisplit_trend_sign_since track how long
sensor.minisplit_bme280_trend has held its current sign with no
reversal (reset to 0 elapsed only on an actual sign flip, not every
run). Once that persistence reaches 40 minutes, the minimum-move
threshold that gates whether a correction actually gets written relaxes
from 1 degree F to 0.5 -- so a real, sustained trend that keeps
computing a sub-1-degree correction can no longer get silently dropped
cycle after cycle the way it did in the 5:06-5:40am incident. A trend
that reverses within those 40 minutes never earns the relaxed
threshold, so this doesn't make the loop more sensitive to a single
noisy blip -- persistence has to actually hold.

2026-08-26, later same day: this automation originally also carried a
live-measured lag estimate (input_number.minisplit_lag_estimate_min)
replacing the cascade automation's fixed `lag_minutes: 16`, updated from
how long the trend took to reach an expected sign after each command.
Removed after a few hours live: the compressor's own ~25-35 minute
on/off duty cycle turned out to imprint a real, smooth, near-periodic
swing directly onto sensor.minisplit_bme280_trend (confirmed against
sensor.bedroom_mini_split_ac_bridge_humidity_3's demand-cycle timestamps, not
just inferred), so "time until the trend reaches the expected sign" was
mostly measuring coincidental alignment with the compressor's own
rhythm, not the room's causal response to a setpoint command. The
estimate drifted from the seeded 16 minutes down toward 11 as a result,
and the persistence gate above was getting reset by those same
compressor-driven swings almost every cycle (many resolved in exactly
one 10-minute tick), reintroducing setpoint oscillation this redesign
was meant to reduce. Back to the fixed 16-minute constant below; the
persistence threshold was raised from 20 to 40 minutes at the same time
so one compressor half-cycle alone is much less likely to satisfy it.

Everything else -- the occupancy-stability gate, the demand-ramp
anti-short-cycle guard (skipped for upward/overshoot moves, same
reasoning as the cascade automation), the outdoor-temp gain scaling, the
asymmetric 1-degree-down/4-degree-up step caps, and the gain-adaptation
logic itself (15% cut on a reversal, 5% ease-up otherwise) -- is carried
over unchanged from the cascade automation.

2026-08-29: tried moving gain-adaptation to run every 10-minute tick
against the computed new_setpoint instead of only on ticks that write,
after tracing gain sitting pinned at its 0.3 floor for hours (real
corrections almost always alternate direction near equilibrium, so
comparing write-to-write treated nearly every correction as a
"reversal"). Simulated against ~39 hours of real data before deploying,
validated the simulator against real history first (exact match), and
the every-tick version genuinely did react faster on average -- but the
same simulation also showed the setpoint running away to the 60F floor
repeatedly (twice even after two rounds of deadband fixes), something
the real write-gated version never did once in the same real window.
Reverted the same day, before it had run long enough live to show the
same runaway for real. The underlying idea (comparing decisions too
close together in time treats normal alternation as a mistake) is
still valid and worth revisiting, but needs a design that can't
compound gain upward indefinitely through a single sustained real
trend with nothing to check it -- not attempted again without a
reworked cap (e.g. capping consecutive eases per persistence episode,
or requiring evidence of actual overshoot rather than just "sign held")
and a fresh simulation showing the runaway is actually gone, not just
smaller.

2026-08-30: rewritten firmware flashed (desired/actual setpoint split,
no more revert-on-failure logic, fixed poll interval, HTTP timeout +
stall self-restart). current_setpoint now reads
climate.bedroom_mini_split_ac_bridge_thermostat_6's temperature
attribute (Matter endpoint 6, dedicated desired-setpoint entity) instead
of input_number.minisplit_desired_setpoint, and the real command target
for climate.set_temperature moved from climate.mini_split_ac_bridge (now
read-only, rejects setpoint writes) to that same new entity.

2026-09-08: this was thermostat_8 (endpoint 8) until the BME280 removal
shifted every later Matter endpoint down by 2 -- HA doesn't renumber an
existing entity when the underlying endpoint moves, it just leaves the
old one frozen and creates a new one at the new number, so this had to
be repointed by hand. (The pre-shift entity_id was actually never
"bedroom_mini_split_ac_bridge_thermostat_8" either -- that one was
created before the device's HA name gained "Bedroom," so its real
entity_id was "mini_split_ac_bridge_thermostat_8" with no prefix. This
automation had been silently targeting a nonexistent entity_id the
whole time; the endpoint-shift fix happens to also correct that, since
thermostat_6 was created after the rename and does carry the prefix.)
input_number.minisplit_desired_setpoint's final write was removed --
the new entity's own attribute is the record now, no separate
bookkeeping helper needed. input_number.minisplit_desired_setpoint
itself is unused by this automation as of this change but left in
configuration.yaml for now rather than deleted, in case anything else
still references it.

2026-08-30, same day: found this automation switched off after the
firmware flash/restart cycle, with nothing in the logs explaining why
and no `initial_state` key to have caused it (that was deliberately
removed back on 2026-08-26 -- see above -- specifically because it
re-asserts on every restart, not just the first). Restoring the
previous run state (whatever it happened to be) across a restart is
HA's own default for automations with no initial_state, so once
something switched this off, it silently stayed off. Setting
`initial_state: true` (below) trades that default for "always come back
on," which is the correct posture for this one specifically -- it's the
intended always-on controller, not a reference/comparison automation
like the cascade one above (which is deliberately left disabled and
should stay whatever a person last set it to).

2026-08-30, later same day: temporarily disabled (initial_state: false)
while the ESP32 bridge was out of the room for reboot-loop debugging --
no point correcting against a BME280 reading/setpoint the unit couldn't
act on. Bridge is back in place and stable (~8.5 hours uptime, no
reboots, no spurious power-offs since the StartUpOnOff fix); re-enabled
the same day.

2026-09-06: BME280 retired for a Zigbee "follow me" temp sensor
(sensor.0x18690afffe5602f1_temperature), read directly with no smoothing
or offset -- see configuration.yaml for the corresponding removal of
sensor.minisplit_bme280_corrected, sensor.minisplit_bme280_smoothed, and
input_number.minisplit_temp_offset. lag_minutes/trend_per_hour/
projected_error (the BME280 drift-projection terms) were removed;
`error` is used directly in `correction` below. The persistent-sign
threshold-relaxation subsystem (input_number.minisplit_trend_last_sign,
input_datetime.minisplit_trend_sign_since, persistence_minutes, the
relaxed 0.5-degree min_move, and the 2026-09-04 max_step_down/up
halving -- see both below) was rebuilt on the sign of `error` itself
rather than removed: that protection was never actually specific to
BME280's supply-airflow noise, it just needs some signal to track
persistence of, and error is the one this automation still reads
directly. The helper entity IDs were kept as-is (still
"trend"-prefixed) so their live state carries over with no reseed.
Also discovered this same day: the 2026-09-04 max_step_down/up
persistence gate had been deployed live but never synced back to this
repo file -- now folded in below along with the rest of this rework.
Day/night setpoints also dropped 5 degrees (75/75 -> 70/70 live).

2026-09-06, later same day: added a flat 29-minute cooldown (see the
condition just above the final action) capping writes to at most one
per cooldown window regardless of direction or confidence -- a direct
measurement of "has the last correction had time to show up yet"
rather than the occupancy/demand-ramp proxy above, which stays in place
alongside it. 29 (not 30) to line up with the compressor's own
~25-35 minute duty cycle.

2026-09-10: added the same room-relative safety clamp as the cascade
automation above, applied after the actual_temp anchor, read off
thermostat_6's own hvac_mode state. Off/other modes are left unclamped.

2026-10-04: fixed that clamp, which was backwards -- it capped Cool at
Follow-Me + 2 and floored Heat at Follow-Me - 2, limiting how far the
setpoint could back off instead of how far past the room it could push.
Found once the unit was in Heat (2026-10-01..04): with the room warmer
than the heat target, the floor dragged the setpoint up with the room,
so the written setpoint sat a median +2F (max +6F) above the heat
target, the room a median +3.8F above it, and 175 of 191
compressor-on minutes were heating a room already >1F above target
(e.g. 2026-10-03 07:00-10:10: night target 66F, setpoint pushed to 72F,
compressor ran three times at 73-74F room). Now Cool is floored at
Follow-Me - 2 and Heat capped at Follow-Me + 2, rounded inward to whole
degrees. Same day, the compressor/demand guard (2026-08-28 note below)
became mode-aware: it used to let any UPWARD move through
unconditionally, which is "backing off" in Cool but "adding heat" in
Heat -- so in Heat it blocked easing off mid-cycle and never blocked
piling on. Now the backing-off direction for the current mode passes
unconditionally and the conditioning direction waits for the compressor
to be off. The hot-day gain boost (outdoor_factor, >75F) is still
cooling-only; no cold-day equivalent yet.

2026-10-05, later: review fixes. (1) Only runs in Heat or Cool -- in
Auto it used the heat target with no bound, so lowering the setpoint
below the room would have made the unit cool. (2) The actual_temp
anchor (keep Desired within 1F of the unit's reported setpoint) was
replaced by a hold on the firmware's own mismatch status: the unit
reports whole-F values 1-2F off for many setpoints (0.5C steps; its 16C
minimum reads as 62F), and the anchor dropped out whenever thermostat_1
went quiet for an hour, which together produced the overnight 60<->61
writes. The firmware compares in its own C/F terms, so the hold needs no
extra slack. (3) Gain no longer adapts while the target bound pins the
result -- it had ratcheted to its 1.2 ceiling and stayed there ~20h
during a one-sided night. (4) The cooldown uses its own last-write
helper instead of thermostat_6's last_updated. (5) outdoor_factor is
mode-aware: Cool boosts above 75F outdoor, Heat below 45F.

2026-10-05: added a target-relative anti-windup bound -- Heat never
written below target - 1F, Cool never above target + 1F. This
controller lowers the setpoint whenever the room is above target, which
in Cool means "cool harder" but in Heat only means "heat less" -- and
with the room 4-7F above the night heat target all night (2026-10-04/05)
it stepped Desired all the way down to the 60F floor. That left the
unit needing ~3 hours of 1F/29min writes to get back to a 66F target if
the room ever cooled, and since the unit's IR minimum is 16C (reported
61F), 60 could never confirm, so it flipped 60<->61 every 30-70 min all
night (ten IR commands, each also dropping Follow-Me until the
2026-10-04 firmware fix is flashed).

2026-11-22: controller removed (owner's call). With Follow-Me the unit
regulates on the real room temperature, and in Heat band control
(packages/minisplit_followme_band.yaml, 2026-11-20) decides start/stop
directly, so the controller had already been forced into "direct" while
band ran. Replaced by "MiniSplit schedule setpoint" (same automation id):
write the target, rounded half-up to whole F, when it's 1F or more from
Desired and has held 2 minutes, in Heat/Cool, outside manual overrides.
Removed with it: input_select.minisplit_setpoint_strategy,
input_number.minisplit_predictive_adaptive_gain /
_predictive_last_correction_sign / _trend_last_sign,
input_datetime.minisplit_trend_sign_since / _predictive_last_write,
sensor.minisplit_duty_cycle_ewma. Known trade-off: if Follow-Me drops
(Zigbee sensor or HA down) nothing compensates for the unit's ~3-4F-warm
onboard sensor, so the room runs ~3F cool until it's back. Same day: the
seasonal weight keeps its last value when the 7-day outdoor average is
briefly unavailable (config reloads), which removes the target spikes.

The automation as it stood before removal:

```yaml
- id: '1787800000001'
  alias: MiniSplit FollowMe predictive setpoint correction
  description: >-
    STRATEGY (2026-10-05): input_select.minisplit_setpoint_strategy.
    "direct" (default) writes Desired = the target (rounded half-up, only
    when it changes, same 29-min cooldown) while
    binary_sensor.minisplit_followme_engaged is on -- with Follow-Me engaged
    the unit regulates on the real room temperature itself, so the
    controller below was correcting a loop that already works (it changed
    Desired 27 times in the 24h before this, winding down to the 60F floor;
    direct would have written 7 times, at the day/night ramps). "predictive"
    -- and "direct" whenever Follow-Me isn't engaged -- uses the controller
    described below, which exists to compensate for the unit judging the
    room by its own ~3-4F-warm sensor.

    CONTROLLER (2026-10-05) -- the dated history below explains how it
    got here. Every 10 minutes, only while thermostat_6 is in Heat or Cool:
    error = Follow-Me - target (sensor.minisplit_computed_setpoint, the
    mode's own day/night target). New setpoint = Desired - gain * error,
    stepped at most 1F per write (0.5F if error's sign just flipped), then:
    room clamp (no more than 2F of conditioning past Follow-Me), then
    target bound (never more than 1F of backing off past the target --
    Heat >= target - 1, Cool <= target + 1). Holds instead of stepping
    further while the firmware reports a setpoint mismatch or Tuya
    unreachable (the unit isn't following). Writes go through only if: the
    move is >= 1F (0.5F once error's sign has held 40+ min), at least 29
    min since this automation's own last write
    (input_datetime.minisplit_predictive_last_write), and -- for moves that
    add conditioning -- the compressor is off and demand stable; backing
    off passes regardless. Gain adapts (cut 15% on a sign reversal, +5%
    otherwise, 0.3-1.2) on ticks the target bound didn't pin, and is
    boosted on hot days in Cool / cold days in Heat.

    MANUAL OVERRIDE (2026-10-05): skipped while
    binary_sensor.minisplit_manual_override is on (a setpoint set on the HA
    card or the remote, until the next 06:00/19:00 day/night change). Each
    write records its value in input_number.minisplit_schedule_setpoint first
    so the override detector can tell it apart.

    Dated design history (2026-08-26 onward): history/setpoint-automation.md
    next to this file -- moved out of this description 2026-10-05.
  initial_state: true
  triggers:
  - trigger: time_pattern
    minutes: /10
  conditions:
  # Anti-short-cycle guard, changed 2026-08-28: only blocks when the
  # compressor is currently OFF *and* stopped less than 5 minutes ago --
  # give a just-stopped compressor a chance to settle before nudging the
  # setpoint again. If it's currently running, act regardless of how long
  # it's been running -- there's no short-cycle risk from adjusting the
  # setpoint while the compressor is already on, only from disturbing it
  # right after it stops. The old version blocked on ANY recent transition
  # in either direction, including one *into* "on" -- which meant two of the
  # last four scheduled ticks got blocked purely by coincidence (a real
  # on/off flip landing 1-2 minutes before the check), even though the
  # compressor was actively running at the time and there was no actual
  # short-cycle risk.
  # 2026-09-09: was binary_sensor.mini_split_ac_bridge_occupancy (no
  # "bedroom_" prefix) -- found dead (state=None) during a live investigation,
  # causing a live template error every run ("as_timestamp got invalid input
  # ''") logged at every automation trigger. Real live entity is
  # binary_sensor.bedroom_mini_split_ac_bridge_occupancy_4 (confirmed against
  # configuration.yaml's own "Fixed to point at the real Compressor Running
  # entity" note, which already uses this same entity_id elsewhere).
  - condition: template
    value_template: >-
      {{ is_state('binary_sensor.bedroom_mini_split_ac_bridge_occupancy_4', 'on')
         or (as_timestamp(now()) - as_timestamp(states.binary_sensor.bedroom_mini_split_ac_bridge_occupancy_4.last_changed)) > 300 }}
  # 2026-10-05: Heat/Cool only -- see description.
  - condition: template
    value_template: "{{ states('climate.bedroom_mini_split_ac_bridge_thermostat_6') in ['heat', 'cool'] }}"
  # 2026-10-05: hands off during a manual override (HA card or remote) --
  # see "MiniSplit Manual Override Detect" below.
  - condition: state
    entity_id: binary_sensor.minisplit_manual_override
    state: "off"
  actions:
  - variables:
      target_temp: "{{ states('sensor.minisplit_computed_setpoint') | float(66) }}"
      adaptive_gain: "{{ states('input_number.minisplit_predictive_adaptive_gain') | float(1.0) }}"
      last_sign: "{{ states('input_number.minisplit_predictive_last_correction_sign') | int(0) }}"
      # 2026-09-06: reads the Zigbee "follow me" temp sensor directly, no
      # smoothing or offset. BME280 (sensor.minisplit_bme280_corrected, and
      # the 5-minute smoothing average it was built on -- both retired the
      # same day) needed that pipeline because it sat in the mini-split's
      # own supply airflow; follow-me doesn't share that placement problem,
      # so there's nothing here left to smooth or offset-correct.
      followme_now: "{{ states('sensor.0x18690afffe5602f1_temperature') | float(0) }}"
      outdoor_temp: "{{ states('sensor.mini_split_ac_bridge_outdoor_temperature') | float(75) }}"
      # 2026-08-30: firmware flashed with the desired/actual setpoint split --
      # climate.bedroom_mini_split_ac_bridge_thermostat_6 (Matter endpoint 6)
      # is a dedicated, HA-writable setpoint that nothing else touches or
      # mirrors Tuya status onto, so it's read directly now instead of via
      # the HA-side input_number.minisplit_desired_setpoint bookkeeping
      # helper that stood in for it pre-flash (retired the same day -- see
      # the removed final action below). climate.mini_split_ac_bridge's own
      # temperature attribute is still never read here: post-flash it's
      # read-only and purely mirrors whatever Tuya's cloud reports, still
      # carrying the same Celsius-0.5-step rounding/compressor-driven-trend
      # noise this avoided pre-flash too.
      current_setpoint: "{{ state_attr('climate.bedroom_mini_split_ac_bridge_thermostat_6', 'temperature') | float(67) }}"
      # 2026-10-05: hold instead of stepping further while the unit isn't
      # following Desired -- firmware's OUTAGE_REASON_SETPOINT_MISMATCH
      # (Desired vs Tuya's temp_set_f differ by >1F in the firmware's own
      # C/F conversion) or Tuya unreachable. Replaces the 2026-09-01
      # actual_temp anchor (keep Desired within 1F of thermostat_1's reported
      # setpoint): that compared raw whole-F values the unit reports 1-2F off
      # for many setpoints, and dropped out whenever thermostat_1 went quiet
      # for an hour (2026-09-10 stale_ok fix), which together caused the
      # 2026-10-05 overnight 60<->61 writes. Same purpose -- stop drifting
      # when corrections don't land -- with no added slack.
      unit_not_following: >-
        {{ states('sensor.minisplit_outage_status') in ['Setpoint mismatch', 'Tuya unreachable'] }}
      error: "{{ followme_now - target_temp }}"
      # 2026-09-06: was sensor.minisplit_bme280_trend's derivative sign until
      # BME280 was retired -- rebuilt on error's own sign instead, since the
      # "a freshly reversed signal isn't trustworthy yet" protection this
      # feeds (both the step-cap below and the min_move relaxation further
      # down) was never actually BME280-specific. Small deadband around zero
      # so float noise right at target doesn't flap the tracked sign.
      # Entities kept as minisplit_trend_last_sign / minisplit_trend_sign_since
      # (configuration.yaml) rather than renamed, so existing live helper
      # state carries over with no new-helper reseed needed.
      current_error_sign: "{{ 1 if error > 0.1 else (-1 if error < -0.1 else 0) }}"
      error_last_sign: "{{ states('input_number.minisplit_trend_last_sign') | int(0) }}"
      is_sign_flip: "{{ current_error_sign != error_last_sign }}"
      # If the sign just flipped this run, persistence is 0 regardless of
      # what minisplit_trend_sign_since still says (it hasn't been updated
      # yet this run) -- computed this way so the threshold-relaxation logic
      # below doesn't need a second read after the helper-update step.
      sign_since_ts: "{{ as_timestamp(states('input_datetime.minisplit_trend_sign_since'), as_timestamp(now())) }}"
      persistence_minutes: "{{ 0 if is_sign_flip else ((as_timestamp(now()) - sign_since_ts) / 60) }}"
  # Record a new sign-since timestamp only on an actual flip, so
  # persistence_minutes above keeps growing across runs where the sign holds.
  - choose:
    - conditions: "{{ is_sign_flip }}"
      sequence:
      - action: input_number.set_value
        target:
          entity_id: input_number.minisplit_trend_last_sign
        data:
          value: "{{ current_error_sign }}"
      - action: input_datetime.set_datetime
        target:
          entity_id: input_datetime.minisplit_trend_sign_since
        data:
          timestamp: "{{ now().timestamp() }}"
  - variables:
      # Gain boost in harsh weather for the current mode: Cool ramps in above
      # 75F outdoor (full at 95F), Heat below 45F (full at 25F). Was
      # cooling-only until 2026-10-05.
      outdoor_factor: >-
        {% set mode = states('climate.bedroom_mini_split_ac_bridge_thermostat_6') %}
        {{ min(max((outdoor_temp - 75) / 20, 0), 1) if mode == 'cool'
           else min(max((45 - outdoor_temp) / 20, 0), 1) if mode == 'heat' else 0 }}
      # 2026-09-03: load_factor added after finding the loop reversing the
      # setpoint almost every tick during low-load stretches -- the BME280's
      # +-1F/20-30min idle swing (supply-airflow placement) looked exactly
      # like a real trend to this formula with nothing distinguishing "the
      # compressor is actually working" from "it's not running and this is
      # noise." sensor.minisplit_duty_cycle_ewma (configuration.yaml) is an
      # EWMA of compressor on/off state, ~20min half-life -- dividing by
      # 0.15 (a typical active-evening duty cycle) and clamping to
      # [0.15, 1.0] means gain is ~full strength once duty cycle reaches
      # that level, scales down smoothly below it, and never fully zeroes
      # out (the 0.15 floor still allows a slow correction to get through
      # given enough persistence, rather than freezing solid during a long
      # idle stretch). Simulated against the 9/2 21:00-9/3 17:00 window
      # before deploying: cut writes 28->8 and reversals 13->2 over that
      # replay, and fully eliminated reversals in the specific afternoon
      # window (compressor idle almost the whole time) that prompted this.
      duty_cycle_ewma: "{{ states('sensor.minisplit_duty_cycle_ewma') | float(0) / 100 }}"
      load_factor: "{{ min(max(duty_cycle_ewma / 0.15, 0.15), 1.0) }}"
      gain: "{{ adaptive_gain * (1 + outdoor_factor) * load_factor }}"
      correction: "{{ gain * error }}"
      # Anchored to current_setpoint, not Tuya's own onboard current_temperature
      # (tuya_now, dropped 2026-08-26 evening) -- that anchored every write to
      # Tuya's own sensor, which reads well below the setpoints this loop
      # actually manages (a persistent multi-degree gap, not noise), so the
      # gap alone dominated raw_setpoint and pushed corrections downward every
      # cycle regardless of what the BME280-driven `correction` term above
      # concluded. Traced from three real corrections the same evening where
      # `correction` was consistently positive (calling to warm up, correctly,
      # since the room was already below target and still falling) but the
      # setpoint moved down anyway. The cascade automation above already made
      # this exact fix on 2026-08-09; this automation should have copied that
      # version instead of the pre-fix formula when it was written.
      raw_setpoint: "{{ min(max(current_setpoint - correction, 60), 75) }}"
      # Persistence gate (originally added 2026-09-04, live-only and never
      # synced back to this repo until the 2026-09-06 BME280->follow-me
      # rework surfaced the gap): a sign that just flipped (persistence_minutes
      # == 0, i.e. this tick or the flip itself) hasn't earned the full
      # 1F/cycle budget yet, so it gets half instead. Traced to a real
      # incident: compressor ran hard then stopped at 13:04, BME280 kept
      # reading colder for several minutes on residual airflow, and the
      # fresh (unconfirmed) trend that produced got a full 1F step at 13:10
      # same as a trend held for an hour would have -- three such ticks in a
      # row (13:10/13:20/13:30) pushed the setpoint 72->75 before the trend
      # reversed at 13:40 and gave a degree back. Backtested against
      # 2026-09-02 12:00 - 2026-09-04 14:00 real history before deploying:
      # this change caps that specific incident's peak at 74 instead of 75
      # (the two ticks with zero persistence -- the initial reaction and the
      # reversal -- get halved; the two ticks that had already held for
      # 10+/20+ minutes are unaffected), while leaving reversal count and
      # where the loop settles over the full 48-hour window essentially
      # unchanged (22 writes/9 reversals baseline vs. 20 writes/9 reversals
      # gated, same final setpoint). Threshold is intentionally short (10
      # min, one tick) -- long enough to filter a single-tick blip, short
      # enough that a real fast trend only loses one half-strength cycle
      # before resuming full authority. 2026-09-06: re-based on error's own
      # sign (persistence_minutes above) rather than BME280's derivative
      # trend sign, same as the min_move relaxation below -- this guard was
      # never actually about BME280's noise specifically.
      max_step_down: "{{ 1 if persistence_minutes >= 10 else 0.5 }}"
      max_step_up: "{{ 1 if persistence_minutes >= 10 else 0.5 }}"
      stepped_setpoint: "{{ (current_setpoint + min(max(raw_setpoint - current_setpoint, -max_step_down), max_step_up)) | round(0) }}"
      # Hold (see unit_not_following above): don't step further while the
      # unit isn't following Desired; the firmware keeps resending it.
      held_setpoint: "{{ current_setpoint if unit_not_following else stepped_setpoint }}"
      # Room-relative safety clamp (2026-09-10, corrected 2026-10-04 -- see
      # description): on top of the hold above, never let the
      # written setpoint call for more than 2 degrees F of conditioning past
      # what Follow-Me is actually reading -- a Cool setpoint more than 2F
      # BELOW the room, or a Heat setpoint more than 2F ABOVE it -- however
      # the gain/error math above got there. Rounded inward to a whole
      # degree so the clamp never produces a fractional setpoint. hvac_mode
      # read straight off thermostat_6's own state (Matter climate entities
      # carry hvac_mode as their state, not an attribute) -- the same entity
      # this automation already reads current_setpoint from.
      hvac_mode_now: "{{ states('climate.bedroom_mini_split_ac_bridge_thermostat_6') }}"
      room_clamped_setpoint: >-
        {{ max(held_setpoint, (followme_now - 2) | round(0, 'ceil')) if hvac_mode_now == 'cool'
           else min(held_setpoint, (followme_now + 2) | round(0, 'floor')) if hvac_mode_now == 'heat'
           else held_setpoint }}
      # Target-relative anti-windup bound (2026-10-05, see description):
      # never back off more than 1F past the target -- Heat no lower than
      # target - 1, Cool no higher than target + 1. Applied last, so it wins
      # over the room clamp above: a setpoint at the user's own target can't
      # over-condition the room.
      controller_setpoint: >-
        {{ max(room_clamped_setpoint, (target_temp - 1) | round(0, 'ceil')) if hvac_mode_now == 'heat'
           else min(room_clamped_setpoint, (target_temp + 1) | round(0, 'floor')) if hvac_mode_now == 'cool'
           else room_clamped_setpoint }}
      # Strategy (2026-10-05, see description): "direct" writes the target
      # itself (rounded half-up to whole F) while Follow-Me is engaged --
      # the unit then regulates on the real room temperature on its own --
      # and falls back to the controller above otherwise.
      # 2026-11-20: always direct while Follow-Me band control is active --
      # its spoofed values trip followme_engaged, and the band's commands
      # are relative to whatever Desired is (packages/minisplit_followme_band.yaml).
      use_direct: >-
        {{ is_state('binary_sensor.minisplit_follow_me_band_active', 'on')
           or (is_state('input_select.minisplit_setpoint_strategy', 'direct')
               and is_state('binary_sensor.minisplit_followme_engaged', 'on')) }}
      new_setpoint: "{{ ((target_temp + 0.5) | round(0, 'floor')) if use_direct else controller_setpoint }}"
      # Which direction eases off the current mode (up in Cool, down in
      # Heat) -- always allowed through the compressor/demand guard below.
      # Direct writes always pass: setting the target isn't a push.
      backing_off: >-
        {{ use_direct or (new_setpoint < current_setpoint if hvac_mode_now == 'heat' else new_setpoint > current_setpoint) }}
      # The threshold this incident actually got stuck on: a sustained
      # (>=40min, no reversal) sign can act on a half-degree move instead of
      # needing a full degree, since persistence has already ruled out noise.
      # 40 (not the original 20) so one compressor half-cycle (~25-35min,
      # see the description above) can't satisfy this on its own.
      min_move: "{{ 0.5 if persistence_minutes >= 40 else 1 }}"
  # 2026-08-29: gain-adaptation runs every 10-minute tick against the
  # computed decision, not just on ticks that clear the write threshold.
  # Original problem: comparing WRITE-to-write direction treated nearly
  # every real correction as a "reversal" (a healthy control loop
  # oscillating near equilibrium naturally alternates direction almost
  # every time it acts), pinning gain at its 0.3 floor for hours overnight
  # as steady-state behavior, not an edge case.
  #
  # Two failed attempts before this one, both caught in simulation before
  # being left live long enough to matter for real:
  #  1. Gating on new_setpoint != current_setpoint (a "tie") turned out to
  #     be a no-op -- current_setpoint/new_setpoint are always whole
  #     numbers, so "not a tie" is mathematically identical to "clears
  #     min_move," collapsing gain-updates back to firing on nearly the
  #     same ticks as real writes and erasing the benefit.
  #  2. Deadbanding on `correction` (|correction|>0.1) let the threshold's
  #     effective sensitivity shrink as gain grew (correction = gain *
  #     projected_error), a self-reinforcing loop -- simulated over ~39
  #     real hours, gain raced to its 1.2 ceiling and the setpoint marched
  #     to the 60F floor repeatedly, something the real deployed policy
  #     never did once in the same window.
  #
  # This version deadbands on `error` instead (gain-independent, doesn't
  # shrink as confidence grows) -- was `projected_error` (BME280's trend-
  # projected error term) until 2026-09-06, renamed when that term was
  # removed along with BME280 itself; the deadband value and reasoning are
  # unchanged. Also re-simulated with the compressor-guard
  # responding to THIS policy's own downward pushes (turns on ~immediately
  # after a push, holds ~15min, matching real measured engagement/cycle
  # data) instead of being graded against the real, milder policy's
  # occupancy history, which had been letting pushes stack up in the
  # simulation that the live guard would actually have blocked. With that
  # more realistic guard: zero floor hits, setpoint bottomed at 63F (vs the
  # real policy's 69F, over the same ~39 real hours) instead of 60F, write
  # frequency stayed roughly flat across the window instead of accelerating,
  # and the latency/error-at-intervention benefit held (~36%/~17% better
  # than the write-gated original) though more modestly than the first
  # (invalid, fixed-history-guard) estimate suggested.
  # 2026-10-05: also skipped while the target bound pins the result
  # (new_setpoint != room_clamped_setpoint) -- otherwise a long one-sided
  # stretch eases gain up 5% every tick with nothing actually being
  # corrected (pinned at 1.2 for ~20h, 2026-10-04/05).
  - choose:
    - conditions: "{{ error | abs > 0.1 and not use_direct and controller_setpoint == room_clamped_setpoint }}"
      sequence:
      - variables:
          this_sign: "{{ -1 if error > 0 else 1 }}"
          gain_step: >-
            {{ adaptive_gain * 0.85 if (last_sign != 0 and this_sign != last_sign)
               else adaptive_gain * 1.05 }}
          new_adaptive_gain: "{{ min(max(gain_step, 0.3), 1.2) | round(2) }}"
      - action: input_number.set_value
        target:
          entity_id: input_number.minisplit_predictive_adaptive_gain
        data:
          value: "{{ new_adaptive_gain }}"
      - action: input_number.set_value
        target:
          entity_id: input_number.minisplit_predictive_last_correction_sign
        data:
          value: "{{ this_sign }}"
  # 2026-08-28: added the occupancy (compressor-proxy) check to the downward
  # branch after finding 3 of 7 compressor cycles overnight got a *second*
  # downward nudge stacked on top of an already-running cooling call, each
  # one landing before the 16-minute lag had a chance to show the first
  # nudge's effect in the trend -- integrator windup, and a likely
  # contributor to the largest overshoot of the night (70->62F on the
  # Tuya-reported temp, 12:10-12:42am, the longest cycle of the night).
  # Upward moves stay unconditional -- easing off mid-cycle is always safe.
  # Downward moves now also require the compressor to be off right now, on
  # top of the existing humidity-stability guard below.
  # 2026-09-09: was binary_sensor.mini_split_ac_bridge_occupancy (no
  # "bedroom_" prefix) -- same dead entity fixed above at this automation's
  # top-level gate; real live entity is
  # binary_sensor.bedroom_mini_split_ac_bridge_occupancy_4 (Compressor
  # Running).
  # 2026-10-04: "upward" generalized to "backing off for the current mode"
  # (up in Cool, down in Heat) -- see description.
  - condition: template
    value_template: >-
      {{ backing_off
         or (is_state('binary_sensor.bedroom_mini_split_ac_bridge_occupancy_4', 'off')
             and (as_timestamp(now()) - as_timestamp(states.sensor.bedroom_mini_split_ac_bridge_humidity_3.last_changed)) > 180) }}
  - condition: template
    value_template: "{{ (new_setpoint - current_setpoint) | abs >= min_move }}"
  # 2026-09-06: hard cooldown -- at most one write per 29 minutes, regardless
  # of direction or how confident the correction is. 2026-10-05: measured
  # from input_datetime.minisplit_predictive_last_write (set right after
  # each write below), not thermostat_6's last_updated -- HA restarts,
  # manual edits, mode changes and the firmware's mode sync all bump that
  # too, which kept restarting the cooldown (five restarts on 2026-10-04).
  #
  # 29 minutes (not a round 30) lines up with the compressor's own
  # documented ~25-35 minute on/off duty cycle -- caps corrections at
  # roughly one per cycle, which is the cadence the guards above were
  # already chasing indirectly (both the 2026-07-15 and 2026-08-28
  # incidents were a second correction landing before the first had time to
  # show up in the reading). This is a direct measurement of that same
  # thing instead of a proxy, so it overlaps in purpose with the
  # occupancy/demand-ramp guard above rather than replacing it -- left both
  # in place for now.
  #
  # Applied to both directions, not just downward like the guard above --
  # the user asked for a flat "once every 29 minutes" limit, not an
  # asymmetric one. Revisit if upward/overshoot corrections end up feeling
  # too sluggish in practice.
  - condition: template
    value_template: >-
      {% set last = states('input_datetime.minisplit_predictive_last_write') %}
      {{ last in ['unknown', 'unavailable', ''] or (as_timestamp(now()) - as_timestamp(last, 0)) >= 1740 }}
  # 2026-08-30: now targets the dedicated Desired Setpoint endpoint (see
  # current_setpoint above), not climate.mini_split_ac_bridge -- writing the
  # main endpoint's setpoint post-flash is rejected outright by the firmware
  # (it's read-only now, mirroring Tuya only). The firmware's own sync_task
  # reconciles this endpoint's value against Tuya every poll and re-sends on
  # its own if they disagree.
  #
  # 2026-10-05: record the value BEFORE writing it, so the override detector
  # (triggered by Desired changing) sees this write as the schedule's own.
  - action: input_number.set_value
    target:
      entity_id: input_number.minisplit_schedule_setpoint
    data:
      value: "{{ new_setpoint }}"
  - action: climate.set_temperature
    target:
      entity_id: climate.bedroom_mini_split_ac_bridge_thermostat_6
    data:
      temperature: "{{ new_setpoint }}"
  - action: input_datetime.set_datetime
    target:
      entity_id: input_datetime.minisplit_predictive_last_write
    data:
      datetime: "{{ now().strftime('%Y-%m-%d %H:%M:%S') }}"
  mode: single
```
