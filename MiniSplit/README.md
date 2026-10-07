# MiniSplit Matter Bridge

An ESP32-C6 that makes a Pioneer/TCL mini-split (Tuya cloud module, TCL112
IR protocol) a local Matter-over-Thread device in Home Assistant. Commands go
to the unit over **IR**; the unit's state comes back from the **Tuya cloud
API**; the room temperature the unit regulates on comes from a Zigbee sensor
via **Follow-Me**. Current as of 2026-10-05 — older design/phase docs are in
[docs/archive/](docs/archive/).

## How it fits together

```
Zigbee temp sensor --Z2M--> HA --Matter/Thread--> bridge --IR--> mini-split
                                ^                   |              |
            HA automations -----+                   +<--Tuya API---+ (status poll)
```

**Firmware tasks** (`src/main.c`):
- `command_task` — turns HA writes (setpoint, mode, power, Fresh Air) into IR
  frames, after a fresh Tuya read and a dedup check.
- `sync_task` — polls Tuya every 3 min, mirrors state to Matter, and
  reconciles setpoint and mode the same way: a change made on the remote/app
  is adopted into Desired, anything else (a missed IR update) gets Desired
  resent. A remote mode change keeps the schedule's temperature. Then it
  sends the Follow-Me heartbeat (the room temperature) from the status it
  just polled, while the unit is on. With Follow-Me engaged the unit
  regulates on that reading, not its own ~3–4°F-warm sensor.
- Pure logic (mode mapping, IR frame building, mode-reconcile decision, Fresh
  Air hold) lives in `src/control_logic.c` and is unit-tested on a host.

Every IR frame is a full state frame. What is and isn't carried from the
unit's live state is listed in
[IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md) ("Fields not
preserved"). No frame other than an explicit power/mode command is ever sent
while the unit is off.

**Matter endpoints / HA entities** (endpoint numbers are part of HA's
unique IDs — only ever add endpoints at the end):

| EP | Role | HA entity |
|---|---|---|
| 1 | Unit's actual state (read-only mirror of Tuya) | `climate.mini_split_ac_bridge_thermostat_1` |
| 6 | **Desired** setpoint + mode (what HA writes) | `climate.bedroom_mini_split_ac_bridge_thermostat_6` |
| 10 | Follow-Me input (HA relays the room temp here) | `climate.bedroom_mini_split_ac_bridge_thermostat_10` |
| 11 | Fresh Air switch | `switch.minisplit_fresh_air` |
| 3/4 | Compressor demand % / running | `sensor..._humidity_3`, `binary_sensor..._occupancy_4` |
| 7/8 | Outage active / reason code | `binary_sensor..._occupancy_7`, `sensor..._humidity_8` |
| 9 | Thread RSSI | `sensor..._humidity_9` (decoded: `sensor.minisplit_thread_rssi`) |

**Home Assistant side** (`../Wyse5070DebSetup/homeassistant-config/`):
- *Setpoint automation* — strategy `direct` (Desired = the day/night target
  for the current mode) while Follow-Me is engaged, falling back to an offset
  controller otherwise. Targets: one seasonal target from Winter/Summer
  day/night settings, blended by the 7-day average outdoor temperature (45°F
  and below = winter, 70°F and above = summer); Heat aims at it, Cool at
  target + 1°F. Manual overrides are logged to `/config/minisplit/overrides.csv`
  on wyse, to tune the seasonal numbers from real choices.
  A setpoint set by hand (HA card, or the remote via the firmware) is a
  *manual override*: the schedule leaves it until the next 07:00/19:00, or
  until the mode changes or **Resume schedule** is pressed.
- *Follow-Me relay* — forwards the room sensor to EP10 when its whole-°C
  value changes.
- *Shadow mode decision* — computes Off/Cool/Heat but never touches the unit;
  mode selection is still manual. See [PLAN_AUTO_MODE.md](PLAN_AUTO_MODE.md).
- Dashboard: sidebar → **MiniSplit**.

## Common tasks

| Task | How |
|---|---|
| Build | see [BUILD.md](BUILD.md) (ESP-IDF 5.4.1, plain-upstream install) |
| Flash | `.\flash.ps1` — refuses any board whose MAC isn't the bridge's (`58:8C:81:5D:89:04`) |
| Unit tests | `sh test/host/run.sh` (gcc, or the gcc Docker image — e.g. on wyse) |
| Deploy HA config | `../Wyse5070DebSetup/homeassistant-config/deploy.sh` |
| Edit HA storage (areas, labels, dashboard) | `../Wyse5070DebSetup/homeassistant-config/patch-storage.sh` |
| Is Follow-Me working? | dashboard "Follow-Me OK", or compare *Unit (actual)* temp with *Room* — within ~1°F when engaged |

## Known limitations

- No OTA: flashing means bringing the board to the PC (USB).
- Mode selection (Heat/Cool/Off) is manual; the shadow automation only
  recommends.
- The unit's IR setpoint floor is 16°C (61°F) and it works in 0.5°C steps
  (the half-degree bit is `state[12]` `0x04`). Until 2026-10-05 the firmware
  always sent that bit, so whole-degree setpoints (68, 70, 72°F…) read back
  1°F higher.
- Health, swing, eco, turbo, quiet and remote timers are reset by any
  bridge-sent frame (by design — see IR_PROTOCOL_REFERENCE.md).
- Tuya client credentials are compiled into the firmware (`include/secrets.h`,
  git-ignored) and readable from the board's flash. ESP32 flash encryption
  was deliberately **not** enabled (2026-10-05): it burns one-time eFuses,
  makes every reflash harder and risks bricking the board, to protect a
  secret on a device inside the house.

## Docs

- [BUILD.md](BUILD.md) — toolchain, build, flash, serial logs
- [COMMISSIONING_GUIDE.md](COMMISSIONING_GUIDE.md) — Matter/Thread commissioning
- [IR_PROTOCOL_REFERENCE.md](IR_PROTOCOL_REFERENCE.md) — TCL112 frame format, Follow-Me behavior
- [TUYA_DP_REFERENCE.md](TUYA_DP_REFERENCE.md) — Tuya data points
- [PLAN.md](PLAN.md) — local IR + Follow-Me plan (implemented; code comments cite its milestones)
- [PLAN_AUTO_MODE.md](PLAN_AUTO_MODE.md) — automatic mode selection (Phase 0, shadow only)
- [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md), [WINDOWS_TOOLCHAIN_SETUP.md](WINDOWS_TOOLCHAIN_SETUP.md) — environment setup
