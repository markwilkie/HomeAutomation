# MiniSplit Matter Bridge - Architecture & Design

## Project Overview
Create a Matter-compliant device on ESP32 that bridges Tuya mini-split AC control to Home
Assistant, over Thread. (Originally built against a SmartThings/WiFi target — see
[Legacy: SmartThings (WiFi build)](COMMISSIONING_GUIDE.md#legacy-smartthings-wifi-build).)

## System Architecture

### Components
```
┌─────────────────┐
│  Home Assistant │
│   (Consumer)    │
└────────┬────────┘
         │ Matter Protocol (via python-matter-server)
         ↓
┌─────────────────────────────────────┐
│  Matter Device (ESP32-C6)           │
│  ├─ Matter Stack                    │
│  ├─ Tuya API Client                 │
│  ├─ Climate Control Cluster         │
│  └─ Device Attributes               │
└────────┬────────────────────────────┘
         │ Thread (802.15.4) -> OTBR border routing
         ↓
┌─────────────────────────────────────┐
│   Tuya Cloud API                    │
│   (openapi.tuyaus.com)              │
└─────────────────────────────────────┘
         │
         ↓
┌─────────────────┐
│   Mini-Split AC │
│   (Controlled)  │
└─────────────────┘
```

## Device Capabilities (from Tuya API)

### Status Properties
- **switch**: Power on/off
- **temp_set**: Target temperature (×100, e.g., 2100 = 21°C)
- **temp_current**: Current temperature reading
- **temp_set_f**: Target temperature in Fahrenheit
- **mode_auto**: Auto mode flag
- **mode_eco**: Eco mode flag
- **mode_dry**: Dry mode flag
- **heat**: Heating mode flag
- **light**: Display light
- **beep**: Beep/sound
- **health**: Health mode
- **cleaning**: Self-cleaning mode
- **fresh_air_valve**: Fresh air valve control

## Matter Clusters & Attributes

### Primary Clusters
1. **On/Off** (0x0006)
   - OnOff attribute (maps to Tuya `switch`)

2. **Thermostat** (0x0201)
   - LocalTemperature (from Tuya `temp_current`)
   - OccupiedHeatingSetpoint (from Tuya `temp_set`)
   - OccupiedCoolingSetpoint (from Tuya `temp_set`)
   - ControlSequenceOfOperation
   - SystemMode (maps to Tuya modes)

3. **Identify** (0x0003)
   - For device identification/commissioning

4. **Temperature Measurement** (0x0402) — standalone Temperature Sensor endpoint
   - MeasuredValue (°C × 100); driven by BME280, or falls back to Tuya `temp_current`
   - Exposed as a routine/automation-usable temperature entity

5. **Relative Humidity Measurement** (0x0405) — standalone Humidity Sensor endpoint
   - MeasuredValue (%RH × 100); driven by BME280
   - Exposed as a routine/automation-usable humidity entity
   - See [SENSORS.md](SENSORS.md) for wiring and details

### Optional Clusters
- **Fan Control** (0x0202) - if fan speed control needed
- **Basic Information** (0x0028) - device info

## Implementation Phases

### Phase 1: Setup & Communication (Foundation)
- [ ] ESP32 Matter SDK environment setup
- [ ] Tuya API authentication & client library
- [ ] Device status polling from Tuya
- [ ] Credentials management (secure storage)

### Phase 2: Matter Device Structure
- [ ] Matter device initialization
- [ ] Implement On/Off cluster
- [ ] Implement Thermostat cluster
- [ ] Matter endpoint configuration
- [ ] Commissioning flow

### Phase 3: Bidirectional Integration
- [ ] Command translation (Matter → Tuya API)
- [ ] State synchronization (Tuya → Matter)
- [ ] Error handling & retry logic
- [ ] Connection resilience

### Phase 4: Advanced Features
- [ ] Mode switching (auto/eco/dry/heat)
- [ ] Temperature unit handling (°C ↔ °F)
- [ ] Secondary features (light, beep, health, etc.)
- [ ] OTA firmware updates

## Technology Stack
- **Platform**: ESP32
- **Framework**: ESP-IDF (or Arduino with Matter support)
- **Matter SDK**: esp-matter or connectedhomeip
- **HTTP Client**: esp-idf native or libcurl
- **Authentication**: HMAC-SHA256 (Tuya requirement)

## Data Flow - Command Execution

```
Home Assistant User
    ↓
Matter Set Command
    ↓
ESP32 Matter Handler
    ↓
Request Translation (Matter → Tuya)
    ↓
Tuya API Call (HTTPS POST)
    ↓
Mini-Split State Change
    ↓
Tuya Status Polling
    ↓
Matter State Update
    ↓
Home Assistant Reflects Change
```

## Security Considerations
- HMAC-SHA256 signing for Tuya API calls
- Matter fabric security (Thread)
- Secure credential storage (NVS flash partition)
- HTTPS/TLS for Tuya API communication
- Rate limiting on API calls

## Configuration Requirements
```json
{
  "tuya": {
    "device_id": "eb11d9ff75ef37d109pihg",
    "client_id": "qt4e7qf9mnagnhptxn37",
    "client_secret": "[SECURE]",
    "access_token": "[SECURE]"
  },
  "matter": {
    "device_type": 0x0301,
    "device_name": "Mini-Split AC",
    "polling_interval_ms": 30000
  }
}
```

## Known Limitations & Challenges
1. Tuya API rate limiting (requests/min)
2. Device polling latency (~30s typical)
3. Thread dependency: no WiFi fallback, so Tuya connectivity relies entirely on OTBR's border
   routing having a working internet path
4. Matter SDK complexity & toolchain setup
5. Tuya authentication token refresh handling

## Success Criteria
- ✅ Device commissioning into Home Assistant
- ✅ Power control works bidirectionally
- ✅ Temperature reading displays correctly
- ✅ Temperature setpoint control works
- ✅ Mode switching functional
- ✅ Stable operation for 24+ hours
