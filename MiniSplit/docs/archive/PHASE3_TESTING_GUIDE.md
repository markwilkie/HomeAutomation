# Phase 3 Testing Guide & Checklist

Complete this guide to verify Phase 3 bidirectional integration works correctly.

## Pre-Test Checklist

Before starting tests:

- [ ] Firmware built successfully (no errors)
- [ ] Flashed to ESP32-S3
- [ ] Serial monitor connected at 115200 baud
- [ ] WiFi SSID and password configured
- [ ] Tuya credentials configured
- [ ] Device powered on and starting
- [ ] SmartThings app installed on phone
- [ ] SmartThings Hub available and connected
- [ ] Device commissioned to SmartThings
- [ ] Mini-split AC unit online in Tuya app

## Boot Sequence Verification

### Test 1: Startup Logs

**What to watch for:**

```
I (xxx) MAIN: === MiniSplit Matter Bridge Starting ===
I (xxx) MAIN: Initializing Tuya client...
I (xxx) TUYA_CLIENT: Tuya client initialized for device: eb11d9ff75ef37d109pihg
I (xxx) MAIN: Initializing Matter device...
I (xxx) MATTER_DEVICE: Matter device initialized successfully on endpoint 1
I (xxx) MAIN: Starting Matter commissioning...
I (xxx) MATTER_DEVICE: Matter commissioning started
I (xxx) sync_task: Status synchronization task started (interval: 30000ms)
I (xxx) command_task: Command routing task started (interval: 5000ms)
I (xxx) health_task: Health monitoring task started
I (xxx) MAIN: === MiniSplit Matter Bridge Ready ===
```

**Expected time:** 10-15 seconds after power on

**Pass Criteria:**
- ✅ All initialization messages appear
- ✅ No error messages
- ✅ Tasks start in correct order
- ✅ Ready message appears

### Test 2: Initial WiFi Connection

**Expected logs:**

```
I (xxx) MAIN: WiFi initialization complete
I (xxx) main: WiFi STA started, connecting...
I (xxx) main: WiFi connected, IP: 192.168.1.XXX
```

**Pass Criteria:**
- ✅ WiFi connects within 10 seconds
- ✅ IP address assigned
- ✅ No repeated reconnection attempts

---

## Phase 3: Status Synchronization (Tuya → Matter)

### Test 3: Initial Status Poll

**Setup:**
1. Let device run for 35 seconds after boot
2. Open SmartThings app
3. Navigate to Mini-Split AC device

**What to watch for in serial monitor:**

```
I (30xxx) sync_task: Tuya Status Update:
I (30xxx) sync_task:   Power: ON
I (30xxx) sync_task:   Current Temp: 22.5°C
I (30xxx) sync_task:   Set Temp: 23.0°C
I (30xxx) sync_task:   Mode: Auto
I (30xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (30xxx) MATTER_DEVICE: Matter LocalTemperature updated: 2250 (22.5°C)
```

**Expected timing:**
- First poll: ~30 seconds after boot
- Subsequent: Every 30 seconds

**Pass Criteria:**
- ✅ Status appears after ~30s
- ✅ Tuya data matches actual device
- ✅ Matter attributes updated
- ✅ SmartThings shows same values

### Test 4: Continuous Polling

**Setup:**
1. Leave device running for 2 minutes
2. Watch serial output

**Expected pattern:**
```
I (30xxx) sync_task: Tuya Status Update:
  [status data]
...
I (60xxx) sync_task: Tuya Status Update:
  [status data]
...
I (90xxx) sync_task: Tuya Status Update:
  [status data]
```

**Pass Criteria:**
- ✅ Poll happens every ~30 seconds
- ✅ No gaps longer than 35 seconds
- ✅ Status logged each time
- ✅ No errors or crashes

### Test 5: Status Reflects Real Changes

**Setup:**
1. Device running and polled once
2. Open Tuya app
3. Manually adjust mini-split (change temp or mode)
4. Watch serial output

**Expected behavior:**
- Within 5 seconds: Change visible in Tuya app
- Within 35 seconds: Change visible in SmartThings
- Within 35 seconds: Serial shows updated status

**Logs should show:**

```
I (60xxx) sync_task: Tuya Status Update:
I (60xxx) sync_task:   Set Temp: 24.0°C  [← NEW VALUE]
I (60xxx) MATTER_DEVICE: Matter HeatingSetpoint updated: 2400 (24.0°C)
```

**Pass Criteria:**
- ✅ Change detected in next poll cycle
- ✅ Matter attribute updated
- ✅ SmartThings reflects change
- ✅ Latency < 35 seconds

---

## Phase 3: Command Routing (Matter → Tuya)

### Test 6: Power Command Routing

**Setup:**
1. Open SmartThings app
2. Locate Mini-Split AC device
3. Tap power button to turn ON

**Expected serial output (within 5 seconds):**

```
D (xxxxx) MATTER_DEVICE: OnOff command from SmartThings: ON
I (xxxxx) command_task: Processing OnOff command from SmartThings
I (xxxxx) sync_task: Tuya Status Update:
I (xxxxx) sync_task:   Power: ON  [← CONFIRMED]
```

**Pass Criteria:**
- ✅ Command detected in SmartThings
- ✅ Matter log shows command received
- ✅ UI responds immediately (optimistic)
- ✅ Next status poll confirms change in Tuya
- ✅ Device responds (AC turns on/off)

### Test 7: Temperature Setpoint Command

**Setup:**
1. Open SmartThings device
2. Locate "Set Temperature" control
3. Adjust slider to 20°C

**Expected sequence:**

```
[User moves slider to 20°C in SmartThings]
    ↓ (immediate)
SmartThings shows: "20°C" (optimistic UI)
    ↓ (within 5 seconds)
D (xxxxx) MATTER_DEVICE: HeatingSetpoint command: 2000 (20.0°C)
I (xxxxx) command_task: Processing heating setpoint command: 20.0°C
I (xxxxx) TUYA_CLIENT: Setting temperature to: 2000
I (xxxxx) TUYA_CLIENT: HTTP Status: 200
I (xxxxx) command_task: Temperature command sent successfully
    ↓ (within 30 seconds)
I (xxxxx) sync_task: Tuya Status Update:
I (xxxxx) sync_task:   Set Temp: 20.0°C  [← CONFIRMED]
```

**Pass Criteria:**
- ✅ Command detected within 5 seconds
- ✅ Tuya API call succeeds (HTTP 200)
- ✅ Device receives command
- ✅ Next poll confirms change
- ✅ No crashes or errors

### Test 8: Mode Command Routing

**Setup:**
1. Open SmartThings device
2. Locate "Thermostat Mode" control
3. Tap to open mode selector
4. Select "Heat"

**Expected output:**

```
D (xxxxx) MATTER_DEVICE: SystemMode command: 1  [1=Heat]
I (xxxxx) command_task: Processing mode command from SmartThings: 1
I (xxxxx) TUYA_CLIENT: Setting mode to: 1
I (xxxxx) command_task: Mode command sent successfully
```

**Pass Criteria:**
- ✅ All modes work: Off, Heat, Cool, Auto
- ✅ Commands sent successfully
- ✅ Device responds in Tuya app
- ✅ No errors

---

## WiFi Resilience Testing

### Test 9: WiFi Disconnection & Reconnection

**Setup:**
1. Device running normally
2. Pause WiFi (disconnect modem or move device away)
3. Wait 10 seconds
4. Reconnect WiFi

**Expected serial output:**

```
I (xxxxx) main: WiFi connected, IP: 192.168.1.100
...
W (xxxxx) main: WiFi disconnected (count: 1), attempting reconnect...
...
I (xxxxx) main: WiFi connected, IP: 192.168.1.100
```

**What happens:**
- Status polls will fail (no network)
- Logs show retry attempts
- WiFi reconnects automatically
- Polling resumes

**Pass Criteria:**
- ✅ Disconnection detected
- ✅ Auto-reconnect triggered
- ✅ No manual intervention needed
- ✅ Polling resumes after reconnect

### Test 10: Multiple Disconnections

**Setup:**
1. Repeat Test 9 three times
2. Watch health monitoring

**Watch for:**

```
I (60000) health_task: === System Health Check ===
I (60000) health_task: WiFi Disconnects: 3  [← COUNT INCREASES]
I (60000) health_task: Status Poll Failures: 0
I (60000) health_task: Free Heap: 185432 bytes
```

**Pass Criteria:**
- ✅ Disconnect counter increments
- ✅ Device recovers each time
- ✅ No memory leaks
- ✅ Still responsive

---

## Error Handling Testing

### Test 11: Network Timeout Recovery

**Setup:**
1. Connect to poor WiFi (long range, interference)
2. Monitor logs for 2 minutes

**Expected behavior:**

```
W (xxxxx) sync_task: Status poll attempt 1 failed, retrying in 2000ms...
W (xxxxx) sync_task: Status poll attempt 2 failed, retrying in 2000ms...
W (xxxxx) sync_task: Status poll attempt 3 failed
E (xxxxx) sync_task: Failed to get Tuya status (failures: 1)
```

**Pass Criteria:**
- ✅ Retries occur automatically
- ✅ Device doesn't crash
- ✅ Failures tracked
- ✅ Next cycle attempts again

### Test 12: Handle Offline Device

**Setup:**
1. Device running normally
2. Turn off mini-split AC physically
3. Let device run for 5 minutes
4. Watch for offline detection

**Expected output (after multiple failures):**

```
E (xxxxx) sync_task: Failed to get Tuya status (failures: 5)
W (xxxxx) sync_task: Multiple status poll failures - device may be offline
```

**Pass Criteria:**
- ✅ Offline detection after 5 failures
- ✅ Warning logged
- ✅ Device remains stable
- ✅ Retries continue

---

## Health Monitoring Testing

### Test 13: Health Check Logs

**Setup:**
1. Device running normally
2. Watch logs for 70 seconds

**Expected output (once every 60 seconds):**

```
I (60000) health_task: === System Health Check ===
I (60000) health_task: WiFi Disconnects: 0
I (60000) health_task: Status Poll Failures: 0
I (60000) health_task: Last Status Update: 30020ms ago
I (60000) health_task: Free Heap: 185432 bytes
```

**Pass Criteria:**
- ✅ Health check appears every 60s ± 5s
- ✅ All metrics reasonable
- ✅ Memory not growing
- ✅ No errors

---

## Integration Tests

### Test 14: Full Cycle - User Makes Change

**Setup:**
1. Device fully running
2. SmartThings app open to device

**Do:**
1. Change temperature in SmartThings to 25°C
2. Wait 35 seconds
3. Check Tuya app
4. Verify AC unit responded

**Expected sequence:**

```
Time 0s:     SmartThings shows 25°C (optimistic)
Time 2s:     Serial: "Processing heating setpoint command: 25.0°C"
Time 3s:     Serial: "Temperature command sent successfully"
Time 5s:     Tuya app (if checked) might show 25°C
Time 30s:    Serial: Status poll shows 25°C
Time 35s:    SmartThings confirmed (from status update)
Time 45s:    AC unit actually at 25°C (physical response)
```

**Pass Criteria:**
- ✅ SmartThings responds immediately
- ✅ Command sent to Tuya
- ✅ Next status poll confirms
- ✅ Tuya app shows change
- ✅ AC unit responds

### Test 15: Tuya App Makes Change

**Setup:**
1. Device running
2. SmartThings app open
3. Tuya app in background

**Do:**
1. Switch to Tuya app
2. Change AC mode to "Heating"
3. Wait 35 seconds
4. Switch to SmartThings

**Expected:**

```
Time 0s:     Tuya app shows mode change
Time ~5s:    Mode transmitted to AC
Time 30s:    Status poll detects change
Time 32s:    SmartThings updated
Time 35s:    SmartThings shows "Heat" mode
```

**Pass Criteria:**
- ✅ Tuya change detected
- ✅ Status poll picks it up
- ✅ SmartThings updates
- ✅ Latency < 35 seconds

---

## Stability Testing

### Test 16: 1-Hour Stability Test

**Setup:**
1. Device running with all tasks active
2. No user interaction

**What to monitor:**
- Does device continue polling every 30s?
- No error messages?
- Memory stable?
- No crashes?

**Expected logs (sampling every 30 seconds):**

```
I (30000) sync_task: Tuya Status Update: [data]
I (60000) health_task: System Health Check: [healthy stats]
I (60000) sync_task: Tuya Status Update: [data]
I (90000) health_task: System Health Check: [healthy stats]
...
[repeats for 1 hour]
```

**Pass Criteria:**
- ✅ No crashes in 1 hour
- ✅ Consistent polling every 30s
- ✅ Memory usage stable
- ✅ No error spikes

### Test 17: Multiple Commands in Sequence

**Setup:**
1. Device running normally
2. Make rapid changes in SmartThings

**Do:**
1. Toggle power: OFF
2. Wait 2 seconds
3. Toggle power: ON
4. Wait 2 seconds
5. Change temperature to 22°C
6. Change temperature to 26°C
7. Change mode: Cool
8. Change mode: Auto

**Expected:**
- Each command detected
- Each command sent to Tuya
- Status updates reflect all changes
- No commands lost

**Pass Criteria:**
- ✅ All commands processed
- ✅ Commands in correct order
- ✅ No queuing issues
- ✅ No crashes

---

## Diagnostic Logs

### Enabling Debug Logging

If you need more detail:

```c
// Add to src/main.c before app_main returns:
esp_log_level_set("TUYA_CLIENT", ESP_LOG_DEBUG);
esp_log_level_set("MATTER_DEVICE", ESP_LOG_DEBUG);
esp_log_level_set("MAIN", ESP_LOG_DEBUG);
```

Then rebuild and flash.

### Key Log Markers

| Log | Meaning |
|-----|---------|
| "Tuya Status Update:" | Polling successful |
| "Processing ... command" | Command detected |
| "Temperature command sent successfully" | API call succeeded |
| "WiFi disconnected" | Network issue |
| "Multiple status poll failures" | Device may be offline |
| "System Health Check" | Diagnostic snapshot |

---

## Test Results Template

Use this to track results:

```
Device: [ESP32-S3 / ESP32-C3]
Firmware Build: [date/time]
WiFi: [SSID]
SmartThings Hub: [model]

TEST RESULTS:
-----------
Test 1 (Boot Sequence): PASS / FAIL
Test 2 (WiFi Connection): PASS / FAIL
Test 3 (Initial Status Poll): PASS / FAIL
Test 4 (Continuous Polling): PASS / FAIL
Test 5 (Status Reflects Changes): PASS / FAIL
Test 6 (Power Command): PASS / FAIL
Test 7 (Temperature Command): PASS / FAIL
Test 8 (Mode Command): PASS / FAIL
Test 9 (WiFi Disconnect/Reconnect): PASS / FAIL
Test 10 (Multiple Disconnects): PASS / FAIL
Test 11 (Network Timeout Recovery): PASS / FAIL
Test 12 (Handle Offline): PASS / FAIL
Test 13 (Health Monitoring): PASS / FAIL
Test 14 (Full SmartThings Cycle): PASS / FAIL
Test 15 (Full Tuya Cycle): PASS / FAIL
Test 16 (1-Hour Stability): PASS / FAIL
Test 17 (Rapid Commands): PASS / FAIL

OVERALL: PASS / FAIL

Issues Found:
[list any problems]

Notes:
[additional observations]
```

---

## When Tests Pass

Congratulations! Phase 3 is verified. You can now:

1. ✅ Operate device from SmartThings
2. ✅ See real-time status updates
3. ✅ Control from Tuya or SmartThings (both work)
4. ✅ Device is resilient to network issues
5. ✅ System is stable for extended use

**Next Step:** Start Phase 4 (Advanced Features) or consider this project complete if Phase 4 features not needed.

## When Tests Fail

If any test fails:

1. **Check serial logs** - Look for error messages
2. **Enable debug logging** - Get more details
3. **Verify configuration** - WiFi SSID, Tuya credentials
4. **Check device online** - Tuya app, SmartThings app
5. **Restart device** - Power cycle ESP32
6. **Review error code** - Check esp_err_to_name() outputs
7. **Consult documentation** - See PHASE3_INTEGRATION.md troubleshooting

See [PHASE3_INTEGRATION.md](PHASE3_INTEGRATION.md#troubleshooting-phase-3) for detailed troubleshooting.

---

## Success!

Once all tests pass, you have a fully functional Matter-enabled Tuya bridge. You can:

- ✅ Control AC from SmartThings
- ✅ See live temperature
- ✅ Monitor from anywhere (via SmartThings hub)
- ✅ Create automations in SmartThings
- ✅ Switch between SmartThings and Tuya apps seamlessly
- ✅ Rely on resilient bidirectional sync

Enjoy your smart mini-split! 🎉
