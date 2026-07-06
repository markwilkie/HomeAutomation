# MiniSplit Matter Bridge — Custom Driver

Custom SmartThings Edge Driver for the [MiniSplit](../../../MiniSplit/) ESP32 Matter bridge (a
Tuya mini-split AC exposed over Matter).

## Why this exists

SmartThings' built-in Matter support maps a commissioned device to a capability set two ways:
either it downloads a manufacturer-certified profile (looked up by vendor/product ID via the
Matter Distributed Compliance Ledger), or — if that fails — it falls back to `MATTER_GENERIC`
fingerprinting, matching the device's endpoint *types* against a small, fixed catalog of common
shapes (e.g. `matter/hvac/thermostat/humidity`).

The MiniSplit firmware uses a CSA test vendor/product ID (`0xFFF1`/`0x8000`, not
DCL-certified), so it always falls through to the generic path. Its actual endpoint composition —
one Thermostat, **three** TemperatureMeasurement endpoints (indoor, outdoor, and a repurposed
"time in state" duration), one RelativeHumidityMeasurement, and **five** repurposed
OnOff+LevelControl endpoints (current/8h/24h compressor load, moving-window cycle count, and
max-24h cycle count) — doesn't match any entry in that catalog. The fallback silently keeps
whichever partial match it finds (thermostat + humidity) and drops everything else. No error, no
missing data indicator — the extra sensors just never appear in the app.

This driver fixes that by fingerprinting directly on the device's vendor/product ID and defining
the full composition explicitly, so all 10 endpoints show up correctly.

## Endpoint → Component mapping

Fixed and hardcoded (`src/init.lua`) — this driver only ever manages this one specific device
shape, so there's no dynamic endpoint discovery:

| Endpoint | Component | Label | Capabilities |
|---|---|---|---|
| 1 (root) | `main` | — | `switch`, `thermostatMode`, `thermostatHeatingSetpoint`, `thermostatCoolingSetpoint`, `temperatureMeasurement`, `refresh` |
| 2 | `indoorTemp` | Room Temp | `temperatureMeasurement` |
| 3 | `humidity` | Room Humidity | `relativeHumidityMeasurement` |
| 4 | `outdoorTemp` | Outside Temp | `temperatureMeasurement` |
| 5 | `compressor` | Compressor Load | `switch`, `radioamber53161.loadPercent` |
| 6 | `compressor8h` | Load Last 8h | `radioamber53161.loadPercent` |
| 7 | `compressor24h` | Load Last 24h | `radioamber53161.loadPercent` |
| 8 | `compressorCycles` | Cycles/hr (last 60min) | `radioamber53161.cycleCount` |
| 9 | `compressorCyclesMax24h` | Max Cycles/hr (24h) | `radioamber53161.cycleCount` |
| 10 | `compressorStateTime` | Time In State | `radioamber53161.duration` |

Only `main` is genuinely controllable (power, mode, setpoints) — every other component has no
registered capability command handlers, so they're read-only displays even though `switch`/
`loadPercent` are technically invokable capabilities.

## Custom capability: `radioamber53161.loadPercent`

Standard `switchLevel` ("Dimmer") didn't fit a percent-load reading, was mislabeled in the app,
and doesn't get a history graph anyway (confirmed by testing — neither does `switchLevel`, only
`temperatureMeasurement`/`relativeHumidityMeasurement` reliably do). This capability has one
attribute, `percent` (`value` + `unit: "%"`), with a plain "Load" card label. Defined in
`capabilities/loadpercent.json` + `capabilities/loadpercentpresentation.json`, following the same
pattern as `../Common-Driver-Files/capabilities/lastupdated.json`.

To push an update to the capability definition/presentation after editing those files:
```
capabilities\updatecapability.bat loadpercent
```
(uses `capabilities:update`/`capabilities:presentation:update` — the capability must already
exist; for a first-time create see the deploy steps below.)

## Custom capabilities: `radioamber53161.cycleCount` and `radioamber53161.duration`

Two more custom capabilities back the compressor-cycling metrics, for the same reason as
`loadPercent` above — no stock capability fits a raw count or a raw minutes value without unit
confusion or an unwanted label.

- **`cycleCount`** (`capabilities/cyclecount.json`): one attribute, `count` (`value` + `unit:
  "cycles"`). Used by both `compressorCycles` (endpoint 8) and `compressorCyclesMax24h` (endpoint
  9) — same capability, different components, since both are "a number of cycles," just over
  different windows.
- **`duration`** (`capabilities/duration.json`): one attribute, `minutes` (`value` + `unit:
  "min"`). Used by `compressorStateTime` (endpoint 10).

**Cycle definition** (per explicit requirement): a cycle is counted on the **leading edge
only** — the compressor turning **on** (`off→on`). Turning back off does not itself start a
second cycle. `compressorCycles` reports a true sliding 60-minute window (not hour-bucketed);
`compressorCyclesMax24h` reports the highest per-hour cycle count seen in the last 24 hourly
buckets. Both endpoints carry the cycle count as a raw integer on `LevelControl.CurrentLevel`, not
a percentage — `init.lua`'s `level_handler` branches on endpoint ID (via the `CYCLES_ENDPOINTS`
lookup table) to route these to `cap_cycle_count` instead of treating them as `loadPercent`.
Likewise, `compressorStateTime` carries raw minutes on `TemperatureMeasurement.MeasuredValue`
(unscaled, not `x100`°C) — `temperature_handler` branches on `DURATION_ENDPOINT` to route it to
`cap_duration` instead of parsing it as a temperature.

## Deploying

Requires the `smartthings` CLI, authenticated, with an existing driver channel and a hub.

```powershell
# One-time: create the custom capability (only needed once, or after a schema change
# that capabilities:update can't apply — see the SmartThings API docs for what counts
# as a breaking change).
smartthings capabilities:create -n radioamber53161 -i capabilities\loadpercent.json
smartthings capabilities:presentation:create -i capabilities\loadpercentpresentation.json radioamber53161.loadPercent
smartthings capabilities:create -n radioamber53161 -i capabilities\cyclecount.json
smartthings capabilities:presentation:create -i capabilities\cyclecountpresentation.json radioamber53161.cycleCount
smartthings capabilities:create -n radioamber53161 -i capabilities\duration.json
smartthings capabilities:presentation:create -i capabilities\durationpresentation.json radioamber53161.duration

# Every time: package, upload, assign to channel, and install to the hub in one step.
smartthings edge:drivers:package . --channel <channel-id> --hub <hub-id>
```

Find your channel/hub IDs with `smartthings edge:channels` and `smartthings edge:drivers:installed`.
This project's channel is `lfp-drivers` (`45f7c322-dca4-4764-af7a-855e2af4e728`); the hub is the
"SmartThings v3 Hub" (`0bc6362a-461e-4c46-af86-cbc0b5c97b58`).

**Deploy this driver before commissioning the MiniSplit device.** If it's already commissioned
under the generic fallback, remove it from the SmartThings app and re-pair — SmartThings only
picks a fingerprint match at commissioning time, so an existing device won't retroactively switch
drivers.

### Watching logs live

```powershell
smartthings edge:drivers:logcat <driver-id> --hub-address <hub-lan-ip>
```
The hub's LAN IP is in `smartthings devices <hub-device-id> -j` under `hub.hubData.localIP` (not
the same as the hub's SmartThings device ID used for `--hub` above).

## Known quirks / gotchas hit during development

- **`fingerprints.yml` needs a top-level `matterManufacturer:` key** — a bare YAML array at the
  document root fails deployment with an opaque Jackson deserialization error.
- **Category names are a fixed enum**, not free text — `TemperatureSensor` doesn't exist; even a
  plain temperature sensor profile in SmartThings' own official `matter-sensor` driver uses
  `Thermostat` as its category.
- **`thermostatMode` doesn't have a command for every mode value.** `auto`/`cool`/`heat` are real
  commands; `fanonly`/`dryair` are valid *reportable* mode values (`thermostatMode.thermostatMode.X`)
  but not invokable commands — referencing `capabilities.thermostatMode.commands.fanonly` is a
  nil-field crash that takes down the whole driver on init, with no partial-failure recovery.
- **`LevelControl.CurrentLevel`'s config field is `nullable<uint8_t>`** on the firmware side, so
  the driver receives/sends it as a nullable type; a plain (non-nullable) value type causes an
  `ESP_ERR_INVALID_ARG` on the firmware side when writing it.
- A capability card's presentation `label` (e.g. `switchLevel` always showing "Dimmer") **cannot
  be overridden per-device** for standard/stock capabilities — that's why a custom capability was
  needed just to relabel one card.
- If SmartThings shows an app-level commissioning error ("can't connect to device", "couldn't
  find mDNS device"), **check the device's own serial log and the SmartThings device list via the
  API before assuming it failed** — several of these turned out to be false-negative UI errors
  where commissioning had actually succeeded.
