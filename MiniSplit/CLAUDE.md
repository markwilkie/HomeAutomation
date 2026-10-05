# MiniSplit

## IR / Device B command paths
- IR frames go out from three tasks: `command_task` (direct HA-triggered writes — power, setpoint, mode, Fresh Air — with dedup against fresh Tuya status), `followme_task`'s 3-minute heartbeat, and `sync_task`'s 5-minute poll, which corrects the setpoint when the Desired Setpoint diverges from Tuya's `temp_set_f` by more than 1°F (re-added 2026-09-09, tolerant this time — see `main.c`'s comment on `setpoint_mismatch_f`) and, since 2026-10-04, reconciles mode (`reconcile_system_mode()` — Desired adopts an out-of-band change from the remote/app, otherwise Desired's mode is resent; see the comment above `MODE_MISMATCH_POLLS`).
- Every full-state frame sets Power On unless told otherwise, and with the unit off its mode byte falls back to Auto. So nothing except an explicit power/mode command may send a frame while the unit is off — the Follow-Me heartbeat and setpoint correction were doing exactly that until 2026-10-04 (unit turning itself back on in Auto). Keep that guard on any new IR send path.
- Two ESP32-C6 boards share this PC's USB ports and the same VID/PID: this bridge (MAC `58:8c:81:5d:89:04`) and `../ThreadRouter` (MAC `…5d:89:1c`). COM numbers move. Before flashing, confirm the MAC (`esptool chip_id`/`read_flash` prints it) — on 2026-10-04 the router was overwritten with this firmware by trusting the COM port.
- Fresh Air's IR encoding is resolved (2026-09-10, see `IR_PROTOCOL_REFERENCE.md`'s "Known gaps") — `state[12]` bit `0x01` of the **Type 2** companion frame (not Type 1, which is why earlier attempts missed it, and not detectable from ms-resolution raw-pin captures, which is why the "byte-identical at every level checked" conclusion in the previous version of this note was itself wrong — see `feedback-no-unverified-dead-ends` memory). Wired into `transmit_ir_state_frame()` via `effective_fresh_air()`. Since 2026-10-04 it's also its own HA control: Matter endpoint 11 (On/Off Plug-in Unit), `switch.minisplit_fresh_air`, flashed 2026-10-04. **Still not confirmed against the real unit** — toggle it and check Tuya's `fresh_air_valve` follows before treating this as fully closed.

## Home Assistant / Climate Rules
- The Sonoff (SNZB-02P) Zigbee temp/humidity sensor is the source of truth for room temperature (relayed into firmware via HA for Follow-Me — see PLAN.md Milestone 3). Apply calibration offsets to the sensor reading, never to the setpoint. The BME280 mentioned in older commits/docs was removed 2026-09-07 — it was never actually wired to the board.
- Do not add smoothing/filtering to Tuya-reported temperature; treat Tuya as a command sink only.
- After changing climate automations, verify by counting setpoint reversals over a comparable window and report before/after numbers.

## Deploying config changes to the live box
- `/mnt/data/appdata/homeassistant/config` on `wyse` (bind-mounted into the `homeassistant` container as `/config`) is NOT a git checkout — there is no `git pull` path to sync it.
- Before overwriting a file there, diff it against the live copy first (`docker exec homeassistant cat /config/<file>` vs the repo version). The live file can have uncommitted edits of its own; overwriting blind can silently clobber them.
- Deploy by `scp`-ing the file to the host, then `docker cp` into the container (e.g. `docker cp /tmp/configuration.yaml homeassistant:/config/configuration.yaml`).
- Sanity-check the YAML parses before reloading/restarting.
