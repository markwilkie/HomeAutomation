# Phase 2: Matter Device Integration

**Status:** ✅ In Progress (Implementation Complete)

**Time Estimate:** 6-8 hours for Phase 2

## What's Implemented

### 1. Matter Device Initialization (✅)
- Creates Matter node with Thermostat device type (0x0300)
- Creates endpoint 1 with Thermostat profile
- Initializes On/Off and Thermostat clusters
- Sets default attribute values

**File:** `src/matter_device.c` - `matter_device_init()`

### 2. On/Off Cluster (✅)
- Handles power on/off commands from SmartThings
- Attribute: OnOff (boolean)
- Command callback for write operations
- Integration with Tuya power control

**Cluster ID:** 0x0006

**Attributes:**
- `OnOff` - Power state (read/write)

### 3. Thermostat Cluster (✅)
- Complete temperature control
- Mode selection (heat, cool, auto, off)
- Temperature setpoint control
- Current temperature reading

**Cluster ID:** 0x0201

**Attributes:**
- `LocalTemperature` - Current temperature (read-only from Tuya)
- `OccupiedHeatingSetpoint` - Heating target (read/write)
- `OccupiedCoolingSetpoint` - Cooling target (read/write)
- `SystemMode` - Operating mode (read/write)

**SystemMode Values:**
- 0 = Off
- 1 = Heat
- 2 = Cool
- 3 = Auto (typical default)

### 4. Commissioning Flow (✅)
- `matter_start_commissioning()` enables BLE advertisement
- Generates QR code for SmartThings scanning
- Handles Matter fabric authentication
- Device discoverable in SmartThings app

**File:** `src/matter_device.c` - `matter_start_commissioning()`

### 5. Attribute Callbacks (✅)
- `on_off_cluster_callback()` - Handles SmartThings power commands
- `thermostat_cluster_callback()` - Handles temperature & mode commands
- Command flags for pending operations
- Integration hooks for Tuya command execution

## Data Structure

### Matter Device State
```c
typedef struct {
    bool onoff;                       // Current power state
    int16_t current_temp;             // Current temperature (×100)
    int16_t heating_setpoint;         // Heat target (×100)
    int16_t cooling_setpoint;         // Cool target (×100)
    uint8_t system_mode;              // 0=off, 1=heat, 2=cool, 3=auto
    bool onoff_command_pending;       // Flag: power command from SmartThings
    int16_t setpoint_command_pending; // Pending temperature command
    uint8_t mode_command_pending;     // Pending mode command
} matter_device_state_t;
```

## API Reference

### Device Lifecycle
```c
// Initialize Matter device
esp_err_t matter_device_init(void);

// Start BLE commissioning
esp_err_t matter_start_commissioning(void);

// Cleanup
void matter_device_deinit(void);
```

### Update Attributes (Tuya → Matter)
```c
void matter_update_onoff(bool on_off);
void matter_update_local_temperature(int16_t temp_c);
void matter_update_heating_setpoint(int16_t temp_c);
void matter_update_cooling_setpoint(int16_t temp_c);
void matter_update_system_mode(uint8_t mode);
```

### Check Commands (SmartThings → Tuya)
```c
bool matter_get_onoff_command(void);              // Returns pending power cmd
int16_t matter_get_heating_setpoint_command(void); // Returns pending setpoint
uint8_t matter_get_system_mode_command(void);     // Returns pending mode

// Clear command flags after processing
void matter_clear_onoff_command(void);
void matter_clear_setpoint_command(void);
void matter_clear_mode_command(void);
```

## Matter Endpoint Architecture

```
Matter Device
├── Node (root)
│   └── Endpoint 1 (Thermostat, device type 0x0300)
│       ├── On/Off Cluster (0x0006)
│       │   └── OnOff Attribute
│       ├── Thermostat Cluster (0x0201)
│       │   ├── LocalTemperature
│       │   ├── OccupiedHeatingSetpoint
│       │   ├── OccupiedCoolingSetpoint
│       │   └── SystemMode
│       └── Identify Cluster (0x0003)
│           └── Identify Command
```

## Commissioning Process

### Prerequisites
- ESP32 with Matter support (S3, C3, H2 recommended)
- SmartThings Hub with Matter support (U8000 or later)
- WiFi connectivity established
- BLE enabled on hub

### Steps

1. **Device Boot**
   ```
   I (xxx) MATTER_DEVICE: Matter device initialized successfully
   I (xxx) MATTER_DEVICE: Starting Matter commissioning
   I (xxx) MATTER_DEVICE: Matter commissioning started
   ```

2. **SmartThings App**
   - Open SmartThings app
   - Tap "+" → "Scan device using camera"
   - Point at QR code displayed/logged by device

3. **Fabric Join**
   - SmartThings scans QR code
   - Sends commissioning request via BLE
   - Device joins Matter fabric
   - Credentials stored in NVS

4. **Device Appears**
   - Listed as "Mini-Split AC" in SmartThings
   - Full control available (power, temperature, mode)

### QR Code / Setup Code

Device will generate:
- **QR Code** - Encoded setup code + device info
- **Setup Code** - Manual entry format (8 digits)

Check serial output for setup code display.

## Build Configuration

### CMakeLists.txt Dependencies
```cmake
idf_component_register(
    SRCS "main.c" "tuya_client.c" "matter_device.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client cjson mbedtls 
            esp_matter esp_matter_bridge
)
```

### sdkconfig Requirements

Enable in `idf.py menuconfig`:
```
Component config → ESP Matter
  ✓ Enable Matter
  ✓ Enable Matter commissioning
  
Component config → BLE
  ✓ Enable Bluetooth Low Energy
  ✓ Enable BLE security (passkey entry)
```

## Temperature Mapping

### Celsius (Our Standard)
- Stored as: `int16_t` with ×100 factor
- Example: `2200` = 22°C
- Range: 15°C to 30°C (1500 to 3000)

### Fahrenheit Support (Phase 4)
- SmartThings may request Fahrenheit
- Conversion: `°F = (°C × 9/5) + 32`
- Both temperatures stored in Matter attributes
- Tuya always uses Celsius

## System Mode Mapping

| Matter Mode | Tuya Code | Behavior |
|-------------|-----------|----------|
| 0 (Off) | All false | Device off |
| 1 (Heat) | heat=true | Heating mode |
| 2 (Cool) | mode_auto=false, heat=false | Cooling mode |
| 3 (Auto) | mode_auto=true | Auto switching |

## Integration with Phase 3

Phase 3 will add:
1. **Command Routing Task**
   - Poll Matter command flags
   - Route to Tuya API
   - Clear flags after processing

2. **Status Sync Task**
   - Get Tuya status
   - Update Matter attributes
   - Bidirectional complete

3. **Error Handling**
   - Connection resilience
   - Retry logic
   - User feedback

## Troubleshooting

### Device Not Appearing in SmartThings

**Issue:** QR code not showing
- Check serial output for setup code
- Verify BLE is enabled
- Restart SmartThings app

**Issue:** Fabric join fails
- Verify hub supports Matter
- Check WiFi connectivity
- Review Matter SDK logs: `esp_log_level_set("esp_matter", ESP_LOG_DEBUG);`

**Issue:** Commands not working
- Check Matter commissioning completed
- Verify endpoint is present: `esp_matter_node_get_endpoint(node, 1)`
- Review cluster registration

### Compilation Errors

**Missing Headers**
```
error: 'esp_matter.h' file not found
```
→ Install esp-matter SDK, update CMakeLists.txt

**HMAC/Signing Issues**
- These are Phase 1 related, should already work
- Check `tuya_client.c` compilation

## Testing Checklist

- [ ] Matter device initializes without errors
- [ ] BLE advertisement starts on commissioning
- [ ] Setup code displays in serial output
- [ ] SmartThings scans QR code successfully
- [ ] Device appears as "Mini-Split AC"
- [ ] Power toggle works (Matter → UI update)
- [ ] Temperature setpoint change works
- [ ] Mode selection shows all options
- [ ] Current temperature reads from Tuya
- [ ] Commands appear in Matter logs
- [ ] All clusters respond to reads
- [ ] No crashes or exceptions

## Next Phase (Phase 3)

See [ROADMAP.md](ROADMAP.md#phase-3-bidirectional-integration--sync) for:
- Command routing from Matter to Tuya
- Status synchronization
- Bidirectional complete integration
- Connection resilience

## References

- [ESP-Matter SDK](https://github.com/espressif/esp-matter)
- [Matter Specification](https://csa-iot.org/matter_spec)
- [Thermostat Cluster Spec](https://csa-iot.org/matter_spec#cluster-0x0201)
- [SmartThings Matter Support](https://developer.smartthings.com)
- [ESP32 Matter Commissioning](https://docs.espressif.com/projects/esp-matter/en/latest/getting_started.html)

## Key Files

- **Implementation**: [src/matter_device.c](src/matter_device.c)
- **Header**: [include/matter_device.h](include/matter_device.h)
- **Build**: [src/CMakeLists.txt](src/CMakeLists.txt)
- **Main**: [src/main.c](src/main.c) - Calls lifecycle functions
- **Architecture**: [ARCHITECTURE.md](ARCHITECTURE.md) - System design
