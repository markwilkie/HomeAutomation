# MiniSplit IR Follow-Me Bridge

Local Matter/Thread control of the Pioneer WT012GLUI25FVQ mini split over IR,
replacing the unreliable Tuya cloud *command* path (Tuya status GETs keep
running in parallel — read-only, harmless) and replicating "follow me" using
a real ambient sensor instead of the unit's own poorly-placed one.

See [instructions.txt](instructions.txt) for the full original plan this
project was scoped from.

## Two devices, one Matter fabric

| | Role | State | Notes |
|---|------|-------|-------|
| **Device A** | Sensor node | Existing (ESPHome + HA native API) | Bedroom BME280; needs porting to a native Matter Temperature Sensor endpoint |
| **Device B** | IR blaster node | New | ESP32 + Dorhea HX-03 IR transmitter; exposes a Matter Thermostat, binds to Device A's temperature, drives the AC over IR |

Related but separate: [../MiniSplit/](../MiniSplit/) is the existing
ESP32-C6 Matter↔Tuya bridge for this same AC unit. That project keeps
using Tuya for status polling; this project only replaces how commands
reach the unit.

## Hardware

- Dorhea 4x transmitter / 4x receiver kit — HX-03 (bare 940nm IR LED, no
  onboard carrier gen — matches IRremoteESP8266's `IRsend` bit-banged
  carrier) and HX-M121 (demodulating receiver, TSOP382x-equivalent role).
- HX-03 rated range is only ~1.3m at 5V — either mount Device B with clean
  line-of-sight to the unit's IR window, or add a 2N2222 + TSAL6200 driver
  stage for real range margin (recommended regardless of mounting distance).
- HX-M121 is 5V nominal; ESP32 GPIOs are 3.3V-input-only — confirm 3.3V
  operation or level-shift.
- IR receiver window on the unit: co-located with the display board on the
  front panel (small smoked/tinted window near the digital display) —
  confirm exact aim point with the real remote before mounting Device B.

## Status

**Not started — Milestone 1 is blocking and physical.** Nothing below it
can be implemented without the captured protocol data.

| Milestone | Description | Status |
|---|---|---|
| 1 | Capture real IR protocol + measure follow-me fallback timeout | ⏳ Do this first, in person, with the real remote + `IRrecvDumpV2` |
| 2 | Device A: ESPHome → esp-matter Temperature Sensor (0x0402), keep existing smoothing filter | 📅 |
| 3 | Device B: esp-matter Thermostat (0x0201) + IR send, shadow-state model | 📅 |
| 4 | Follow-me heartbeat loop (interval ≈ half measured timeout) + stale-subscription fail-safe | 📅 |

## Suggested build order

1. **You:** Milestone 1 capture (wire HX-M121 to any spare ESP32, flash
   `IRrecvDumpV2`, capture full-state frames + the follow-me heartbeat
   frame + the real fallback timeout). Hand the captured data back rather
   than having this be guessed at.
2. Device B skeleton: esp-matter Thermostat device type, commissioning
   onto the Thread fabric, no IR logic yet.
3. Wire in IR send logic using the captured frames.
4. Device A conversion to esp-matter Temperature Sensor.
5. Bind Device B to Device A; verify `LocalTemperature` updates flow.
6. Follow-me heartbeat + timeout fail-safe.

## Hard invariants for whoever implements Milestone 3+

See [CLAUDE.md](CLAUDE.md) — the state model and command-send heuristics
are load-bearing (full-frame-only protocol, debounce/dedup/spacing rules,
heartbeat fail-safe) and easy to accidentally violate with a naive
"attribute write → IR send" implementation.

## Open questions

1. Protocol identity — unknown until Milestone 1.
2. Actual follow-me fallback timeout for this unit — unknown until measured.
3. Confirm Device A's chip is Thread-capable (ESP32-C6/H2 class); if not,
   it needs Matter-over-WiFi instead (different esp-matter build config).
4. Optional: should Device B also accept plain IR-remote input (via the
   spare HX-M121 receivers) to keep a physical remote press in sync with
   Matter state? Not required for the core follow-me goal.
