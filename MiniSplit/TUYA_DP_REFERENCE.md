# Tuya Device DP (Data Point) Reference

Full data point schema for the mini-split, originally pulled from Tuya's **"Query Things Data
Model"** API (`/v1.0/devices/{device_id}/specification`) in the Tuya IoT Platform API Explorer,
and cross-checked against live values from the device's shadow (see
[Which API Endpoint](#which-api-endpoint-to-use) below).

- **Device ID:** `eb11d9ff75ef37d109pihg`
- **Model ID:** `e1ky592s`

This is the manufacturer's full product template — many DPs below (voice assistant, generator
mode, Bluetooth pairing, etc.) are shared across a hardware family and may not be physically
present or functional on this specific unit. Treat `capabilities` (bitmap) as the authoritative
list of which optional features this hardware actually supports.

## Which API Endpoint to Use

There are two different Tuya endpoints for reading device state, and they return **different
subsets of DPs** for this device:

| Endpoint | What it returns |
|---|---|
| `GET /v1.0/iot-03/devices/status?device_ids={id}` | A small, standardized/curated subset. For this device: `switch`, `temp_set`, `temp_current`, `mode_auto`, `mode_eco`, `mode_dry`, `heat`, `light`, `beep`, `temp_set_f`, `health`, `cleaning`, `fresh_air_valve`. Notably, `mode_auto`/`mode_eco`/`mode_dry` **do not exist as real DPs** on this device — Tuya synthesizes them from the real `mode` enum for backward-compatible/standardized API consumers. `compressor_frequency`, `ure`, `electricity`, etc. never appear here, regardless of their actual value. |
| `GET /v2.0/cloud/thing/{id}/shadow/properties` | The complete, current-value snapshot of **every** DP the device has ever reported — the real underlying data. This is the one that actually contains `compressor_frequency`, `ure` (outdoor temp), `electricity`, `external_unit_fanspeed`, and the real `mode` enum. |

`tuya_client.c` uses the **shadow/properties** endpoint (`TUYA_SHADOW_PROPERTIES_ENDPOINT_FMT`)
for exactly this reason. Confirmed by directly querying both endpoints for this device on
2026-07-05: the `/status` response omitted `compressor_frequency`/`ure` entirely (not just as
zero — the keys were absent), while `/shadow/properties` returned live values matching what the
Tuya IoT Platform's Device Logs panel showed being reported by the device in real time.

One quirk of `/shadow/properties`: enum-typed DPs (`mode`, `fan_speed_enum`, etc.) come back as
JSON **strings** (e.g. `"value":"1"`), not numbers — the parser has to check for that explicitly
rather than reading `valueint` directly.

## Core Climate Control

| Code | Access | Type | Description |
|---|---|---|---|
| `switch` | rw | bool | Power on/off |
| `mode` | rw | enum 0–4 (string-valued) | 0=auto, 1=cool, 2=dry, 3=fan, 4=heat. **The real mode DP** — confirmed live (`"mode":"1"` while cooling). |
| `temp_set` | rw | value ×100, 16–31°C, step 0.5 | Target temperature (Celsius) |
| `temp_current` | ro | value ×100, -100–100°C | Current measured temperature |
| `temp_set_f` | rw | value, 61–88°F | Target temperature (Fahrenheit) |
| `temperature_type` | rw | bool | Display unit: 0=°C, 1=°F |
| `upper_tem_limit` | ro | value ×100, 26–31°C | Hardware-supported upper temp limit |
| `lower_tem_limit` | ro | value ×100, 16–25°C | Hardware-supported lower temp limit |
| `eight_add_hot` | rw | bool | "8°C heating" frost-protection mode |
| `heat` | rw | bool | Auxiliary electric heat enable (separate from `mode`==heat) |
| `heat_status` | ro | bool | Actual electric-heat relay state |

## Compressor / Outdoor Unit

| Code | Access | Type | Description |
|---|---|---|---|
| `compressor_frequency` | ro | value, 0–150Hz (see note) | Actual compressor running frequency (0 = idle/off) |
| `outdoor_comptar_freqrun` | ro | value, 0–150Hz | Outdoor unit's self-calculated compressor target |
| `outdoor_comptar_freqset` | rw | value, 0–150Hz | Cloud-issued compressor target override (used by AI Eco) |
| `outdoor_eevtar_opendegree` | rw | value, 0–500 | Electronic expansion valve target opening |
| `ure` | ro | value ×100, -50–250°C | Outdoor ambient temperature |
| `external_unit_fanspeed` | ro | value, 0–3000 rpm | Outdoor fan motor speed |
| `outdoor_fan_tarspeed` | rw | value, 0–200 (×10 scaled) | Cloud-set outdoor fan target speed |

> **Scale correction:** the product spec's `typeSpec` claims `compressor_frequency` is ×10-scaled
> (max 1500 = 150.0Hz). Live data contradicts this: on 2026-07-05, `compressor_frequency` and
> `outdoor_comptar_freqrun` both read **14** simultaneously — the same raw, unscaled Hz value.
> Treat `compressor_frequency` as raw Hz (max ~150) to match observed behavior, not the spec's
> stated scale. The firmware (`src/main.c`, `COMPRESSOR_FREQUENCY_MAX`) uses 150.

## Fan & Airflow

| Code | Access | Type | Description |
|---|---|---|---|
| `fan_speed_enum` | rw | enum 0–7 | Stop/mute/low/med-low/med/med-high/high/turbo |
| `auto` | rw | bool | Fan speed auto mode (0=manual, 1=auto) — **not** overall AC mode |
| `wind_speed_percentage` | rw | value 0–100% | Fan speed as percentage (device notes: not currently processed) |
| `soft_wind` | rw | enum 0–4 | Soft/gentle wind (only 0/1 used; others default to 1) |
| `smart_windmode` | rw | enum 0–2 | 0=off, 1=avoid people, 2=blow at people |
| `vertical_wind` | rw | enum 0/1 | Vertical swing on/off |
| `horizontal_wind` | rw | enum 0/1 | Horizontal swing on/off |
| `gear_vertical` | rw | enum 0–13 | Fixed vertical louver position |
| `gear_horizontal` | rw | enum 0–17 | Fixed horizontal louver position |
| `cool_feel_wind` | rw | bool | "Cool-feel" wind effect |
| `high_temperature_wind` | rw | bool | High-temp wind mode |

## Fresh Air Module

| Code | Access | Type | Description |
|---|---|---|---|
| `fresh_air_valve` | rw | bool | Fresh air intake valve on/off |
| `supply_fan_speed` | rw | enum 0–3 | Stop/low/med/turbo |
| `new_wind_auto_switch` | rw | bool | Fresh air auto mode |
| `newwind_super` | rw | bool | Fresh air super-strong wind |
| `freshair_status` | ro | bool | Fresh air filter dirty/clogged (0=clean, 1=clogged) |

## Comfort / Special Modes

| Code | Access | Type | Description |
|---|---|---|---|
| `eco` | rw | bool | Energy-saving mode |
| `ai_eco_switch` | rw | bool | AI energy-saving mode enable |
| `ai_eco_status` | rw | enum 0–2 | 0=inactive, 1=AI freq-modulation running, 2=AI PID running |
| `health` | rw | bool | Health/anion function |
| `drying` | rw | bool | Self-clean drying (evaporator dry-out on power-off, unrelated to `mode`==dry) |
| `cleaning` | rw | bool | Evaporator self-clean (0=off, 1=on) |
| `sleep_enum` | rw | enum 0–3 | 0=off, 1=standard, 2=elderly, 3=child |
| `light_sense` | rw | bool | Light sensor enable |
| `light_senser_status` | ro | bool | Light sensor reading: 0=day, 1=night |

## Maintenance / Diagnostics

| Code | Access | Type | Description |
|---|---|---|---|
| `filter_block_status` | ro | bool | Filter clogged (0=clean, 1=clogged) |
| `filter_blocknotify` | ro | bool | Voice announcement for filter clog |
| `error_code` | ro | raw bitmap, 99 flags | Fault codes: comms fault, sensor faults, compressor protection, over-current, etc. |
| `capabilities` | rw | raw bitmap, 49 flags | Which optional features this hardware variant supports |
| `examine_mode` | ro | bool | Factory inspection mode |
| `stores_mode` | ro | bool | Showroom/demo mode |
| `access_card_insert` | ro | bool | Energy-saving access card inserted (hotel-style units) |

## Timers

| Code | Access | Type | Description |
|---|---|---|---|
| `weektimer1` | rw | string, maxlen 255 | Weekly schedule 1 (8 on/off timers) |
| `weektimer2` | rw | string, maxlen 255 | Weekly schedule 2 |
| `specialtimer` | rw | string, maxlen 255 | One-shot on/off timer pair |

## Power / Electricity

| Code | Access | Type | Description |
|---|---|---|---|
| `electricity` | ro | value ×100, kWh | Energy consumption, reported ~every 2 min |
| `electricity_starttime` | ro | value, seconds | Accumulation window start timestamp |
| `electricity_endtime` | ro | value, seconds | Accumulation window end timestamp |
| `clearele_starttime` | rw | value | Reset accumulated electricity counter — start |
| `clearele_endtime` | rw | value | Reset accumulated electricity counter — end |
| `virtual_period` | rw | value, seconds | Cloud-only virtual runtime (not sent to device) |
| `virtual_electricity` | ro | value | Cloud-only virtual electricity average (not sent to device) |
| `generator_mode` | rw | enum 0–6 | 0=off, 1–6=LV1–LV6 generator power-limit level |
| `auto_gen_mode` | rw | enum 0–7 | Auto generator mode |
| `power_source` | ro | enum 0/1 | 0=grid, 1=generator |

## Front Panel

| Code | Access | Type | Description |
|---|---|---|---|
| `light` | rw | bool | Display light |
| `beep` | rw | bool | Key-press beep |

## Voice Assistant (mic/speaker module)

| Code | Access | Type | Description |
|---|---|---|---|
| `voice_switch` | rw | bool | Voice assistant enable |
| `voice_status` | ro | enum 0–5 | Standby/listening/recognized/failed/TTS-playing/audio-playing |
| `volume` | rw | value, 10–100 | Voice playback volume |
| `max_volume` | ro | value | Voice max volume (hardware limit) |
| `wakeup_word` | rw | enum 0–3 | Custom wake-word state (none/set/clear/user-triggered) |
| `wakeup_status` | ro | enum | Wake-word self-learning progress/result |
| `sound_location` | ro | enum 0–4 | Detected direction of speaker's voice |
| `mic_distance` | ro | value, mm | Mic array spacing (hardware info) |
| `ble_module_status` | ro | enum 0/1 | Bluetooth broadcast pairing state |
| `ble_module_mac` | ro | string | Bluetooth module MAC address |

## Implementation Status

`tuya_get_device_status()` in [`src/tuya_client.c`](src/tuya_client.c) calls the
**shadow/properties** endpoint and currently parses:

`switch`, `temp_set`, `temp_current`, `temp_set_f`, `mode` (→ `ac_mode`, real enum), `heat`,
`health`, `cleaning`, `fresh_air_valve`, `compressor_frequency`, `ure` (→ `outdoor_temp`)

Written, via `tuya_set_power()`, `tuya_set_temperature()`, `tuya_set_mode()`:

`switch`, `temp_set`, `temp_set_f`, `mode` (real enum, sent as a string DP value)

`light`/`beep` DPs and their Matter endpoints were removed entirely (unused; see git history if
ever needed again).

**Mode handling** (`src/main.c`): `map_tuya_mode_to_matter()` / `map_matter_mode_to_tuya()`
translate between Tuya's real `mode` enum (0=auto,1=cool,2=dry,3=fan,4=heat) and Matter's
`Thermostat::SystemModeEnum` (kOff=0, kAuto=1, kCool=3, kFanOnly=7, kDry=8, kHeat=4) in both
directions. `kOff` is routed through `tuya_set_power(false)` rather than the mode DP, since
Tuya's `mode` enum has no "off" value. Matter modes with no Tuya equivalent (EmergencyHeat,
Precooling, Sleep) are logged and ignored rather than sent.

### Matter endpoint layout

`src/matter_device.cpp` exposes 10 endpoints (fixed, hardcoded — this bridge only ever manages
this one physical device, so there's no dynamic endpoint discovery):

| Endpoint | Clusters | Source | Notes |
|---|---|---|---|
| 1 (root) | OnOff, Thermostat | `switch_state`, `temp_current`, `temp_set`, `ac_mode` | Main AC control |
| 2 | TemperatureMeasurement | `temp_current` (or BME280 if fitted) | "Room Temp" |
| 3 | RelativeHumidityMeasurement | BME280 only (frozen at 50% placeholder if not fitted) | "Room Humidity" |
| 4 | TemperatureMeasurement | `ure` | "Outside Temp" |
| 5 | OnOff, LevelControl | `compressor_frequency` → 0–100% | Current compressor load |
| 6 | OnOff, LevelControl | 8h rolling average of endpoint 5's load | `load_history_average(8)` |
| 7 | OnOff, LevelControl | 24h rolling average of endpoint 5's load | `load_history_average(24)` |
| 8 | OnOff, LevelControl | Cycles in trailing 60 min (RAW count, not %) | `compressor_cycles_moving_window()` |
| 9 | OnOff, LevelControl | Max cycles/hr over last 24h (RAW count, not %) | `compressor_cycles_max_24h()` |
| 10 | TemperatureMeasurement | Minutes in current on/off state (RAW, not x100°C) | `compressor_time_in_state_minutes()` |

Endpoints 5–7 also write the load percentage to the Thermostat cluster's `PICoolingDemand`
sub-attribute (endpoint 1 only, current load) per spec, but that's dead weight for SmartThings
specifically — see the custom-driver note below. Endpoints 8/9/10 repurpose their clusters for
raw (unscaled) values rather than their spec-defined meaning — the custom driver's Lua handlers
know to branch on endpoint ID to avoid misinterpreting them as a percentage or a real temperature.

The 8h/24h load averages (`main.c`, `load_history_record_sample()` / `load_history_average()`)
are a 24-slot ring buffer of hourly averages, updated once per hour from the running accumulator
of 30s samples — not a true continuous sliding window, but close enough for "how hard has it
been working" and far cheaper than storing 2880 raw samples for a 24h window.

**Compressor cycling** (`compressor_cycle_track()` and friends): a "cycle" is counted on the
**leading edge only** — the compressor turning **on** (`off→on`). Turning back off is not itself
a second cycle; only the off→on transition starts one. Two different windows are tracked:
- **Moving window** (endpoint 8): a true sliding 60-minute window over a 64-slot ring buffer of
  off→on timestamps, recomputed live on every poll — not hour-bucketed like the load average.
- **Max cycles/24h** (endpoint 9): each wall-clock hour's off→on count is pushed into its own
  24-slot hourly ring buffer (independent of the load-history buckets, on its own hour boundary);
  this endpoint reports the max across those 24 buckets.

**Time in state** (endpoint 10): minutes since `compressor_pct > 0` last flipped, regardless of
cycle completion — this is a plain duration, unrelated to the cycle counters above.

### Legacy: SmartThings requires the custom Edge Driver

**This device now targets Home Assistant, not SmartThings** — see
[README.md](README.md#minisplit-matter-bridge) and
[COMMISSIONING_GUIDE.md](COMMISSIONING_GUIDE.md). HA's Matter Server (`python-matter-server`)
doesn't do CSA-certified fingerprint matching the way SmartThings does, so it should expose all 10
endpoints natively with no custom driver needed — **unverified, worth double-checking on first
commission**. The rest of this section is kept for the legacy SmartThings path (e.g. if the hub
is still around for other devices).

**Do not rely on SmartThings' automatic Matter pairing for this device.** Its vendor/product ID
(0xFFF1/0x8000) is in the CSA test range, so SmartThings can't fetch a certified profile and
falls back to `MATTER_GENERIC` fingerprint matching — a fixed catalog of common shapes (e.g.
"thermostat + humidity"). This device's actual shape (2× TemperatureMeasurement + a repurposed
OnOff/LevelControl endpoint) doesn't match any catalog entry, so the fallback silently keeps only
2 of the 7 endpoints and drops the rest — no error, just missing data.

The fix is a custom driver:
`../SmartThings_Edge/EdgeDrivers/MiniSplit-Matter-Driver/` (sibling repo). It fingerprints on
this device's exact vendor/product ID and defines the full 5-component composition explicitly.
**It must be installed and assigned to your hub *before* commissioning** (or you'll need to
remove and re-pair the device afterward). See that driver's own README for deployment steps.

Everything else in this document — fresh air module, timers, electricity metering, voice
assistant, generator mode, `external_unit_fanspeed`, etc. — is available via shadow/properties
but not yet integrated into the bridge. Electricity (`electricity`, cumulative kWh) was scoped
but deferred: its native Matter cluster (`ElectricalEnergyMeasurement`) uses a delegate/
cluster-instance implementation pattern, unlike every other cluster this bridge currently uses
(simple attribute-store updates), so it's a larger follow-up rather than a drop-in addition.

## Live Snapshot (2026-07-05, for reference)

Captured via `/v2.0/cloud/thing/{id}/shadow/properties` while the unit was actively cooling:

| Code | Value |
|---|---|
| `switch` | true |
| `mode` | "1" (Cool) |
| `temp_set` | 2100 (21.0°C) |
| `temp_current` | 2030 (20.3°C) |
| `ure` | 2600 (26.0°C outdoor) |
| `compressor_frequency` | 14 |
| `outdoor_comptar_freqrun` | 14 |
| `outdoor_comptar_freqset` | 0 |
| `external_unit_fanspeed` | 650 (rpm) |
| `electricity` | 0 (start of a new 2-min accumulation window) |
| `fan_speed_enum` | "0" |
| `gear_vertical` / `gear_horizontal` | "8" / "8" (current position, fixed) |
