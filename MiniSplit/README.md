# MiniSplit Matter Bridge

A Matter-compliant ESP32 device that bridges Tuya mini-split AC control to Home Assistant.

> This device previously commissioned to a SmartThings hub, which required a custom Edge Driver
> to avoid losing 8 of its 10 endpoints (see [Legacy: SmartThings](#legacy-smartthings) below).
> Home Assistant's Matter Server doesn't do CSA-certified fingerprint matching the way
> SmartThings does, so it should expose all 10 endpoints natively with no custom driver needed —
> **flag this as worth double-checking on first commission**, since it hasn't been verified here
> yet.

## Quick Start

> **Building?** See **[BUILD.md](BUILD.md)** for the authoritative, verified build
> procedure (exact toolchain versions, partition sizing, and Windows gotchas).

### Prerequisites
- ESP32-C6 development board (RISC-V) — has the on-die 802.15.4 radio this needs for Thread
- **ESP-IDF v5.4.1** — do **NOT** use 6.x (it removed the bundled `json` component)
- Tuya mini-split AC unit with IoT module
- Home Assistant (Container install) with the `python-matter-server` container running —
  see `../Wyse5070DebSetup/setup-matter-server.sh`
- An OpenThread Border Router (OTBR) on the same network — see
  `../Wyse5070DebSetup/setup-mg24.sh`. This device joins the Matter fabric over Thread, not WiFi;
  OTBR is what bridges the Thread mesh to your LAN/internet.
- A short component-cache path on Windows (`$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"`)

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
- ✅ Current compressor load, 0–100% (derived from Tuya `compressor_frequency`)
- ✅ Rolling 8h and 24h load averages, computed on-device
- ✅ Outdoor ambient temperature (Tuya `ure` DP)
- ℹ️ On the [legacy SmartThings path](#legacy-smartthings), this requires the custom Edge Driver
  to be visible in the app; on Home Assistant it should appear as a native entity with no
  extra driver

### Compressor Cycling
- ✅ Cycles/hour in a true sliding 60-minute window (a cycle = leading edge only, `off→on`;
  turning back off doesn't count as a second cycle)
- ✅ Max cycles/hour observed over the last 24 hours
- ✅ Time in current on/off state (minutes), computed on-device
- ℹ️ Same [legacy SmartThings](#legacy-smartthings) custom-driver caveat as above

### Environmental Sensing (BME280)
- ✅ Standalone **Temperature** sensor endpoint (usable in HA automations)
- ✅ Standalone **Humidity** sensor endpoint (usable in HA automations)
- ✅ Optional BME280 over I2C (temp / humidity / pressure)
- ✅ Falls back to mirroring the Tuya indoor temperature when no BME280 is fitted
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
# I (xxx) MATTER_DEVICE: Matter device initialized: thermostat_ep=1 temp_sensor_ep=2 humidity_sensor_ep=3 outdoor_temp_sensor_ep=4 compressor_ep=5 compressor_avg8h_ep=6 compressor_avg24h_ep=7 compressor_cycles_ep=8 compressor_cycles_max24h_ep=9 compressor_state_duration_ep=10
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
- Check serial output for setup code
- Verify the `python-matter-server` container is running and reachable from HA
  (`ws://localhost:5580/ws`) — see `../Wyse5070DebSetup/setup-matter-server.sh`
- Verify OTBR is up and has a Thread network formed (its web UI shows network diagnostics)
- Ensure BLE is enabled on the device and on whatever host is running `python-matter-server`
  (BLE is required to hand over the Thread dataset during commissioning)
- Restart the Matter integration in HA if scan fails

### Device Not Responding
- Verify Thread network attachment (check serial logs; confirm the device shows up in OTBR's
  network diagnostics)
- Check Tuya credentials are correct
- Ensure device is online in Tuya app
- Review API response in logs (enable debug)
- If Tuya calls fail specifically (but the device is on the Thread mesh), suspect OTBR's border
  routing to the internet rather than the mesh itself

## References

- **Tuya API**: https://developer.tuya.com
- **Matter Spec**: https://csa-iot.org/matter_spec
- **ESP-Matter SDK**: https://github.com/espressif/esp-matter
- **Home Assistant Matter integration**: https://www.home-assistant.io/integrations/matter/
- **python-matter-server**: https://github.com/matter-js/python-matter-server
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

