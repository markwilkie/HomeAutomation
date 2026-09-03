# MiniSplit

## Home Assistant / Climate Rules
- BME280 is the source of truth for room temperature. Apply calibration offsets to the sensor reading, never to the setpoint.
- Do not add smoothing/filtering to Tuya-reported temperature; treat Tuya as a command sink only.
- After changing climate automations, verify by counting setpoint reversals over a comparable window and report before/after numbers.

## Deploying config changes to the live box
- `/mnt/data/appdata/homeassistant/config` on `wyse` (bind-mounted into the `homeassistant` container as `/config`) is NOT a git checkout — there is no `git pull` path to sync it.
- Before overwriting a file there, diff it against the live copy first (`docker exec homeassistant cat /config/<file>` vs the repo version). The live file can have uncommitted edits of its own; overwriting blind can silently clobber them.
- Deploy by `scp`-ing the file to the host, then `docker cp` into the container (e.g. `docker cp /tmp/configuration.yaml homeassistant:/config/configuration.yaml`).
- Sanity-check the YAML parses before reloading/restarting.
