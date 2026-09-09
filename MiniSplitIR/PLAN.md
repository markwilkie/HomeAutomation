# Pioneer Mini Split — IR Follow-Me Bridge Implementation Plan (v2)

**Superseded 2026-09-04 by [../MiniSplit/PLAN.md](../MiniSplit/PLAN.md).**
Decision: fold IR control into the existing MiniSplit bridge instead of
running it as this separate commissioned node, and source Follow-Me's
ambient temperature from a SONOFF SNZB-02P over MQTT (Zigbee2MQTT) instead
of a Matter binding to MiniSplit's BME280. Reasoning is recorded in the new
plan's header. The protocol byte map and derived facts below have been
migrated to [../MiniSplit/IR_PROTOCOL_REFERENCE.md](../MiniSplit/IR_PROTOCOL_REFERENCE.md);
this file and [captures/protocol_capture.md](captures/protocol_capture.md)
stay in place as the raw historical record (source of truth if a byte value
ever needs re-verifying) until the new plan's milestones are done and this
directory is removed — not before. Everything below this notice is the
original v2 plan, unchanged, kept for reference.

---

Supersedes [instructions.txt](instructions.txt), which stays in place as the
original ask/historical record (same convention as
[../MiniSplit/instructions.txt](../MiniSplit/instructions.txt)). This version
replaces guesses with what's actually been measured and built, and corrects
two inaccuracies that crept into an earlier draft of this rewrite (flagged
inline below where they matter).

## Goal (unchanged)

Bypass the unreliable Tuya cloud command path for the Pioneer WT012GLUI25FVQ
mini split by controlling it locally over Matter/Thread, and replicate the
unit's "follow me" behavior using a real ambient temperature sensor instead
of the mini split's own (poorly placed / Tuya-mediated) sensor. Tuya's status
GETs keep running in parallel, read-only and harmless — this project only
replaces the *command* path.

**Correction from earlier drafts of this plan:** "Device A" is not a
separate device, ESPHome or otherwise -- there is no such device anywhere in
this repo or in the Matter fabric. **Device A is simply the existing,
already-commissioned MiniSplit bridge** (node `@1:18`), specifically its aux
BME280 endpoint (`temp_sensor_ep=2`, a standalone Temperature Sensor
endpoint fed by `matter_update_aux_temperature()` in
[../MiniSplit/src/matter_device.cpp](../MiniSplit/src/matter_device.cpp)) --
confirmed by matching `sensor.mini_split_ac_bridge_temperature_2` (referenced
throughout `Wyse5070DebSetup/homeassistant-config/configuration.yaml`'s
BME280 filter/trend automations) against that endpoint's Matter naming
pattern. This sensor is already live, already smoothed on the HA side (see
the "MiniSplit BME280 Smoothed"/"Corrected" template sensors in that same
config for the actual real-world filtering already applied), and needs zero
new firmware work. **Milestone 2 (the originally-planned ESPHome rewrite) is
eliminated entirely** -- see that section below.

So there's really only one device to build here:

- **Device A — Sensor source.** The existing MiniSplit bridge's aux BME280
  Temperature Sensor endpoint (node `@1:18`, endpoint 2). Already running,
  already commissioned, nothing to do.
- **Device B — IR blaster node.** New. **ESP32-C6-WROOM-1-N4 DevKitM-1**
  (native 802.15.4/Thread radio). Matter Thermostat, drives the AC over IR,
  runs the follow-me heartbeat using Device A's temperature via a Matter
  binding. **Commissioned and live in Home Assistant's Matter fabric as of
  2026-09-03** (node `@1:27`) — see Milestone 3 for what that does and
  doesn't mean yet.

Only one new physical board is needed for this whole project (Device B) --
already on hand and already flashed/commissioned.

---

## Hardware

**Capture-phase kit (Milestone 1, done) — Dorhea 4x/4x** (HX-03 transmitter,
HX-M121 receiver). HX-M121 is what actually mattered here (the receiver);
HX-03 is a fine bench-test transmitter but not the recommended final-mount
part — see below.

**Final-mount transmitter — adhesive "IR blaster eye" cable** (e.g. AooCare
3.5mm-plug extender, ~$7). Recommended over the Dorhea LED for the deployed
Device B:

- Same deal as the Dorhea part electrically — bare, unmodulated IR LED, no
  onboard carrier generation, drive it through a GPIO (ideally via a
  transistor stage — cheap insurance, do it regardless of range).
- 3.5mm mono plug: cut it off and identify the two leads (tip = anode
  typically, sleeve = cathode/ground; LEDs are directional — if it doesn't
  fire, swap leads, no damage from getting it backwards), or use a cheap
  female 3.5mm breakout jack instead of cutting.
- **Why this over the Dorhea LED:** adhesive backing sticks it directly onto
  the unit's receiver window, point-blank — sidesteps the ~1.3m Dorhea range
  limit entirely rather than needing Device B's whole enclosure within
  line-of-sight. Locate the window first (below), place the emitter, then
  route the wire back to wherever Device B's enclosure lives.

**Receiver window location:** co-located with the display board module on
the front panel (small smoked/tinted window near the digital display, per
the WYS-series factory disassembly guide's component grouping) — confirm
exact aim point with the real remote (a chirp/beep or display change
confirms receipt) before final placement. Already done for Milestone 1's
capture rig; same window is the target for the final emitter.

No hardware work needed for Device A -- see the correction above, it's the
existing MiniSplit bridge's already-installed BME280.

---

## Milestone 1 — Capture the real protocol ✅ DONE

See [captures/protocol_capture.md](captures/protocol_capture.md) for the full
session log. Summary of what's now known cold:

**Protocol: `TCL112AC`** (Gree/TCL-family, 112-bit). Every button press sends
two back-to-back frames — a "Type 2" special/quiet frame and a "Type 1"
full-state frame; Type 1 carries the human-readable state.

**Wire encoding** (needed for Milestone 3's transmitter, since Device B is
ESP-IDF, not Arduino — see that milestone for why this matters):
- Header: mark ~3100us, space ~1560-1600us (varies slightly by frame; not
  timing-critical, ~25% tolerance observed to decode fine)
- Per bit: mark ~500-530us (constant), then space ~290-320us = `0`, or
  ~1060-1110us = `1`
- 112 bits, LSB-first per byte, 14 bytes total, transmitted MSB-of-array-first
  (`state[0]` first)
- 38kHz carrier during marks (standard for this protocol family; not
  independently re-measured, inherited from IRremoteESP8266's `IRTcl112Ac`
  defaults which decoded our captures cleanly)
- No separate short/temp-only frame exists for this protocol family, contrary
  to the original plan's assumption — see Follow Me heartbeat below.

**State byte map** (`uint8_t state[14]`), all confirmed by direct capture:
| Byte | Meaning |
|---|---|
| `state[0..2]` | Fixed header: `0x23 0xCB 0x26` |
| `state[3]` | Frame type: `0x01` = Type 1 (full state), `0x02` = Type 2 (special/quiet) |
| `state[4]` bit 7 | Follow Me enabled (`0x80` set) / disabled (`0x00`) |
| `state[6]` low nibble | Mode: `1`=Heat, `2`=Dry, `3`=Cool, `7`=Fan, `8`=Auto |
| `state[6]` bit 7 | Follow Me enabled (mirrors `state[4]` bit 7, same bit position) |
| `state[7]` | Setpoint: `Temp = 31 - state[7]` (e.g. `0x0A`→21C, `0x08`→23C). Ignored by the unit in Auto mode -- setpoint writes there are silently no-ops. |
| `state[8]` | Fan: `0`=Auto, `2`=Quiet/Low (disambiguated only by the Type 2 frame's Quiet flag, see below), `3`=Medium, `5`=High |
| `state[11]` | Follow Me sensor temp, whole degrees C, only meaningful when `state[4]`/`state[6]` bit 7 is set |
| `state[12..13]` | Checksum-dependent trailer (state[12] carries a toggle/mode bit, state[13] is a computed checksum over the frame -- byte-level formula not reverse-engineered; reuse IRremoteESP8266's `IRTcl112Ac::checksum()`-equivalent logic rather than re-deriving) |

**Fan cycle order:** Auto → Quiet → Low → Med → High → (back to Auto). Quiet
and Low share `state[8]=2`; only the paired Type 2 frame's Quiet flag tells
them apart -- a naive "did state[8] change" check misses the Auto→Quiet step.

**Follow Me heartbeat:** real remote re-sends every **3 minutes** (not the
"short frame" originally assumed) -- it's the *exact same* full 112-bit
frame as the enable frame, with one bit cleared (`state[5]` bit `0x20`) to
distinguish "enable" from "periodic re-send." Device B's heartbeat sender
can reuse the exact same full-frame serializer as any other command.

**Follow Me fallback timeout:** not measured (battery-pull test was skipped).
Using **10 minutes** as a placeholder -- unverified, re-measure before relying
on it for anything comfort-relevant. Device B's own heartbeat interval
doesn't need this number regardless -- it should just match the real
remote's observed 3-minute cadence directly, not a "half of timeout"
derivation.

---

## Milestone 2 — Device A — ELIMINATED, already exists

No work here. Device A is the existing MiniSplit bridge's aux BME280
Temperature Sensor endpoint (node `@1:18`, endpoint 2, `0x0402` Temperature
Measurement cluster) -- already running ESP-IDF + `esp-matter` (not
ESPHome), already commissioned, already publishing readings that
`Wyse5070DebSetup/homeassistant-config/configuration.yaml`'s "MiniSplit
BME280 Smoothed"/"Corrected" template sensors already consume in production.
The binding target for Device B's `LocalTemperature` is this existing
endpoint directly -- see Milestone 3's binding section below.

If MiniSplit's own BME280 firmware ever needs changes (smoothing tweaks,
additional endpoints), that work belongs in `../MiniSplit/`, not here --
this project only *consumes* that endpoint, it doesn't own it.

---

## Milestone 3 — Device B: IR blaster + Thermostat Matter device

**Actual status, corrected:** the esp-matter Thermostat *skeleton* is built,
flashed, and fully commissioned into Home Assistant's Matter fabric (node
`@1:27`) as of 2026-09-03 -- see [README.md](README.md) for the verified
commissioning log detail. **IR command sending is not implemented at all
yet** -- there is no shadow-state struct, no transmitter driver, and no
wiring from Matter attribute writes to an actual IR send in the current
`src/main.c` / `src/matter_device.cpp` (an earlier draft of this plan said
IR sending was "presumed working per that commissioning" -- that's wrong;
commissioning only proves the Matter/Thread skeleton works, it says nothing
about IR at all, which hasn't been touched). `LocalTemperature` not updating
is expected, not a mystery to diagnose -- unlike an earlier draft of this
plan assumed, Device A *does* already exist as a Matter node (it's the
existing MiniSplit bridge, node `@1:18` -- see the correction above), but no
binding/subscription from Device B to it has been attempted yet, so there's
nothing feeding the attribute. That's exactly what's left to build below,
not an open bug.

**One thing worth verifying now that commissioning works, before writing
more code on top of it:** confirm SystemMode/setpoint *writes* from Home
Assistant actually reach `matter_get_system_mode_command_pending()` etc. in
`src/matter_device.cpp` -- only attribute *reads* have been confirmed
working so far (HA can see the entity; whether HA can control it hasn't been
exercised). **Fan speed isn't part of this check** -- there's no Matter
attribute for it yet at all, separate gap, see below.

### Control surface coverage -- every aspect of the real remote, checked off explicitly

The goal is full control (everything the real remote does), not just a
partial command pass-through, and equally the real *point* of this project
(controlling off real room temperature, not the unit's own sensor) -- so
tracking both halves explicitly here rather than letting either one hide:

| Control | Matter attribute | Shadow-state field | IR encoding | Status |
|---|---|---|---|---|
| Mode (Heat/Dry/Cool/Fan/Auto) | `SystemMode` -- exists | `mode` | `state[6]` low nibble -- captured, all 5 values | Matter side done; needs a **SystemMode-enum -> protocol-mode-value mapping table** (Matter's SystemMode enum values, e.g. `Cool=3, Heat=4, FanOnly=7, Dry=8, Auto=1`, do not numerically match the protocol's `1=Heat,2=Dry,3=Cool,7=Fan,8=Auto` -- easy to get backwards, write the table explicitly in code, don't assume they line up) |
| Setpoint | `OccupiedHeatingSetpoint`/`OccupiedCoolingSetpoint` -- exist | `setpoint` | `state[7]`, `Temp = 31 - state[7]` -- captured | Matter side done |
| Fan speed | **missing**, being added (Fan Control cluster, above) | `fan_speed` | `state[8]` + Type-2 Quiet flag -- captured, all 5 values (incl. Quiet/Low disambiguation) | Matter side is the current gap, tracked above |
| Power on/off | **not confirmed to exist as a distinct concept at all** -- see below | -- | **not captured** -- every Milestone 1 capture had the unit already on | Real gap, needs a targeted capture (see Open items) before this can be wired either side |
| Swing (V/H) | none | none | not captured (stayed at Auto/Off defaults throughout) | Deliberately unimplemented, unchanged from original plan -- revisit only if a real need shows up |
| **Real ambient temperature (BME280) driving the AC** -- the actual point of this project | `LocalTemperature` on Device B -- exists, unpopulated | `followme_sensor_temp` | `state[11]` (enable/heartbeat frames) -- captured, encoding confirmed | **Not yet wired at all.** Needs: (1) the Matter binding from Milestone 3's binding section, (2) Milestone 4's heartbeat loop actually sending it. Treat as equal priority to the command-side work above, not a lower-priority follow-on -- see the Suggested order note. |

**Power on/off, specifically:** Matter's `SystemMode` attribute has an
`Off` value distinct from `Heat`/`Cool`/etc., so the *Matter side* of
"turn it off" already has somewhere to go -- what's unconfirmed is the
*IR side*: does this protocol have a dedicated power bit separate from
mode, or does "off" just not exist as a mode value and get handled some
other way (a specific `state[6]`/`state[3]` pattern never seen in captures
so far)? Don't guess -- capture a real power-off (and power-on-from-off)
button press before writing any code that assumes an answer either way.

### IR transmitter: don't port IRremoteESP8266 -- write a small RMT-based sender instead

Milestone 1's capture rig used Arduino + IRremoteESP8266's `IRsend` class.
**Device B is a plain ESP-IDF project (`idf_component_register`-based, no
Arduino framework)** -- IRremoteESP8266 is an Arduino library and doesn't
drop into this project as-is. Two options:

1. Pull in `arduino-esp32` as an ESP-IDF component just to get `IRsend`
   working -- heavyweight for what's needed, and this project deliberately
   avoided Arduino for Device B already (matching Device A/MiniSplit's plain
   ESP-IDF approach).
2. **Recommended:** write a minimal transmitter directly against ESP-IDF's
   `driver/rmt_tx` peripheral driver. The protocol is now fully characterized
   (see Milestone 1's wire encoding table above) -- this is maybe 60-80 lines
   of C: build an `rmt_symbol_word_t` array from the 14-byte shadow state
   (header + 112 bits, each bit a fixed mark + one of two space durations),
   configure the RMT channel's carrier for 38kHz, transmit once. No protocol
   guessing needed since every byte and timing value is already pinned down
   from real captures.

Either way, this is a new source file (e.g. `src/ir_tcl112.c` +
`src/ir_tcl112.h`), not something bolted onto `matter_device.cpp`.

### Fan Control cluster — missing, needs adding (not just an IR-wiring gap)

**Current state, confirmed by reading `src/matter_device.cpp`:** the
Thermostat endpoint exposes `SystemMode`, `OccupiedHeatingSetpoint`, and
`OccupiedCoolingSetpoint` as real Matter attributes, with write-callback
handling already in place for all three -- mode and setpoint just need the
IR transmitter + shadow-state wiring below to become fully functional.
**Fan speed has no Matter attribute at all right now** -- there is nowhere
for a controller to even send a fan-speed command to, independent of any IR
work. This is a real gap in the endpoint's cluster composition, not
something the shadow-state/transmitter work below will incidentally fix.

**Fix:** add the **Fan Control cluster (`0x0202`)** onto Device B's existing
Thermostat endpoint. Confirmed available in this exact esp-matter build --
`managed_components/espressif__esp_matter/components/esp_matter/data_model/generated/clusters/fan_control/`
(no pre-built "Room Air Conditioner" composed device type exists in this
esp-matter version, so this follows the same manual-cluster-attachment
pattern MiniSplit already uses for e.g. `PICoolingDemand`, not a different
endpoint type). Concretely:
- `FanMode` attribute (`0x0000`, enum 0-6 per Matter spec: Off/Low/Medium/
  High/On/Auto/Smart) maps reasonably onto the captured Auto/Quiet/Low/
  Medium/High states -- exact mapping needs deciding (Quiet has no obvious
  slot in the stock enum; likely folds into Low, distinguished internally by
  the shadow state's own `fan_speed` field carrying the finer-grained value
  the IR frame actually needs, same way state[8]/Type-2-Quiet-flag works in
  the real protocol).
- Needs its own write-callback case in `matter_attribute_callback()`,
  mirroring the existing `SystemMode` one, setting a new
  `fan_speed_command_pending` flag/getter pair (same shape as the existing
  `matter_get_system_mode_command_pending()` family in `matter_device.h`).
- Swing(V)/Swing(H): still not planned -- unexercised in capture, no Matter
  cluster attached for it either. Same "not required for the core goal"
  status as before, just noting it's now doubly true (no protocol data *and*
  no Matter attribute).

### State model — full frames, not deltas (unchanged from original plan, now with real byte layout)

- Shadow state struct fields map directly to the byte table above:
  `{power (folded into mode/checksum, no dedicated on/off bit found --
  confirm before assuming one exists), mode, setpoint, fan_speed,
  followme_enabled, followme_sensor_temp}`. (Swing wasn't exercised during
  capture -- Swing(V)/Swing(H) stayed at their defaults throughout; treat as
  unimplemented/always-default until a real need and a capture to back it.)
- A Matter attribute write updates one field in memory; every actual IR
  transmission serializes and sends the **entire** 14-byte frame, computing
  the checksum byte fresh each time -- never send a stale cached checksum.
- Same reasoning applies to Follow Me: turning it on/off means a full frame
  with the *current* mode/setpoint/fan alongside the bit flip, not a bare
  toggle.
- Persist the shadow state to NVS (reuse MiniSplit's `nvs_persist_u8`/
  `nvs_persist_i16` pattern verbatim) so a Device B reboot doesn't risk
  sending a stale/wrong frame before the next real command arrives.

### Command send heuristics (unchanged from original plan -- still the right call, nothing new learned that changes this)

- Debounce rapid writes (~300-500ms), coalesce to the final settled values.
- Skip sends within the unit's own 1-degree setpoint resolution of current
  state.
- Dedup against shadow state -- don't retransmit an echo of what's already
  set.
- Enforce a minimum inter-command spacing floor.
- A command send resets/delays the next Follow Me heartbeat tick by one
  interval (3 minutes, see Milestone 4) -- only one thing on the IR LED at a
  time, and an immediately-following heartbeat would be redundant anyway.
- Cross-checking Tuya status **before** sending, from Device B itself: not
  planned -- ruled out by the closed-loop decision (Device B has no Tuya
  client at all, by design). The related but distinct idea of checking Tuya's
  status **after** sending, as a round-trip verification that a command
  actually reached the unit, is still very much part of the plan -- see
  "Two live command paths" (Milestone 3, further down) for why MiniSplit's
  read-only Tuya mirror stays important precisely for this.

### `LocalTemperature` via Matter binding (the actual mechanism, now confirmed available)

Confirmed: `esp-matter`'s Binding cluster machinery is present in this
project's own dependency tree --
`managed_components/espressif__esp_matter/components/esp_matter/data_model/generated/clusters/binding/`
(and the underlying `BindingManager`/`BindingCluster` in the vendored
connectedhomeip source under
`managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/clusters/bindings/`).
This is the standard "thermostat + remote sensor" pattern (same mechanism
Ecobee/Nest remote sensors use) -- Device B's Thermostat endpoint needs a
Binding cluster client entry pointing at **node `@1:18`, endpoint 2** (the
existing MiniSplit bridge's aux BME280 Temperature Sensor endpoint -- see
Milestone 2), plus a subscription client that writes incoming reports into
`matter_update_local_temperature()` (already exists in
`src/matter_device.cpp`, just not called by anything real yet). Concrete
wiring details (which esp-matter helper functions actually set up the
subscription vs. just the binding-table entry) need a read through those
generated cluster files plus esp-matter's own examples before writing code --
not fully mapped out yet, flagged here rather than guessed at. One thing
worth double-checking early: whether MiniSplit's existing ACL
(`FabricAccessControl`, seen in its own matter-server logs) already grants
read/subscribe access broadly enough for a second node on the same fabric to
subscribe to this endpoint, or whether MiniSplit's firmware needs an ACL
update first.

### Two live command paths -- decided: closed loop, Tuya command path retired entirely

Once Device B works, Home Assistant would otherwise end up with **two
separate Matter Thermostat entities for the same physical AC** -- the
existing MiniSplit bridge's own (`@1:18` ep1, commands routed to Tuya) and
Device B's new one (`@1:27`, commands routed to IR). Left as-is, both stay
live and accept writes independently, with no coordination between them --
setting the temperature on the wrong one in HA silently does nothing to the
unit.

**Decided (final): closed loop.** `HA -> Device B (Matter) -> IR -> unit`
is the entire command path, full stop. No forwarding to Tuya at all -- not
a toggle, not a binding-driven mirror, nothing. Tuya's status GET keeps
running exactly as the original project goal always said: "Tuya's status
GETs can keep running in parallel -- they're read-only and harmless." It
becomes purely an independent verification/history signal, not part of the
command loop in any way.

**Concretely, this means both of MiniSplit's currently-writable,
forwarded-to-Tuya surfaces get permanently retired, not toggled:**
1. `SystemMode` on the main Thermostat endpoint (`@1:18` ep1) -- currently
   accepted unconditionally and forwarded to Tuya. Reject it unconditionally
   instead, the same way that endpoint's own setpoint attributes already are
   (`ESP_ERR_NOT_SUPPORTED`, see the `matter_attribute_callback()` comment
   starting "Rejecting write to read-only setpoint..." in
   [../MiniSplit/src/matter_device.cpp](../MiniSplit/src/matter_device.cpp))
   -- same mechanism, just extended to `SystemMode` too, no flag needed.
2. The separate **"Desired Setpoint" endpoint** (`@1:18` ep8, a second
   minimal Thermostat cluster purpose-built for HA to write a target
   temperature to, reconciled against Tuya by `sync_task`) -- its entire
   *reason for existing* was forwarding a setpoint to Tuya. With that
   forwarding gone, the endpoint has no remaining purpose. Remove it
   outright (endpoint, its `sync_task` reconciliation logic, and
   `command_task`'s Tuya-setpoint-sending path) rather than leaving a
   pointless read-only shell around.

No new NVS flag, no new switch entity, no manual on/off decision to
remember. MiniSplit's main Thermostat becomes a permanent, unconditional
read-only mirror of Tuya's polled status; Device B's Thermostat becomes the
sole place anything is actually commanded from.

**The read-only mirror is not just leftover cruft -- it's the round-trip
verification signal for the whole loop, and stays important.** All *writes*
go to Device B; the *confirmation that a command actually reached the
physical unit* still only exists on MiniSplit's side, via Tuya's
independently-polled status GET (what Tuya's cloud believes the unit's
actual state is, regardless of how it got there). Concretely: after Device
B sends an IR command, compare Device B's shadow state against MiniSplit's
Tuya-mirrored `LocalTemperature`/`SystemMode`/setpoint a poll cycle or two
later. If they still disagree well past the point the change should have
landed, that's a real signal something's wrong with the IR path itself (line
of sight blocked, LED failed, emitter knocked loose) -- not just a stale
read, since IR has no ack mechanism of its own to detect that on its own.
This turns the original plan's "optional: cross-check Tuya status before
sending" heuristic (see Command send heuristics, above) into something with
a clearer purpose: not just dedup, but the closest thing this system has to
knowing whether a command actually worked. Whether that comparison becomes
an HA automation, a periodic check in Device B's own firmware, or is left
as a manual "does the read-only entity match what I expect" spot-check is
an implementation choice for later, not decided here -- but the underlying
mechanism (MiniSplit's read-only mirror staying accurate and live) is worth
being deliberate about keeping, not something to view as no-longer-needed
once writes move to Device B.

**Enhancement worth building, not just the passive version above:**
MiniSplit's existing `sync_task` GET poll runs on its own fixed ~5-minute
timer (`STATUS_POLL_INTERVAL_MS`, unrelated to any IR command) -- left
purely passive, verification could lag a real command by up to that whole
interval. Better: have MiniSplit **subscribe to Device B's Thermostat
attributes** (`SystemMode`/setpoints) via a *second* Matter binding --
opposite direction from Device B's own binding to MiniSplit's BME280 above,
so the two devices end up bound to each other both ways, each acting as
both a Matter client and server for different clusters. When MiniSplit's
subscription client receives a changed-attribute report from Device B, it
triggers an **on-demand Tuya GET** immediately (in addition to, not instead
of, the existing periodic poll), refreshing the read-only mirror as soon as
the physical unit's own Tuya module has had a chance to report its new
state -- typically much sooner than the next scheduled 5-minute poll.
**Needs its own debounce/minimum-spacing guard** (same spirit as Device B's
own command debounce) so a rapid run of changes on Device B (a HA slider
drag, several quick taps) doesn't trigger a burst of extra Tuya API calls --
MiniSplit's own code already flags Tuya quota usage as something to be
deliberate about (see `STATUS_POLL_INTERVAL_MS`'s own comment in
`main.c`), so this needs to stay quota-conscious, not fire on every single
attribute-changed event unconditionally.

This is cross-project work -- implementing the write-path retirement means
editing `../MiniSplit/src/matter_device.cpp` and `main.c` (removing the
Desired Setpoint endpoint and its `sync_task`/`command_task`
Tuya-command-sending logic, and making `SystemMode` unconditionally
read-only); the triggered-GET enhancement additionally touches
`sync_task`/`tuya_client.c` (a new on-demand GET path, debounced) and needs
MiniSplit to gain its own Matter *client* subscription code (mirroring what
Device B needs to build for the opposite binding, in Milestone 3's binding
section above) -- neither is anything in this project's own `src/`.

---

## Milestone 4 — Follow-me heartbeat logic

Unchanged from the original plan's design, now with real numbers:

- Background timer task, interval = **3 minutes**, matching the real
  remote's measured cadence directly (not derived from the unmeasured
  10-minute fallback-timeout placeholder).
- Each tick: use last-known-good value from the Device A subscription (no
  fresh poll), format into the full-frame Follow Me heartbeat encoding
  (Milestone 1: same as the enable frame, `state[5]` bit `0x20` cleared),
  send via the Milestone 3 transmitter.
- If Device A's subscription goes stale (no updates for ~2x its normal
  report interval), stop the heartbeat -- let the unit's own timeout fall
  back to its internal sensor, same as the real remote losing power.
- One-time Follow Me enable frame sent when SystemMode moves to an active
  conditioning mode, through the same shadow-state full-frame path as any
  other command.
- Shares the IR send queue/spacing rules with Milestone 3's command
  heuristics -- a heartbeat is a lower-priority frame competing for the same
  LED.

---

## Open items

1. ~~Protocol identity~~ -- resolved, Milestone 1.
2. ~~Device A identity~~ -- resolved, it's the existing MiniSplit bridge
   (node `@1:18`, endpoint 2), not a separate device.
3. Follow Me fallback timeout -- still unmeasured, 10-minute placeholder.
4. **Power on/off encoding -- not captured at all.** Every Milestone 1
   capture had the unit already on, so whether there's a dedicated power bit
   distinct from mode is unknown. Needs a targeted capture (power off, then
   on from off) before either the Matter or IR side can be wired for it --
   see "Control surface coverage" in Milestone 3.
5. Optional: Device B also accepting plain IR-remote input (via a spare
   HX-M121) to keep a physical remote press in sync with Matter state -- not
   required for the core goal, unchanged from original plan.
6. The exact per-frame checksum formula (`state[13]`) hasn't been
   reverse-engineered from the captures -- reuse IRremoteESP8266's
   `IRTcl112Ac` checksum logic as reference (same algorithm, just needs
   porting to the RMT-based sender) rather than re-deriving it from scratch.
7. Whether MiniSplit's ACL already permits a second fabric node to subscribe
   to its aux BME280 endpoint, or needs a firmware-side grant first (see
   Milestone 3's binding section).
8. ~~Two live command paths~~ -- decided, see Milestone 3's "Two live command
   paths" section (closed loop, Tuya command path retired entirely on
   MiniSplit -- cross-project work not yet implemented).
9. Triggered-GET verification enhancement (Milestone 3's "Two live command
   paths" section) -- MiniSplit binding to Device B and firing an on-demand
   Tuya GET on change, debounced, *alongside* (not replacing) the existing
   5-minute poll. Decided in principle, not yet implemented -- lower
   priority than the core command/binding work, a refinement on top of the
   passive verification that already works once the binding above exists.

## Suggested order for Claude Code

1. ~~Milestone 1~~ ✅ done.
2. ~~Device B skeleton + commissioning~~ ✅ done. Verify SystemMode/setpoint
   *writes* actually land (only reads confirmed so far).
3. Add the Fan Control cluster (`0x0202`) to Device B's Thermostat endpoint
   + its write-callback case -- see Milestone 3's "Fan Control cluster"
   section. Do this before or alongside step 4, not after -- otherwise the
   shadow-state/transmitter wiring in step 4 has no fan input to wire up.
4. Write the RMT-based IR transmitter (`src/ir_tcl112.c`) against the
   Milestone 1 wire-encoding table -- standalone, testable by pointing it at
   the real unit before wiring in Matter at all.
5. Wire the shadow-state struct + command heuristics + transmitter into
   `matter_device.cpp`'s attribute callback (mode, setpoint, and now fan
   speed).
6. **Bind Device B to the existing MiniSplit bridge's aux BME280 endpoint**
   (Milestone 3's binding section, node `@1:18` ep2); verify
   `LocalTemperature` updates flow. Treat this as equal priority to steps
   3-5, not a follow-on nice-to-have -- the whole point of this project is
   controlling the AC using *real* room temperature instead of the unit's
   own poorly-placed sensor, and command pass-through alone doesn't deliver
   that. See "Control surface coverage" (Milestone 3, above) for the full
   picture.
7. Follow-me heartbeat loop (Milestone 4) -- this is what actually *uses*
   step 6's binding to keep the AC's internal Follow-Me state fresh; without
   it, step 6 alone only gets the temperature reading into HA, not into the
   AC's own follow-me behavior.
8. Retire the Tuya command path on `../MiniSplit/` (its own "Two live
   command paths" task above -- unconditionally reject `SystemMode` writes,
   remove the Desired Setpoint endpoint and its `sync_task`/`command_task`
   Tuya-forwarding logic). Do this **last**, only once steps 3-5
   (mode/setpoint/fan over IR) are verified working end-to-end against the
   real unit -- not before, so there's a working fallback during Device B's
   bring-up.
9. Optional refinement, after step 8: the triggered-GET verification
   enhancement -- MiniSplit binds to Device B's Thermostat and fires a
   debounced on-demand Tuya GET on change, on top of (not replacing) the
   existing 5-minute poll. Not required for the core loop to work -- the
   passive version (wait for the next scheduled poll) is already correct,
   this just tightens the verification lag.
