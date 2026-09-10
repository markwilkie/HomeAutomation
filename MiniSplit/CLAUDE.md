# MiniSplit

## IR / Device B command paths
- IR frames go out from exactly two places: `command_task` (direct HA-triggered writes, with dedup against fresh Tuya status) and `followme_task`'s 3-minute heartbeat. `sync_task`'s 5-minute poll also sends a correction when the Desired Setpoint diverges from Tuya's `temp_set_f` by more than 1°F (re-added 2026-09-09, tolerant this time — see `main.c`'s comment on `setpoint_mismatch_f` for why exact-match correction was previously removed).
- Fresh Air's IR encoding is resolved (2026-09-10, see `IR_PROTOCOL_REFERENCE.md`'s "Known gaps") — `state[12]` bit `0x01` of the **Type 2** companion frame (not Type 1, which is why earlier attempts missed it, and not detectable from ms-resolution raw-pin captures, which is why the "byte-identical at every level checked" conclusion in the previous version of this note was itself wrong — see `feedback-no-unverified-dead-ends` memory). Wired into `transmit_ir_state_frame()` from `status->fresh_air_valve`. Compiled and building clean as of 2026-09-10; **not yet flashed/confirmed against the real unit** — verify end-to-end before treating this as fully closed.

## Home Assistant / Climate Rules
- The Sonoff (SNZB-02P) Zigbee temp/humidity sensor is the source of truth for room temperature (relayed into firmware via HA for Follow-Me — see PLAN.md Milestone 3). Apply calibration offsets to the sensor reading, never to the setpoint. The BME280 mentioned in older commits/docs was removed 2026-09-07 — it was never actually wired to the board.
- Do not add smoothing/filtering to Tuya-reported temperature; treat Tuya as a command sink only.
- After changing climate automations, verify by counting setpoint reversals over a comparable window and report before/after numbers.

## Deploying config changes to the live box
- `/mnt/data/appdata/homeassistant/config` on `wyse` (bind-mounted into the `homeassistant` container as `/config`) is NOT a git checkout — there is no `git pull` path to sync it.
- Before overwriting a file there, diff it against the live copy first (`docker exec homeassistant cat /config/<file>` vs the repo version). The live file can have uncommitted edits of its own; overwriting blind can silently clobber them.
- Deploy by `scp`-ing the file to the host, then `docker cp` into the container (e.g. `docker cp /tmp/configuration.yaml homeassistant:/config/configuration.yaml`).
- Sanity-check the YAML parses before reloading/restarting.
