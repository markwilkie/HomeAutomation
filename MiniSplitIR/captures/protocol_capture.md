# Milestone 1 — IR protocol capture log

Captured with [../capture_tools/IRrecvDumpV2/](../capture_tools/IRrecvDumpV2.ino)
(IRremoteESP8266 v2.9.0) on an ESP32-C6, HX-M121 receiver on GPIO2 (5V VCC,
resistor divider to 3.3V on DAT — see [../capture_tools/README.md](../capture_tools/README.md)).

**Protocol: `TCL112AC`** — decodes cleanly, full named-field state available via
the library's `IRTcl112Ac` class / `IRac` API. Pioneer's WYS-series unit is
apparently OEM'd from the TCL/Gree-family 112-bit protocol, as guessed in the
original plan.

Each button press on the real remote sends **two back-to-back 112-bit
frames**: a "Type 2" special/quiet frame and a "Type 1" full-state frame. The
Type 1 frame is the one carrying the human-readable state (Power/Mode/Temp/
Fan/Swing/etc).

## Power On (Cool, 21C, default fan/swing)

```
Protocol  : TCL112AC
Code      : 0x23CB260200402000C30000000149 (112 Bits)  -- Type 2 (quiet/special) frame
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49};

Protocol  : TCL112AC
Code      : 0x23CB26010024030A0000000084CA (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 21C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x84, 0xCA};
```

## Dry mode (one mode-button press from Cool)

```
Protocol  : TCL112AC
Code      : 0x23CB2601002402050200000080C2 (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 2 (Dry), Temp: 26C,
            Fan: 2 (Low), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x02, 0x05, 0x02, 0x00, 0x00, 0x00, 0x80, 0xC2};
```
(Type 2 special frame for this press: `state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x40, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x68}`)

## Fan mode

```
Protocol  : TCL112AC
Code      : 0x23CB2601002407050000000080C5 (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 7 (Fan), Temp: 26C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x07, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC5};
```

## Heat mode

```
Protocol  : TCL112AC
Code      : 0x23CB2601002401050000000080BF (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 1 (Heat), Temp: 26C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80, 0xBF};
```

## Auto mode

```
Protocol  : TCL112AC
Code      : 0x23CB2601002408050000000080C6 (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 8 (Auto), Temp: 26C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC6};
```

Mode field: `1 = Heat`, `2 = Dry`, `3 = Cool`, `7 = Fan`, `8 = Auto`.
**Mode-cycle order on the remote: Cool → Dry → Fan → Heat → Auto → (back to Cool).**
The Type 2 special frame is identical across all mode changes
(`{0x23,0xCB,0x26,0x02,0x00,0x40,0x20,0x00,0xC3,0x00,0x00,0x00,0x01,0x49}`) —
only power-on used a different Type 2 payload (`...0x40,0x40,0x00,0xC3,...`).
A noisy/corrupted capture around t=75-76s (4x `UNKNOWN`, unusable) was almost
certainly the Auto and Cool transitions during rapid double-pressing — harmless,
since both are now captured cleanly by pressing one at a time.

## Setpoint changes

**Auto mode does not accept setpoint changes** — pressing temp-up while in
Auto mode left `Temp: 26C` unchanged in both the before/after captures; only
an unrelated toggle byte (`state[12]`: `0x80`↔`0x84`, `0xC6`↔`0xCA`
checksum) flipped. Confirm this holds for Dry/Fan too if it matters later;
Cool/Heat clearly do support it (below).

In **Cool mode**, one temp-up press: `21C -> 22C`.
```
Protocol  : TCL112AC
Code      : 0x23CB2601002403090000000080C5 (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 22C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x09, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC5};
```
Temp byte (`state[7]`) is inverse-encoded: `0x0A = 21C`, `0x09 = 22C` (i.e.
`Temp = 31 - state[7]`, consistent with the earlier Dry/26C capture where
`state[7] = 0x05` → `31-5=26`). Matches the TCL112AC library's known encoding.

Second data point, one more temp-up press: `22C -> 23C`.
```
Protocol  : TCL112AC
Code      : 0x23CB2601002403080000000080C4 (112 Bits)  -- Type 1 (state) frame
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC4};
```
Confirms `Temp = 31 - state[7]` (`0x08 = 8 -> 31-8 = 23`).

## Fan speed changes

**Fan cycle order on the remote: Auto -> Quiet -> Low -> Med -> High -> (back to Auto).**
Quiet and Low share the same `Fan` field value (2) in the Type 1 frame --
they're distinguished only by the `Quiet` flag in the paired Type 2 frame.
A naive "did the Fan field change" check would miss the Auto->Quiet step.

First press, Cool/23C, `Auto -> Quiet`:
```
Protocol  : TCL112AC  (Type 1 / state frame)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 2 (Low), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x80, 0xC6};
```
Paired Type 2 frame -- `Quiet: On`:
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x60, 0x40, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x89};
```

Second press, `Quiet -> Low` (Type 1 identical to above -- Fan field doesn't
move, this step only lives in the Type 2 frame):
```
Protocol  : TCL112AC  (Type 1 / state frame -- same state[14] as the Quiet step)
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x80, 0xC6};
```
Paired Type 2 frame -- `Quiet: Off` (back to the power-on/mode-cycle Type 2
shape, `state[6]` back to `0x40`):
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x40, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x69};
```

Third press, `Low -> Medium`:
```
Protocol  : TCL112AC  (Type 1 / state frame)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 3 (Medium), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x03, 0x00, 0x00, 0x00, 0x80, 0xC7};
```
Paired Type 2 frame -- `Quiet: Off`, `state[6]` now `0x60` (was `0x40` at
Low/Auto, `0x20` at plain mode-cycle, `0x40` w/ different state[5]=0x60 at
Quiet -- this byte seems to move with fan-speed step, not just be a toggle):
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x60, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x89};
```

Fan field (`state[8]`) so far: `0 = Auto`, `2 = Quiet/Low`, `3 = Medium`. Still
need High to complete the enum.

Fourth press: Type 1 **unchanged** (`Fan: 3 (Medium)`, identical `state[14]`
to the third press). Only the Type 2 frame moved -- its `state[6]` counter
byte went `0x60 -> 0x80` (sequence so far: `0x40`@Auto/Low(quiet-off),
`0x60`@Quiet(quiet-on)/Medium(quiet-off) -- note 0x60 appears at two different
Type1 states, `0x80`@this step). Working theory: Type 2 carries a
finer-grained fan-speed counter than Type 1's 4-value named enum, and we may
be one press away from Type 1 finally flipping to High. Needs one more data
point.
```
-- Type 2 for this (4th) press:
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x80, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0xA9};
```

Fifth press, `Medium -> High`:
```
Protocol  : TCL112AC  (Type 1 / state frame)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 5 (High), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x05, 0x00, 0x00, 0x00, 0x80, 0xC9};
```
Sixth press (cycle wraps -- Type1 unchanged, still `Fan: 5 (High)`; Type2
counter kept incrementing `0xA0 -> 0xC0`, confirming High is the top of the
Type1 enum and the Type2 byte is just a raw step counter that keeps climbing
past it):
```
-- Type 2 state[14] for presses 5 and 6, for reference:
{0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0xA0, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0xC9}  -- press 5
{0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0xC0, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0xE9}  -- press 6
```

**Fan enum (state[8]) complete: `0=Auto, 2=Quiet/Low (Type2 Quiet flag
disambiguates), 3=Medium, 5=High`.**

## Follow Me on

Captured with unit in Cool/23C/Fan High (no other state changes). Compare to
the baseline High capture above.

```
Protocol  : TCL112AC  (Type 1 / state frame)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 5 (High), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x80, 0x24, 0x83, 0x08, 0x05, 0x00, 0x00, 0x16, 0x80, 0xDF};
```
vs. baseline (Follow Me off, otherwise identical state):
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x08, 0x05, 0x00, 0x00, 0x00, 0x80, 0xC9};
```

**Follow-me-on encoding:**
- `state[4]`: `0x00 -> 0x80` (bit 7 set)
- `state[6]`: `0x03 -> 0x83` (bit 7 set -- same bit position as state[4], on
  top of the existing Mode nibble which stayed `3=Cool` in the low bits)
- `state[11]`: `0x00 -> 0x16` -- **new, nonzero.** `0x16 = 22` decimal. Almost
  certainly the remote's own ambient-temperature-sensor reading in whole
  degrees C, embedded directly in the enable frame. This is the field
  Device B's heartbeat needs to keep updating with Device A's Matter
  temperature reading.
- Library's `IRTcl112Ac` decode doesn't surface a named "Follow Me" or
  "Sensor temp" field in `toString()` for this frame -- these two bits +
  state[11] are undocumented-by-the-library but clearly load-bearing. May be
  worth checking the library source (`ir_Tcl.cpp`/`.h`) for a
  `setSensorTemp`/similar method that already knows about `state[11]` before
  hand-rolling it.

## Follow Me heartbeat

Captured passively, no remote input, ~360s (6:00, almost exactly) after the
Follow Me enable frame (enable frame at t=520.384s; this heartbeat's Type 1
at t=880.703s -- delta 360.319s).

**Confirmed: the real heartbeat interval is actually 3 minutes, not 6.**
Checked by continuing to watch (no remote input) for a frame ~180s after the
t=880.7s heartbeat -- one landed right on schedule at t=1061.170s (delta
180.267s from the previous clean one, and the earlier t=700 candidate was
180.1s after the enable frame -- both fit the same 3-minute grid).

The 3-minute-offset frames initially looked like a second, undecodable frame
variant (`IRrecvDumpV2` reported them as `UNKNOWN`, 114 bits, with visibly
irregular per-bit timing). **They are not a different frame.** Manually
decoding the raw pulse trains by bucketing on total bit-period duration
(mark+space summed, ~818us -> 0 / ~1586us -> 1) instead of the library's
stricter per-edge matching recovers the *exact same* 14 bytes as the clean
6-minute heartbeat: `23 CB 26 01 80 04 83 08 05 00 00 16 80 BF`. So the AC
unit really does send one identical heartbeat frame every 3 minutes; a couple
of individual bit edges on alternating transmissions get distorted just
enough (plausibly multipath/reflection at ~4" receiver range splitting a
mark/space boundary while the total period stays correct) to trip
`IRrecvDumpV2`'s strict decoder, even though no data actually changed.
Verified with a throwaway decode script, cross-checked first against the
known-good frame (reproduced it exactly) before trusting the same method on
the two `UNKNOWN` captures.

```
Protocol  : TCL112AC  (Type 1 / state frame)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 23C,
            Fan: 5 (High), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x80, 0x04, 0x83, 0x08, 0x05, 0x00, 0x00, 0x16, 0x80, 0xBF};
```
vs. the Follow Me **enable** frame:
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x80, 0x24, 0x83, 0x08, 0x05, 0x00, 0x00, 0x16, 0x80, 0xDF};
```

**Heartbeat encoding: it's the exact same 112-bit full-state frame as the
enable frame, with `state[5]` bit `0x20` cleared** (`0x24 -> 0x04`) --
everything else identical, including `state[4]`/`state[6]` bit 7 (still set)
and `state[11] = 0x16` (sensor temp, unchanged since ambient presumably
hadn't moved a whole degree in 6 minutes). So: `state[5]` bit `0x20` = "this
is the enable/command instance" vs. cleared = "this is a periodic
heartbeat re-send" of the same otherwise-identical state. **No separate
short/temp-only frame exists for this protocol family** -- contrary to the
original plan's assumption, Device B's heartbeat sender can reuse the exact
same full-frame serializer as any other command, just flipping one bit and
updating `state[11]` to the latest sensor temp.

## TODO (rest of Milestone 1)
- [x] Power on / Cool default
- [x] Dry mode
- [x] Heat mode
- [x] Fan mode
- [x] Auto mode
- [x] Setpoint changes (Cool 21C -> 22C -> 23C, confirmed `Temp = 31 - state[7]`; Auto mode locks setpoint)
- [x] Fan speed: Auto -> Quiet -> Low -> Medium -> High (full enum captured)
- [x] Follow Me on frame -- encoding identified (state[4]/state[6] bit 7, state[11] = sensor temp)
- [x] Follow Me heartbeat frame -- captured clean, 360s (6 min) after enable;
      same full-state frame with one bit cleared vs. the enable frame
- [x] Follow-me fallback timeout -- **NOT measured, assumed.** Skipped the
      battery-pull/stopwatch test; using **10 minutes** as a placeholder
      (user's call) -- roughly 3x the real remote's confirmed 3-minute
      heartbeat cadence, a plausible safety margin but still a guess.
      **Unverified -- re-test with the real battery-pull method before
      relying on this for anything safety- or comfort-relevant.** Device B's
      own heartbeat interval (Milestone 4) doesn't actually need this number
      anyway: it should just match the real remote's observed 3-minute
      cadence directly rather than deriving an interval from a guessed
      timeout via the "half of timeout" heuristic.

## Power / Setpoint / Fresh Air capture session (2026-09-07)

Scope trimmed by user decision: Light, Swing(V), Swing(H), Health stay as
the sourced-but-unconfirmed library hypothesis in
[../../MiniSplit/IR_PROTOCOL_REFERENCE.md](../../MiniSplit/IR_PROTOCOL_REFERENCE.md)
with no capture planned. Only Power and Fresh Air are being confirmed here;
Setpoint is re-confirmed opportunistically since it's a one-press diff off
the same baseline (formula already known from the Setpoint changes section
above).

Method: one baseline reference frame, then one-field-at-a-time diffs against
it. Expect `state[12]` bit `0x04` (and the checksum) to change on every
single capture regardless -- that's the already-documented anti-repeat
toggle, not a new field.

- [x] Baseline reference frame -- plan changed mid-session (see below):
      ended up using Fan/21C/Fan-Auto rather than the originally-planned
      Cool/21C/Fan-Medium. No functional difference for what this session
      needed to isolate (Power, Fresh Air) since both are diffed as
      single-field changes off whatever baseline is currently in the unit.
- [x] Power off -- **CONFIRMED**. Baseline
      `{0x23,0xCB,0x26,0x01,0x00,0x24,0x07,0x0A,0x00,0x00,0x00,0x00,0x80,0xC6}`
      (Type 1, Fan mode, 21C, Fan Auto, Power on) vs. Power-off capture
      `{0x23,0xCB,0x26,0x01,0x00,0x20,0x07,0x0A,0x00,0x00,0x00,0x00,0x80,0xC6}`
      -- exactly one bit differs, `state[5]`: `0x24` -> `0x20` (`0x04` bit
      cleared), checksum verifies correctly on both. Confirms `Power =
      state[5] bit 0x04`, clear = off / set = on, exactly matching the
      library's sourced hypothesis. No longer "unconfirmed" --
      [../../MiniSplit/IR_PROTOCOL_REFERENCE.md](../../MiniSplit/IR_PROTOCOL_REFERENCE.md)
      updated.
- [x] Setpoint -- reconfirmed opportunistically while cycling through temps
      in Fan mode on the way to the above (26C -> 25C -> 24C -> 23C ->
      22C -> 21C, `state[7]` = `0x05` through `0x0A`), all consistent with
      the already-known `Temp = 31 - state[7]` formula. No new information,
      formula already had high confidence.
- [ ] Fresh Air -- **ABANDONED, still unresolved.** See "Fresh Air capture
      attempt" writeup below for what was tried and why it's genuinely
      inconclusive (not just "didn't get around to it").

### Fresh Air capture attempt -- abandoned, inconclusive

Several Fresh Air button presses (confirmed by the user to be a real,
dedicated button on the remote) produced a byte-for-byte identical Type 1
frame to the pre-press baseline (only the anti-repeat toggle/counter moved,
which happens on every press regardless) -- ruling out "receiver missed it"
as the sole explanation, since a miss would be inconsistent, not a clean
repeat every time.

Eventually, moving the remote much closer to the receiver got the
receiver's LED to visibly blink and produced two real captures that the
library's TCL112AC-specific decoder couldn't match (fell through to its
generic `Protocol: UNKNOWN` path):
- First frame manually decoded (using this project's own bit-space
  threshold, ~687us) to a **perfectly valid, checksum-correct Type 2
  frame** identical to the everyday one
  (`{0x23,0xCB,0x26,0x02,0x00,0x40,0x20,0x00,0xC3,0x00,0x00,0x00,0x00,0x48}`,
  toggle counter just happened to be at 0) -- no Fresh Air information in
  it, and the "UNKNOWN" classification looks like a library quirk (likely
  expects the Type2+Type1 pair together) rather than a real decode failure.
- Second frame (the Type 1 half, where Fresh Air should actually show up)
  arrived **corrupted partway through** -- only 111 of the expected 112
  bit-pairs recovered cleanly -- so nothing past that point can be trusted.

Net result: still don't know Fresh Air's wire encoding. Two live
possibilities, not distinguished by this session:
1. The remote's IR emitter/aim for this particular button is marginal
   (weaker signal, different LED, or user's hand position for this specific
   button differs from mode/temp presses) -- plausible given the receiver
   needed to be unusually close to register anything at all.
2. Fresh Air genuinely uses a different, less forgiving wire format than
   the everyday Type 1/Type 2 pair (explaining why the specific decoder
   rejects it even when signal is strong enough to mostly recover).

**Decision: not pursuing further right now** (user call, 2026-09-07). Fresh
Air stays out of scope for Milestone 2 -- see
[../../MiniSplit/IR_PROTOCOL_REFERENCE.md](../../MiniSplit/IR_PROTOCOL_REFERENCE.md)'s
"Known gaps" section and
[../../MiniSplit/PLAN.md](../../MiniSplit/PLAN.md).

### Base/template frame -- captured 2026-09-07

Purpose: Milestone 2 needs a real starting byte array to build outgoing
commands from, so that every field this project doesn't control or
understand yet (Light, Swing(V)/(H), Health, Turbo, Econo, Timers, and any
still-undiscovered bits) gets sent as the unit's own natural default rather
than a guessed/zeroed value. This is that array -- a single real capture
with the AC left in the user's own everyday settings, checksum-verified.

```
Protocol  : TCL112AC
Code      : 0x23CB26010064070B00000000840F (112 Bits)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 7 (Fan), Temp: 20C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: Off, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x64, 0x07, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x84, 0x0F};
```

When Milestone 2 constructs a command, start from this array and only
overwrite the bytes/fields this project actually controls (currently:
`state[5]` bit `0x04` Power, `state[6]` bits 0-3 Mode, `state[7]` Setpoint,
`state[8]` bits 0-2 Fan) -- recomputing the checksum
(`ir_tcl112_checksum()`) after. `state[12]`'s anti-repeat toggle bit doesn't
need special handling; leave it as captured here, since nothing in this
protocol appears to require it to actually alternate correctly (the AC
accepted plenty of repeated/non-alternating values during today's session's
back-to-back identical-state presses).

## Re-capture session (2026-09-09) -- 21C reference frame, Follow Me on/off, heartbeat re-verification

Captured with the same rig (ESP32-C6 + HX-M121, `IRrecvDumpV2`, IRremoteESP8266
v2.9.0 -- library version bumped since the 2026-09-07 session's v2.8.6, no
decode differences observed). Purpose: (1) get a real Cool/21C base-frame pair
to evaluate against the "does the current template clobber Fresh Air" concern,
(2) capture Follow Me **off** via a real button press (only **on** existed
before today), (3) re-verify the Follow Me heartbeat's frame count -- the
2026-09-07-era conclusion ("no separate frame, Type-1-only") turned out to be
wrong, corrected below.

### 21C reference pair -- and a correction to the Fresh Air hypothesis

AC set to Cool/21C, **Fresh Air confirmed on by the user at the moment of
capture** (asked directly, not assumed):

```
Type 2 (pre-frame):
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49};

Type 1 (state frame):
Protocol  : TCL112AC
Code      : 0x23CB26010024030A0000000080C6 (112 Bits)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 21C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC6};
```

**`state[4] = 0x00` with Fresh Air confirmed ON.** This is byte-for-byte
identical (`state[12]`/checksum toggle aside) to the original Milestone-1
"Power On (Cool, 21C)" capture at the top of this file, where Fresh Air's
state was never confirmed either way. Since the same `state[4]` value shows
up regardless of Fresh Air's real on/off state, **`state[4]` is not Fresh
Air's bit** -- this disproves `IR_PROTOCOL_REFERENCE.md`'s standing hypothesis
("state[4] bits 0-6 unaccounted for, Fresh Air's most likely home"). Combined
with the 2026-09-07 "Fresh Air capture attempt" (button presses produced
byte-identical Type 1 frames), this now looks less like "the receiver missed
a changed bit" and more like: **Fresh Air genuinely isn't carried in the
everyday Type 1/Type 2 pair at all.** If the AC really does drop out of Fresh
Air mode whenever it receives a new command (the behavior that prompted this
whole re-capture), that's most likely the unit's own internal firmware
reacting to *receiving any new command frame*, not something encoded as a
bit this project's template can preserve or clobber. Swapping this cleaner
Cool/21C pair in as the new Milestone 2 base/template frame (replacing the
Fan/20C one above) is still worth doing for other reasons -- it's a more
representative everyday state -- but **it does not fix a Fresh-Air-dropping
problem**, since there's no evidence such a bit exists to preserve. Fresh Air
stays an open gap; if it matters enough to chase further, the next step
would be the close-range/high-signal capture method from the abandoned
2026-09-07 attempt, not a template swap.

### Follow Me off -- captured for the first time

Prior sessions only had Follow Me **on**. AC was Cool/21C/Fan-Auto with
Follow Me already enabled (ambient sensor reading 23C); pressed the "I feel"
button once to disable it:

```
Type 2 (pre-frame -- identical to the everyday one above):
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49};

Type 1 (state frame):
Protocol  : TCL112AC
Code      : 0x23CB26010024030A0000000080C6 (112 Bits)
Mesg Desc.: Model: 1 (TAC09CHSD), Type: 1, Power: On, Mode: 3 (Cool), Temp: 21C,
            Fan: 0 (Auto), Swing(V): 0 (Auto), Swing(H): Off, Econo: Off,
            Health: Off, Turbo: Off, Light: On, On Timer: Off, Off Timer: Off
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC6};
```

**Byte-for-byte identical to this session's 21C reference Type 1 frame above**
(same `state[12]`/checksum too, `0x80C6` both times -- coincidence of the
toggle landing on the same value, not a decode error, confirmed by comparing
raw capture timestamps). Confirms Follow Me off is exactly "the everyday
full-state frame with `state[4]`/`state[6]` bit `0x80` cleared and
`state[11]` zeroed," matching the encoding already documented for the
enable/heartbeat frames, just in reverse -- no separate "disable" frame
shape, same full-frame-every-time model as everything else in this protocol.

### Follow Me heartbeat -- frame-count correction

**The 2026-09-07-era conclusion that the heartbeat is a lone Type 1 frame
with "no separate short/temp-only frame" was wrong.** Re-verified today
across three consecutive heartbeats after a fresh Follow Me enable
(23:17:59 enable, ambient 23C):

```
Enable (23:17:59):
Type 2: {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49}
Type 1: {0x23, 0xCB, 0x26, 0x01, 0x80, 0x24, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x17, 0x80, 0xDD}

Heartbeat 1 (23:20:59, +3:00): Type 2 decoded via IRrecvDumpV2 as `Protocol: UNKNOWN`
(114 bits) -- bucket-decoded manually (same method as the 2026-07-xx session:
classify each bit on total mark+space period, >1200us -> 1 else -> 0) to:
Type 2: {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49}  -- everyday pre-frame, exact match
Type 1 (decoded cleanly by the library this time): {0x23, 0xCB, 0x26, 0x01, 0x80, 0x04, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x17, 0x80, 0xBD}

Heartbeat 2 (23:24:00, +3:01): roles reversed -- Type 2 decoded cleanly by
the library this time, Type 1 came through as `Protocol: UNKNOWN` (114 bits)
and needed the same manual bucket-decode:
Type 2 (clean): {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49}
Type 1 (bucket-decoded from UNKNOWN): {0x23, 0xCB, 0x26, 0x01, 0x80, 0x04, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x17, 0x80, 0xBD}  -- exact match to heartbeat 1's Type 1

Heartbeat 3 (23:27:00, +3:00): both frames decoded cleanly by the library,
no manual decoding needed at all:
Type 2: {0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x01, 0x49}
Type 1: {0x23, 0xCB, 0x26, 0x01, 0x80, 0x04, 0x83, 0x0A, 0x00, 0x00, 0x00, 0x18, 0x80, 0xBE}  -- state[11] now 0x18=24C, ambient ticked up one degree
```

**Corrected conclusion: the heartbeat is a normal Type 2 + Type 1 pair, same
shape as every other command in this protocol, not a lone Type 1 frame.**
What the 2026-07-xx session actually saw was one half of this same pair
failing to decode (reported as `UNKNOWN`, 114 bits) while assuming the
*other, already-visible* Type 1 capture was the missing half re-transmitted
-- an easy mistake since both frame types produce 112-114 "bits" of raw
timing data at a glance, and that session didn't have a second, independently
clean capture of the *other* frame type to compare against. Today's session
had the luxury of three consecutive cycles, and the pattern (which half fails
to decode varies capture-to-capture, but bucket-decoding the failure always
recovers the everyday Type 2 payload, never a second copy of Type 1) makes
the real shape unambiguous. **Heartbeat interval reconfirmed at a clean 3:00**
across all three gaps (23:20:59, 23:24:00, 23:27:00 -- one gap ran 3:01 due to
serial-log timestamp rounding, not a real cadence change).

`IR_PROTOCOL_REFERENCE.md`'s Follow Me section and PLAN.md's Milestone
1/3/4 heartbeat notes need updating to match -- Device B's heartbeat sender
must send the Type 2 pre-frame before every heartbeat Type 1, exactly like
`transmit_ir_state_frame()` already does for ordinary commands (confirmed
already true as of the 2026-09-09 correction in `IR_PROTOCOL_REFERENCE.md`'s
Type 2 section -- `send_followme_frame()` shares that same function, so this
was likely already correct in practice; this session's finding just corrects
the *documentation*, not a firmware gap).
