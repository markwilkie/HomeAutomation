# Self-Learning Mode Selection (Off / Cool / Heat) — Plan

**Status: Phase 0 (shadow mode) deployed and live, 2026-09-09.** All new
helpers, the two trend sensors, the decision-inputs logging sensor, and the
"MiniSplit Auto Mode Shadow Decision" automation are live on `wyse` —
`configuration.yaml`/`automations.yaml` in this repo reflect the deployed
state. **It still cannot touch the real unit**: the automation writes only
to `input_select.minisplit_shadow_mode`, never `climate.set_hvac_mode`.
Phases 1-4 (offline simulation, live-fixed, band learning, predictor-weight
learning) are still just plan, not built. Once this is fully built and live
through Phase 2+, the rationale below should migrate into the automation's
own `description:` field in `automations.yaml` — that's this repo's
established place for automation design history (see the "MiniSplit
FollowMe predictive setpoint correction" automation's multi-paragraph
description) — and this file should either be deleted or marked superseded,
not kept as a second copy of the truth.

**Two real implementation findings from the Phase 0 deploy, worth keeping
for Phase 2+:**
- A new `template:` sensor with no `homeassistant`/`start` trigger sits at
  `unknown` after every restart until its other trigger entities next
  change — confirmed live on the new outdoor-temp passthrough sensor,
  fixed by adding that trigger (same fix this file's "MiniSplit Outage
  Status"/"MiniSplit Duty Cycle EWMA" sensors already had to apply, so this
  was a known pattern this draft simply missed copying the first time).
- `input_select`'s `initial:` value reasserted itself across a restart even
  after the entity had already changed state live (`minisplit_shadow_mode`
  went from a real 'heat' decision back to 'off' on the very next restart,
  a few minutes later) — unlike `input_number`'s `RestoreEntity` behavior,
  which *did* correctly restore a seeded value across the same restarts.
  Not investigated further since Phase 0 self-corrects within one 10-minute
  tick regardless, but **this needs to be understood, not just tolerated,
  before Phase 2** — if it turns out to be `input_select`-specific rather
  than a fluke, the real automation's own persisted mode state (once it's
  actually controlling the unit) can't be allowed to silently reset on
  every HA restart the way this shadow one just did.

## Goal

A new HA automation that decides whether the mini split should be **Off**,
**Cool**, or **Heat**, replacing what's currently 100% manual mode
selection (confirmed: `grep`-ing `automations.yaml` for
`hvac_mode`/`system_mode`/`climate.turn_off` found nothing — every existing
MiniSplit automation only ever touches setpoint temperature, never mode).
Inputs: the existing Follow-Me sensor, the new Upstairs Hallway sensor, and
outdoor temperature from the Met.no weather integration. "Self-learns" means
adapting its own decision thresholds from observed outcomes over time, not a
literal ML model — this repo has no ML infra, and the existing setpoint
automations already establish the actual pattern this should follow:
input_number-backed adaptive gain, adjusted by simple rules (cut on a
reversal, ease up otherwise), not a black box.

**Explicit design goal: once a mode is picked, it should hold for multiple
hours, not minutes.** This isn't just an anti-short-cycle nicety on top of
an otherwise-reactive controller — it's the actual target this automation is
optimizing for. It's the reason the predictive horizon below is measured in
hours rather than minutes, and it means every guard downstream (dwell floor,
reversal-window for band learning, manual-override cooldown) should be sized
against "hours," not against the compressor's own ~25-35 minute duty cycle —
that duty-cycle number is still the *floor* those guards must clear, not the
target they should sit at.

## Critical scoping boundary — read this before writing any YAML

**This automation must only write `SystemMode`. It must never write
`temperature`.** The existing "MiniSplit FollowMe cascade/predictive
setpoint correction" automations already own the setpoint value on the same
entity and actively fight each other if both are enabled — the predictive
automation's own description says so in plain terms about its cascade
sibling. A mode-selection automation that also nudges setpoint would be a
third writer on top of an already-contested attribute. Division of
responsibility: the existing automations own *what temperature*; this one
owns *whether the unit runs at all, and in which direction*. Treat
`thermostat_6`'s current setpoint as a read-only input (the target to
compare indoor temp against), never as something this automation writes.

**Control surface is `climate.bedroom_mini_split_ac_bridge_thermostat_6`,
not `climate.mini_split_ac_bridge`.** Per `PLAN.md` Milestone 4 item 1
(done 2026-09-09), the main Thermostat endpoint now rejects `SystemMode`
writes outright (`ESP_ERR_NOT_SUPPORTED`). The Desired Setpoint endpoint
(`thermostat_6`) is the confirmed live mode-control surface — it's also
what the predictive setpoint automation itself was repointed to on
2026-08-30. Writing mode to the wrong entity will silently fail against
firmware that's already been deliberately hardened to reject it.

## Inputs (confirmed against live HA state, 2026-09-09)

| Signal | Entity | Notes |
|---|---|---|
| Follow-Me (primary comfort reference) | `sensor.0x18690afffe5602f1_temperature` | Device name `minisplit_followme_temp`. Already the AC's own Follow-Me source and the existing setpoint automations' error signal. No known placement bias (unlike the retired BME280, which read ~6°F high). |
| Hallway (secondary/corroborating) | `sensor.0x70d07efffea47190_temperature` | Device name "Upstairs Hallway" — new, not yet used by any automation. Both this and Follow-Me currently show HA area "Bedroom" in the device registry, which is stale/wrong for the hallway one — worth fixing before this ships, see Open items. |
| Outdoor temperature | `sensor.mini_split_ac_bridge_outdoor_temperature`, via the `sensor.minisplit_outdoor_temp` passthrough | 2026-09-10: was `weather.forecast_home` (Met.no `temperature` attribute) — switched after finding an 11.6°F disagreement with the AC unit's own outdoor sensor at the same moment (60°F forecast vs 71.6°F local, ~10:02am). The unit's own sensor is physically at the house; the forecast is a regional grid estimate. Per direct instruction. Forecast-based anticipatory logic (further out than the 3-hour trend below) is no longer planned against `weather.forecast_home` unless revisited later. |
| Current setpoint (read-only) | `climate.bedroom_mini_split_ac_bridge_thermostat_6`, `temperature` attribute | What the setpoint-correction automations are separately driving toward. This automation reads it, never writes it. |
| Compressor duty cycle (corroboration/override) | `sensor.minisplit_duty_cycle_ewma` | Already computed for the predictive setpoint automation's gain (`configuration.yaml`). Added 2026-09-10 as a Cool/Heat→Off override — see below. |
| Outdoor trend, large/sustained (entry gate) | `sensor.minisplit_outdoor_trend_3h` | Added 2026-09-10 — 3-hour derivative window, separate from the 1-hour `sensor.minisplit_outdoor_trend` the predictive early-trigger layer already used. Gates entering Cool/Heat from Off — see below. |

## Hallway and outdoor temp are predictors, not just corroboration

**Revised 2026-09-09 per explicit direction:** the original draft of this
plan used the hallway sensor only as a fault/corroboration check and
deferred outdoor temp's forecast to an optional phase-2 nice-to-have, with
Follow-Me-vs-setpoint hysteresis as the entire decision. That undersells
both signals. Hallway and outdoor temperature should actively **predict
which mode will be needed before Follow-Me itself crosses the band** — this
is core v1 behavior now, not a later refinement.

Why these two specifically make good leading indicators, and why using them
this way is *safer* than it sounds given this repo's own history:

1. **Hallway leads Follow-Me for whole-floor thermal load.** The hallway
   isn't directly conditioned by this mini split — it responds to the same
   whole-floor heat gain/loss (sun load, outdoor infiltration, other
   rooms' HVAC) that will eventually reach the bedroom, but isn't damped by
   the bedroom unit's own local effect. A sustained hallway trend is a
   reasonable early signal that the bedroom is about to follow.
2. **Outdoor temp (and its forecast trend) is the actual external driver.**
   A cold snap or hot afternoon moving in is knowable ahead of time from
   `weather.forecast_home`, well before it shows up as indoor drift.
3. **Neither signal is contaminated by the compressor's own duty cycle —
   unlike the setpoint automation's abandoned lag estimator.** That earlier
   attempt (see Self-learning section below) measured *Follow-Me's own*
   trend after a command and got fooled by the compressor's ~25-35 minute
   on/off rhythm imprinting directly onto the room's own sensor. Hallway
   and outdoor temp are exogenous to that cycle — they don't move because
   this unit's compressor just kicked on. That's precisely why they're
   usable as predictors here where Follow-Me's own trend wasn't.

Follow-Me-vs-setpoint hysteresis remains the **authoritative backstop**:
predictors only change *when* a mode switch happens (earlier, anticipating
the crossing), never *whether* it eventually happens. If the predictor never
fires, Follow-Me actually crossing the band still triggers the change
reactively. Don't let the predictive layer become the sole decision-maker —
keep it strictly additive on top of the proven reactive rule.

## Decision logic

Two layers: a **reactive backstop** (unchanged, still the ground truth) and
a **predictive layer** on top of it that's allowed to act earlier when
hallway/outdoor signals agree on a direction.

**Reactive backstop** — three-zone hysteresis against Follow-Me relative to
the current setpoint (`error = followme_temp - setpoint_temp`):

- `error > cool_band` → want **Cool**
- `error < -heat_band` → want **Heat**
- otherwise → want **Off**

`cool_band` and `heat_band` are self-learned (see below), seeded around
1.5°F each — in the same neighborhood as this system's existing constants
(the FollowMe setpoint-nudge automation's 2°F threshold; the setpoint
automations' 1°F minimum-move floor) — and tuned from real data rather than
guessed tighter.

**Off needs to be the widest, stickiest zone, not just "whatever's left
over."** Full on/off/mode cycling is more mechanically and energy-expensive
than a setpoint nudge, so once Off is entered, don't leave it until `error`
clears the *learned* cool/heat band by its full width again — a proper
Schmitt-trigger deadband around the Off zone, not a single boundary that can
chatter right at the edge.

**Compressor-idle override on the Cool/Heat→Off exit (added 2026-09-10,
live).** `error` alone isn't reliable as the *only* signal for "is this mode
still needed" — Follow-Me and the unit's own onboard sensor don't
necessarily agree, and that gap is not always noise. Confirmed live
2026-09-09 22:00–2026-09-10 05:56: the real compressor
(`binary_sensor...occupancy_4`) sat off for ~8 continuous hours while
`error` (Follow-Me vs. thermostat_6's setpoint) stayed +3.9 to +4.6°F the
entire time — Follow-Me reading meaningfully warmer than the setpoint for
hours after the unit's own sensor had already decided it was satisfied and
stopped running. A reactive rule keyed only on `error` would have held
"cool" the whole 8 hours on a stale signal. Fix: exit Cool/Heat to Off when
either `error` crosses zero (existing rule) **or** `sensor.
minisplit_duty_cycle_ewma` reads below 5% — that threshold is well past
what one normal ~25-35 minute compressor off-cycle decays to (this EWMA's
20-minute half-life means only a genuinely sustained idle stretch reaches
it; confirmed against the incident above, where the EWMA sat at 15-70%
during ordinary evening cycling and only crossed under 1% after ~2.5 hours
of continuous off). Deliberately **not** used to gate entry into Cool/Heat
from Off — an idle compressor says nothing about whether a fresh need is
just starting.

**Large-outdoor-trend gate on entering Cool/Heat from Off (added
2026-09-10, live).** Explicit design decision, per direct discussion: large,
hours-scale outdoor trends should drive mode entry more than a single noisy
indoor-delta reading — the reactive rule as originally built reacts to one
10-minute `error` snapshot with no regard for what outdoor conditions are
actually doing. Motivating incident: 2026-09-10 09:20, this automation
called Heat because Follow-Me sat ~2.6°F below a setpoint that had itself
been ramped up 70→71→72°F over the prior 2.5 hours by the separate "MiniSplit
Day/Night Setpoint Ramp" automation — Follow-Me itself never actually moved
(68.9–70.2°F all morning, flat), so this was the *target* moving, not the
room, and outdoor was warming the entire time (+3.05°F/hr on the 1-hour
trend) — the opposite of a real heating need. Fix: `sensor.
minisplit_outdoor_trend_3h` (3-hour derivative, separate from the 1-hour
trend the predictive layer above still uses for early-triggering) averages
out both the compressor's own duty cycle and single-hour blips like that
morning warmup, so a real reading past ±0.5°F/hr (a genuine ~1.5°F+ drift
over the window) means the trend is actually sustained. When it crosses
that threshold, entering Cool/Heat from Off now requires it to agree with
(or not contradict) `error`'s own direction. When it's flat/unavailable
(`outdoor_leaning = 'none'`), the reactive rule falls back to indoor error
alone exactly as before — this can never permanently deadlock the
automation if the outdoor sensor goes stale. Deliberately does **not**
affect exits from Cool/Heat (those stay on `error`/`compressor_idle` only,
per the override above) — the complaint was specifically about entering a
mode too eagerly, not about leaving one too slowly.

**Predictive layer** — compute a short-horizon projected Follow-Me error
using hallway and outdoor trends as leading terms, and allow it to trigger
the *same* mode the reactive rule would eventually pick, just earlier:

```
predicted_error = error
                 + w_hallway * hallway_trend_per_min   * horizon_min
                 + w_outdoor * outdoor_trend_per_min    * horizon_min
```

- `hallway_trend_per_min` / `outdoor_trend_per_min`: short-window slope of
  each sensor (HA `trend` platform, or a derivative template sensor — this
  repo already has direct precedent for a hand-rolled trend sensor,
  `sensor.minisplit_bme280_trend` from the pre-Follow-Me era; reuse that
  pattern rather than inventing a new one).
- `horizon_min`: how far ahead to project — **at least 60 minutes**,
  deliberately looking for broad trends rather than short-term noise. A
  short horizon (the original draft's 15-20 min) is exactly the range where
  transient blips — a door open, direct sun hitting a sensor — look like a
  real trend; a 60+ minute window is long enough that a genuine whole-floor
  or outdoor swing dominates the slope calculation instead. This also
  pushes the horizon comfortably past the ~25-35 minute compressor duty
  cycle the predictive setpoint automation had to account for, so a single
  compressor half-cycle can't masquerade as the trend on its own.
- `w_hallway` / `w_outdoor`: **learned weights**, not fixed guesses — see
  Self-learning below. Seed both low (e.g. 0.3) so the predictive layer
  starts conservative and only gets more influence once it's shown to be
  right more often than not.
- Apply the *same* `cool_band`/`heat_band` thresholds to `predicted_error`
  as the reactive rule uses on `error`. If the predicted zone agrees with
  where `error` is already heading (same sign), allow the mode switch to
  fire early. If the predictors disagree with each other, or don't move the
  projection out of the current zone, do nothing extra — fall back to the
  reactive rule.

**Sanity guard using outdoor temperature:** don't act on a decision that
outdoor temp makes physically implausible — e.g. don't switch to Heat when
outdoor is 85°F. That combination means either a sensor fault or a stale
reading, not a real heating need; log and skip rather than acting on it.
This applies to both the reactive and predictive layers.

**Forecast trend (beyond the ~60 min slope above):** if `weather.forecast_home`
shows a larger swing coming further out (e.g. an overnight cold snap several
hours away), that's a longer-horizon version of the same `w_outdoor` term —
feed it in once the ~60 min version above has live hours behind it, per the
rollout phases below. Confirm this HA version's forecast access method
(state attribute vs. `weather.get_forecasts` service) before wiring it in —
see Open items.

## Self-learning mechanism: adaptive band width, not an adaptive lag estimate

This repo already tried two different self-adjusting mechanisms for the
setpoint-correction problem and has clear, documented outcomes for both —
reuse the one that worked, and explicitly avoid re-attempting the one that
didn't:

- **Worked and is still live:** gain adaptation on outcome (cut on a
  reversal, ease up otherwise), backed by `input_number` helpers, with a
  hard floor to stop it from ever reaching zero/runaway.
- **Tried and reverted:** a live-measured lag estimate derived from how
  quickly a trend sensor's sign flipped after a command. It looked
  reasonable but was actually mostly measuring the compressor's own
  ~25-35 minute on/off duty cycle, not the room's real causal response —
  confirmed against independent humidity-sensor duty-cycle timestamps
  before reverting. **Do not rebuild a mode-selection lag estimator the
  same way** (e.g. "how long after switching to Cool did Follow-Me's trend
  turn negative") without first checking it against an independent signal
  the same way that revert did — the compressor-cycle contamination risk is
  identical here.

So: the thing this automation learns is **`cool_band` / `heat_band` width**,
using the same reversal-triggered adjustment shape as the existing gain
adaptation:

1. Every time this automation itself changes mode, log the timestamp to a
   new `input_datetime.minisplit_last_auto_mode_change`.
2. If this automation triggers another mode change in the *opposite*
   direction (or an Off that immediately follows a Cool/Heat it just set)
   within a **reversal window sized to the multi-hour goal, not just past
   the compressor's own duty cycle** — the compressor's ~25-35 minute
   on/off rhythm (per the predictive automation's own investigation) is
   only the floor this window must clear, not the target; since the actual
   goal is holding a mode for multiple hours, start the reversal window at
   **3 hours**, so a change that happens sooner than that is treated as a
   real short-cycle event regardless of whether the compressor's own cycle
   happens to explain it — widen the relevant band by 15% (matching the
   existing gain automation's own reversal-response percentage, for
   consistency across the codebase), capped at a ceiling (e.g. 4°F — beyond
   that the automation isn't really doing its job).
3. If a long stretch (start at 24 hours) passes with zero short-cycle
   events at the current band, ease it back down 5% (again matching the
   existing ease-up percentage) toward a floor (e.g. 0.75°F), so the band
   doesn't only ever ratchet wider.
4. **Outdoor-temperature-bucketed bands are a real future refinement**
   (the building's recovery behavior at 20°F outside is not the same as at
   95°F) but deliberately out of scope for v1 — this repo has direct,
   recent, documented history (2026-08-29) of a compounding-adjustment
   mechanism running the controlled value to its floor twice in simulation
   even after two rounds of deadband fixes, specifically when it was
   changed to adjust on every tick instead of only on ticks that actually
   acted. Don't add a second dimension (outdoor bucket) to the learning
   state until the one-dimensional version has real live hours behind it.

**A second, separate learned quantity: the predictor weights (`w_hallway`,
`w_outdoor`) from the decision-logic section above.** These get adjusted by
whether an early (predictor-triggered) mode switch turned out justified,
checked *after the fact* against Follow-Me's own actual trajectory — not
against the predictors' own inputs, to avoid the same circularity that made
the abandoned lag estimator unreliable:

1. When the predictive layer fires a mode switch early (before the reactive
   rule alone would have), record the trigger.
2. Check back after the reversal-window/dwell-floor elapses: did Follow-Me's
   `error` actually continue moving into the predicted zone (validating the
   early call), or did it reverse/plateau before ever reaching where the
   reactive rule would have fired on its own (meaning the prediction jumped
   the gun)?
3. Validated → ease the relevant weight (`w_hallway` or `w_outdoor`,
   whichever contributed more to that call) up slightly, capped well below
   1.0. Jumped the gun → cut it, same 15%-cut/5%-ease asymmetry as the band
   learning above, for consistency.

Keep the two learned quantities (band width, predictor weights) as separate
`input_number` groups adjusted by separate rules — don't conflate "the zone
boundaries are wrong" with "the early-trigger predictors are unreliable,"
they're different failure modes with different fixes.

## Hard safety floor, independent of the learned band

A minimum dwell time between any two mode changes *this automation
triggers*, enforced regardless of what the learned band currently says —
same role the existing gain automation's hard floor plays. Given the
explicit multi-hour design goal above, start this at **2 hours**, not a
double-digit-minutes value — a compressor mode/reversing-valve change is
mechanically more expensive than a setpoint nudge, and "multiple hours per
mode" is the actual target being optimized for, not just "longer than the
compressor's own cycle." This should be set at or below the band-learning
reversal window (3 hours, above) — the dwell floor is the hard "never
sooner than this" wall, the reversal window is the (necessarily looser)
threshold used to decide whether a change that did clear the floor still
counts as premature for learning purposes.

## Respecting manual/out-of-band changes

Before writing a mode decision, this automation should check whether the
live mode already matches what a human (physical remote, Tuya app, or a
direct HA card interaction) set it to out of band recently, and back off for
a cooldown window (start at **2 hours**, matching the dwell floor above — a
human's manual choice deserves at least as much standing as this
automation's own decisions) rather than immediately overriding it.
This mirrors the same principle the firmware itself already applies at the
IR layer (`PLAN.md` Milestone 2: refresh from Tuya immediately before every
send, don't silently revert an out-of-band change) — the same respect
should apply one layer up, at the HA automation level, for mode.
Concretely: compare the live mode against what `input_datetime.
minisplit_last_auto_mode_change` says this automation itself set, and skip
if the live mode changed more recently than that timestamp and doesn't match
what this automation would currently pick.

## Rollout phases

Matches this repo's own established practice for the predictive setpoint
automation — simulate against real history before trusting live behavior,
don't skip straight to production:

1. **Phase 0 — shadow mode. This is the actual starting point of
   implementation, and it never calls `climate.set_hvac_mode` or touches
   the real unit at all.**

   **Correction: this can't be a plain stateless template sensor.** A
   template sensor has no memory of its own last value, but the hysteresis
   (sticky Off zone) and dwell-floor logic that make "hold for hours" mean
   anything both depend on knowing the *current* shadow mode and *when it
   last changed* — without that, the shadow output would just chatter every
   time `error` crosses a raw threshold, which is precisely the behavior
   this whole design exists to avoid, and would make the eventual evaluation
   meaningless (it wouldn't represent what the real automation will actually
   do). Implement it instead as:
   - `input_select.minisplit_shadow_mode` (`off`/`cool`/`heat`) — the
     decision itself, a settable helper rather than a derived template.
   - `input_datetime.minisplit_shadow_last_change` — shadow-only mirror of
     what `input_datetime.minisplit_last_auto_mode_change` will become once
     this is live, so the dwell floor can be exercised in shadow mode too.
   - A time-pattern automation (matching the real automation's eventual
     cadence) that reads the current inputs, applies the reactive rule +
     predictive layer + dwell floor against the current `input_select`
     value, and updates it — this is the actual decision engine, running
     for real, just writing to a helper instead of the climate entity.

   `input_select.minisplit_shadow_mode` is an ordinary HA entity once
   created, so its state history is recorded automatically like any other
   helper — that's what makes the later evaluation possible without extra
   plumbing. Alongside it, add a template sensor
   (`sensor.minisplit_auto_mode_decision_inputs`, attributes only —
   `error`, `hallway_trend`, `outdoor_trend`, `predicted_error`, whether the
   predictive layer is what triggered the current value) that records *why*
   each decision was made at the time it was made — needed because the raw
   trend values themselves aren't otherwise persisted anywhere, and
   reconstructing "what would the trend have read three weeks ago" after the
   fact isn't reliable once recorder history ages out.

   **Evaluating correctness, concretely — two separate checks, not one:**
   - *Agreement check:* does the shadow decision at time T match what was
     actually manually set on the real unit around T? Cheap, immediate,
     available from day one — just diff two entities' history in Logbook or
     a short template.
   - *Outcome check (the more important one, and the actual point of this
     phase):* for stretches where the shadow decision *disagreed* with the
     human's actual choice, look at how Follow-Me's `error` behaved over
     the following hours under whatever the human actually chose — did it
     stay comfortable anyway (human was right, or it didn't matter), or did
     it visibly drift out of band in the direction the shadow decision
     would have corrected (human was arguably wrong, or slow, and the
     shadow decision would have caught it earlier)? This is what actually
     answers "was the automation's proposed mode correct," not just
     "did it match what a person happened to do."

   Run this for at least several days spanning a real range of outdoor
   temps before moving to Phase 1.
2. **Phase 1 — offline simulation.** Export Follow-Me/Hallway/weather
   history plus the actual manual mode-change history from the recorder DB
   and run the proposed decision function (reactive + predictive) against
   it, the same way the predictive setpoint automation's gain-adaptation
   logic was simulated against ~39 hours of real data before it was trusted
   live.
3. **Phase 2 — live, everything fixed (no self-learning yet).** Deploy
   behind a new `input_boolean.minisplit_auto_mode_enabled` kill switch
   (default off), with `cool_band`/`heat_band` and `w_hallway`/`w_outdoor`
   all fixed at their seed values. Validate the discrete Off/Cool/Heat
   decision, the sanity guard, the predictive layer's early triggers, and
   the anti-short-cycle floor all behave correctly against the real unit
   before adding any adaptation on top.
4. **Phase 3 — enable band learning.** Only after phase 2 has run with zero
   short-cycle events hitting the hard floor guard for a real stretch of
   time.
5. **Phase 4 — enable predictor-weight learning.** Only after phase 3's band
   learning has itself been stable for a real stretch — don't have two
   different adaptive mechanisms both moving for the first time in the same
   window, or a bad outcome is ambiguous about which one to blame.

## New helpers needed

- `input_number.minisplit_mode_cool_band`, `input_number.minisplit_mode_heat_band`
- `input_number.minisplit_mode_w_hallway`, `input_number.minisplit_mode_w_outdoor`
- `sensor.minisplit_hallway_trend`, `sensor.minisplit_outdoor_trend` — use
  the `derivative` platform (confirmed via live config check, 2026-09-09:
  `sensor.minisplit_bme280_trend` used exactly this platform before being
  removed 2026-09-06 when BME280 itself was retired — the sensor instance
  is gone, but the mechanism was already proven for this exact problem
  shape in this exact HA install, so reuse it rather than reaching for the
  `trend` binary-sensor platform, which only gives rising/falling, not a
  usable slope value). Set `time_window` to the ~60 min horizon.
- `input_datetime.minisplit_last_auto_mode_change`
- `input_boolean.minisplit_auto_mode_enabled` (kill switch, default off)

Remember the existing gotcha with new HA helper entities in this setup: a
freshly created `input_number`/template helper needs a manual stop/edit
storage file/start cycle to get its area_id and starting value set
correctly — plain YAML alone doesn't do it. Don't assume `configuration.yaml`
changes alone are sufficient the way they are for automations.

Also remember `initial_state`'s actual behavior on this HA install, learned
the hard way on the predictive setpoint automation: omitting it means HA
restores whatever on/off state the automation last had (including "silently
off with no explanation"), while `initial_state: true` forces it back on
every restart. Decide deliberately which one this automation should use —
probably `initial_state: true` once it's the trusted always-on controller,
same reasoning the predictive automation itself landed on — rather than
leaving it unset by default.

## Open items — needs a decision before implementation starts

- **Hallway sensor's HA area is currently "Bedroom"** (device registry,
  confirmed 2026-09-09) — almost certainly wrong for a sensor named
  "Upstairs Hallway." Worth correcting before this ships so any future
  dashboard/area-based automation doesn't inherit the same confusion.
- **Forecast access method** — confirm whether this HA version's Met.no
  integration still exposes the `forecast` state attribute or requires
  `weather.get_forecasts`; only matters for the phase-2 anticipatory logic,
  not the reactive v1.
- **Exact seed values** (band widths, reversal window, dwell floor,
  cooldown window) are starting points reasoned from this system's existing
  constants, not measured for this specific decision yet — phase 0/1 above
  exist specifically to replace them with real numbers before anything
  adapts live.
- Confirm firmware behavior is actually fine with `SystemMode = Off` being
  set/cleared frequently by this new automation, on top of the existing
  3-minute Follow-Me heartbeat (`followme_task`) still running regardless
  of mode — Off itself is already a confirmed, tested encoding
  (`PLAN.md` Milestone 1/2, 2026-09-07), but this automation would exercise
  it far more often than manual use has so far.
