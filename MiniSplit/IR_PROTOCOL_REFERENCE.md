# IR Protocol Reference — TCL112AC (Pioneer WT012GLUI25FVQ)

Migrated from [../MiniSplitIR/PLAN.md](../MiniSplitIR/PLAN.md) and
[../MiniSplitIR/captures/protocol_capture.md](../MiniSplitIR/captures/protocol_capture.md)
as part of folding IR control into this project (see [PLAN.md](PLAN.md)).
This file holds the derived/authoritative facts; the full raw capture
session log (every button-press example, byte-for-byte) stays in
`../MiniSplitIR/captures/protocol_capture.md` until that project is
actually removed — read it there if a byte value here needs re-deriving
or double-checking against a real capture.

**2026-09-04 update:** cross-checked everything below against the actual
IRremoteESP8266 library source (`ir_Tcl.h`/`ir_Tcl.cpp`, the library that
decoded our own captures in the first place) rather than just its
`toString()` output. That source has a complete bitfield struct
(`Tcl112Protocol`) for the whole 14-byte frame and the real checksum
algorithm — both included below, with the checksum independently verified
against five of our own already-captured frames (all matched exactly, see
Milestone 1 in [PLAN.md](PLAN.md)). Treat the newly-added bit positions
(Power, Light, Swing(V), Swing(H), Health, Econo, Turbo, Timers) as a
strong, sourced hypothesis, not yet independently confirmed against a real
capture of *this* unit — one already-found discrepancy (Fan's Quiet
encoding, below) proves the generic library isn't a perfect match for this
exact OEM variant, so the planned capture session still matters, just as
confirmation rather than blind discovery now.

## Protocol identity

**`TCL112AC`** (Gree/TCL-family, 112-bit). Pioneer's WT012GLUI25FVQ unit is
OEM'd from this family — the library itself lists compatible OEM
brands/models including TCL, Leberg, Teknopoint, Daewoo, and Electrolux.
Decodes cleanly with IRremoteESP8266's `IRTcl112Ac` class.

Every real-remote button press sends **two back-to-back 112-bit frames**:
a "Type 2" special/quiet frame, then a "Type 1" full-state frame. Type 1
carries the human-readable state; Type 2 carries a handful of not-fully-
decoded extras (see below).

## Wire encoding (for the RMT transmitter)

Canonical values, from the library's own constants (matches our own
captures' measured ranges, e.g. header mark ~3100us / space ~1560-1600us
observed vs. these nominal values — safe to transmit at nominal, this is
the sender side, not a receiver needing tolerance bands):

- Header: mark **3000us**, space **1650us**
- Bit: mark **500us** (constant), then space **325us** = `0`, or **1050us**
  = `1`
- 112 bits, LSB-first per byte, 14 bytes total, transmitted MSB-of-array-first
  (`state[0]` first)
- 38kHz carrier during marks
- No separate short/temp-only frame exists for this protocol family — Follow
  Me's heartbeat reuses the full 112-bit frame (see below), not a short one
- **Footer, after the 112th data bit:** one more mark of the same **500us**
  bit-mark duration, then a gap (**100000us** / 100ms — IRremoteESP8266's
  own source calls this "just a guess," `kDefaultMessageGap`, not a real
  measurement). Missing from an earlier draft of this project's TX
  implementation entirely — found via `test_apps/ir_loopback`'s receive
  side (2026-09-07): without it, a real receiver has no closing edge to
  bound bit 111's (the last data bit's) space against, so that one bit
  decodes as truncated garbage regardless of its real value, while all 111
  bits before it decode perfectly. Confirmed against `IRTcl112Ac`'s
  `sendTcl112Ac()` in IRremoteESP8266's `src/ir_Tcl.cpp`, which passes
  `footermark=kTcl112AcBitMark` (the existing bit-mark constant, not a
  separate one) and `gap=kTcl112AcGap=kDefaultMessageGap` to
  `sendGeneric()`.

## Checksum — resolved and verified

```
is_special = (state[3] == 0x02)
checksum   = (sum(state[0..12]) + (is_special ? 0x0F : 0x00)) & 0xFF
```

From `IRTcl112Ac::calcChecksum()` or `sumBytes()` in IRremoteESP8266's
`IRutils.cpp` (plain byte sum over the first 13 bytes, `+0x0F` extra for
Type 2/special frames only). **Independently verified against five of our
own already-captured frames** (three Type 1, one Type 2, both Follow-Me
frames) — all five recompute to the frame's actual trailing byte exactly.
No longer an open item. Never send a stale/cached checksum — recompute on
every transmit, since Milestone 2's pre-send refresh means the other 13
bytes can legitimately change between sends.

## Base/template frame for Milestone 2

**Updated 2026-09-09** — `main.c`'s `kBaseFrame` now uses a fresher real
capture (Power: On, Mode: Cool, Temp: 21C, Fan: Auto, Light: On,
Swing/Econo/Health/Turbo/Timers off), replacing the original 2026-09-07
Fan/20C capture kept below for history. Full writeup in
`../MiniSplitIR/captures/protocol_capture.md`'s "Re-capture session
(2026-09-09)" section.

```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x84, 0xCA};
// Power: On, Mode: Cool, Temp: 21C, Fan: Auto, Swing(V): Off, Swing(H): Off,
// Econo: Off, Health: Off, Turbo: Off, Light: On, On/Off Timer: Off.
```

**Byte-for-byte equivalent to the old Fan/20C template for every field
`build_ir_state_frame()` doesn't overwrite** (Timers, Econo, Health, Turbo,
SwingV, the Follow-Me flag, isTcl/toggle) — verified by direct comparison.
Mode/Temp/Fan/Light differ between the two captures but are all
unconditionally overwritten by that function regardless of which template
they start from, so this swap does not change on-wire behavior — confirmed
via live testing against the real unit 2026-09-09 (see below).

Original 2026-09-07 capture, kept for history:
```
uint8_t state[14] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x64, 0x07, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x84, 0x0F};
// Power: On, Mode: Fan, Temp: 20C, Fan: Auto, Swing(V): Off, Swing(H): Off,
// Econo: Off, Health: Off, Turbo: Off, Light: Off, On/Off Timer: Off.
```

**Milestone 2 should build every outgoing command from this array**,
overwriting only the bytes/fields this project actually controls
(`state[5]` bit `0x04` Power, `state[6]` bits 0-3 Mode, `state[7]`
Setpoint, `state[8]` bits 0-2 Fan) and recomputing the checksum after. This
supersedes the earlier plan (below, under "Known gaps") of deliberately
zeroing unconfirmed fields like the timers — starting from a real captured
frame means every field this project doesn't understand yet (Light,
Swing, Health, Turbo, Econo, Timers, and any still-undiscovered bits) gets
sent as the unit's own natural default instead of a guess.

## State byte map (`uint8_t state[14]`)

Bit numbering below is LSB = bit 0. Fields marked **(sourced, unconfirmed)**
come from the library's `Tcl112Protocol` struct, not yet independently
verified by a capture of this exact unit — see the note at the top of this
file.

| Byte | Bits | Meaning |
|---|---|---|
| `state[0..2]` | — | Fixed header: `0x23 0xCB 0x26` |
| `state[3]` | 0-1 | `MsgType`: `0b01`=Type 1 (normal/full-state), `0b10`=Type 2 (special/quiet) |
| `state[4]` | 7 | Follow Me enabled (`0x80` set) / disabled (`0x00`). **Not in the library's model at all** — byte 4 is entirely unclaimed/padding there, meaning the real firmware repurposes it for a feature this library doesn't implement. Bits 0-6 unaccounted for and ruled out as Fresh Air's home (2026-09-09) — see `state[12]` bit 0 below for where Fresh Air actually lives (in the Type 2 frame only). |
| `state[5]` | 2 | `Power` on/off **(confirmed)** — `0x04`, clear = off / set = on. Confirmed 2026-09-07: a real capture pair differing in exactly this one bit (`0x24`→`0x20`, both checksum-valid) — see `../MiniSplitIR/captures/protocol_capture.md`'s "Power / Setpoint / Fresh Air capture session". No longer blocks `SystemMode`'s `Off` case in Milestone 2. |
| `state[5]` | 3 | `OffTimerEnabled` **(sourced, unconfirmed)** — `0x08` |
| `state[5]` | 4 | `OnTimerEnabled` **(sourced, unconfirmed)** — `0x10` |
| `state[5]` | 5 | `Quiet` (Type 2 only — see below) / Follow-Me enable-vs-heartbeat toggle (Type 1 only, our own capture-confirmed finding, not in the library's model) — `0x20`. Same bit position, different meaning depending on `MsgType`; see "Two meanings for one bit" below. |
| `state[5]` | 6 | `Light` **(sourced, unconfirmed)** — `0x40`. **Inverted**: library's `setLight()` stores `!on` — i.e. bit **clear** = light on, bit **set** = light off. Confirm this polarity in the capture, easy to get backwards. |
| `state[5]` | 7 | `Econo` **(sourced, unconfirmed)** — `0x80`. Out of scope (no Tuya DP), noted for completeness. |
| `state[6]` | 0-3 | Mode: `1`=Heat, `2`=Dry, `3`=Cool, `7`=Fan, `8`=Auto — independently confirmed by our own captures |
| `state[6]` | 4 | `Health` **(sourced, unconfirmed)** — `0x10` |
| `state[6]` | 5 | `Turbo` **(sourced, unconfirmed)** — `0x20`. Out of scope (no Tuya DP), noted for completeness. |
| `state[6]` | 7 | Follow Me enabled — mirrors `state[4]` bit 7 (our own capture-confirmed finding; unclaimed in the library's model, same as `state[4]`) |
| `state[7]` | 0-3 | Setpoint: `Temp = 31 - (state[7] & 0x0F)` (e.g. `0x0A`→21C, `0x08`→23C). Silently ignored (no-op) in Auto mode. Bits 4-7 unclaimed (library never sets them; matches every capture showing `state[7] <= 0x0F`). |
| `state[8]` | 0-2 | Fan — **see "Fan speed discrepancy" below, this is where our own capture and the library disagree** |
| `state[8]` | 3-5 | `SwingV` **(sourced, unconfirmed)** — mask `0x38`: `0`=Off (`kTcl112AcSwingVOff` in the library's own source, `ir_Tcl.h` — verified 2026-09-07; ignore the generic decoder's "(Auto)" display label mentioned below, that's a shared AC-family pretty-printer quirk, not TCL112-specific), `1`=Highest, `2`=High, `3`=Middle, `4`=Low, `5`=Lowest, `7`=On (continuous sweep), `6` undefined |
| `state[8]` | 6 | `TimerIndicator` **(sourced, unconfirmed)** — `0x40`, OR of `OnTimerEnabled`/`OffTimerEnabled` |
| `state[9]` | 1-6 | `OffTimer` **(sourced, unconfirmed)** — mask `0x7E`, minutes ÷ 20 (0-720min range, 0=off) |
| `state[10]` | 1-6 | `OnTimer` **(sourced, unconfirmed)** — mask `0x7E`, same units as `OffTimer` |
| `state[11]` | — | Follow Me sensor temp, whole degrees C — only meaningful when the Follow Me bit is set; `0x00` otherwise. **This is the field this project's Follow-Me feature writes.** Fully unclaimed in the library's model (all 8 bits "00000000"), same situation as `state[4]` — another feature this library doesn't implement, that our own captures found the real firmware using. |
| `state[12]` | 0 | **Fresh Air** — `0x01`. **Type 2 frame only** (Type 1's `state[12]` byte 12 doesn't carry it — see "Known gaps" below for the full writeup). Set = on, clear = off. Found 2026-09-10 via bit-level capture comparison (6 captures, 3 each state, all checksum-valid). |
| `state[12]` | 2 | Unnamed toggle bit that flips per remote button-press even when no field actually changed (our own capture-confirmed finding, e.g. `0x80`↔`0x84`) — anti-repeat/session toggle, not decoded further. Now pinned to a specific bit rather than "somewhere in this byte." |
| `state[12]` | 3 | `SwingH` **(sourced, unconfirmed)** — `0x08` |
| `state[12]` | 5 | `HalfDegree` **(sourced, unconfirmed)** — `0x20`. Not used by this project (whole-degree setpoints only). |
| `state[12]` | 7 | `isTcl` **(sourced)** — `0x80`, a TCL-vs-clone model flag the library sets by default and every one of our own captures shows set. High confidence this one's correct without further confirmation, since 100% of captures agree. |
| `state[13]` | — | Checksum — see above, resolved and verified. |

### Fan speed discrepancy — real unit vs. generic library model

**Our own captures are ground truth here; the library's generic model
doesn't match this unit.** The library defines `kTcl112AcFanMin = 0b001`
(aliased as both `"Night"` and `"Quiet"`) as a value **distinct** from
`kTcl112AcFanLow = 0b010`. But every one of our own captures shows this
unit's real firmware using **`state[8]=2` for both Quiet and Low**,
disambiguated only by the paired Type 2 frame's Quiet flag (`state[5]` bit
`0x20` when `MsgType`=Type 2) — the library's separate value `1` for
"Quiet" is never observed. **Keep our own captured Fan enum as authoritative:
`0`=Auto, `2`=Quiet/Low (Type 2 flag disambiguates), `3`=Medium, `5`=High.**
This is exactly why every other newly-sourced field above is flagged
"unconfirmed" rather than trusted outright — the generic library is a
strong hint, not a guarantee, for this specific OEM variant.

**Fan cycle order:** Auto → Quiet → Low → Med → High → (back to Auto).

### Two meanings for one bit: `state[5]` bit `0x20`

- **Type 2 frame (`state[3]`=`0b10`):** this is the library's real `Quiet`
  bit — `setQuiet()` only writes it when `MsgType == kTcl112AcSpecial`.
  Matches our own capture of the Quiet-on Type 2 frame (`state[5]`: `0x40`→
  `0x60`, a `+0x20` diff).
  **This resolves what earlier notes called an "incrementing step counter"
  on `state[5]`/`state[6]` in the Type 2 frame — it's just the real named
  `Quiet` bit, not a counter.**
- **Type 1 frame (`state[3]`=`0b01`):** the library never touches this bit
  in Type 1 mode (Quiet is a Type-2-only concept in its model) — so our own
  capture-confirmed Follow-Me "enable vs. heartbeat" toggle is the real
  firmware repurposing an otherwise-unused-in-Type-1 bit position for a
  feature outside the library's scope. No actual conflict, just two
  different frame types giving the same bit position two different jobs.

## Type 2 frame — required alongside Type 1, confirmed 2026-09-07

Applying the same `Tcl112Protocol` struct to a Type 2 frame (`state[3]` =
`0b10`) explains what earlier notes called an unresolved "step counter" on
`state[6]`: per the struct, `state[6]` bits 0-3 are `Mode` and bits 4-5 are
`Health`/`Turbo` — fields that arguably have no real meaning in a
special/quiet frame, but aren't a counter either. `state[7]`, `state[9-11]`
being constant `0x00` in every Type 2 capture now reads as "Setpoint/Timers
just left at zero in this frame type," and `state[8]`'s constant `0xC3`
(`0b11000011`) decomposes cleanly as `Fan` bits0-2=`0b011`=3(Medium),
`SwingV` bits3-5=`0b000`=0(Off), `TimerIndicator` bit6=1, and bit7=1 —
plausible field values that just don't carry real meaning in a special
frame, rather than a mystery constant.

**Resolved by `test_apps/ir_live_test` against the real unit (2026-09-07):
the AC does not respond to a lone Type 1 frame.** First real end-to-end
test (IR LED aimed at the unit itself, not a loopback receiver) sent Type 1
alone — LED confirmed flashing (checked via phone camera), unit did not
react to any command. Sending a real captured Type 2 frame immediately
before the Type 1 frame (mirroring real remote behavior — see "Protocol
identity" above) fixed it: the unit started responding correctly to every
command in the test list. The specific Type 2 payload sent was the
"everyday" one from `../MiniSplitIR/captures/protocol_capture.md`
(`{0x23,0xCB,0x26,0x02,0x00,0x40,0x20,0x00,0xC3,0x00,0x00,0x00,0x00,0x48}`,
constant across ordinary mode-cycle presses there) — since `state[6]`'s
value is a real capture-confirmed free-running counter that doesn't appear
to gate acceptance (see the capture log's Fan-speed session, presses 1-6),
a single fixed real capture was enough; no need to reproduce the counter's
exact sequence.

**Production impact — fixed 2026-09-09 correction: this section previously
said `src/main.c`'s `send_ir_frame()` still only sent Type 1. That was
stale by the time it was written — it already sent both.** `send_ir_frame()`
calls `transmit_ir_state_frame()`, which sends the Type 2 companion frame
before the Type 1 frame on every call, shared by both `send_ir_frame()` and
`send_followme_frame()`. See PLAN.md Milestone 2.

## Follow Me behavior

- **Enable:** `state[4]`/`state[6]` bit 7 set, `state[11]` = current ambient
  temp in whole °C, `state[5]` bit `0x20` set.
- **Heartbeat:** identical full frame, `state[5]` bit `0x20` cleared,
  `state[11]` updated to the latest ambient reading. Real remote re-sends
  every **3 minutes** — match this cadence, don't derive it from the
  (unverified, placeholder) 10-minute fallback-timeout guess below.
- **Disable:** captured for the first time 2026-09-09 (previously only
  enable had been captured) — a real "I feel" off button-press produces the
  everyday Type 2 pre-frame plus a Type 1 frame that's byte-for-byte
  identical to the plain (no-Follow-Me) baseline: `state[4]`/`state[6]` bit 7
  cleared, `state[11]` zeroed. No separate "disable" shape — same
  full-frame-every-time model as everything else in this protocol.
- **Frame count — corrected 2026-09-09:** the heartbeat is a **Type 2 +
  Type 1 pair**, exactly like every other command, not a lone Type 1 frame.
  An earlier capture session concluded otherwise (mistook one half of the
  pair failing to decode — reported by the receiver as `Protocol: UNKNOWN`,
  114 bits — for the whole story); re-verified across three consecutive
  heartbeat cycles on 2026-09-09, each one a full pair (see
  `../MiniSplitIR/captures/protocol_capture.md`'s "Re-capture session
  (2026-09-09)" for the raw decode). **No firmware change needed** — `main.c`'s
  `transmit_ir_state_frame()` already sends the Type 2 companion frame ahead
  of every Type 1 send, shared by both `send_ir_frame()` and
  `send_followme_frame()`, so this was already correct in practice; only the
  documentation was wrong.
- **Fallback timeout:** not measured (battery-pull test was skipped) — 10
  minutes was used as an unverified placeholder in earlier planning. Doesn't
  actually gate this project's own heartbeat interval (see above), so it's
  low-priority to re-measure unless something else starts depending on it.

## Known gaps

- **Fresh Air — resolved 2026-09-10.** `state[12]` bit `0x01` **of the Type 2
  frame** (not Type 1 — every earlier attempt focused on Type 1, which is why
  this took so long to find). Found via `MiniSplitIR/capture_tools`'
  microsecond-resolution `RawPinTest` (interrupt-buffered, `micros()`
  timestamps — the project's original ms-resolution raw-pin captures and the
  2026-09-07/09-09 attempts below were structurally incapable of seeing this:
  every IR bit, 0 or 1, produces the same *number* of edges, so a bit-value
  difference only shows up in space *duration*, not edge pattern/count).
  6 bit-level-decoded captures (3 with Fresh Air on, 3 off; user-confirmed via
  the remote's own display each time), all checksum-valid: Type 2
  `state[12]` = `0x01` when on, `0x00` when off, consistently; Type 1's
  `state[12]` was byte-identical (`0x84`) across all 6 — Fresh Air isn't
  carried there at all. Wired into `src/main.c`'s `transmit_ir_state_frame()`
  (which builds the Type 2 companion frame) from `status->fresh_air_valve`,
  2026-09-10 — this was the actual root cause of Fresh Air reverting on every
  Device B command: the Type 2 template had this bit hardcoded to `0x00`.
  **Not yet confirmed against the real unit** (compiled, not yet flashed to
  Device B / tested end-to-end) — do that before considering this fully
  closed.

  <details><summary>Earlier attempts (superseded, kept for history)</summary>

  Attempted 2026-09-07: had a real, dedicated remote button (confirmed by the
  user). Several presses at normal range produced a byte-for-byte identical
  frame to the pre-press baseline; at very close range the receiver picked up
  two frames but neither was usable (one an ordinary already-known Type 2
  frame, the other — the Type 1 half — corrupted partway through). Full
  writeup in `../MiniSplitIR/captures/protocol_capture.md`'s "Fresh Air
  capture attempt" section. 2026-09-09: `state[4]` was ruled out (a
  Fresh-Air-on capture still showed `state[4] = 0x00`). Both attempts only
  ever inspected Type 1 / used ms-resolution captures, which is why they
  missed the Type 2 `state[12]` bit found above.

  </details>
- **Light** has a sourced, unconfirmed bit position (table above,
  `state[5]` bit `0x40`, inverted) — **wired into `src/main.c`'s
  `build_ir_state_frame()` anyway (2026-09-07)**, on the library's sourced
  hypothesis as-is, after a real regression (Light silently reverting on
  unrelated commands) made shipping it worth more than waiting for a
  capture to confirm the polarity. Revisit if it turns out inverted.
- **Swing(V), Swing(H), Health** have sourced, unconfirmed bit positions
  (table above) — **deliberately not being captured/confirmed or preserved**
  (user call, 2026-09-07); not a blocker for Milestone 2.
- **On Timer / Off Timer:** now known with high confidence (sourced bit
  positions above, real getter/setter logic behind them in the library —
  not a placeholder/static value as an earlier draft of this file
  speculated). No capture planned regardless — decided to always transmit
  these bits/bytes as zero (`OnTimerEnabled`=`OffTimerEnabled`=
  `TimerIndicator`=0, `OnTimer`=`OffTimer`=0) on every send, which is now a
  precise, deliberate zeroing instead of "hoping the observed defaults
  hold." Accepted tradeoff: a timer set via the real remote or the Tuya app
  gets cleared by this device's next command. See [PLAN.md](PLAN.md)
  Milestone 2.
- **Econo, Turbo:** sourced bit positions now known (table above), but
  deliberately not being chased right now — no confirmed Tuya DP for
  either. See [PLAN.md](PLAN.md)'s Open Items.
