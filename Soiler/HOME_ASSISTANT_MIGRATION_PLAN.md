# Soiler: Migration Plan (SmartThings -> Home Assistant)

## Implementation status

- **Phases 1-3: done in firmware, not yet flashed/verified on hardware.** The user asked to
  proceed destructively (git can restore if needed), so this was done as one pass instead of
  staged behind the old hub path:
  - **MQTT lives in its own class**, `SoilerGateway/MyMqtt.h`/`.cpp` (not bolted onto `MyWifi`,
    which now only does WiFi connect + OTA). `MyMqtt` owns a `PubSubClient` connection to
    `192.168.15.24:1883` (`MQTT_SERVER`), with a Last Will on `soiler/gateway/status` (`offline`,
    retained) and an `online` publish on connect. `loop()` is rate-limited
    (`MQTT_RECONNECT_INTERVAL_MS`) so a down broker can't block LoRa packet reception.
  - **Per-sensor state publishing**: `publishSoilState()` in `SoilerGateway.ino` builds a JSON
    payload (`soil_moisture`, `vcc_voltage`, `rssi`, `firmware_version`, `heap_frag`) and
    publishes it to `soiler/<id>/state` on every LoRa packet received. The static HA-side
    `mqtt: sensor:` YAML from the draft config below still needs to actually be added to
    `Wyse5070DebSetup/homeassistant-config`.
  - **SmartThings/Rachio coupling fully removed from firmware**: `MyWifi`'s HTTP server/client
    (`/handshake` endpoint, `sendPostMessage`/`sendGetMessage`/`sendPutMessage`/`readJsonFile`),
    the epoch sync (`syncEpoch`, `currentTime`, `millisAtEpoch`), the `Preferences`-based hub
    IP/port caching, the `config.json` fetch, and the direct Rachio API call
    (`putSoilMoisture`/`updateSoilMoisture`) are all deleted. The `SmartThings_Edge/EdgeDrivers/
    Soil-Driver` Lua driver itself is now orphaned (nothing talks to it) but hasn't been removed
    from the SmartThings hub yet - that's a manual step on the hub, not a firmware change.
  - `Soiler/SoilerGateway/config.json` was **kept** (not deleted) even though firmware no longer
    fetches it - it's the only record of the real sensor-id -> Rachio zone-GUID mapping, and
    needs to be hand-transcribed into the HA automation's `zone_map` (the draft below uses
    placeholder `switch.*` entity ids, not the real zone GUIDs from that file).
  - Needs the `PubSubClient` library installed (Arduino Library Manager) before it'll compile,
    and an actual flash + serial-monitor check that it connects/publishes `online`, that LoRa
    reception is unaffected, and that soil readings actually reach the broker - none of which
    could be verified without the hardware. **Irrigation zone updates will not happen at all
    until the HA automation is actually added**, since the firmware-side Rachio call is gone.
- Phase 4 (optional client-side calibration downlink): not started.

## Current architecture

Three tiers:

1. **SoilerClient** ([SoilerClient/SoilerClient.ino](SoilerClient/SoilerClient.ino)) - battery
   Feather 32u4 + LoRa. Reads a soil probe, sleeps ~1hr, wakes and transmits a raw 6-byte
   packet (`SOH, boardId, perc, voltage, crc, EOT`). Pure transmit, never listens.
2. **SoilerGateway** ([SoilerGateway/SoilerGateway.ino](SoilerGateway/SoilerGateway.ino)) -
   Heltec ESP32. Receives LoRa packets, decodes them, then (a) reports up to a "hub" over
   plain HTTP via [MyWifi.cpp](SoilerGateway/MyWifi.cpp), and (b) calls the Rachio cloud API
   directly to adjust irrigation zones, using a hardcoded API key (`properties.h`) and a zone
   map fetched from a GitHub raw URL (`config.json`).
3. **The "hub"** - `SmartThings_Edge/EdgeDrivers/Soil-Driver`, a Lua LAN Edge Driver running on
   a SmartThings hub. This is what all the `/registerDevice`, `/epoch`, `/soilgw`, `/handshake`
   traffic in `MyWifi.cpp` talks to.

### Why the handshake is so convoluted

The `hubSoilSWPort`/epoch/registerDevice dance isn't general IoT practice - it's a workaround
for a SmartThings Edge Driver limitation: LAN drivers `bind('*', 0)` (ephemeral OS-assigned
port, see `_myserver.lua`), so the port changes on every hub reboot/driver reload. That forces:

- The hub to POST its current IP:port back to the ESP32 whenever it comes back up
  (`handshakeNow` in `_myclient.lua`)
- The ESP32 to cache that in flash (`Preferences`) since it can't discover it any other way
- A manual epoch exchange, since there's no shared clock source between the two

None of this is a HA requirement - it's SmartThings-specific plumbing that goes away entirely
once the SmartThings hub is out of the picture, rather than being reimplemented against HA.

## Target: MQTT, not Matter

MQTT is the right transport here (not Matter, which is the heavy commissioning-grade path used
for MiniSplit - overkill for a device that's just translating a custom LoRa protocol). HA's
two-way pattern via MQTT:

| Need | Current (SmartThings) | HA/MQTT equivalent |
|---|---|---|
| "Are you alive?" | epoch polling, `STALE_THRESHOLD`, `handshakeRequired` flag | **LWT (Last Will)** - device connects with a will message (`soiler/gateway/status` -> `offline`), publishes `online` once connected. Broker enforces it even on ungraceful disconnect. |
| "Register yourself" | `/registerDevice`, per-device port negotiation | Not needed if entities are declared statically in YAML (see decision below) instead of MQTT Discovery. |
| Telemetry (device->HA) | `POST /soil`, `POST /soilgw` | `publish()` to a state topic (`soiler/<id>/state`) whenever a LoRa packet arrives. |
| Commands (HA->device) | didn't really exist | HA `publish()`s to a command topic; device `subscribe()`s and acks by echoing its new state back. |
| Shared clock | epoch GET/response | Device does its own SNTP (`configTime()`); no exchange needed. |

The existing broker at `192.168.15.24:1883` (already used by `esp8266/mqttclient`) is reused;
no new infrastructure required.

## Config-as-code decisions

Two forks worth deciding deliberately, since config-as-code is a hard requirement here (the
existing `homeassistant-config` is already fully hand-maintained YAML, not UI-managed):

1. **MQTT Discovery vs. static YAML entities -> static YAML.** Discovery means the entity is
   defined by a retained broker message the firmware publishes at runtime, not a line in git.
   With only a handful of sensor boards, the auto-onboarding benefit of discovery doesn't
   outweigh losing a diffable, reviewable definition. Declare each MQTT sensor explicitly in
   YAML instead.
2. **One automation per zone vs. one automation + a mapping table -> mapping table.** The old
   `sensorZones` dictionary (plus linked-zone chaining, e.g. brick<->walkway) is data, not
   logic. One automation with the sensor->zone(s) mapping and calibration window as a
   `variables:` block, single source of truth in git, replaces `config.json` fetched over HTTP
   at boot.

## What stays, what moves, what dies

**Stays untouched:** `SoilerClient.ino` and the LoRa packet format (no WiFi/HA awareness
needed, preserves battery life). OLED display, OTA, LED blink, CRC/validity checks in the
gateway.

**Moves out of firmware, into HA:** Rachio zone mapping (`sensorZones`, `config.json` fetch,
`putSoilMoisture`, the hardcoded API key in `properties.h`). HA's built-in `rachio` integration
already holds the Rachio auth token and exposes `rachio.set_zone_moisture_percent` (field:
`percent`, 0-100, targets the zone's `switch` entity) - no zone GUIDs or hand-built JSON needed.

**Dies:** the entire SmartThings `Soil-Driver` Edge Driver, `MyWifi.cpp`'s HTTP server/client,
`Preferences` hub-caching, `syncEpoch`/`millisAtEpoch`.

## Phased plan

1. **MQTT plumbing.** Add `PubSubClient` to the gateway, connect to `192.168.15.24` with LWT
   (`soiler/gateway/status`), reconnect loop mirroring `esp8266/mqttclient`'s pattern. Replace
   `wifi.listen()`'s handshake-server role with `mqttClient.loop()`.
2. **State publishing.** On each LoRa packet, publish moisture/voltage/rssi to
   `soiler/<id>/state`. Declare the corresponding `mqtt: sensor:` entities statically in HA
   YAML (per the config-as-code decision above) - this replaces `postSoil()`/`postSoilGW()`.
3. **Rip out Rachio + SmartThings coupling.** Delete `putSoilMoisture`, `sensorZones`,
   `config.json` fetch, `properties.h` key, epoch code from the gateway firmware. Retire the
   Soil-Driver from the SmartThings hub. Add the HA automation below.
4. **(Optional/stretch)** True downlink to `SoilerClient` for remote calibration. It currently
   never listens (calibration is done over wired serial). Adding a listen window costs battery
   life - worth a separate call, not required for this migration.

## Draft HA config

```yaml
# --- static entities, e.g. mqtt.yaml or a new file included from configuration.yaml ---
mqtt:
  sensor:
    - name: "Soil Moisture 15"
      unique_id: soil_15_moisture
      state_topic: "soiler/15/state"
      value_template: "{{ value_json.soil_moisture }}"
      unit_of_measurement: "%"
      availability_topic: "soiler/gateway/status"
      device:
        identifiers: "soil_15"
        name: "Soil Sensor 15 (Candy Tuffs)"
        via_device: soiler_gateway

    - name: "Soil Moisture 15 Voltage"
      unique_id: soil_15_voltage
      state_topic: "soiler/15/state"
      value_template: "{{ value_json.vcc_voltage }}"
      unit_of_measurement: "V"
      device_class: voltage
      availability_topic: "soiler/gateway/status"
      device:
        identifiers: "soil_15"

    # ...repeat per sensor id (16, 17, 19, 20...) - same shape every time,
    # this is the part that stays boring and explicit on purpose.

# --- automation.yaml ---
automation:
  - alias: "Soiler: update Rachio zone moisture from soil sensors"
    id: soiler_rachio_moisture_sync
    variables:
      # sensor entity -> [target zone switch entities], and this sensor's calibration window
      zone_map:
        sensor.soil_15_moisture:
          targets: ["switch.candy_tuffs"]
          dry: 20
          wet: 60
        sensor.soil_16_moisture:
          targets: ["switch.flowers_by_brick", "switch.flowers_walkway"]  # linked zones
          dry: 20
          wet: 60
        sensor.soil_17_moisture:
          targets: ["switch.flowers_side_of_house"]
          dry: 20
          wet: 60
        sensor.soil_19_moisture:
          targets: ["switch.garden"]
          dry: 15
          wet: 55
    trigger:
      - platform: state
        entity_id:
          - sensor.soil_15_moisture
          - sensor.soil_16_moisture
          - sensor.soil_17_moisture
          - sensor.soil_19_moisture
    condition:
      - condition: template
        value_template: "{{ trigger.entity_id in zone_map }}"
      - condition: template
        value_template: "{{ trigger.to_state.state not in ['unknown','unavailable'] }}"
    action:
      - variables:
          cfg: "{{ zone_map[trigger.entity_id] }}"
          raw: "{{ trigger.to_state.state | float }}"
          pct: >
            {{ (((raw - cfg.dry) / (cfg.wet - cfg.dry) * 100) | round(0) | max(0) | min(100)) }}
      - repeat:
          for_each: "{{ cfg.targets }}"
          sequence:
            - action: rachio.set_zone_moisture_percent
              target:
                entity_id: "{{ repeat.item }}"
              data:
                percent: "{{ pct }}"
```

The `pct` calculation is a direct port of the old firmware's `(perc-20)/(60.0-20.0)` clamp -
done once, in one place, per-sensor-configurable instead of a single hardcoded `20`/`60` for
every zone.

**Open item:** the real `switch.<zone>` entity ids need to come from the Rachio integration
once it's set up in HA (Developer Tools -> Actions -> `rachio.set_zone_moisture_percent` ->
entity picker, or Settings -> Entities filtered by the Rachio integration) - the ids above are
placeholders.

## Open question

Where this file should live relative to the existing `automations.yaml`/`configuration.yaml`
split in `Wyse5070DebSetup/homeassistant-config` - e.g. append to the existing files, or a new
`packages/soiler.yaml` - not yet decided.
