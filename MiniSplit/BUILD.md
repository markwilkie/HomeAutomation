# Build & Flash Guide — MiniSplit Matter Bridge

This is the authoritative, verified build procedure for this project. It captures the
exact toolchain versions, configuration, and gotchas discovered while getting the
firmware to compile and run on real hardware. If you only read one build doc, read this one.

## TL;DR — verified working build

**This machine's ESP-IDF is installed via EIM (Espressif Installation Manager), not a bare
`git clone` + `export.ps1`.** EIM lays things out differently than the generic ESP-IDF docs
describe — see "EIM vs. plain ESP-IDF" below before you go looking for `export.ps1`.

```powershell
# From a PowerShell prompt (Windows)

# 1. Activate the EIM-managed ESP-IDF 5.4.1 environment (NOT export.ps1 — see below)
. C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1

# 2. Work around the Windows path-length limit for the component cache
$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"

# 3. From the project root
cd C:\Users\Administrator\Documents\GitHub\HomeAutomation\MiniSplit

# 4. Target is ESP32-C6 (RISC-V). Only needed once, or after fullclean.
#    (Usually not needed -- the committed sdkconfig already targets esp32c6.)
idf.py set-target esp32c6

# 5. Build
idf.py build

# 6. Flash (adjust COM port -- check Device Manager, don't assume COM6; see below)
idf.py -p COM4 flash

# 7. Monitor: idf.py monitor requires an interactive TTY and will fail in any
#    non-interactive/automated shell ("Monitor requires standard input to be
#    attached to TTY"). From an actual interactive PowerShell window it's fine:
#    idf.py -p COM4 monitor
#    From automation (no TTY), read the port directly instead -- see
#    "Reading serial output without a TTY" below.
```

## EIM vs. plain ESP-IDF -- read this first

There can be **two different, non-interchangeable** ESP-IDF installs on a Windows machine:

1. **EIM (Espressif Installation Manager)** — installed via `winget install Espressif.EIM-CLI`
   then `eim install -i v5.4.1`. This is what's actually set up and working on this machine.
   - Framework source: `C:\esp\v5.4.1\esp-idf`
   - Toolchain + Python venv: under `C:\Espressif\tools\` (e.g.
     `C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe`)
   - Activation script: `C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1`
     (**not** `export.ps1` — EIM's Python venv lives outside the
     `%USERPROFILE%\.espressif` path ESP-IDF's own `export.ps1`/`install.sh` expect, so
     sourcing `export.ps1` directly creates a *second, separate* venv under
     `%USERPROFILE%\.espressif` that the project's existing `build/` cache was never
     configured with)
   - `build.ps1` in this repo already knows this and auto-activates the EIM profile script
     if `idf.py` isn't on PATH — prefer running that over reinventing activation.
2. **Plain upstream ESP-IDF** (`install.sh`/`install.ps1` from a `git clone` of the esp-idf
   repo, or the "offline installer"), which uses `export.ps1`/`export.sh` and puts its venv
   under `%USERPROFILE%\.espressif`.

**Do not run `install.sh` or `install.bat` "just to be safe" if a build already works** — it
creates a second, independent Python venv/toolchain set that doesn't match the one the
existing `build/` directory's CMake cache was configured against. Mixing them produces:
`'...\.espressif\python_env\...\python.exe' is currently active in the environment while the
project was configured with 'C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe'. Run
'idf.py fullclean' to start again.` If you see that error, activate the *other* environment
(check which one matches the error message) rather than fullclean-ing a working configured
build. Also note: `install.sh`/`install.bat` **fail outright under MSYS/Git-Bash** ("MSys/Mingw
is not supported") — they must run from native PowerShell or cmd.exe.

Also note: `idf.py monitor` needs a real interactive terminal — it fails immediately
("Monitor requires standard input to be attached to TTY") when run from any automated/
non-interactive shell (CI, an agent's tool-call shell, etc.). See "Reading serial output
without a TTY" below for a workaround in that situation.

## Verified environment

| Item | Value | Notes |
|------|-------|-------|
| Host OS | Windows | PowerShell (native `powershell.exe`, not Git-Bash/MSYS) |
| ESP-IDF | **v5.4.1** | Installed via EIM. Framework at `C:\esp\v5.4.1\esp-idf`, tools/venv at `C:\Espressif\tools\`. **Do NOT use 6.x.** |
| Toolchain | gcc (riscv32) | Selected automatically for the C6 target |
| Target | **esp32c6** | RISC-V architecture |
| esp-matter | 1.5.0 | Pulled by the IDF Component Manager (`src/idf_component.yml`) |
| cJSON | espressif/cjson ^1.7.19 | Component Manager |
| Flash port | **COM4** on this machine (native USB-Serial-JTAG, VID 303A/PID 1001) — **not** COM6; always verify against Device Manager / `Get-CimInstance Win32_PnPEntity \| ? Name -match 'COM\d+'` rather than assuming |

## Prerequisites

1. **ESP-IDF v5.4.1 via EIM** (verified working setup on this machine — see "EIM vs. plain
   ESP-IDF" above for the exact activation path).
   - Alternative: the offline/Windows installer, or `git clone -b v5.4.1 --recursive
     https://github.com/espressif/esp-idf.git` then `install.ps1 esp32c6` + `export.ps1`. Pick
     **one** approach and stick with it — don't layer a second install on top of a working one.
   - **Do not use ESP-IDF 6.x** (see below).
2. **ESP-Matter** — no manual clone needed. It is pulled automatically as a managed
   component (`espressif/esp_matter` in `src/idf_component.yml`). The first build downloads
   it into `managed_components/`.
3. **A short component-cache path** — create `C:\icc` (or any short path). See
   "Windows path-length limit" below.
4. **USB drivers** for the ESP32-C6 dev board (native USB-Serial-JTAG; shows as "USB Serial
   Device" in Device Manager, VID 303A/PID 1001 — this is Espressif's own VID, not a
   CP210x/CH340 adapter).
5. **Secrets** — copy `include/secrets.example.h` to `include/secrets.h` and fill in
   your Tuya credentials. (`secrets.h` is git-ignored.) No WiFi credentials needed —
   this device joins over Thread, provisioned during Matter commissioning.

## Why 5.x, not 6.x

ESP-IDF **6.x removed the bundled `json` component**, which several managed dependencies
(and the legacy build wiring) still expect. On 6.x the build fails resolving the `json`
component. We pin to **5.4.1**, where the component still exists. (A stub `components/json`
wrapper was added as a belt-and-suspenders measure, but the supported path is staying on 5.4.1.)

## Target: ESP32-C6 (RISC-V)

The project is configured for **esp32c6**:

```powershell
idf.py set-target esp32c6
```

`set-target` is only required the first time, or after `idf.py fullclean` / deleting
`sdkconfig`. The committed `sdkconfig` already targets `esp32c6`
(`CONFIG_IDF_TARGET="esp32c6"`).

## Partition table — 3 MB factory app

The Matter + BLE image is large. A standard 2 MB factory app partition **overflows**, so
the factory `app` partition was bumped from `0x200000` (2 MB) to **`0x300000` (3 MB)**.

`partitions.csv`:

```csv
# Name,   Type, SubType, Offset,   Size,      Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x300000,
```

Enabled via `sdkconfig`:

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

> If you ever see a "Image size ... doesn't fit partition" / app-too-big error, this
> partition size is the first thing to check.

## Bluetooth: NimBLE only (Matter commissioning)

Matter commissioning over BLE on the C6 uses the **NimBLE** host. **Bluedroid must be
disabled** — enabling it caused link errors in esp-matter's Bluedroid `BLEManager`
(`esp_ble_gap_*` unresolved).

`sdkconfig` (already set):

```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
# CONFIG_BT_BLUEDROID_ENABLED is not set
```

When working, the serial log shows `chip[DL]: CHIPoBLE advertising started` plus the
`SetupQRCode` / manual pairing code.

## Windows path-length limit (component cache)

The IDF Component Manager extracts dependencies with very long paths, which hits the
Windows `MAX_PATH` (260-char) limit and fails to download/extract `esp_matter`. Point the
cache at a short path **before building**:

```powershell
$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"
```

(Set it for the session, or persist it as a user environment variable.)

## Flashing notes

```powershell
idf.py -p COM4 flash monitor
```

- **Verify the actual COM port first** — don't assume COM6. On this machine it's COM4.
  Check Device Manager, or from PowerShell:
  `Get-CimInstance -ClassName Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' }`
- **Close any open serial monitor before `flash` or `erase-flash`** — an open monitor
  holds the port and causes port-contention errors.
- To start clean: `idf.py -p COM4 erase-flash` then `idf.py -p COM4 flash`.
- On boot, a healthy image logs the Matter endpoint init:
  `thermostat_ep=1 temp_sensor_ep=2 humidity_sensor_ep=3 outdoor_temp_sensor_ep=4 compressor_ep=5 compressor_running_ep=6`.
  (If a BME280 is fitted, also `Environment sensor task started` and periodic `BME280:` lines.)
- **The device retains its Thread/Matter commissioning across reflashes** (it's stored in the
  `nvs` partition, which isn't touched by a normal `flash`) — it rejoins the mesh automatically
  after a reflash without needing to be re-paired at the BLE/commissioning level. However, if
  the firmware's *endpoint composition* changed (endpoints added/removed/changed device type),
  Home Assistant's Matter integration won't pick that up on its own — see
  [SENSORS.md](SENSORS.md)'s re-commissioning note. You'll need to remove and re-add the device
  in Home Assistant to see the new/changed entities.

## Reading serial output without a TTY

`idf.py monitor` requires an interactive terminal and will refuse to run otherwise. If you need
to capture boot/runtime logs from a non-interactive context (an automation shell, an agent's
tool-call shell, etc.), read the COM port directly instead, e.g. from PowerShell:

```powershell
$port = New-Object System.IO.Ports.SerialPort COM4,115200,None,8,one
$port.ReadTimeout = 3000
$port.Open()
$sb = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    try { $sb.Append($port.ReadExisting()) | Out-Null } catch {}
    Start-Sleep -Milliseconds 200
}
$port.Close()
$sb.ToString() | Out-File "path\to\capture.log" -Encoding utf8
```

Note: opening the port this way does **not** reliably reset the board (unlike `idf.py flash`,
which resets via RTS at the end, or `idf.py monitor`, which esptool's monitor also resets on
attach) — toggling `RtsEnable`/`DtrEnable` manually was tried and did not trigger a reset on
this board's native USB-Serial-JTAG interface. If you need a fresh boot log specifically, catch
it right after a `flash` (which does reset the board) rather than trying to force a reset from
a separate later connection.

## Clean rebuild

```powershell
idf.py fullclean
idf.py set-target esp32c6   # re-required after fullclean
idf.py build
```

## Known build gotchas (already fixed in-tree)

- `matter_device` must be **C++** (`matter_device.cpp`, not `.c`) — esp-matter is a C++ API.
  Headers consumed from C need `extern "C"` guards and `#include "esp_err.h"`.
- `-Wno-format` is applied to suppress a `uint32_t` format warning under gcc 14.x.
- `tuya_client.c` uses the non-`_ret` mbedtls SHA-256 functions (the `*_ret` variants are
  deprecated/removed).

## Reference

- `MATTER_SDK_SETUP.md` — deeper SDK/menuconfig background (some examples use other targets).
- `config/SECURE_STORAGE.md` — credential storage and NVS.
- `partitions.csv` — partition layout.
