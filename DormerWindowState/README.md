# Dormer Window State

A Matter-enabled window/door contact sensor for the ESP32-C6. A single digital
GPIO reads a reed switch on the window frame; the device exposes itself to
any Matter ecosystem (Apple Home, Google Home, SmartThings, Home Assistant,
etc.) as a standard **Contact Sensor** (device type `0x0015`), reporting open
or closed via the Boolean State cluster's `StateValue` attribute.

This project mirrors the structure and build conventions of the `MiniSplit`
ESP-Matter project: ESP-IDF + the `esp_matter` component pulled in via the
IDF Component Manager, targeting the ESP32-C6.

## How it works

```
Reed switch on GPIO ──interrupt──> debounce task ──> Matter Boolean State
  (window magnet)                  (FreeRTOS task)     (StateValue attribute)
```

- `main/window_sensor.cpp` configures the GPIO as an input with an interrupt
  on any edge, and runs a small FreeRTOS task that debounces the signal and
  pushes confirmed state changes into the Matter attribute.
- `main/app_main.cpp` brings up the Matter stack, creates a single
  `contact_sensor` endpoint seeded with the GPIO's state at boot, then hands
  control to the driver for ongoing updates.

No cloud service, polling loop, or app-layer bridging is involved — the
state lives entirely on-device and Matter handles delivering attribute
changes (subscriptions/reports) to whatever hub or controller commissioned
it.

## Hardware

- ESP32-C6 (required for Matter: native Thread/802.15.4 + BLE 5 + WiFi 6)
- A reed switch (magnetic contact sensor), wired between the configured GPIO
  and GND
- Default wiring assumption (`WINDOW_SENSOR_ACTIVE_LOW=y`): the internal
  pull-up is enabled, so the pin reads **HIGH when open**, **LOW when closed**
  (switch shorts to GND when the magnet is present). If your switch is wired
  the other way, disable `WINDOW_SENSOR_ACTIVE_LOW` in menuconfig.

See [config/CONFIG.md](config/CONFIG.md) for the full set of options.

## Build & flash

Requires ESP-IDF (5.1+) and internet access for the Component Manager to
fetch `espressif/esp_matter` and its transitive dependencies on first build.

**On Windows:** native builds work fine (same setup as `MiniSplit`) — run
`setup-toolchain.ps1` or see [WINDOWS_TOOLCHAIN_SETUP.md](WINDOWS_TOOLCHAIN_SETUP.md)
for the full walkthrough (Espressif Installation Manager, build, flash over
a COM port). WSL2 is only needed as a fallback for the rare build step that
genuinely requires Linux-only Matter host tooling.

```bash
idf.py set-target esp32c6
idf.py menuconfig   # optional: adjust GPIO pin / polarity / debounce
idf.py build
idf.py -p <PORT> flash monitor
```

Or use the helper script:

```bash
./build.sh esp32c6 <PORT>   # PORT defaults to COM6 — Linux/macOS/WSL2
```

```powershell
.\build.ps1 -Target esp32c6 -Port COM6   # native Windows
```

## Commissioning

On first boot the device starts BLE commissioning automatically. Serial
output will show a setup code / QR payload once the Matter stack is up:

```
I (xxx) app_main: Window contact sensor created on endpoint 1
I (xxx) chip[DIS]: ... QR code: MT:...
I (xxx) chip[SVR]: Manual pairing code: ...
```

From your controller's app: Add Device → Matter → scan the QR code (or enter
the manual pairing code), select a room, and confirm. The device will appear
as a Contact Sensor reporting Open/Closed.

## Project layout

```
CMakeLists.txt          Root build file (ESP-IDF project)
partitions.csv           Flash partition table (3MB factory app)
sdkconfig.defaults        Target, BLE, mbedTLS, NVS-encryption defaults
build.sh                  Convenience build/flash/monitor script (bash)
build.ps1                 Convenience build/flash/monitor script (Windows)
setup-toolchain.ps1       Installs ESP-IDF natively on Windows (EIM)
setup-toolchain.sh        WSL2/Linux fallback toolchain install
WINDOWS_TOOLCHAIN_SETUP.md  Windows build/flash walkthrough
main/
  CMakeLists.txt           Component registration
  idf_component.yml        Declares the espressif/esp_matter dependency
  Kconfig.projbuild         GPIO pin / polarity / debounce menuconfig options
  app_main.cpp               Matter stack bring-up, endpoint creation
  window_sensor.h/.cpp        GPIO read, debounce, attribute updates
config/
  CONFIG.md                 Sensor configuration reference
```
