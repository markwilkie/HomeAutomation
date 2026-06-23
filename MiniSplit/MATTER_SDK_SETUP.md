# Matter SDK Setup & Build Guide

This guide covers installing and configuring the ESP-Matter SDK for the MiniSplit project.

## Quick Summary

| Component | Status | Command |
|-----------|--------|---------|
| ESP-IDF | ✅ Required | `git clone esp-idf` |
| ESP-Matter | ✅ Required | `git clone esp-matter` |
| Connectedhomeip | ✅ Auto | Installed by esp-matter |
| Android/iOS App | ✅ Required | Download SmartThings |

## Step 1: Install ESP-Matter SDK

### Option A: As IDF Component (Recommended for ESP-IDF)

```bash
# Navigate to your IDF components directory
cd $IDF_PATH/components

# Clone ESP-Matter
git clone https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --recursive

# Install Python dependencies
pip install -r requirements.txt
```

### Option B: Separate Installation (Alternative)

```bash
# Create a separate workspace
mkdir ~/esp-matter-workspace
cd ~/esp-matter-workspace

# Clone and setup
git clone https://github.com/espressif/esp-matter.git esp-matter
cd esp-matter
git submodule update --init --recursive

# Add to CMakeLists.txt
export ESP_MATTER_PATH=~/esp-matter-workspace/esp-matter
```

## Step 2: Configure ESP-IDF for Matter

### menuconfig Options

```bash
idf.py menuconfig
```

Navigate to and enable:

**Component config → ESP Matter**
```
[*] Enable Matter
[*] Enable Matter commissioning
[*] Enable BLE support (for Thread-less commissioning)
    Fabric size: 1
    Maximum clusters per endpoint: 10
```

**Component config → Connectivity → Bluetooth**
```
[*] Bluetooth
[*]   Bluetooth Low Energy (BLE)
[*]     Enable Bluetooth LE
[*]   Bluetooth LE Privacy Support
```

**Component config → Protocols → Mbedtls**
```
[*] mbedTLS
    [*] Certificate Bundle
    [*] Asymmetric key operations
```

**Component config → Storage → NVS**
```
[*] Enable NVS encryption
```

## Step 3: Build Configuration

### Update CMakeLists.txt

**Root CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.5)

# Must come before project()
# Option A (IDF component):
# (automatic if esp-matter in $IDF_PATH/components)

# Option B (separate):
set(ESP_MATTER_PATH "/path/to/esp-matter")
include(${ESP_MATTER_PATH}/cmake/esp-matter.cmake)

project(minisplit-matter-bridge)

# Rest of project config
set(COMPONENTS esp-matter ...)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project_start()
```

**src/CMakeLists.txt:**
```cmake
idf_component_register(
    SRCS "main.c" "tuya_client.c" "matter_device.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client cjson mbedtls esp_matter
)
```

## Step 4: Build for Target

### ESP32-S3 (Recommended for Matter)

```bash
cd ~/github/HomeAutomation/MiniSplit

# Set target
idf.py set-target esp32s3

# Configure
idf.py menuconfig
# (Enable options from Step 2)

# Build
idf.py build

# Flash
idf.py flash -p /dev/ttyUSB0 monitor
```

### ESP32-C3 (Alternative)

```bash
idf.py set-target esp32c3
idf.py build
```

### Chip Architecture Comparison

| Chip | Matter Support | Flash | RAM | BLE | Thread | Recommended |
|------|---|-------|-----|-----|--------|-------------|
| ESP32 | ⚠️ Limited | 4MB | 520KB | ✓ | ✗ | No |
| ESP32-S3 | ✅ Full | 16MB | 520KB | ✓ | ✓ | **YES** |
| ESP32-C3 | ✅ Good | 8MB | 400KB | ✓ | ✗ | Yes |
| ESP32-H2 | ✅ Full | 8MB | 320KB | ✓ | ✓ | Emerging |

## Step 5: Configuration File

Create `config/BUILD_CONFIG.md`:

```markdown
# Build Configuration

## Target
ESP32-S3

## Features Enabled
- Matter commissioning
- BLE advertisement
- NVS encryption
- HMAC-SHA256

## Important Paths
- IDF: $IDF_PATH
- Matter: $ESP_MATTER_PATH
- Project: ~/github/HomeAutomation/MiniSplit

## Build Commands
\`\`\`bash
cd ~/github/HomeAutomation/MiniSplit
idf.py build
idf.py flash -p /dev/ttyUSB0 monitor
\`\`\`
```

## Troubleshooting

### Build Error: "esp_matter.h not found"

**Cause:** ESP-Matter SDK not properly installed

**Solution:**
```bash
# Verify location
ls $IDF_PATH/components/esp-matter/

# If not found, install Option A:
cd $IDF_PATH/components
git clone https://github.com/espressif/esp-matter.git

# Re-run full build (not incremental)
idf.py fullclean
idf.py build
```

### Build Error: "esp-matter CMake not configured"

**Cause:** CMakeLists.txt not properly including esp-matter

**Solution:**
```cmake
# In root CMakeLists.txt, before project():
if(DEFINED ESP_MATTER_PATH)
    include(${ESP_MATTER_PATH}/cmake/esp-matter.cmake)
endif()
```

### Runtime Error: "No commissioning credentials"

**Cause:** Device not commissioned

**Solution:**
1. Check serial output for setup code/QR
2. Open SmartThings app
3. Add Device → Scan QR or manual entry
4. Device should join fabric

### BLE Advertisement Not Starting

**Cause:** BLE not enabled or commissioning not called

**Solution:**
```c
// In main.c, ensure:
matter_device_init();        // Initialize device
matter_start_commissioning(); // Start advertisement

// Check logs:
esp_log_level_set("esp_matter", ESP_LOG_INFO);
```

### Insufficient Flash

**Cause:** ESP32 (2MB) doesn't have enough space

**Solution:**
- Use ESP32-S3 (16MB) instead
- Or reduce other components
- Recommended: Upgrade to ESP32-S3

## Verification Checklist

Build succeeds:
```bash
$ idf.py build
...
Project build complete
```

Firmware flashes:
```bash
$ idf.py flash -p /dev/ttyUSB0
Flashing bin files to serial port /dev/ttyUSB0
...
Leaving...
Hard resetting via RTS pin...
```

Serial monitor shows startup:
```
I (xxx) TUYA_CLIENT: Tuya client initialized
I (xxx) MATTER_DEVICE: Matter device initialized successfully
I (xxx) MATTER_DEVICE: Matter commissioning started
```

## Size Information

### Expected Build Sizes

| Component | Size | Percentage |
|-----------|------|-----------|
| Matter SDK | ~1.2MB | 40% |
| connectedhomeip | ~800KB | 27% |
| App Code | ~100KB | 3% |
| Other | ~900KB | 30% |
| **Total** | ~3MB | 100% |

### ESP32-S3 (16MB Flash):
- Ample headroom for app & OTA

### ESP32-C3 (8MB Flash):
- Tight but workable

### ESP32 (4MB Flash):
- ❌ NOT RECOMMENDED - too small

## Performance Notes

- Initial boot: ~3-5 seconds
- Matter commissioning: ~1-2 minutes
- Status sync: Every 30s after startup
- Memory usage: ~200KB RAM typical

## Next Steps

1. Install ESP-Matter SDK
2. Update CMakeLists.txt
3. Run: `idf.py menuconfig` (enable options)
4. Build: `idf.py build`
5. Flash: `idf.py flash -p <PORT> monitor`
6. Commission: Use SmartThings app

## References

- [ESP-Matter GitHub](https://github.com/espressif/esp-matter)
- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter)
- [Matter Spec](https://csa-iot.org/matter_spec)
- [SmartThings Matter](https://developer.smartthings.com)

## Build Environment Variables

```bash
# Optional: Set paths for easier builds
export IDF_PATH=~/esp-idf
export ESP_MATTER_PATH=~/esp-matter
export MCP_HOME=~/github/HomeAutomation/MiniSplit

# Then:
cd $MCP_HOME
idf.py build
```

## Summary

| Step | Command | Expected Output |
|------|---------|-----------------|
| 1 | Install Matter | `esp-matter/` directory exists |
| 2 | menuconfig | Options enabled in sdkconfig |
| 3 | Build | `Project build complete` |
| 4 | Flash | `Hard resetting...` |
| 5 | Monitor | Matter device initialized |

Done! Ready for Phase 2 implementation.
