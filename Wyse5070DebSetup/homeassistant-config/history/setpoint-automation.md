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
