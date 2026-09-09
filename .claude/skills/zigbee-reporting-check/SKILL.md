---
name: zigbee-reporting-check
description: Audit (and optionally fix) Zigbee2MQTT device attribute-reporting configuration on wyse, and walk through pairing a new sensor
---
Host: wyse (192.168.15.30, SSH as `mwilkie`). Containers: `zigbee2mqtt`, `mosquitto`, `homeassistant`.

## Background
A Zigbee device's attribute-reporting policy (how often it pushes updates, and
what change threshold triggers an early report) is **not** controlled by
Z2M's `configuration.yaml` `devices:` block — that block only holds
`friendly_name`/`homeassistant.name`. Reporting lives on the physical
device's own firmware, applied via a ZCL `configureReporting` command sent
over the air. Z2M's local `bridge/devices` cache mirrors what it believes is
configured, but it is not the source of truth and won't reapply drift. A
factory reset or re-pair silently reverts a device to Z2M's built-in
defaults with nothing in git to catch it. See
`Wyse5070DebSetup/README.md` ("Zigbee temp/humidity sensors") for the
original investigation and rationale.

## Expected reporting profile
| Model | Cluster | min interval | max interval | reportable_change | Notes |
|---|---|---|---|---|---|
| SNZB-02P | `msTemperatureMeasurement` | 10s | 3600s | `10` (0.1°C) | Tightened from Z2M's factory default of `100` (1.0°C) |
| SNZB-02P | `msRelativeHumidity` | 10s | 3600s | `100` (1.0%) | Left at factory default — no need identified yet to tighten |

If auditing a device model not in this table, treat Z2M's factory default
(`min 10s / max 3600s / change 100`, i.e. whatever the converter's raw scale
implies as 1.0 unit) as the baseline to compare against, and ask whether it
should be added to this table with a tighter profile.

## Step 0 — pairing a new sensor (skip if auditing existing devices only)
1. `docker ps --filter name=zigbee2mqtt` to confirm the container is up.
2. Open `http://192.168.15.30:8099/`, click "Permit join (all)".
3. Hold the sensor's pairing button ~5s until its LED blinks.
4. Watch the Z2M "Devices" page for it to appear (~10-20s), then set a
   friendly name.
5. Continue to the audit/fix steps below — a freshly paired device is
   exactly the case that needs its reporting tightened.

## Audit
1. Pull the live device list:
   `docker exec mosquitto mosquitto_sub -h localhost -p 1883 -u mwilkie -P <password from zigbee2mqtt's data/configuration.yaml, mqtt.password> -t 'zigbee2mqtt/bridge/devices' -C 1 -W 5`
2. For each device whose model is in the table above, inspect
   `endpoints.1.configured_reportings` and compare `reportable_change` (and
   interval bounds) against the expected profile. Flag any mismatch —
   most commonly a device still sitting on the factory default.
3. Report findings: device name, cluster, current vs. expected
   `reportable_change`.

## Fix (only after confirming with the user which devices to change)
These are battery/sleepy end devices — they only listen for a brief window
right after they check in, so a bind/configureReporting request sent at a
random moment will time out (`Bind ... failed ... timed out after 15000ms`).
Catch it right after a check-in instead of retrying blind:

1. Write the target config to a file (avoids shell-quoting failures across
   nested `ssh`/`docker exec` layers — inline `-m` JSON has silently
   mismatched in the past):
   ```json
   {"id":"<friendly_name>","endpoint":"1","cluster":"<cluster>","attribute":"measuredValue","minimum_report_interval":10,"maximum_report_interval":3600,"reportable_change":<value>}
   ```
   `scp` it to the host, then `docker cp` it into the `mosquitto` container.
2. Block on the device's next check-in, then immediately publish the
   request:
   ```
   docker exec mosquitto sh -c "
     mosquitto_sub -h localhost -p 1883 -u mwilkie -P <password> -t 'zigbee2mqtt/<friendly_name>' -C 1 -W 400 >/dev/null
     mosquitto_pub -h localhost -p 1883 -u mwilkie -P <password> -t 'zigbee2mqtt/bridge/request/device/reporting/configure' -f /tmp/reporting_payload.json
   "
   ```
   Run this in the background — it blocks until the device's next report,
   which can be several minutes.
3. Confirm via `docker logs zigbee2mqtt --since 2m`: look for
   `Configured reporting for '<name>', '<cluster>.measuredValue'` with
   `status":"ok"` in the bridge response. A bind timeout just means it
   missed the wake window — retry.
4. Re-pull `bridge/devices` (per Audit step 1) and confirm
   `configured_reportings` now matches the target.

## Report
Summarize per device: model, cluster(s) checked, prior vs. new
`reportable_change`, and whether it now matches the expected profile table.
If any device's expected profile isn't yet in the table above (new model),
say so explicitly rather than silently leaving it unaudited.
