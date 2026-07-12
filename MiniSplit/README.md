# MiniSplit Matter Bridge

A Matter-compliant ESP32 device that bridges Tuya mini-split AC control to Home Assistant.

> This device previously commissioned to a SmartThings hub, which required a custom Edge Driver
> to avoid losing 8 of its 10 endpoints (see [Legacy: SmartThings](#legacy-smartthings) below).
> **Confirmed on real hardware:** Home Assistant's Matter Server doesn't need the SmartThings-style
> custom driver workaround — the device commissions and exposes its endpoints natively.

## Quick Start

> **Building?** See **[BUILD.md](BUILD.md)** for the authoritative, verified build
> procedure (exact toolchain versions, partition sizing, and Windows gotchas).

### Prerequisites
- ESP32-C6 development board (RISC-V) — has the on-die 802.15.4 radio this needs for Thread
- **ESP-IDF v5.4.1** — do **NOT** use 6.x (it removed the bundled `json` component)
- Tuya mini-split AC unit with IoT module
- Home Assistant (Container install) with the `matterjs-server` container running —
  see `../Wyse5070DebSetup/setup-matter-server.sh`, and BlueZ running on that host for BLE
  commissioning — see `../Wyse5070DebSetup/setup-bluetooth.sh`
- An OpenThread Border Router (OTBR) on the same network — see
  `../Wyse5070DebSetup/setup-mg24.sh`. This device joins the Matter fabric over Thread, not WiFi;
  OTBR is what bridges the Thread mesh to your LAN/internet. NAT64 (needed for the device to reach
  the IPv4 internet — DNS, NTP, Tuya's API) is provided by Jool, not OTBR's own built-in
  translator — see [ARCHITECTURE.md](ARCHITECTURE.md#nat64-jool-not-otbrs-built-in-translator)
- A short component-cache path on Windows (`$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"`)

> **Full commissioning walkthrough, prerequisites checklist, and troubleshooting:**
> [COMMISSIONING_GUIDE.md](COMMISSIONING_GUIDE.md)

> ESP-Matter (esp_matter 1.5.0) is pulled automatically by the IDF Component Manager —
> no manual `git clone` required.

### Setup

1. **Configure secrets**
   ```powershell
   cd C:\Users\mawilkie\github\HomeAutomation\MiniSplit
   copy include\secrets.example.h include\secrets.h
   # Edit include\secrets.h with your Tuya credentials (git-ignored). No WiFi
   # credentials needed -- this device joins over Thread, provisioned during
   # Matter commissioning, not baked into firmware.
   ```

2. **Build (PowerShell on Windows)**
   ```powershell
   . C:\esp-idf-v5.4.1\export.ps1
   $env:IDF_COMPONENT_CACHE_PATH = "C:\icc"
   idf.py set-target esp32c6   # first time / after fullclean only
   idf.py build
   ```

3. **Flash & Commission**
   ```powershell
   idf.py -p COM6 flash monitor   # close any open monitor first
   # Watch for the Matter setup QR / pairing code in serial output
   # In Home Assistant: Settings -> Devices & Services -> Add Integration -> Matter
   # -> Add device -> scan the QR code (or enter the setup code manually)
   ```

## Project Status

| Phase | Component | Status | Doc |
|-------|-----------|--------|-----|
| 0 | Design & Architecture | ✅ Complete | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 1 | Tuya API Integration | ✅ Complete | [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) |
| 2 | Matter Device & Commissioning | ✅ Complete | [PHASE2_MATTER.md](PHASE2_MATTER.md) |
| 3 | Bidirectional Sync | ✅ Complete | [PHASE3_INTEGRATION.md](PHASE3_INTEGRATION.md) |
| 4 | Advanced Features | ⏳ Next | [ROADMAP.md](ROADMAP.md) |

Full Tuya data point schema for this device: [TUYA_DP_REFERENCE.md](TUYA_DP_REFERENCE.md)

## Project Structure

```
MiniSplit/
├── instructions.txt           # Original requirements
├── README.md                  # This file
├── BUILD.md                   # ✅ Verified build & flash guide (read this first)
├── ARCHITECTURE.md            # System design (Phase 0)
├── PHASE1_COMPLETE.md         # Tuya API details (Phase 1)
├── PHASE2_MATTER.md           # Matter integration (Phase 2)
├── MATTER_SDK_SETUP.md        # SDK installation guide
├── SENSORS.md                 # ✅ Temp/humidity endpoints + BME280 wiring
├── ROADMAP.md                 # Development roadmap
├── TUYAOPEN_REFERENCE.md      # TuyaOpen analysis
├── TUYA_DP_REFERENCE.md       # Full Tuya DP (data point) schema for this device
├── IMPLEMENTATION_NOTES.md    # Technical details
├── CMakeLists.txt
├── build.sh
├── src/
│   ├── main.c                 # Entry point (orchestration)
│   ├── tuya_client.c          # ✅ Tuya API (Phase 1)
│   ├── matter_device.cpp      # ✅ Matter device + sensor endpoints (Phase 2)
│   ├── bme280.c               # ✅ BME280 temp/humidity/pressure driver
│   ├── tuya_api_test.c        # Test harness
│   └── CMakeLists.txt
├── include/
│   ├── tuya_client.h          # ✅ Tuya API interface
│   ├── matter_device.h        # ✅ Matter interface
│   ├── bme280.h               # ✅ BME280 driver interface
└── config/
    ├── CONFIG.md              # Configuration template
    ├── SECURE_STORAGE.md      # Credential storage
    └── BUILD_CONFIG.md        # Build setup
```

## Development Roadmap

### Current Phase: 3 - Bidirectional Integration ✅

**Completed:**
- ✅ Status synchronization task (Tuya → Matter every 30s)
- ✅ Command routing task (Matter → Tuya every 5s)
- ✅ Network resilience & reconnection (Thread)
- ✅ Error handling with retries
- ✅ Health monitoring & diagnostics
- ✅ Task orchestration (3 concurrent FreeRTOS tasks)

**Files:**
- [PHASE3_INTEGRATION.md](PHASE3_INTEGRATION.md) - Phase 3 complete documentation
- `src/main.c` - Three concurrent tasks: sync, command routing, health monitoring
- `src/tuya_client.c` - API communication (Phase 1)
- `src/matter_device.c` - Matter attributes (Phase 2)

### Next Phase: 4 - Advanced Features

Will implement:
- Temperature unit conversion (°C ↔ °F)
- Secondary feature control (light, beep, etc.)
- OTA firmware updates
- Performance optimization
- Comprehensive testing

See [ROADMAP.md](ROADMAP.md) for details.

## Key Features

### Power Control
- ✅ On/off through Home Assistant
- ✅ Reflects current device state

### Temperature Monitoring
- ✅ Real-time current temperature
- ✅ Celsius display (×100 format)
- ℹ️ Fahrenheit support Phase 4

### Temperature Control
- ✅ Set target temperature
- ✅ Heating and cooling setpoints
- ✅ Range validation

### Operating Modes
- ✅ Off, Heat, Cool, Auto
- ✅ Maps to Tuya device modes
- ✅ Mode persistence

### Compressor Load Monitoring
- ✅ Current compressor load, 0–100% (derived from Tuya `compressor_frequency`), as a read-only
  numeric sensor (Humidity Sensor device type, repurposed for its exact %-scale match)
- ✅ Compressor running state as a read-only binary_sensor (Occupancy Sensor device type), with
  full on/off history in Home Assistant
- ✅ Outdoor ambient temperature (Tuya `ure` DP)
- ℹ️ On the [legacy SmartThings path](#legacy-smartthings), this requires the custom Edge Driver
  to be visible in the app; on Home Assistant it should appear as a native entity with no
  extra driver

### Environmental Sensing (BME280)
- ✅ Standalone **Temperature** sensor endpoint — the BME280's own indoor reading, independent of
  the mini-split's indoor reading on the Thermostat
- ✅ Standalone **Humidity** sensor endpoint (usable in HA automations)
- ✅ Optional BME280 over I2C (temp / humidity / pressure)
- ℹ️ See [SENSORS.md](SENSORS.md) for wiring, config, and endpoint details

## Technical Architecture

```
Home Assistant (Matter Server integration)
    ↓ (Matter Protocol)
┌──────────────────────────────┐
│  ESP32 Matter Bridge         │
├──────────────────────────────┤
│ Matter Device Layer (Phase 2)│
│ ├─ On/Off Cluster           │
│ ├─ Thermostat Cluster       │
│ ├─ Temperature Measurement  │
│ ├─ Relative Humidity Meas.  │
│ └─ Commissioning            │
├──────────────────────────────┤
│ Tuya API Client (Phase 1)    │
│ ├─ HMAC-SHA256 Auth         │
│ ├─ Status Polling           │
│ └─ Command Execution        │
├──────────────────────────────┤
│ Networking Layer            │
│ ├─ Thread (802.15.4, FTD)   │
│ └─ BLE (for commissioning)  │
└──────────────────────────────┘
    ↓ (via OTBR border routing)
Tuya Cloud API
    ↓
Mini-Split AC Unit
```

## Configuration

1. **Network**: No pre-configuration needed — the Thread Operational Dataset is provisioned by
   the commissioner (Home Assistant, via OTBR) during Matter/BLE commissioning
2. **Tuya**: Store credentials in secure NVS (see [SECURE_STORAGE.md](config/SECURE_STORAGE.md))
3. **Polling**: Configure intervals in `src/main.c`
4. **Matter**: Enable in menuconfig (see [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md))

## Commissioning Steps

1. Device boots and starts BLE advertisement (no network yet — Matter/Thread provisioning happens
   as part of commissioning, not before it)
2. Serial output shows setup code (8-digit or QR)
3. In Home Assistant: Settings → Devices & Services → Add Integration → Matter → Add device
4. Scan the QR code or enter the setup code manually
5. Home Assistant/OTBR hands the device a Thread Operational Dataset and it joins the mesh
6. Device appears as "Mini-Split AC"
7. Full control available

Using the legacy SmartThings path instead? See [Legacy: SmartThings](#legacy-smartthings) below —
note that path was built and tested for the WiFi transport, not Thread.

## Build & Flash

```bash
# Terminal 1: Build (see BUILD.md for the full, verified procedure)
cd ~/github/HomeAutomation/MiniSplit
idf.py set-target esp32c6
idf.py build

# Terminal 2: Flash (COM6 on Windows; /dev/ttyUSB0 on Linux)
idf.py -p COM6 flash monitor

# Output (check for):
# I (xxx) TUYA_CLIENT: Tuya client initialized for device: eb11d9ff75ef37d109pihg
# I (xxx) MATTER_DEVICE: Matter device initialized: thermostat_ep=1 temp_sensor_ep=2 humidity_sensor_ep=3 outdoor_temp_sensor_ep=4 compressor_ep=5 compressor_running_ep=6
# I (xxx) MATTER_DEVICE: Matter commissioning started
```

## Legacy: SmartThings

This device previously commissioned to a SmartThings hub over WiFi. That path is no longer the
primary target — see the [top of this README](#minisplit-matter-bridge) and
[Commissioning Steps](#commissioning-steps) for the current Home Assistant/Thread path — but is
kept here since the SmartThings hub may still be around for other devices, or as a rollback
option. **Note this legacy content describes the old WiFi-based build; the firmware now joins over
Thread (see [Technical Architecture](#technical-architecture)), so re-verify before relying on it.**

This device uses a manufacturer/product ID in the CSA test range (not Matter-DCL-certified), so
SmartThings can't fetch a real certified profile for it. Its built-in `MATTER_GENERIC`
fingerprinting falls back to the closest known generic shape ("thermostat + humidity") and
**silently drops every endpoint that doesn't fit** — no error, the missing data just never
appears. Of this device's 10 endpoints, only 2 survive that fallback.

The fix is a custom Edge Driver that fingerprints on this device's exact vendor/product ID and
defines the real 10-component composition explicitly:
`../SmartThings_Edge/EdgeDrivers/MiniSplit-Matter-Driver/` (sibling repo — see its own README for
deployment steps: packaging, channel assignment, and installing to your hub via the SmartThings
CLI).

**Deploy that driver before commissioning this device.** If you already commissioned it under
the generic fallback, you'll need to remove and re-pair it after installing the driver so
SmartThings' fingerprint matcher picks up the custom one instead.

See [TUYA_DP_REFERENCE.md](TUYA_DP_REFERENCE.md#legacy-smartthings-requires-the-custom-edge-driver)
for the full endpoint-to-component mapping.

## Troubleshooting

### Build Issues
- See [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md) "Troubleshooting" section
- Check esp-matter is installed: `ls $IDF_PATH/components/esp-matter/`
- Verify menuconfig options enabled

### Commissioning Issues
See [COMMISSIONING_GUIDE.md](COMMISSIONING_GUIDE.md#troubleshooting) for the full, verified
troubleshooting flow — in particular, three non-obvious real-hardware issues worth knowing about
before you start digging:
- The commissioning UI can report "failed" even when the device actually finished successfully,
  if anything upstream was slow enough to blow past Matter's `ArmFailSafe` timeout — see
  [that section](COMMISSIONING_GUIDE.md#home-assistant--matter-server-reports-commissioning-failed-but-the-device-is-actually-working).
- A device can join Thread and work completely normally (DNS/NTP/Tuya) while still being
  undiscoverable to the commissioner's final "operational discovery" step, if
  `CONFIG_OPENTHREAD_SRP_CLIENT` isn't enabled — see
  [that section](COMMISSIONING_GUIDE.md#device-joins-thread-and-works-fine-but-commissioning-never-completes-operational-discovery-timeout).
- `matterjs-server` needs its Thread credentials re-pushed after every restart/reboot before
  it can commission *new* Thread devices — automated via
  `../Wyse5070DebSetup/setup-thread-credentials-sync.sh`, see
  [that section](COMMISSIONING_GUIDE.md#cant-commission-a-new-device-even-though-an-existing-one-still-works).

### Device Not Responding
- Verify Thread network attachment (check serial logs; confirm the device shows up in OTBR's
  network diagnostics)
- Check Tuya credentials are correct
- Ensure device is online in Tuya app
- Review API response in logs (enable debug)
- If Tuya calls fail specifically (but the device is on the Thread mesh), suspect NAT64 (Jool) —
  see [ARCHITECTURE.md](ARCHITECTURE.md#nat64-jool-not-otbrs-built-in-translator) — rather than
  the mesh itself; `docker exec otbr sh -c "ot-ctl nat64 state"` should show `Disabled` for both
  (Jool handles translation externally, not OTBR's own built-in translator)

## References

- **Tuya API**: https://developer.tuya.com
- **Matter Spec**: https://csa-iot.org/matter_spec
- **ESP-Matter SDK**: https://github.com/espressif/esp-matter
- **Home Assistant Matter integration**: https://www.home-assistant.io/integrations/matter/
- **matterjs-server**: https://github.com/matter-js/matterjs-server
- **SmartThings** (legacy path): https://developer.smartthings.com
- **ESP-IDF**: https://docs.espressif.com

## Implementation Status

### Phase 1: Tuya API ✅
- ✅ HMAC-SHA256 signing
- ✅ Device status polling
- ✅ Power/temperature/mode commands
- ✅ JSON parsing
- ✅ Error handling

### Phase 2: Matter ✅
- ✅ Device endpoint setup
- ✅ On/Off cluster
- ✅ Thermostat cluster
- ✅ Commissioning
- ✅ Attribute callbacks

### Phase 3: Integration ⏳
- ⏳ Command routing
- ⏳ Status sync
- ⏳ Bidirectional control
- ⏳ Connection resilience

### Phase 4: Polish 📅
- 📅 OTA updates
- 📅 Multiple devices
- 📅 Advanced features
- 📅 Production hardening

## Performance Metrics

- **Status poll**: ~500ms per request
- **Command latency**: ~300ms to device
- **Memory usage**: ~200KB runtime
- **Flash usage**: ~3MB (esp32s3 has 16MB)
- **CPU utilization**: <5% typical

## Security

- ✅ HMAC-SHA256 API signing
- ✅ HTTPS/TLS for Tuya API
- ✅ Matter fabric encryption
- ✅ NVS credential storage (encrypted)
- ✅ No secrets in code

## License

TBD

## Next Steps

1. Install ESP-Matter SDK (see [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md))
2. Configure and build
3. Commission device via Home Assistant
4. Verify Matter controls work
5. Move to Phase 3 (bidirectional sync)

See [PHASE2_MATTER.md](PHASE2_MATTER.md) for detailed Phase 2 information.

