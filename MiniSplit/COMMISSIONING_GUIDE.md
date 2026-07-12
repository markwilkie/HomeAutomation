# Matter Commissioning Guide for Home Assistant

This guide walks through commissioning the MiniSplit Matter Bridge to Home Assistant over Thread.

> Looking for the old SmartThings/WiFi procedure? See
> [Legacy: SmartThings (WiFi build)](#legacy-smartthings-wifi-build) at the bottom of this file.
> That path was written and verified against an earlier firmware build that joined over WiFi;
> the firmware now joins over Thread, so it's kept for reference/rollback only.

## Prerequisites Checklist

Before attempting commissioning:

- [ ] ESP32-C6 with firmware flashed (Thread-enabled build — see `sdkconfig.defaults`). Verify
      `CONFIG_OPENTHREAD_SRP_CLIENT=y` and `# CONFIG_USE_MINIMAL_MDNS is not set` actually made it
      into the built `sdkconfig` (not just `sdkconfig.defaults`) — see
      [Device Joins Thread But Commissioning Never Completes](#device-joins-thread-but-commissioning-never-completes-operational-discovery-timeout)
      below if you skip this and hit that symptom.
- [ ] Device powered on and running
- [ ] OpenThread Border Router (OTBR) running and has a Thread network formed —
      see `../Wyse5070DebSetup/setup-mg24.sh`; check its web UI for network diagnostics
- [ ] Jool configured as the NAT64 gateway — `../Wyse5070DebSetup/setup-mg24.sh` calls
      `setup-nat64-jool.sh` automatically, but if you're troubleshooting, confirm with
      `docker exec otbr sh -c "ot-ctl nat64 state"` (should show `Disabled` for both — OTBR's own
      translator is deliberately turned off in favor of Jool, see
      [ARCHITECTURE.md](ARCHITECTURE.md#nat64-jool-not-otbrs-built-in-translator))
- [ ] BlueZ running on whatever host runs `matterjs-server` — see
      `../Wyse5070DebSetup/setup-bluetooth.sh`. Needed to hand the Thread dataset to the device
      during BLE commissioning; `matterjs-server`'s own websocket server-info message should
      show `"bluetooth_enabled": true`
- [ ] `matterjs-server` has the Thread network's credentials loaded — check its websocket
      server-info message for `"thread_credentials_set": true`. **This resets to `false` on every
      `matterjs-server` restart** (including a host reboot) and silently blocks Thread
      commissioning until re-pushed — see `../Wyse5070DebSetup/setup-thread-credentials-sync.sh`,
      which does this automatically via a systemd service, but verify it's actually `enabled`
      (`systemctl is-enabled thread-credentials-sync.service`) if you're setting this up fresh.
- [ ] Home Assistant running (Container install) with the `matterjs-server` container up —
      see `../Wyse5070DebSetup/setup-matter-server.sh`
- [ ] Matter integration added in Home Assistant (Settings → Devices & Services → Add
      Integration → Matter), pointed at `ws://localhost:5580/ws`

## Serial Output Verification

Before commissioning, verify device is ready. Note there's **no WiFi connection line** in this
build — the device starts BLE/Matter commissioning immediately, before any network is up, and
only joins Thread once commissioning hands it a dataset:

```
I (xxx) MAIN: === MiniSplit Matter Bridge Initializing ===
I (xxx) MATTER_DEVICE: Matter device initialized: thermostat_ep=1 ...
I (xxx) MATTER_DEVICE: Starting Matter commissioning...
I (xxx) MATTER_DEVICE: Matter commissioning started
I (xxx) MAIN: Waiting for network connectivity (commission via Home Assistant if not already paired)...
```

If you see this output, the device is ready for commissioning. It will sit at "Waiting for
network connectivity..." indefinitely until commissioned — that's expected on first boot.

## Step-by-Step Commissioning

### Step 1: Open Home Assistant

1. Open Home Assistant in a browser
2. Go to **Settings → Devices & Services**
3. Click **Add Integration** (or, if Matter is already added, use its **Add device** action)

### Step 2: Add a Matter Device

```
Settings → Devices & Services
└─ "+ Add Integration" (or Matter integration's "Add device")
   └─ Search "Matter" if adding fresh
      └─ "Scan QR code" or "Enter setup code manually"
```

### Step 3: Get Setup Code

#### Option A: QR Code (Easiest)
- Check ESP32 serial output for the QR code / onboarding payload printed by
  `PrintOnboardingCodes()` in `matter_device.cpp`
- Point your phone/browser camera at it if scanning via a mobile HA session, or use the manual
  code if driving HA from a desktop browser

#### Option B: Manual Setup Code
- Setup code format: 8-digit number (e.g., 12345678), sometimes shown as `123-45-678`
- Enter it directly in the HA Matter add-device flow

### Step 4: Commissioning + Thread Provisioning

Unlike the old WiFi build, this step now does two things at once:
1. Matter fabric commissioning (as before)
2. **Thread network provisioning** — HA's Matter Server, working with OTBR, hands the device a
   Thread Operational Dataset over BLE. The device joins the Thread mesh as part of this exchange,
   not before it.

**Expected time:** 30–90 seconds — a bit longer than the old WiFi flow since Thread attachment
happens as part of commissioning rather than beforehand.

### Step 5: Verify in Home Assistant

After commissioning completes, the device should appear under **Settings → Devices & Services →
Matter**, exposing (per the endpoint layout in
[TUYA_DP_REFERENCE.md](TUYA_DP_REFERENCE.md#matter-endpoint-layout)):

```
Home Assistant
└─ Devices
   └─ Mini-Split AC
      ├─ Climate entity (power, mode, setpoints, current temp)
      ├─ Room Temp / Room Humidity sensors
      ├─ Outside Temp sensor
      └─ Compressor load / cycling entities
```

If entities are missing, this is the thing worth double-checking first — see the note at the top
of [README.md](README.md) about HA's Matter Server not needing the SmartThings-style custom
driver workaround (unverified, flag it if endpoints are still missing here).

## Testing Controls

### Test Power Control
1. Toggle the climate entity's power in HA — device should turn on/off
2. Check the Tuya app — mini-split should respond within the ~5s command-poll interval

### Test Temperature Setpoint
1. Change the setpoint in HA
2. Watch serial output: `Processing heating setpoint command: ...`
3. Check Tuya app — temperature should update

### Test Mode Selection
1. Change mode in HA (Off/Heat/Cool/Auto)
2. Watch serial output: `Processing mode command from controller: ...`
3. Check Tuya app — mode should change

### Expected Serial Output During Control

```
I (xxx) MATTER_DEVICE: OnOff command from controller: ON
I (xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (xxx) sync_task: Tuya command: Power ON
```

## Troubleshooting

The three issues below were all hit and root-caused on real hardware during this project's
WiFi→Thread migration, roughly in the order you're likely to hit them. Check them in order before
digging elsewhere — each has a specific, verifiable signature.

### Home Assistant / matter-server Reports "Commissioning Failed" But the Device Is Actually Working

**Symptom:** The commissioning UI (HA or matter-server's own web UI) shows a failure, but the
device's serial log shows it completed `AddNOC`, joined Thread, and is happily polling Tuya's API
— it just took a while to get there.

**Cause:** Matter's `ArmFailSafe` timer caps how long the commissioner will wait for the device to
prove it's operational before giving up (the device requests progressively longer windows on
retry — 60s, then 94s, then 120s in practice). If anything upstream of the device is slow — most
notably a broken/slow NAT64 gateway making DNS/NTP take minutes instead of milliseconds (see the
next section) — the whole commissioning flow can blow past that window. The device doesn't "undo"
a successful `AddNOC` just because the commissioner stopped waiting, so it finishes on its own
after the UI has already reported failure.

**Solutions:**
1. Check matter-server's logs (`docker logs matter-server`) for the actual commissioning
   transcript rather than trusting the UI's summary verdict alone.
2. If the device's own serial log shows `Time synchronized` within a few seconds of Thread
   attach (not tens of seconds), NAT64 is healthy and this isn't the cause — look at the next
   section instead.
3. If the device did complete commissioning despite the UI saying otherwise, a clean recommission
   (remove the device, retry) is worth doing once NAT64 is confirmed fast, mainly to avoid
   orphaned fabric entries — but the device is already functional either way.

### Device Joins Thread and Works Fine, But Commissioning Never Completes ("Operational Discovery" Timeout)

**Symptom:** The device joins Thread, resolves DNS, syncs NTP, and talks to Tuya's API
successfully — it's fully functional — but the commissioner's UI hangs or times out on the final
step, and matter-server's logs show repeated `Timeout waiting for mDNS resolution` /
`operational discovery failed... Timeout` over several minutes before giving up. Checking
`docker exec otbr sh -c "ot-ctl srp server host"` on the OTBR host shows **nothing registered**,
even while the device is actively attached to the mesh.

**Cause:** The firmware's `sdkconfig` had `CONFIG_USE_MINIMAL_MDNS=y` (the connectedhomeip
default) — this makes the device advertise its Matter service via raw mDNS multicast sent
directly over the Thread mesh interface, which never crosses the border router to reach a LAN-side
commissioner. `CONFIG_OPENTHREAD_SRP_CLIENT` (which registers the service with OTBR, which *does*
bridge to real LAN mDNS) was never enabled — it is **not** implied by `CONFIG_OPENTHREAD_ENABLED`,
it's a separate, easy-to-miss toggle. With neither path correctly wired for a Thread-only build,
every DNS-SD call in the firmware silently compiled to a no-op. This is fixed in
`sdkconfig.defaults` (`CONFIG_OPENTHREAD_SRP_CLIENT=y` / `# CONFIG_USE_MINIMAL_MDNS is not set`),
but **requires `idf.py fullclean`** to actually take effect — see the note below.

**Solutions:**
1. Confirm the built `sdkconfig` (not just `sdkconfig.defaults`) actually has
   `CONFIG_OPENTHREAD_SRP_CLIENT=y` and `# CONFIG_USE_MINIMAL_MDNS is not set`. Since
   `USE_MINIMAL_MDNS` defaults to `y` at the Kconfig level, your local `sdkconfig` almost
   certainly already has it explicitly written as `y` from a prior build — and per ESP-IDF's
   merge rules, `sdkconfig.defaults` only fills in values that *aren't already set*, so an
   incremental build won't pick up this change. Run `idf.py fullclean` before rebuilding.
2. After a fresh boot with the fix in place, watch serial output for `SrpClient` state
   transitions right after Thread attach, and confirm
   `docker exec otbr sh -c "ot-ctl srp server host"` shows the device's host registration within
   a few seconds — not just `Done` with nothing listed.
3. If it's still empty, check `docker exec otbr sh -c "ot-ctl srp server state"` is `running`, and
   that the device is actually attached (`docker exec otbr sh -c "ot-ctl child table"` — devices
   attach as children before optionally becoming routers).

### Can't Commission a *New* Device, Even Though an Existing One Still Works

**Symptom:** An already-commissioned device keeps working fine, but commissioning a different (or
newly-flashed) Thread device fails immediately, often without the device even progressing past
BLE.

**Cause:** `matterjs-server`'s own knowledge of the Thread network's credentials
(`thread_credentials_set` in its websocket server-info message) lives only in memory and resets
to `false` on every container restart — including after a host reboot. This doesn't affect
already-commissioned devices (Thread network operation is entirely between the device and OTBR,
independent of matter-server), but it silently blocks commissioning *new* Thread devices until
the credentials are re-pushed.

**Solutions:**
1. Check matter-server's websocket server-info message (or `docker logs matter-server`) for
   `thread_credentials_set`. If `false`, run `../Wyse5070DebSetup/setup-thread-credentials-sync.sh`
   to push OTBR's active dataset into matter-server.
2. Confirm the `thread-credentials-sync.service` systemd unit is enabled
   (`systemctl is-enabled thread-credentials-sync.service`) so this reapplies automatically after
   every reboot — if it's not, re-run the script above once to install it.

### Phone Shows an Unfamiliar Thread Network During Commissioning ("Checking connectivity to Thread network ST-...")

**Symptom:** Scanning the QR code with a phone (rather than commissioning directly through
matter-server's own web UI) shows a Thread network selection/connectivity-check screen naming a
network that was never set up here — e.g. an `ST-xxxxxxxxxx`-style name — even though Home
Assistant's own Thread storage (`.storage/thread.datasets`) has no such entry and OTBR was never
configured with it.

**Cause (best understanding, not fully proven):** Android/Google Play Services maintains its
**own system-level Thread credential store**, shared across every app signed into the same Google
account on that phone — separate from whatever app is doing the commissioning. If any other app
on the device (a SmartThings app, a Google Home app, etc.) has ever shared Thread credentials for
a different network into that system store, the phone's own commissioning UI can surface that
network as an option or connectivity target, entirely independent of what Home Assistant/OTBR
knows about. This was never conclusively traced to a specific source app, and became moot once
commissioning proceeded directly through matter-server's own web UI instead of a phone — which
bypasses the phone's system Thread store entirely.

**Solutions:**
1. **Easiest:** skip the phone. Commission directly through matter-server's own web UI (or
   whatever the HA Matter integration exposes) using the manual setup code instead of a QR scan —
   this sidesteps the phone's system Thread store completely and is what actually worked here.
2. If you need to use a phone, try clearing Google Play Services' cache/data, or removing/
   forgetting the unfamiliar network from the phone's own Thread network settings (varies by
   Android version — look for a system-level "Thread networks" or "Nearby devices" settings
   page, not anything inside the commissioning app itself).
3. Don't spend time searching Home Assistant or OTBR configuration for the unfamiliar network
   name — if it's not in `.storage/thread.datasets` or `ot-ctl dataset active`, it isn't coming
   from this stack.

### Device Not Appearing / Commissioning Hangs (Generic)

**Symptom:** HA's Matter add-device flow doesn't find the device at all, or times out immediately
(distinct from the operational-discovery timeout above, which happens *after* BLE/fabric steps
succeed)

**Solutions:**
1. Verify serial output shows commissioning started and BLE advertising
2. Confirm `matterjs-server` is running and HA's Matter integration shows it connected
3. Confirm BLE is available on the host running `matterjs-server` (see Prerequisites above)
4. Restart the HA Matter integration and retry

### Commissioning Starts but Thread Join Fails

**Causes:**
- OTBR isn't running or has no Thread network formed
- Device out of Thread radio range of OTBR/existing mesh routers
- BLE connection interrupted mid-handoff

**Solutions:**
1. Check OTBR's web UI for an active Thread network and visible topology
2. Move the device physically closer to the OTBR dongle (or any FTD already in the mesh) during
   first commissioning
3. Power-cycle the ESP32 and retry
4. Check serial output for Matter/OpenThread-specific errors

### Device Commissioned but No Controls Work

**Causes:**
- Thread joined but Tuya API calls are failing — check DNS/NTP first: if `Starting SNTP time
  sync...` in the serial log is followed by repeated `Still waiting for SNTP time sync` warnings
  rather than `Time synchronized` within a few seconds, NAT64 (Jool) isn't working; check
  `docker exec otbr sh -c "ot-ctl nat64 state"` (should be `Disabled` for both — Jool handles it
  externally now, see [ARCHITECTURE.md](ARCHITECTURE.md#nat64-jool-not-otbrs-built-in-translator))
  and `jool -i nat64 session display --udp` (via the chroot pattern in `setup-nat64-jool.sh`) for
  active sessions
- Tuya API credentials incorrect
- Device offline in Tuya app

**Solutions:**
1. Verify Tuya device is online: check in Tuya app
2. Check Tuya credentials in `include/secrets.h`
3. Review serial logs for `TUYA_CLIENT` errors specifically (vs. Matter/Thread errors)
4. Remove device from HA and recommission if the fabric itself seems stuck

## Advanced Troubleshooting

### Enable Debug Logging

In `src/main.c`:
```c
esp_log_level_set("MATTER_DEVICE", ESP_LOG_DEBUG);
esp_log_level_set("esp_matter", ESP_LOG_DEBUG);
```

Then rebuild, flash, and retry commissioning.

### Check Matter/Thread Logs

```bash
# In serial monitor, look for:
I (xxx) esp_matter: [Matter log messages]
D (xxx) MATTER_DEVICE: [Debug output]
I (xxx) OPENTHREAD: [OpenThread stack messages]
```

### Verify Device Endpoint

Serial output should show:
```
I (xxx) MATTER_DEVICE: Matter device initialized: thermostat_ep=1 ...
```

If you don't see this, device initialization failed before commissioning even started.

### Manual Device Reset

If stuck, power-cycle ESP32:
1. Unplug USB or power connector
2. Wait 5 seconds
3. Reconnect power
4. Device will restart; already-commissioned devices should reattach to Thread within seconds

## After Commissioning

### Verify Device Works

1. **Power Control:** Toggle on/off in Home Assistant
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

Once commissioned, you can create Home Assistant automations:

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
- Home Assistant becomes controller
- Commands routed through HA's Matter Server
- Thread mesh formed via OTBR (required — this build has no WiFi fallback)

## Next Steps After Commissioning

1. ✅ Device commissioned to Home Assistant over Thread
2. ✅ Controls verified working
3. Power-cycle the device once to confirm it reattaches to Thread automatically (no
   re-commissioning) — validates the "already commissioned" boot path
4. Confirm Tuya API calls succeed reliably over several sync cycles (exercises OTBR's border
   routing to the internet, which this app hasn't relied on before)

See [PHASE2_MATTER.md](PHASE2_MATTER.md) for the underlying Matter cluster/endpoint details (note:
written against the original WiFi build, but the cluster/endpoint content itself is unaffected by
the transport change).

## Reference Links

- [Home Assistant Matter integration](https://www.home-assistant.io/integrations/matter/)
- [matterjs-server](https://github.com/matter-js/matterjs-server)
- [Matter Specification](https://csa-iot.org/matter_spec)
- [ESP-Matter Commissioning](https://docs.espressif.com/projects/esp-matter/en/latest/getting_started.html)
- [OpenThread](https://openthread.io/)

## Support

If you encounter issues during commissioning:

1. Check logs in [PHASE2_MATTER.md](PHASE2_MATTER.md#troubleshooting)
2. Review [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md#troubleshooting)
3. Enable debug logging (see "Advanced Troubleshooting" above)
4. Check OTBR's web UI and `matterjs-server` container logs for Thread-side issues
5. Verify all prerequisites are met

Good luck with commissioning! 🎉

---

## Legacy: SmartThings (WiFi build)

> **This section describes the original commissioning procedure, written and verified against an
> earlier firmware build that pre-connected to WiFi with baked-in credentials, then used BLE only
> for Matter fabric join.** The firmware has since switched to Thread as its network transport
> (see [README.md](README.md#technical-architecture)) — Thread has no WiFi-credential equivalent,
> so most of the WiFi-specific detail below no longer applies to the current build. Kept for
> reference in case you're running an older build, still use the SmartThings hub for other
> devices, or need a rollback path.

This guide walks through commissioning the MiniSplit Matter Bridge to SmartThings.

### Prerequisites Checklist

Before attempting commissioning:

- [ ] ESP32-C6 with firmware flashed
- [ ] Device powered on and running
- [ ] WiFi connected (check serial logs)
- [ ] SmartThings Hub with Matter support (U8000 or Hub 3+)
- [ ] Hub connected to same WiFi network
- [ ] SmartThings app installed on smartphone
- [ ] BLE enabled on hub and smartphone
- [ ] Matter enabled in hub settings

### Serial Output Verification

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

### Step-by-Step Commissioning

#### Step 1: Open SmartThings App

1. Open SmartThings app on your smartphone
2. Tap the "+" icon (typically top-right or bottom)
3. Select "Add Device" or "Scan Device"

#### Step 2: Initiate Scanning

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

#### Step 3: Get Setup Code

##### Option A: QR Code (Easiest)
- Check ESP32 serial output for QR code display
- Some Matter devices print QR code to serial terminal
- Point smartphone camera at QR code

##### Option B: Manual Setup Code
- Setup code format: 8-digit number (e.g., 12345678)
- Serial output may show: `Setup Code: 12345678`
- Or as PIN: `123-45-678`
- Enter manually in SmartThings app

##### Option C: Cannot see QR?
If QR code not visible in serial output:
1. Check logs are properly configured
2. Ensure Matter commissioning started
3. Setup code should be available (check documentation)
4. Ask device for setup code via serial console

#### Step 4: Scan or Enter Code

**If scanning QR code:**
1. Point smartphone camera at screen/terminal
2. Let app scan automatically
3. It should recognize Matter device code

**If entering manually:**
1. Tap "Enter Setup Code" in SmartThings app
2. Type 8-digit code from device
3. Tap continue/next

#### Step 5: Confirm Device Location

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

#### Step 6: Confirm Device Name

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

#### Step 7: Wait for Commissioning

Device will:
1. Receive commissioning request via BLE
2. Join Matter fabric
3. Establish connection to hub
4. Register in SmartThings network

**Expected time:** 30-60 seconds

#### Step 8: Verify in App

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

### Expected Controls in SmartThings

Once commissioned, the device appears with:

#### Power Control
```
On / Off
├─ Currently: ON
└─ Tap to toggle
```

#### Temperature Display
```
Current Temperature
├─ Reading: 22.5°C
└─ Updates every ~30 seconds
```

#### Temperature Setpoint
```
Set Temperature
├─ Current: 22°C
├─ Slider: 15°C - 30°C
└─ Increments: 0.5°C
```

#### Mode Selection
```
Thermostat Mode
├─ Off
├─ Heat (heating only)
├─ Cool (cooling only)
├─ Auto (automatic switching)
└─ Current: Auto
```

### Testing Controls

After commissioning:

#### Test Power Control
1. Tap "On" - device should turn on
2. Check Tuya app - mini-split should respond
3. Tap "Off" - device should turn off

#### Test Temperature Setpoint
1. Slide temperature control to 24°C
2. Watch serial output: "HeatingSetpoint command: 2400"
3. Check Tuya app - temperature should update

#### Test Mode Selection
1. Tap Mode dropdown
2. Select "Heat" mode
3. Watch serial output: "SystemMode command: 1"
4. Check Tuya app - mode should change

#### Expected Serial Output During Control

```
I (xxx) MATTER_DEVICE: OnOff command from SmartThings: ON
I (xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (xxx) sync_task: Tuya command: Power ON
```

### Troubleshooting

#### Device Not Appearing to Scan

**Symptom:** Camera doesn't recognize code/device not showing in SmartThings discovery

**Solutions:**
1. Verify serial output shows commissioning started
2. Check WiFi connection (logs should show connected)
3. Ensure BLE is enabled on hub and phone
4. Restart SmartThings app and try again
5. Check setup code/QR is correct

#### Commissioning Fails / "Device Did Not Respond"

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

#### Device Commissioned but No Controls Work

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

#### Can't See Device in SmartThings After Commissioning

**Causes:**
- Hub not properly joined
- Device removed from network
- Incorrect location selected

**Solutions:**
1. Go to SmartThings Settings → Hubs & Bridges
2. Verify Matter bridge is active
3. Check hub is online and connected
4. Retry commissioning from beginning

### Advanced Troubleshooting

#### Enable Debug Logging

In `src/main.c`:
```c
esp_log_level_set("MATTER_DEVICE", ESP_LOG_DEBUG);
esp_log_level_set("esp_matter", ESP_LOG_DEBUG);
```

Then rebuild, flash, and retry commissioning.

#### Check Matter Logs

```bash
# In serial monitor, look for:
I (xxx) esp_matter: [Matter log messages]
D (xxx) MATTER_DEVICE: [Debug output]
```

#### Verify Device Endpoint

Serial output should show:
```
I (xxx) MATTER_DEVICE: Matter device initialized successfully on endpoint 1
```

If you don't see this, device initialization failed.

#### Manual Device Reset

If stuck, power-cycle ESP32:
1. Unplug USB or power connector
2. Wait 5 seconds
3. Reconnect power
4. Device will restart and restart commissioning

### After Commissioning

#### Verify Device Works

1. **Power Control:** Toggle on/off in SmartThings
2. **Temperature:** Adjust setpoint and watch update
3. **Mode:** Switch between Off/Heat/Cool/Auto
4. **Status:** Check current temperature reads
5. **Tuya App:** Verify changes visible there too

#### Monitor Integration

In serial terminal, watch for:
```
I (xxx) sync_task: Getting device status for: eb11d9ff75ef37d109pihg
I (xxx) TUYA_CLIENT: Device status retrieved: switch=1, temp_current=2200, temp_set=2200
I (xxx) MATTER_DEVICE: Matter OnOff updated: ON
I (xxx) MATTER_DEVICE: Matter LocalTemperature updated: 2200 (22.0°C)
```

#### Set Up Automations (Optional)

Once commissioned, you can create SmartThings automations:

1. **Temperature-based:** Turn off if temp > 28°C
2. **Schedule:** Turn on at 7 AM
3. **Presence:** Turn on when home
4. **Notifications:** Alert if temp drops below 16°C

### Reference: QR Code Format

Matter QR code encodes:
- Vendor ID
- Product ID
- Setup PIN
- Device info

If manual setup code is needed, format is:
- 8 digits total
- May be displayed as: XXX-XX-XXX

### Reference: Matter Fabric

After commissioning:
- Device joins Matter fabric
- Hub becomes controller
- Commands routed through hub
- Thread mesh formed (if available)
- WiFi fallback (always available)

### Common Setup Codes

If you need to manually enter a code:
- Format: 8 digits or 3-digit-2-digit-3-digit
- Example: 12345678 or 123-45-678
- Check device serial output or Matter spec

### Next Steps After Commissioning

1. ✅ Device commissioned to SmartThings
2. ✅ Controls verified working
3. ⏳ Move to Phase 3: Bidirectional integration
4. ⏳ Implement command routing
5. ⏳ Test full end-to-end automation

See [PHASE2_MATTER.md](PHASE2_MATTER.md) for Matter details.

### Reference Links

- [SmartThings Matter Support](https://support.smartthings.com)
- [Matter Specification](https://csa-iot.org/matter_spec)
- [ESP-Matter Commissioning](https://docs.espressif.com/projects/esp-matter/en/latest/getting_started.html)
- [SmartThings Developer](https://developer.smartthings.com)

### Support

If you encounter issues during commissioning:

1. Check logs in [PHASE2_MATTER.md](PHASE2_MATTER.md#troubleshooting)
2. Review [MATTER_SDK_SETUP.md](MATTER_SDK_SETUP.md#troubleshooting)
3. Enable debug logging (see "Advanced Troubleshooting" above)
4. Check Matter SDK logs for specific errors
5. Verify all prerequisites are met
