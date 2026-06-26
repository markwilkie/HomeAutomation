# Build & Flash Guide — MiniSplit Matter Bridge

This is the authoritative, verified build procedure for this project. It captures the
exact toolchain versions, configuration, and gotchas discovered while getting the
firmware to compile and run on real hardware. If you only read one build doc, read this one.

## TL;DR — verified working build

```powershell
# From a PowerShell prompt (Windows)

# 1. Activate ESP-IDF 5.4.1 (NOT 6.x — see "Why 5.x not 6.x" below)
. C:\esp-idf-v5.4.1\export.ps1

# 2. Work around the Windows path-length limit for the component cache
$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"

# 3. From the project root
cd C:\Users\mawilkie\github\HomeAutomation\MiniSplit

# 4. Target is ESP32-C6 (RISC-V). Only needed once, or after fullclean.
idf.py set-target esp32c6

# 5. Build
idf.py build

# 6. Flash + monitor (adjust COM port). Close any open monitor first!
idf.py -p COM6 flash monitor
```

## Verified environment

| Item | Value | Notes |
|------|-------|-------|
| Host OS | Windows | PowerShell (`pwsh`) |
| ESP-IDF | **v5.4.1** | Installed at `C:\esp-idf-v5.4.1`. **Do NOT use 6.x.** |
| Toolchain | gcc (riscv32) | Selected automatically for the C6 target |
| Target | **esp32c6** | RISC-V architecture |
| esp-matter | 1.5.0 | Pulled by the IDF Component Manager (`src/idf_component.yml`) |
| cJSON | espressif/cjson ^1.7.19 | Component Manager |
| Flash port | COM6 | Typical; verify with Device Manager |

## Prerequisites

1. **ESP-IDF v5.4.1** installed to `C:\esp-idf-v5.4.1`.
   - Use the offline/Windows installer or `git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git`
     then run `install.ps1 esp32c6`.
   - **Do not use ESP-IDF 6.x** (see below).
2. **ESP-Matter** — no manual clone needed. It is pulled automatically as a managed
   component (`espressif/esp_matter` in `src/idf_component.yml`). The first build downloads
   it into `managed_components/`.
3. **A short component-cache path** — create `C:\icc` (or any short path). See
   "Windows path-length limit" below.
4. **USB drivers** for the ESP32-C6 dev board (CP210x / native USB-Serial-JTAG).
5. **Secrets** — copy `include/secrets.example.h` to `include/secrets.h` and fill in
   WiFi + Tuya credentials. (`secrets.h` is git-ignored.)

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
idf.py -p COM6 flash monitor
```

- **Close any open serial monitor before `flash` or `erase-flash`** — an open monitor
  holds COM6 and causes port-contention errors.
- To start clean: `idf.py -p COM6 erase-flash` then `idf.py -p COM6 flash`.
- On boot, a healthy image logs the Matter endpoint init: `thermostat=1, light=2, beep=3`.

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
