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

## NAT64: Jool, Not OTBR's Built-In Translator

The device is Thread-only (no WiFi fallback), so every outbound call it makes — DNS resolution,
NTP time sync, HTTPS to Tuya's cloud API — depends on NAT64 translation at the border router to
reach the IPv4 internet. `openthread/otbr`'s image ships its own built-in userspace NAT64
translator, but it has an unresolved bug in this deployment: translated packets are correctly
parsed and marked in netfilter's mangle PREROUTING chain, then vanish with zero trace anywhere
else in the IP stack — no forwarding-counter increase, no drop counter, no martian-source log, no
conntrack entry — while its own `ot-ctl nat64 counters` still reports them as successfully
translated with zero errors. `rp_filter`, `accept_local`, dedicated local/policy routes for the
translator's internal pool — none of it got translated packets to actually leave the host. A
synthetic packet sent via a normal socket with the exact same source address worked perfectly,
proving the bug is specific to otbr-agent's translator/tun-device interaction rather than the
surrounding routing/NAT config.

**NAT64 is instead provided by [Jool](https://www.jool.mx/en/), a standard kernel-level NAT64
module that translates via netfilter hooks instead of a tun device.** `Wyse5070DebSetup/setup-nat64-jool.sh`:
1. Installs and loads the Jool kernel module
2. Disables OTBR's own translator (`ot-ctl nat64 disable` — this is an all-or-nothing toggle
   covering both the prefix manager and the translator, no way to keep just the former)
3. Manually re-advertises the *identical* NAT64 prefix into Thread Network Data, so devices
   relying on it via DNS64 don't need to change
4. Configures a Jool NAT64 instance (pool6 = the same prefix, pool4 = the host's own real IP, so
   Jool's own NAPT produces a directly routable source address with no separate MASQUERADE rule
   needed)
5. Installs a systemd service (`nat64-jool.service`) so all of this reapplies after every reboot

Check `ot-ctl nat64 state` on the OTBR container any time — it should read `Disabled` for both
`PrefixManager` and `Translator`. If it ever shows `Active`, something (a container recreate, an
image update) re-enabled OTBR's own broken translator; re-run `setup-nat64-jool.sh`.

## SRP Client, Not Minimal mDNS

Similarly, the firmware itself needs the right service-discovery mechanism to be *reachable* by a
commissioner once it's on Thread. connectedhomeip defaults to `CONFIG_USE_MINIMAL_MDNS=y`, which
advertises the device's operational `_matter._tcp` service via raw mDNS multicast sent directly
over the Thread mesh interface — that traffic never crosses the border router to reach a LAN-side
commissioner. The fix (`sdkconfig.defaults`) is `CONFIG_OPENTHREAD_SRP_CLIENT=y` with
`CONFIG_USE_MINIMAL_MDNS` disabled, which routes service registration through OpenThread's SRP
client to OTBR, which then bridges it to real LAN mDNS. See
[COMMISSIONING_GUIDE.md](COMMISSIONING_GUIDE.md#device-joins-thread-and-works-fine-but-commissioning-never-completes-operational-discovery-timeout)
for the full symptom/fix writeup — this is the kind of setting that's easy to leave at its
default since the device works completely normally (DNS/NTP/Tuya) without it; only commissioner
discoverability breaks.

## Home Assistant Presentation

The device's identity strings and secondary telemetry endpoints were shaped by SmartThings'
now-retired custom Edge driver, which could relabel/reinterpret generic Matter capabilities in its
own UI. Home Assistant's Matter integration has no equivalent customization layer — it renders
standard Matter device types literally, which surfaced two classes of presentation problems:

- **Node/vendor/product identity** (`src/matter_device.cpp`): esp_matter's root node was created
  with an empty `NodeLabel` (Home Assistant fell back to a generic "Node \<id\>" device name), and
  `VendorName`/`ProductName` were never set, so the Basic Information cluster served
  connectedhomeip's compiled-in example-app defaults — literally `"TEST_VENDOR"`/`"TEST_PRODUCT"`.
  `NodeLabel` is fixed by setting `node_cfg.root_node.basic_information.node_label` at node
  creation (a normal, persisted attribute). `VendorName`/`ProductName` are **not** — despite an
  earlier fix here calling `attribute::update()` for them, that was confirmed on real hardware to
  be a no-op (HA still showed `"TEST_PRODUCT"`): connectedhomeip's `BasicInformationCluster.cpp`
  reads these two specific attributes directly from the `CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME`/
  `_PRODUCT_NAME` compile-time macros via `DeviceInstanceInfoProvider`, bypassing esp_matter's
  attribute store entirely. The actual fix is `include/chip_project_config.h`, wired in via
  `CONFIG_CHIP_PROJECT_CONFIG` in `sdkconfig.defaults` — connectedhomeip's own project-config
  override mechanism. (`VendorId`/`ProductId` are left at `0xFFF1`/`0x8000` — the CSA's reserved
  test range, correct for an uncertified DIY device — this fix is only about the human-readable
  strings.)
- **Compressor load/running endpoints** (`src/matter_device.cpp`): these repurpose a Humidity
  Sensor endpoint (`matter_update_compressor_demand`, %RH x100 scale reused for 0-100% load) and an
  Occupancy Sensor endpoint (`matter_update_compressor_running`, OccupancySensing's `Occupancy`
  bitmap reused for running/idle) rather than an actionable `dimmable_light`/`on_off_light` device
  type — deliberately chosen so Home Assistant renders them as read-only `sensor.*`/`binary_sensor.*`
  entities instead of a fake toggleable light/switch. The cost is a mismatched default label/icon
  (shows "Humidity" and "Occupancy"/Detected-Clear instead of "Compressor Load"/"Compressor
  Running"). A Contact Sensor (BooleanState cluster) endpoint was tried first for the running state
  — a better semantic fit — but confirmed on real hardware to fail: this esp-matter/connectedhomeip
  version has migrated BooleanState's StateValue attribute to a newer C++ cluster class whose live
  value bypasses esp_matter's generic attribute store, so `attribute::update()` against it returns
  `ESP_ERR_NOT_SUPPORTED`. Reaching the live cluster instance directly via
  `esp_matter::data_model::provider::get_instance().registry().Get()` + a `static_cast` is
  technically possible but couples to an SDK internal-implementation detail esp_matter doesn't
  document as supported; OccupancySensing (still on the classic attribute-store path in this SDK
  version) keeps every endpoint in this file on the same `attribute::update()` pattern instead. The
  spec-correct fix for the label mismatch — a `FixedLabel` cluster naming each endpoint — requires
  hand-encoding
  esp_matter's raw Ember list-attribute byte format (`attribute::create_label_list()`/
  `esp_matter_array()`), which isn't something to write blind without a build+flash+HA test loop;
  not yet done. Confirmed this isn't a shortcut worth taking: `esp_matter_data_model.cpp`'s
  `get_val()`/`set_val()` both explicitly reject `ESP_MATTER_VAL_TYPE_ARRAY`
  (`ESP_ERR_NOT_SUPPORTED`), and no working example of `create_label_list()` usage exists anywhere
  in the vendored esp_matter/connectedhomeip tree to copy the byte layout from — getting it wrong
  risks a corrupted or crashing attribute read, not just "doesn't help." Until addressed, rename
  these entities manually in Home Assistant (Settings → Devices & Services → Entities — persists
  locally, zero firmware risk).

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
