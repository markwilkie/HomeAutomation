# SpaV2 firmware: spa monitor (Matter over Thread)

ESP32-C6 DevKitM-1 (MAC 58:8C:81:5D:89:1C), ESP-IDF 5.4.1, esp_matter 1.5.0 (pinned like MiniSplit /
ThreadRouter). Design and history: `../PLAN.md`. Home Assistant side: `../homeassistant/`.

| Source | Does |
|---|---|
| `src/spa_config.h` | Pins and tunables: pump thresholds, gating times, LED timings |
| `src/probes.c` | pH / ORP from the Atlas isolators' PWM (MCPWM capture) using Atlas's `atlas_gravity` math; guarded calibration in NVS |
| `src/sensors.c` | DS18B20 water temperature (GPIO7); SCT-013-030 pump CT RMS (GPIO6) |
| `src/spa_leds.c` | SmarterSpa LED decoder (1 ms sampling) → state; electrode-hours counter |
| `src/matter_device.cpp` | The 14 Matter endpoints (table at the top of the file) and calibration buttons |
| `src/main.cpp` | Publishing loop, pH/ORP pump gate, serial console |

## Build / flash

```powershell
cd SpaV2\firmware
.\build.ps1          # first build compiles ~2000 files
.\flash.ps1          # SpaV2 board only; never erases NVS (keeps commissioning + calibration)
```

Commissioning: `../homeassistant/README.md`.

## Serial console (USB, 115200)

Every 30 s it logs one status line (pH, ORP, temperature, pump amps, CT bias, SmarterSpa state). Commands
match the original sketch:

| Command | Effect |
|---|---|
| `PHCAL,7` / `PHCAL,4` / `PHCAL,10` | calibrate a pH point (probe in that buffer, settled ~1 min) |
| `PHCAL,CLEAR` | pH back to library defaults |
| `ORPCAL,<mV>` | ORP offset against a reference solution of that value |
| `ORPCAL,CLEAR` | ORP offset to 0 |
| `STATUS` | print stored calibration and pH slopes |

A calibration press stores the last 30 s mean with no stability or range limits (PLAN.md Phase 3); only *no
signal* is refused. Check the logged mean, drift and slopes, and redo or clear a bad point.

## Known provisional values

- ORP reference default 225 mV (`ORP_CAL_REF_MV`): change it if your solution differs.
