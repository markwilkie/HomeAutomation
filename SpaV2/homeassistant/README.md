# SpaV2: Home Assistant side

| File | What it is |
|---|---|
| `rename_entities.py` | After commissioning, gives the Spa Monitor's Matter entities stable ids (by endpoint number) and hides the raw ones. |
| `../../Wyse5070DebSetup/homeassistant-config/packages/spa.yaml` | The HA package, deployed with the rest of the HA config: templates (pH, ORP, chlorine Low/Good/High, SmarterSpa state, pump current, electrode hours, calibration status), threshold helpers, 24 h stats, calibration date stamps, alerts. |
| `spa_dashboard.yaml` + `create_dashboard.py` | The **Spa** sidebar dashboard (`/spa-dashboard`): now, 24 h graphs, calibration buttons and thresholds. |

Not deployed yet: these need the commissioned device. Everything below runs on **wyse**.

## 1. Commission the board

**First time only: this board was the ThreadRouter** (Matter node 42, "Thread Router" in HA), and its NVS
still holds that commissioning. `flash.ps1` never erases NVS, so:

1. Remove the old node from HA: Settings → Devices & services → Matter → *Thread Router* → ⋮ → Delete
   device.
2. Erase once: `idf.py -p COM4 erase-flash` (in `SpaV2/firmware`, with ESP-IDF 5.4.1 activated), then
   `.\flash.ps1`. Later flashes keep NVS (commissioning, calibration, electrode hours).

Then read the pairing code from the serial log (`Manual pairing code: [...]`; the example
commissionable data gives `34970112332`, same as MiniSplit) and follow `Wyse5070DebSetup/README.md` →
"Adding a new Matter-over-Thread device". Don't use the phone (see `ThreadRouter/CLAUDE.md`):

```bash
ssh mwilkie@192.168.15.30 git -C github/HomeAutomation pull
ssh mwilkie@192.168.15.30 github/HomeAutomation/Wyse5070DebSetup/commission-thread-device.sh --check
ssh mwilkie@192.168.15.30 github/HomeAutomation/Wyse5070DebSetup/commission-thread-device.sh 34970112332
```

The board must be in Bluetooth range of wyse. Check: `docker exec otbr ot-ctl router table` lists it,
partition `0x2d2f19a4` (not `0x4d6f48d3`, the SmartThings network).

## 2. Stable entity ids

```bash
cd ~/github/HomeAutomation && git pull
T=$(sed -n 's/^HA_TOKEN=//p' /mnt/data/appdata/homeassistant/.env)
docker exec -i -e HA_TOKEN="$T" homeassistant python3 - < SpaV2/homeassistant/rename_entities.py
```

It must end with `All Spa Monitor entities mapped.` If it reports `NOT FOUND`, the endpoint layout
and `MAP` disagree. Fix `MAP`, not the templates.

## 3. Install the package

The package lives in `Wyse5070DebSetup/homeassistant-config/packages/spa.yaml` and is loaded by
`configuration.yaml`'s `packages: !include_dir_named packages`. Deploy it like any HA config change, with a
restart the first time (new template / input_number entities):

```bash
Wyse5070DebSetup/homeassistant-config/deploy.sh --restart
```

## 4. Dashboard

```bash
ssh mwilkie@192.168.15.30
cd ~/github/HomeAutomation/SpaV2/homeassistant && git pull
T=$(sed -n 's/^HA_TOKEN=//p' /mnt/data/appdata/homeassistant/.env)
docker exec -i -e HA_TOKEN="$T" homeassistant python3 -c "$(cat create_dashboard.py)" < spa_dashboard.yaml
```

Change the dashboard in `spa_dashboard.yaml` and rerun: the script overwrites edits made in HA's editor.

## 5. Verify

- `sensor.spa_water_temperature`, `sensor.spa_ph` and `sensor.spa_orp` show values (pH/ORP only once
  `binary_sensor.spa_readings_live` is on: the pump has run 5 min, or right after a calibration press).
- `sensor.smarterspa_state` matches the panel lights.
- Press **pH 7** with the probe in pH 7 buffer: status goes *Calibrating...* → *OK*, and
  `input_datetime.spa_last_ph_calibration` updates.
- The thresholds show 650 / 800 mV and pH 7.2 / 7.8, seeded on the first start.
