# Matter Commissioning Guide for SmartThings

This guide walks through commissioning the MiniSplit Matter Bridge to SmartThings.

## Prerequisites Checklist

Before attempting commissioning:

- [ ] ESP32-S3 (or ESP32-C3) with firmware flashed
- [ ] Device powered on and running
- [ ] WiFi connected (check serial logs)
- [ ] SmartThings Hub with Matter support (U8000 or Hub 3+)
- [ ] Hub connected to same WiFi network
- [ ] SmartThings app installed on smartphone
- [ ] BLE enabled on hub and smartphone
- [ ] Matter enabled in hub settings

## Serial Output Verification

Before commissioning, verify device is ready:

```
I (xxx) MAIN: === MiniSplit Matter Bridge Initializing ===
I (xxx) MAIN: WiFi initialization complete
I (xxx) TUYA_CLIENT: Tuya client initialized for device: eb11d9ff75ef37d109pihg
I (xxx) MATTER_DEVICE: Matter device initialized successfully on endpoint 1
I (xxx) MATTER_DEVICE: Matter commissioning started
I (xxx) MATTER_DEVICE: Scan the QR code displayed in the Matter app to commission device
```

If you see this output, device is ready for commissioning.

## Step-by-Step Commissioning

### Step 1: Open SmartThings App

1. Open SmartThings app on your smartphone
2. Tap the "+" icon (typically top-right or bottom)
3. Select "Add Device" or "Scan Device"

### Step 2: Initiate Scanning

```
SmartThings App
└─ "+" Menu
   └─ "Add Device" / "Scan Device Code"
      └─ "Use Camera"
         └─ Point camera at QR code or setup code display
```

Expected behavior:
- Camera screen opens
- Device requests permission to access camera
- Scanning indicator appears

### Step 3: Get Setup Code

#### Option A: QR Code (Easiest)
- Check ESP32 serial output for QR code display
- Some Matter devices print QR code to serial terminal
- Point smartphone camera at QR code

#### Option B: Manual Setup Code
- Setup code format: 8-digit number (e.g., 12345678)
- Serial output may show: `Setup Code: 12345678`
- Or as PIN: `123-45-678`
- Enter manually in SmartThings app

#### Option C: Cannot see QR?
If QR code not visible in serial output:
1. Check logs are properly configured
2. Ensure Matter commissioning started
3. Setup code should be available (check documentation)
4. Ask device for setup code via serial console

### Step 4: Scan or Enter Code

**If scanning QR code:**
1. Point smartphone camera at screen/terminal
2. Let app scan automatically
3. It should recognize Matter device code

**If entering manually:**
1. Tap "Enter Setup Code" in SmartThings app
2. Type 8-digit code from device
3. Tap continue/next

### Step 5: Confirm Device Location

SmartThings app will prompt:

```
"Where is this device?"
├─ Living Room
├─ Bedroom
├─ Kitchen
├─ Bathroom
└─ [Custom]
```

Choose appropriate location (e.g., "Living Room")

### Step 6: Confirm Device Name

```
"What would you like to call this device?"

[Mini-Split AC]  ← Default name
or customize...
```

You can rename to:
- "AC Unit"
- "Bedroom AC"
- "Living Room Mini-Split"
- Anything descriptive

### Step 7: Wait for Commissioning

Device will:
1. Receive commissioning request via BLE
2. Join Matter fabric
3. Establish connection to hub
4. Register in SmartThings network

**Expected time:** 30-60 seconds

### Step 8: Verify in App

After commissioning completes:

```
SmartThings App
└─ Devices
   └─ [Location]
      └─ Mini-Split AC  ← Device appears here
         ├─ Power (On/Off)
         ├─ Temperature (°C display)
         ├─ Set Temperature
         ├─ Mode (Off/Heat/Cool/Auto)
         └─ Current Temp
```

## Expected Controls in SmartThings

Once commissioned, the device appears with:

### Power Control
```
On / Off
├─ Currently: ON
└─ Tap to toggle
```

### Temperature Display
```
Current Temperature
├─ Reading: 22.5°C
└─ Updates every ~30 seconds
```

### Temperature Setpoint
```
Set Temperature
├─ Current: 22°C
├─ Slider: 15°C - 30°C
└─ Increments: 0.5°C
```

### Mode Selection
```
Thermostat Mode
├─ Off
├─ Heat (heating only)
├─ Cool (cooling only)
├─ Auto (automatic switching)
└─ Current: Auto
```

## Testing Controls

After commissioning:

### Test Power Control
1. Tap "On" - device should turn on
2. Check Tuya app - mini-split should respond
3. Tap "Off" - device should turn off

### Test Temperature Setpoint
1. Slide temperature control to 24°C
2. Watch serial output: "HeatingSetpoint command: 2400"
3. Check Tuya app - temperature should update

### Test Mode Selection
1. Tap Mode dropdown
2. Select "Heat" mode
3. Watch serial output: "SystemMode command: 1"
4. Check Tuya app - mode should change

### Expected Serial Output During Control

```
I (xxx) MATTER_DEVICE: OnOff command from SmartThings: ON
I (xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (xxx) sync_task: Tuya command: Power ON
```

## Troubleshooting

### Device Not Appearing to Scan

**Symptom:** Camera doesn't recognize code/device not showing in SmartThings discovery

**Solutions:**
1. Verify serial output shows commissioning started
2. Check WiFi connection (logs should show connected)
3. Ensure BLE is enabled on hub and phone
4. Restart SmartThings app and try again
5. Check setup code/QR is correct

### Commissioning Fails / "Device Did Not Respond"

**Causes:**
- Hub and device on different WiFi networks
- BLE connection interrupted
- Device crashed during commissioning

**Solutions:**
1. Check both hub and device on same WiFi
2. Move hub closer to device
3. Restart device: power cycle ESP32
4. Check for Matter-specific errors in serial output
5. Verify hub has Matter enabled in settings

### Device Commissioned but No Controls Work

**Causes:**
- Matter fabric joined but device endpoints missing
- Tuya API credentials incorrect
- Device offline in Tuya app

**Solutions:**
1. Verify Tuya device is online: check in Tuya app
2. Check Tuya credentials in code
3. Review serial logs for Tuya API errors
4. Remove device from SmartThings and recommission
5. Restart both hub and device

### Can't See Device in SmartThings After Commissioning

**Causes:**
- Hub not properly joined
- Device removed from network
- Incorrect location selected

**Solutions:**
1. Go to SmartThings Settings → Hubs & Bridges
2. Verify Matter bridge is active
3. Check hub is online and connected
4. Retry commissioning from beginning

## Advanced Troubleshooting

### Enable Debug Logging

In `src/main.c`:
```c
esp_log_level_set("MATTER_DEVICE", ESP_LOG_DEBUG);
esp_log_level_set("esp_matter", ESP_LOG_DEBUG);
```

Then rebuild, flash, and retry commissioning.

### Check Matter Logs

```bash
# In serial monitor, look for:
I (xxx) esp_matter: [Matter log messages]
D (xxx) MATTER_DEVICE: [Debug output]
```

### Verify Device Endpoint

Serial output should show:
```
I (xxx) MATTER_DEVICE: Matter device initialized successfully on endpoint 1
```

If you don't see this, device initialization failed.

### Manual Device Reset

If stuck, power-cycle ESP32:
1. Unplug USB or power connector
2. Wait 5 seconds
3. Reconnect power
4. Device will restart and restart commissioning

## After Commissioning

### Verify Device Works

1. **Power Control:** Toggle on/off in SmartThings
2. **Temperature:** Adjust setpoint and watch update
3. **Mode:** Switch between Off/Heat/Cool/Auto
4. **Status:** Check current temperature reads
5. **Tuya App:** Verify changes visible there too

### Monitor Integration

In serial terminal, watch for:
```
I (xxx) sync_task: Getting device status for: eb11d9ff75ef37d109pihg
I (xxx) TUYA_CLIENT: Device status retrieved: switch=1, temp_current=2200, temp_set=2200
I (xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (xxx) MATTER_DEVICE: Matter LocalTemperature updated: 2200 (22.0°C)
```

### Set Up Automations (Optional)

Once commissioned, you can create SmartThings automations:

1. **Temperature-based:** Turn off if temp > 28°C
2. **Schedule:** Turn on at 7 AM
3. **Presence:** Turn on when home
4. **Notifications:** Alert if temp drops below 16°C

## Reference: QR Code Format

Matter QR code encodes:
- Vendor ID
- Product ID
- Setup PIN
- Device info

If manual setup code is needed, format is:
- 8 digits total
- May be displayed as: XXX-XX-XXX

## Reference: Matter Fabric

After commissioning:
- Device joins Matter fabric
- Hub becomes controller
- Commands routed through hub
- Thread mesh formed (if available)
- WiFi fallback (always available)

## Common Setup Codes

If you need to manually enter a code:
- Format: 8 digits or 3-digit-2-digit-3-digit
- Example: 12345678 or 123-45-678
- Check device serial output or Matter spec

## Next Steps After Commissioning

1. ✅ Device commissioned to SmartThings
2. ✅ Controls verified working
3. ⏳ Move to Phase 3: Bidirectional integration
4. ⏳ Implement command routing
5. ⏳ Test full end-to-end automation

See [PHASE2_MATTER.md](PHASE2_MATTER.md) for Matter details.

## Reference Links

- [SmartThings Matter Support](https://support.smartthings.com)
- [Matter Specification](https://csa-iot.org/matter_spec)
- [ESP-Matter Commissioning](https://docs.espressif.com/projects/esp-matter/en/latest/getting_started.html)
- [SmartThings Developer](https://developer.smartthings.com)

## Support

If you encounter issues during commissioning:

1. Check logs in [PHASE2_MATTER.md](PHASE2_MATTER.md#troubleshooting)
2. Review [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md#troubleshooting)
3. Enable debug logging (see "Advanced Troubleshooting" above)
4. Check Matter SDK logs for specific errors
5. Verify all prerequisites are met

Good luck with commissioning! 🎉
