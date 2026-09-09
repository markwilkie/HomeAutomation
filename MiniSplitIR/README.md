# MiniSplit IR Follow-Me Bridge

Local Matter/Thread control of the Pioneer WT012GLUI25FVQ mini split over IR,
replacing the unreliable Tuya cloud *command* path (Tuya status GETs keep
running in parallel — read-only, harmless) and replicating "follow me" using
a real ambient sensor instead of the unit's own poorly-placed one.

See [PLAN.md](PLAN.md) for the current implementation plan (protocol byte
tables, corrected milestone status, concrete next steps) — it supersedes
[instructions.txt](instructions.txt), which stays as the original ask/
historical record.

## Two devices, one Matter fabric

**"Device A" is not a separate device.** It's the existing MiniSplit
bridge's aux BME280 endpoint (node `@1:18`, endpoint 2) — already running
esp-matter (not ESPHome), already commissioned, already feeding real HA
automations. Only one new device exists in this project:

| | Role | State | Notes |
|---|------|-------|-------|
| **Device A** | Sensor source | Existing, already commissioned | The MiniSplit bridge's aux BME280 Temperature Sensor endpoint (`@1:18` ep2) — zero new firmware work |
| **Device B** | IR blaster node | New | ESP32-C6; exposes a Matter Thermostat, binds to Device A's temperature, drives the AC over IR |

[../MiniSplit/](../MiniSplit/) is the existing ESP32-C6 Matter↔Tuya bridge
for this same AC unit — the same device that hosts "Device A" above. That
project keeps using Tuya for status polling; this project only replaces how
*commands* reach the unit.

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

Milestone 1 complete (see [captures/protocol_capture.md](captures/protocol_capture.md)
for the full findings). Milestone 3 skeleton (Device B) built, flashed, and
**fully commissioned into Home Assistant's Matter fabric** on real hardware
on 2026-09-03 (node `@1:27`, Thermostat endpoint live alongside the existing
MiniSplit node):

- Boots cleanly, creates the Thermostat endpoint (`thermostat_ep=1`)
- Starts Matter commissioning, BLE (NimBLE) advertising comes up
- OpenThread attaches to the Thread netif as a Router
- Prints a real manual pairing code and QR code URL
- BLE PASE pairing, ArmFailsafe, regulatory config, device attestation, NOC
  installation, and `GeneralCommissioning.Complete` all succeeded
  (`errorCode: 0` throughout); `matter-server` shows the Thermostat endpoint
  live and subscribed

| Milestone | Description | Status |
|---|---|---|
| 1 | Capture real IR protocol + measure follow-me fallback timeout | ✅ Protocol identified (`TCL112AC`), full mode/setpoint/fan/Follow-Me encoding decoded; fallback timeout **not measured**, assumed 10 min |
| 2 | ~~Device A: ESPHome → esp-matter rewrite~~ | ✅ N/A — turned out to already be the existing MiniSplit bridge's aux BME280 endpoint |
| 3 | Device B: esp-matter Thermostat (0x0201), commissioning skeleton | ✅ Commissioned into HA as node `@1:27`, Thermostat endpoint live. IR send logic itself: 📅 |
| 4 | Follow-me heartbeat loop (3 min interval, matching the real remote's measured cadence) + stale-subscription fail-safe | 📅 |

**Still not verified:** that SystemMode/setpoint writes from Home Assistant
actually reach `matter_get_system_mode_command_pending()` etc. in this
firmware (currently just logged by `command_task` in `src/main.c`, not acted
on) -- worth a quick check from the HA UI before moving on to Milestone 3's
IR logic.

**Known non-fatal boot warnings:** three `E (...) chip[DIS]:` lines
(`Failed to remove/advertise commissionable node/finalize service update: 3`)
appear right at every boot, before Thread has attached. Confirmed genuinely
benign -- seen consistently across roughly six commissioning attempts
(including the one that fully succeeded) with zero effect on the outcome.
Root cause not investigated further (plausible mDNS/SRP advertise-too-early
race), not worth chasing given zero observed impact.

Commissioning Device B surfaced two real `matterjs-server`/`wilkie-home-server`
infrastructure issues, now fixed and documented in
[../MiniSplit/COMMISSIONING_GUIDE.md](../MiniSplit/COMMISSIONING_GUIDE.md) since
they'll affect any future device, not just this one: a flaky BlueZ/D-Bus BLE
connection state (fixed with a Bluetooth service restart), and
`matterjs-server`'s default policy rejecting esp-matter's test-range Vendor ID
until `ENABLE_TEST_NET_DCL=true` was added to
`../Wyse5070DebSetup/setup-matter-server.sh`.

## Device B — build & flash

Same toolchain and gotchas as [../MiniSplit/BUILD.md](../MiniSplit/BUILD.md)
in full detail; short version:

```powershell
# Activate the EIM-managed ESP-IDF 5.4.1 environment
. C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1
$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"   # Windows path-length workaround

cd C:\Users\Administrator\Documents\GitHub\HomeAutomation\MiniSplitIR
idf.py set-target esp32c6   # first time / after fullclean only
idf.py build
idf.py -p COM3 flash        # verify the actual COM port first, don't assume
```

Reading serial output without a TTY (e.g. from an agent/automation shell,
where `idf.py monitor` refuses to run): see
[../MiniSplit/BUILD.md](../MiniSplit/BUILD.md)'s "Reading serial output
without a TTY" section, or reuse [capture_tools/](capture_tools/)'s
`serial_logger.ps1` pattern (the RTS-only reset pulse, DTR raised after it
settles).

## Project structure

```
MiniSplitIR/
├── instructions.txt        # Original plan (see also captures/ and capture_tools/)
├── README.md                # This file
├── CLAUDE.md                 # Hard invariants for the IR send logic
├── capture_tools/            # Milestone 1 capture sketches (IRrecvDumpV2, RawPinTest)
├── captures/                 # Milestone 1 findings (protocol_capture.md)
├── CMakeLists.txt             # Device B: esp-matter Thermostat (ESP-IDF project root)
├── partitions.csv             # 3MB factory app, same as ../MiniSplit
├── sdkconfig.defaults          # Thread/BLE/mbedTLS config, same gotchas as MiniSplit
├── main/                        # Dummy main component (esp-matter build requirement)
├── include/
│   ├── matter_device.h
│   └── chip_project_config.h    # VendorName/ProductName overrides
└── src/
    ├── main.c                    # Boot sequence, command_task (logs pending commands only)
    ├── matter_device.cpp          # Thermostat endpoint, commissioning
    └── idf_component.yml          # espressif/esp_matter ^1.5
```

## Suggested build order

1. ~~Milestone 1 capture~~ ✅ done — see [captures/protocol_capture.md](captures/protocol_capture.md).
2. ~~Device B skeleton~~ ✅ done and commissioned. Next: confirm SystemMode/setpoint writes from HA actually reach the firmware (only reads verified so far).
3. Wire in IR send logic using the captured frames (shadow-state model, full-frame-only sends — see [CLAUDE.md](CLAUDE.md)).
4. Bind Device B to the existing MiniSplit bridge's aux BME280 endpoint (`@1:18` ep2); verify `LocalTemperature` updates flow.
5. Follow-me heartbeat + timeout fail-safe.

## Hard invariants for whoever implements Milestone 3+

See [CLAUDE.md](CLAUDE.md) — the state model and command-send heuristics
are load-bearing (full-frame-only protocol, debounce/dedup/spacing rules,
heartbeat fail-safe) and easy to accidentally violate with a naive
"attribute write → IR send" implementation.

## Open questions

See [PLAN.md](PLAN.md)'s "Open items" section for the current, maintained
list — kept there rather than duplicated here to avoid the two drifting out
of sync (as happened once already: Device A's identity was wrong in this
file until [PLAN.md](PLAN.md) corrected it).
