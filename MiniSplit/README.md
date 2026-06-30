# MiniSplit Matter Bridge

A Matter-compliant ESP32 device that bridges Tuya mini-split AC control to SmartThings.

## Quick Start

> **Building?** See **[BUILD.md](BUILD.md)** for the authoritative, verified build
> procedure (exact toolchain versions, partition sizing, and Windows gotchas).

### Prerequisites
- ESP32-C6 development board (RISC-V)
- **ESP-IDF v5.4.1** — do **NOT** use 6.x (it removed the bundled `json` component)
- Tuya mini-split AC unit with IoT module
- SmartThings Hub with Matter support
- A short component-cache path on Windows (`$env:IDF_COMPONENT_CACHE_PATH = "C:\icc"`)

> ESP-Matter (esp_matter 1.5.0) is pulled automatically by the IDF Component Manager —
> no manual `git clone` required.

### Setup

1. **Configure secrets**
   ```powershell
   cd C:\Users\mawilkie\github\HomeAutomation\MiniSplit
   copy include\secrets.example.h include\secrets.h
   # Edit include\secrets.h with WiFi + Tuya credentials (git-ignored)
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
   # Use the SmartThings app to scan the QR code
   ```

## Project Status

| Phase | Component | Status | Doc |
|-------|-----------|--------|-----|
| 0 | Design & Architecture | ✅ Complete | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 1 | Tuya API Integration | ✅ Complete | [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) |
| 2 | Matter Device & Commissioning | ✅ Complete | [PHASE2_MATTER.md](PHASE2_MATTER.md) |
| 3 | Bidirectional Sync | ✅ Complete | [PHASE3_INTEGRATION.md](PHASE3_INTEGRATION.md) |
| 4 | Advanced Features | ⏳ Next | [ROADMAP.md](ROADMAP.md) |

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
- ✅ WiFi resilience & reconnection
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
- ✅ On/off through SmartThings
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

### Environmental Sensing (BME280)
- ✅ Standalone **Temperature** sensor endpoint (usable in SmartThings routines)
- ✅ Standalone **Humidity** sensor endpoint (usable in SmartThings routines)
- ✅ Optional BME280 over I2C (temp / humidity / pressure)
- ✅ Falls back to mirroring the Tuya indoor temperature when no BME280 is fitted
- ℹ️ See [SENSORS.md](SENSORS.md) for wiring, config, and endpoint details

## Technical Architecture

```
SmartThings App
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
│ ├─ WiFi (STA mode)          │
│ └─ BLE (for commissioning)  │
└──────────────────────────────┘
    ↓ (HTTPS REST)
Tuya Cloud API
    ↓
Mini-Split AC Unit
```

## Configuration

1. **WiFi**: Set SSID and password in `src/main.c`
2. **Tuya**: Store credentials in secure NVS (see [SECURE_STORAGE.md](config/SECURE_STORAGE.md))
3. **Polling**: Configure intervals in `src/main.c`
4. **Matter**: Enable in menuconfig (see [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md))

## Commissioning Steps

1. Device boots and starts BLE advertisement
2. Serial output shows setup code (8-digit or QR)
3. Open SmartThings app on phone
4. Tap "+" → "Scan device using camera"
5. Point at QR code or enter setup code manually
6. SmartThings joins Matter fabric
7. Device appears as "Mini-Split AC"
8. Full control available

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
# I (xxx) MATTER_DEVICE: Matter device initialized: thermostat_ep=1 light_ep=2 beep_ep=3 temp_sensor_ep=4 humidity_sensor_ep=5
# I (xxx) MATTER_DEVICE: Matter commissioning started
```

## Troubleshooting

### Build Issues
- See [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md) "Troubleshooting" section
- Check esp-matter is installed: `ls $IDF_PATH/components/esp-matter/`
- Verify menuconfig options enabled

### Commissioning Issues
- Check serial output for setup code
- Verify SmartThings Hub supports Matter
- Ensure BLE is enabled in device
- Restart SmartThings app if scan fails

### Device Not Responding
- Verify WiFi connection (check logs)
- Check Tuya credentials are correct
- Ensure device is online in Tuya app
- Review API response in logs (enable debug)

## References

- **Tuya API**: https://developer.tuya.com
- **Matter Spec**: https://csa-iot.org/matter_spec
- **ESP-Matter SDK**: https://github.com/espressif/esp-matter
- **SmartThings**: https://developer.smartthings.com
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
3. Commission device to SmartThings
4. Verify Matter controls work
5. Move to Phase 3 (bidirectional sync)

See [PHASE2_MATTER.md](PHASE2_MATTER.md) for detailed Phase 2 information.

