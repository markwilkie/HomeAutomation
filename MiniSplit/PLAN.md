# Local IR Control + Real-Sensor Follow-Me — Implementation Plan

**Supersedes [../MiniSplitIR/PLAN.md](../MiniSplitIR/PLAN.md) as of 2026-09-04.**
That project was originally scoped as a second physical device ("Device B")
so Home Assistant would talk to the AC over local IR/Matter instead of the
unreliable Tuya cloud command path, using a Matter Binding to pull real
ambient temperature from this project's existing BME280 endpoint. Decision,
made after Device B's Matter/Thread skeleton was already commissioned but
before any IR logic was written: **fold that work into this project instead
of running it as a second commissioned node.** Reasoning:

- Two devices meant two Matter Thermostat entities in Home Assistant for one
  physical AC, requiring a cross-device "closed loop" scheme (one endpoint
  demoted to read-only, a Matter Binding each direction) to avoid conflicting
  writes. One device avoids that problem outright instead of managing it.
- The round-trip verification idea that scheme was built around — comparing
  an IR command's shadow state against Tuya's independently-polled cloud
  status, since IR has no ACK of its own — works identically inside one
  firmware. Nothing is lost by merging.
- MiniSplitIR's Follow-Me temperature source was also being reconsidered
  (SONOFF SNZB-02P via Zigbee2MQTT instead of a wired BME280 or a Matter
  binding) — see Milestone 4 below. That reconsideration only made sense
  once IR control was moving into this codebase anyway, so both changes
  landed together.
- No new physical board is required at all now — this is 100% firmware work
  on the existing, already-commissioned MiniSplit ESP32-C6.

MiniSplitIR's hard-won protocol reverse-engineering isn't lost — the byte
map and derived facts live in [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)
now. The MiniSplitIR project directory itself stays in place for the moment
(its raw capture session log is still the source of truth if a byte value
ever needs re-verifying) and gets removed once the milestones below are
actually done — not before.

## Goal

Bypass the unreliable Tuya cloud command path for the Pioneer WT012GLUI25FVQ
mini split by controlling it locally over IR, sent directly from this
project's existing ESP32-C6, and replicate the unit's "Follow Me" behavior
using a real ambient-temperature reading instead of the unit's own (poorly
placed / Tuya-mediated) sensor. Tuya's status GET keeps running in parallel,
read-only — it becomes this project's only way to verify an IR command
actually reached the unit, since IR itself has no acknowledgment.

## Architecture (after this plan)

```
Home Assistant
   |  (Matter, existing commissioned node @1:18)
   v
MiniSplit ESP32-C6
   |-- Thermostat cluster (Power/Mode/Setpoint only) -> IR transmitter (RMT, GPIO) -> AC unit
   |-- Tuya cloud client -> read-only status mirror (pre-send refresh source + post-send verification, no writes)
   |-- BME280 (existing, I2C) -> unrelated room-temp endpoint, untouched -- see SENSORS.md
   `-- MQTT client (new) -> Zigbee2MQTT/Mosquitto on wyse -> SNZB-02P reading -> Follow-Me state[11]
```

Nothing about the existing BME280 endpoint or the HA template sensors that
already consume it (`sensor.mini_split_ac_bridge_temperature_2` and its
"Smoothed"/"Corrected" derivatives) changes. Follow-Me gets its ambient
reading from a different source (the Zigbee sensor, over MQTT) because
that's a better sensor for that specific job — see Milestone 4 — not
because BME280 is being retired.

## Milestone 1 — IR transmitter

Port the RMT-based sender scoped in MiniSplitIR's plan (never implemented
there): build an `rmt_symbol_word_t` array from the 14-byte shadow state per
[IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)'s wire encoding, 38kHz
carrier, transmit the full frame fresh (recomputed checksum) on every send.
New source files (`src/ir_tcl112.c` + `.h`), not bolted onto
`matter_device.cpp`.

**RMT sender — build- and flash-verified 2026-09-07, no longer an open
item.** `test_apps/ir_loopback` (a standalone RMT-only project, see its
README.md) sends a known frame out an IR LED and decodes it back on an IR
receiver wired to the same ESP32-C6, both build-verifying `ir_tcl112.c` for
the first time (it had never seen a working toolchain before) and
hardware-verifying the actual waveform. Caught one real bug this way: a
missing footer mark/gap after the 112th data bit meant the checksum's top
bit decoded as truncated garbage while all 111 bits before it were already
correct — fixed by adding the footer IRremoteESP8266's own
`sendTcl112Ac()` sends (see [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)'s
"Wire encoding" section). After the fix, 5/5 consecutive loopback sends
decoded back byte-for-byte identical.

**Confirmed against the real AC unit — also 2026-09-07, no longer an open
item.** `test_apps/ir_live_test` (IR LED aimed at the unit itself, not a
loopback receiver) found the unit doesn't respond to a lone Type 1 frame —
see Milestone 2's Type 2 note below — but once the Type 2 companion frame
was added, the unit responded correctly to every command in the test list.
This is also confirmed wired into the real production path: `src/main.c`'s
`send_ir_frame()` sends both frames via `transmit_ir_state_frame()`, not
just Type 1 — an earlier draft of this plan said otherwise; that was stale,
not a fix that was still pending. **Still genuinely open:** whether the IR
emitter used for that test reflects the final physical mount or just a
bench setup, and the wire-run distance from the enclosure to the unit's
receiver window (see Hardware note below) — a physical-world fact, not
something resolvable from the firmware.

**Checksum — resolved 2026-09-04, no longer an open item.** Pulled the real
algorithm from IRremoteESP8266's own source (`IRTcl112Ac::calcChecksum()` /
`sumBytes()`) rather than re-deriving it: `sum(state[0..12]) + (0x0F if
state[3]==0x02 else 0x00)`, mod 256. Independently verified against five of
this project's own already-captured frames (three Type 1, one Type 2, both
Follow-Me frames) — all five recompute to the frame's actual trailing byte
exactly. See [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md) for the
full derivation.

**Hardware:** adhesive IR-blaster-eye cable (bare LED, driven through a GPIO
via a transistor stage) stuck directly on the AC unit's receiver window
(co-located with the front-panel display, small smoked/tinted window),
wired back to wherever this board's enclosure lives. Confirm the run
distance from the existing enclosure to that window before assuming this is
a trivial cable length — the adhesive-eye approach means only the tiny
emitter needs line-of-sight, not the whole enclosure, but the wire still has
to physically reach.

## Milestone 2 — Wire Thermostat writes to IR instead of Tuya

**Two different questions, not one: what HA can control, vs. what this
device must not silently clobber.** HA-controllable surface stays
deliberately narrow — Power (on/off), Mode, and Setpoint only. No Fan
Control cluster, no Mode Select cluster, nothing added for Fan/Swing/Econo/
Health/Turbo/Light/Timers as far as HA is concerned. But because every IR
send carries the *entire* 14-byte frame, every field the AC unit tracks —
including ones HA can't touch — has to get *some* value on every send, and
if this device doesn't know or preserve the real current value, it
overwrites it with whatever's baked into the code. That's a real bug, not a
scoping question, and it's handled separately below ("Preserve fields HA
doesn't control").

`matter_device.cpp` already has working write-callback handling for
`SystemMode` and the two setpoint attributes (currently forwarded to Tuya).
Repoint them at the new IR shadow-state + transmitter from Milestone 1:

- `SystemMode` → protocol mode nibble, via an explicit mapping table (Matter
  `SystemModeEnum` values do **not** numerically match the protocol's
  `1=Heat,2=Dry,3=Cool,7=Fan,8=Auto` — write the table out, don't assume
  alignment). **Done** — `map_matter_mode_to_ir()`/`map_matter_mode_to_tuya()`
  in `src/main.c`. `SystemMode`'s `Off` value needed a real IR encoding for
  the `Power` bit (every original capture had the unit already on, so power
  on/off had never actually been captured from this unit) — **confirmed
  2026-09-07** via a real before/after capture pair (`state[5]` bit `0x04`,
  see [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)) and wired in
  (`src/main.c`'s `build_ir_state_frame()`); no longer an open item.
- Setpoint → `state[7] = 31 - Temp`, honoring the existing Auto-mode no-op
  behavior confirmed in captures. **Done**, including a rounding fix
  (2026-09-07: plain truncation biased every setpoint down by up to ~1°C —
  see `build_ir_state_frame()`'s comment).
- Shadow state persists to NVS (reuse the existing `nvs_persist_u8`/
  `nvs_persist_i16` pattern already in this codebase) so a reboot doesn't
  risk sending a stale/wrong frame before the next real command. **Done**
  for Mode/Setpoint/OnOff via the existing `g_matter_state` NVS pattern
  (`matter_device.cpp`'s `nvs_persist_i16(NVS_KEY_DESIRED_SETPOINT, ...)`
  etc.) — no separate IR-specific shadow state ended up being needed, since
  the pre-send-refresh design below reads Fan/Light/Swing/Health live from
  Tuya on every send instead of caching them locally.
- **Coalesce to final settled values: effectively already true by design,
  not a separate mechanism.** `command_task`'s pending-command flags store
  the *latest* Matter-written value, not a queue -- so several rapid writes
  between polls just keep overwriting the same field, and only the final
  value is ever seen/sent when the 5s poll fires. A literal 300-500ms timer
  would be redundant on top of that: `COMMAND_POLL_INTERVAL_MS` (5s) already
  exceeds it 10x over.
- **Dedup against shadow state — done (2026-09-09).** Each of
  `command_task`'s three write-handling blocks (OnOff, Desired Setpoint,
  System Mode) now compares the desired value against the freshly-refreshed
  Tuya status and skips the IR send (and its ~90s post-send verify loop)
  entirely when they already match -- e.g. the physical remote or Tuya app
  already made the same change, or HA re-sent a value it already set.
- **Minimum inter-command spacing floor: not implemented as its own
  mechanism**, but `post_send_verify_and_sync()`'s ~90s blocking loop after
  every real send already imposes a large de facto floor between distinct
  IR transmissions (arguably a bigger gap than a dedicated spacing floor
  would have added) — not revisited further here.
- A command send delays the next Follow-Me heartbeat tick by one interval —
  **done** (`followme_task` re-derives its wait from `g_last_ir_send_tick`,
  which `transmit_ir_state_frame()` bumps on every successful send).

**Refresh the full frame from Tuya immediately before every send — don't
trust the cached shadow state for anything HA isn't actively changing.
Done** (`command_task`'s three write-handling blocks in `src/main.c` each
call `tuya_get_device_status()` immediately before building the outgoing
frame). Every IR transmission has to carry the entire 14-byte frame (the protocol
has no partial-update concept). If the shadow state is only ever updated by
our own past commands, any change made out-of-band — the physical remote,
or the Tuya app — gets silently reverted the next time HA triggers a send,
because we'd resend our stale cached values for every field except the one
HA just changed. Concretely:

1. On any HA-triggered write (`SystemMode` or setpoint), before building the
   outgoing frame, trigger an **immediate, on-demand Tuya status GET** (not
   just relying on the periodic 5-minute `sync_task` poll — the whole point
   is not sending a frame built from data that's already stale by up to 5
   minutes) and use its response to refresh every shadow-state field this
   project tracks.
2. Overlay only the specific field HA is actually asking to change onto that
   freshly-refreshed state.
3. Serialize and transmit the merged frame.

This means the on-demand Tuya GET this milestone needs is the same
mechanism Milestone 4 below uses for post-send verification — one shared
on-demand-GET helper, triggered from three places: immediately before a
send (this milestone), the short retry-poll loop right after a send
(Milestone 4), and the existing periodic `sync_task`. Same
quota-consciousness applies as `sync_task`'s existing comment on
`STATUS_POLL_INTERVAL_MS` already flags — this is deliberately spending
extra Tuya API calls at exactly the moment it matters (about to overwrite
real device state), not on every idle tick.

**Preserve fields HA doesn't control — Fan speed, Light, Swing(V), Swing(H),
Health.** These aren't exposed as HA-controllable Matter attributes (see
above), but the pre-send refresh still needs to read their real current
value and carry it forward into every constructed frame, or this device
becomes something that silently reverts them on every unrelated command —
e.g. turn the display off with the real remote, then have HA nudge the
setpoint, and the display comes back on because the frame-construction code
had nothing better to put in that bit. Two different states of readiness
here:

- **Fan speed — done (2026-09-09).** The IR encoding is fully captured
  (`state[8]`, see [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)) and
  Tuya exposes a matching DP (`fan_speed_enum`, enum 0-7:
  Stop/Mute/Low/Med-Low/Med/Med-High/High/Turbo — a **different, more
  granular enum than the protocol's own Auto/Quiet/Low/Medium/High**, so
  `map_tuya_fan_speed_to_ir()` in `src/main.c` collapses each Tuya step to
  the closest real IR level). `build_ir_state_frame()` reads
  `status->fan_speed` (parsed from Tuya's `fan_speed_enum`, note it comes
  back as a JSON string from `/shadow/properties` — same quirk `mode`
  already had to handle) and writes the mapped value into `state[8]` bits
  0-2 on every send. Quiet vs. Low still can't be distinguished at the IR
  level, since both map to the same `state[8]=2` and disambiguating them
  needs the Type 2 companion frame's Quiet bit, which this project still
  always sends as a fixed frame — not attempted here, out of scope for this
  pass.
- **Light: done (2026-09-07).** `build_ir_state_frame()` now reads Tuya's
  `light` DP from the pre-send-refreshed status and writes `state[5]` bit
  `0x40` (inverted polarity, per the reference doc) on every send. Landed
  after a real regression during HA testing: an unrelated Desired Setpoint
  change was silently turning the physical Light off, because the frame
  used to carry the base template's fixed value for that bit regardless of
  the unit's real current state.
- **Swing(V), Swing(H), and Health have sourced-but-unconfirmed bit
  positions** (IRremoteESP8266's `Tcl112Protocol` struct — `state[8]` bits
  `0x38` for Swing(V); `state[12]` bit `0x08` for Swing(H); `state[6]` bit
  `0x10` for Health — full detail in
  [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)). **Deliberately not
  being captured/confirmed or preserved (user call, 2026-09-07)** — not a
  blocker for this milestone. Same caveat as `Off` above if this gets
  revisited: confirm with a real capture rather than trust the library
  blind — it already got Fan's Quiet encoding wrong for this exact unit
  (see the reference doc's "Fan speed discrepancy" section).
- **Fresh Air is the one genuine unknown left** — no field in the library's
  model corresponds to it at all (unlike the other four above), so unlike
  them this really is a from-scratch discovery capture, not a
  confirmation. **Fresh Air has a real, confirmed button on the physical
  remote** — it wasn't even known to be missing from this plan until
  pointed out. Don't assume it lives in the same 112-bit Type 1/Type 2
  frame structure as everything else just because those did — the capture
  needs to confirm whether pressing it produces a variant of the known
  frame (most likely `state[4]` bits 0-6, the only byte left entirely
  unclaimed by both this project's own findings and the library's struct)
  or a structurally different code entirely, before assuming either way.
- Tuya DPs already exist for all five of these fields (`light`,
  `vertical_wind`, `horizontal_wind`, `health`, `fresh_air_valve`) — and
  `fresh_air_valve` specifically is already being read by this codebase's
  existing `tuya_get_device_status()` today, so once its IR bit is found
  that one's actually closer to Fan speed's "cheap" tier than to Light's.
  Bundle all five confirmation/discovery captures into the same session as
  the `SystemMode` `Off` capture already required above — same before/
  after diff method, just more button presses in one sitting.

**Timer: no capture needed — always force it off, and now with precise
bits instead of a guess.** IRremoteESP8266's source confirms Timer is real
(contrary to an earlier draft's reasonable-at-the-time skepticism, back
when all we had was the library's static `toString()` output) — it's
backed by actual bitfields: `OnTimerEnabled`/`OffTimerEnabled` (`state[5]`
bits `0x10`/`0x08`), `TimerIndicator` (`state[8]` bit `0x40`), and the
`OnTimer`/`OffTimer` minute values themselves (`state[9]`/`state[10]`,
6-bit fields). No capture needed regardless — this project still always
transmits those specific bits/bytes as zero on every send, which now means
"deliberately clearing five known bits" rather than "hoping observed
defaults happen to hold." Accepted tradeoff, unchanged: a timer set via the
real remote or the Tuya app gets cleared by this device's next command.
Lower-stakes than Light/Swing/Health/Fresh-Air flipping, which is why those
get real preservation and this doesn't.

**What's genuinely out of scope, and why:**
- **Econo and Turbo** stay unaddressed for now — no confirmed Tuya DP
  identified for either yet (unlike Light/Swing/Health), and their IR bits
  are equally unmapped. Revisit if they turn out to matter.
- **Correction: Fresh Air is not out of scope.** An earlier draft of this
  plan assumed Fresh Air wasn't part of the IR protocol at all, reasoning
  from the `IRTcl112Ac` library's decoded-field list not naming it. That
  reasoning was wrong — there's a real Fresh Air button on the physical
  remote, the library just doesn't parse whatever bit it uses. Moved into
  the "needs a new capture" list above alongside Light/Swing/Health.

## Milestone 3 — Follow-Me ambient temperature, relayed through HA (SNZB-02P)

Source: **SONOFF SNZB-02P**, paired to the existing Zigbee2MQTT instance on
`wyse` (`setup-zigbee2mqtt.sh`, port 8099), broker is the existing Mosquitto
instance also on `wyse`. Reporting config in Zigbee2MQTT: min interval 10s,
max interval 3600s, reportable change `10` (raw units, = 0.1°C) — validated
against independent real-world testing, comfortably fresher than the
3-minute Follow-Me heartbeat needs. The 0.1°C resolution/flutter question
that testing raised doesn't actually matter here: `state[11]` only carries
whole-degree values, so any sub-degree noise is rounded away before it ever
reaches an IR frame.

**Architecture change, 2026-09-07: relayed through HA, not a direct MQTT
client.** The original plan below (direct MQTT to Mosquitto) was actually
implemented and build/flash-verified, but the MQTT connection itself never
worked: this device is Thread-only (no WiFi, confirmed no `esp_wifi_*` init
anywhere in this codebase — the plan's "already runs WiFi concurrently"
assumption was simply wrong, never checked against the actual code before
being written). Three approaches were tried against Mosquitto's plain LAN
IP, a hand-built NAT64-embedded IPv6 literal of it, and a real hostname
(wyse's Tailscale MagicDNS name) — all failed. The literal-address attempts
failed because esp-tls's hostname resolver never sets `AI_NUMERICHOST`
before calling lwIP's `getaddrinfo()`, so lwIP always attempts a real DNS
lookup even for a syntactically valid IP literal (confirmed via live serial
log: `getaddrinfo() returns 202`/`EAI_FAIL`, regardless of `::`-compression
formatting) — a real limitation in this exact esp-mqtt/esp-tls/lwIP version
combination, not a config mistake. The real-hostname attempt failed because
OTBR's DNS64 proxy apparently uses its own upstream resolver that only
successfully resolves public internet names (matching the existing Tuya
client's `openapi.tuyaus.com`), not Tailscale's private MagicDNS zone, even
though OTBR runs in the host's own network namespace and that same host's
own DNS does resolve MagicDNS names directly.

**Decision (user call): route through Home Assistant instead**, which
already reads this exact sensor for its own existing FollowMe automations
(`automations.yaml`, `sensor.0x18690afffe5602f1_temperature`). A new
Matter endpoint (`g_followme_endpoint` in `matter_device.cpp`, same
"borrow a Thermostat cluster's writable setpoint as a generic HA-writable
numeric input" pattern as the Desired Setpoint endpoint) receives the
sensor's real reading via an HA automation calling `climate.set_temperature`
on it. This firmware has no MQTT client at all now — `followme_client.c/h`
and the `mqtt`/`esp_driver_uart`-adjacent build dependency were removed
after being fully implemented and proven non-functional on this transport.
**Done (2026-09-07):** the HA automation that relays
`sensor.0x18690afffe5602f1_temperature` → the new endpoint's climate
entity — `automations.yaml`'s "MiniSplit FollowMe temp relay to firmware"
(id `1788809000000`), triggered on the sensor's state change, calling
`climate.set_temperature` on the Follow-Me endpoint. Milestone 3 is
functionally complete: firmware side (`followme_task`, 3-minute heartbeat,
`src/main.c`) and HA side are both live.

**Original MQTT plan (superseded, kept for context):**
- New MQTT client using ESP-IDF's built-in `esp-mqtt` component (core IDF,
  no managed-component pull needed).
- Subscribe to the SNZB-02P's Zigbee2MQTT topic (`zigbee2mqtt/<friendly_name>`)
  and parse `temperature` out of the JSON payload with cJSON (already a
  project dependency).
- Feed the parsed value into the shadow state's `followme_sensor_temp`
  field, written into `state[11]` on both the enable frame and the
  3-minute heartbeat re-send (see [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)).

Still true regardless of transport:
- **Naming stability:** the sensor's Zigbee2MQTT friendly name is
  `minisplit_followme_temp` (confirmed 2026-09-07 directly against
  `wyse`'s zigbee2mqtt configuration.yaml/database.db) — HA's own entity_id
  (`sensor.0x18690afffe5602f1_temperature`) still shows the raw IEEE-address
  form since HA never picked up the later Z2M rename; use the friendly
  name, not HA's stale entity_id slug, if this ever needs re-deriving.
- Hardware note: only 1-2 SNZB-02P units are being trialed initially before
  any larger purchase — this milestone should work against whichever one is
  paired first; no need to wait for a larger fleet.

## Milestone 4 — Retire the Tuya command paths, keep the read-only mirror

**Status, 2026-09-09: mostly done, and not in the order this section
originally assumed.** The Tuya command paths (`tuya_send_device_command()`)
were disabled outright back in Milestone 2's rollout, not deferred to here —
see `src/tuya_client.c`'s "DISABLED 2026-09-04" comment. The post-send
retry-poll loop and item 1 below are both done; item 2 turned out to be
based on a stale premise and won't happen as originally written (see below).

Once Milestones 2-3 land, two currently-writable, Tuya-forwarding surfaces
in this codebase get permanently retired (not toggled — no new NVS flag, no
switch entity):

1. **Done (2026-09-09).** `SystemMode` on the main Thermostat endpoint
   (`@1:18` ep1) is now rejected (`ESP_ERR_NOT_SUPPORTED`, same mechanism as
   the setpoint attributes on that endpoint) — see
   `matter_device.cpp`'s "Rejecting SystemMode write on main thermostat
   endpoint" log line. This was genuinely an open decision, not just an
   implementation task: the Desired Setpoint endpoint independently started
   accepting `SystemMode` writes too (added 2026-09-07 so its own climate
   card's Heat option works), so there were two duplicate
   `SystemMode`-accepting surfaces feeding the same `command_task` path.
   Resolved by user confirmation: the Desired Setpoint entity
   (`thermostat_6`) is what's actually used for mode control in HA, not the
   main endpoint's card, so rejecting the main endpoint's write was safe.
2. **Corrected 2026-09-09, not done, and won't be as originally
   written.** This item's premise was stale: it assumed the standalone
   **"Desired Setpoint" endpoint** (`@1:18` ep8/`thermostat_6`)'s only
   reason for existing was forwarding a setpoint to Tuya via `sync_task`'s
   reconciliation, and that removing it was just cleanup once that
   forwarding was gone. That's no longer true — this endpoint's write
   callback is now the real trigger for `command_task`'s IR-sending Desired
   Setpoint path (Milestone 2), and it's the entity the user just confirmed
   they actually use for both setpoint *and* mode control (see item 1
   above). **Do not remove this endpoint** — it's a live production control
   surface, not a vestigial shell.

`sync_task`'s existing Tuya status poll (`STATUS_POLL_INTERVAL_MS`, 5
minutes, `src/main.c`) keeps running exactly as-is as the long-run fallback.
The read-only mirror it feeds now gets updated by two additional, faster
paths on top of that fixed cadence: the **pre-send refresh** from
Milestone 2, and a **post-send retry-poll loop** described here.

**Post-send retry-poll loop — done (2026-09-07), ahead of this milestone's
original 2-3-then-4 ordering.** `src/main.c`'s `post_send_verify_and_sync()`
implements exactly this: called after every IR send, it re-polls Tuya every
`POST_SEND_VERIFY_RETRY_DELAY_MS` (15s, chosen from real-world measurement —
see below) for up to `POST_SEND_VERIFY_MAX_ATTEMPTS` (6, ~90s total) until
the reported state matches what was sent, updating the read-only mirror as
soon as it does. Don't wait on the fixed 5-minute cadence for HA to show
current info, and don't GET once and assume the answer is final — there's a
real propagation chain between "IR frame transmitted"
and "Tuya's cloud status reflects it": the AC's own Tuya module has to
notice the new state and report it up before our GET can see it. A single
immediate GET after sending would very likely read stale (pre-command)
data and wrongly look like the command failed; a single GET after a fixed
guessed delay just trades one failure mode for "too early sometimes, too
slow always." Instead, right after transmitting an IR frame:

1. Start polling Tuya on a short, fixed interval — **not** `sync_task`'s
   5-minute cadence — separate from and in addition to it.
2. As soon as a poll shows the state matching what was just commanded,
   stop immediately and push that result into the read-only mirror
   endpoint right then. This is what actually gets HA showing current
   info quickly, typically within one or two short intervals rather than
   the fixed periodic cadence.
3. If it still hasn't matched after a capped number of attempts, stop
   retrying. That sustained disagreement is the real "something's wrong
   with the IR path" signal (line of sight blocked, LED failed, emitter
   knocked loose) — not just a stale read, since IR can't self-report
   failure on its own. Whether that failure gets surfaced as an HA
   automation, an in-firmware log/flag, or a manual spot-check is an
   implementation choice for later, not decided here. Either way, fall
   back to `sync_task`'s normal periodic poll from that point rather than
   retrying forever.

**The short interval and attempt cap were placeholders — now set from a
real measurement, not guessed.** 15s / 6 attempts (~90s total) is what
shipped in `post_send_verify_and_sync()`. Per its own comment, live HA
testing (2026-09-07) found a pre-send refresh or an immediate post-send GET
both still see the OLD value — only `sync_task`'s old 5-minute cadence
happened to be slow enough for the round-trip to finish. A fixed-interval
retry was enough for a bounded window this short; no exponential backoff
needed. Small, bounded burst of extra Tuya calls per command, not unbounded
polling — consistent with `sync_task`'s existing quota-consciousness.

## Open items (carried over, still unresolved)

**Still genuinely open, as of 2026-09-09 (verified against code, not just
this doc — see below):**
- **Swing(V), Swing(H), Health, Fresh Air** — deliberately not
  captured/preserved (user call, 2026-09-07 for the first three; Fresh Air
  was attempted and abandoned — see
  [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)'s "Known gaps"). Not
  a blocker for Milestone 2.
- Confirm physical wire-run distance from this board's enclosure to the AC
  unit's IR receiver window, and whether the emitter used in
  `test_apps/ir_live_test` reflects the final mount, before calling
  Milestone 1's hardware step done (Milestone 1).
- Econo, Turbo: genuinely out of scope, no confirmed Tuya DP identified for
  either — see Milestone 2's note. Revisit only if a real need shows up
  later.
- Once Milestones 1-4 are done and verified on real hardware, remove
  `../MiniSplitIR/` entirely (its own `PLAN.md` is already marked
  superseded, pointing back here).

**Resolved (kept here for history — this section had gone stale relative to
the code before a 2026-09-09 pass corrected it throughout this file):**
- Power on/off — **confirmed 2026-09-07**, see Milestone 1/2 and
  [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md). No longer blocks
  `SystemMode`'s `Off` case.
- Light — **done 2026-09-07**, see Milestone 2.
- Fan speed — **done 2026-09-09**, see Milestone 2.
- Dedup against shadow state — **done 2026-09-09**, see Milestone 2. (A
  literal debounce timer and a separate inter-command spacing floor were
  deliberately not added — see Milestone 2 for why both are redundant given
  the existing design.)
- Timer — needs no capture; always transmits fixed zero values for its
  now-known bit positions (sourced from the same library source, see
  Milestone 2's note and
  [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md)).
- Checksum formula — **resolved 2026-09-04**, see Milestone 1. The Type 2
  frame's `state[5]`/`state[6]` "step-counter" is also resolved (it's real
  `Quiet`/`Mode`/`Health`/`Turbo` bitfields, just not meaningful in a
  special frame) — see [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md).
- Type 2 frame — **confirmed required 2026-09-07** and **wired into the
  real production path**: `src/main.c`'s `send_ir_frame()` sends both
  frames via `transmit_ir_state_frame()`. An earlier draft of this section
  said the production path "still only sends Type 1" — that was wrong by
  the time it was written, not a fix still pending.
- Milestone 3's HA relay automation — **done 2026-09-07**, see Milestone 3.
- Milestone 4's post-send retry-poll loop — **done 2026-09-07**, see
  Milestone 4.
- Milestone 4's `SystemMode` rejection on the main endpoint — **done
  2026-09-09**, see Milestone 4. (Its Desired Setpoint endpoint removal was
  never done and won't be — that item's premise was stale, see Milestone 4.)
