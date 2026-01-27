# Bluetooth power monitor
Arduino library to interrogate BT devices and display the results on a small touch screen

## BLE Library
This project uses **NimBLE-Arduino 2.x** for Bluetooth Low Energy communication. Key implementation notes:
- NimBLE runs as a FreeRTOS task - the main loop must call `delay(1)` to yield CPU time for BLE event processing
- Blocking operations (WiFi, HTTP requests, ADC reads) must stop BLE first to avoid notification timeouts
- SOK batteries use write-without-response (`writeValue(data, len, false)`) - the TX characteristic only supports `canWriteNoResponse`
- Sequential command pattern: only one command sent at a time, wait for response before sending next
- Connection parameters: 30-60ms interval, 0 latency, 4s supervision timeout

## BLE Architecture

### BLEManager
The `BLEManager` class handles all BLE operations including scanning, connections, reconnections, and device lifecycle:

**Initialization Flow:**
1. `init()` - Initializes NimBLE stack, creates scan object, sets up callbacks
2. `registerDevice()` - Registers BTDevice instances (BT2Reader, SOKReader) to manage
3. `startScanning()` - Begins continuous BLE scan for target devices

**Connection Flow:**
1. **Scan** - NimBLE scans for devices, `onScanResult()` callback checks each against registered device names
2. **Match Found** - When a target device is found, scan stops and `doConnect` flag is set
3. **Connect** - `poll()` detects `doConnect` flag, calls `connectToServer()`:
   - Checks for existing client to reuse (with 500ms delay before reconnect)
   - Creates new client if none exists
   - Initiates connection with 5-second timeout
4. **Service Discovery** - Device's `connectCallback()` discovers services/characteristics
5. **Notifications** - Subscribe to RX characteristic for incoming data
6. **Resume Scan** - After connection, scanning resumes to find remaining devices

**Reconnection & Backoff System:**
The BLEManager implements a per-device retry/backoff system to handle unreliable devices without constant restarts:

- `deviceRetryCount[MAX_DEVICES]` - Tracks connection attempts per device
- `deviceBackoffUntil[MAX_DEVICES]` - Timestamp when backoff ends for each device
- `BLE_MAX_RETRIES` (5) - Max attempts before entering backoff
- `BLE_BACKOFF_TIME` (30 minutes) - Duration of backoff period

**checkForDisconnectedDevices() Logic (runs every 30 seconds):**
```
For each registered device:
  If not connected:
    If in backoff (backoffUntil > now):
      Skip - device is expected to be offline
    Else if backoff expired:
      Reset retry count, log "Backoff expired, will retry"
    Increment retry count
    If retryCount >= MAX_RETRIES:
      Enter backoff mode for 30 minutes
      Log "Device not found after X attempts, backing off"
    Else:
      Log "Device not connected, attempt X/5"
      Mark as needing reconnection
  Else if connected but data is stale:
    Disconnect device, mark for reconnection
    Reset retry count
  Else (connected and current):
    Reset retry count
```

**Key Methods:**
- `startScanning()` / `turnOn()` - Start BLE scan, reset all timers
- `turnOff()` - Stop scanning, disconnect all devices (keeps clients for reuse)
- `resetStack()` - Full BLE restart: turnOff, delay, turnOn
- `isDeviceInBackoff(index)` - Check if device is in backoff (used by UI)
- `poll()` - Called from main loop, processes connections and timeouts

### BTDevice Base Class
Abstract base class for BLE device readers (BT2Reader, SOKReader):
- `getPerifpheryName()` - Returns BLE device name to match during scan
- `isConnected()` - Returns true if currently connected
- `isCurrent()` - Returns true if recently received data (not stale)
- `scanCallback()` - Called when device is found in scan
- `connectCallback()` - Called after successful connection
- `disconnectCallback()` - Called on disconnection
- `notifyCallback()` - Called when data notification received

### Display Status Colors
The ScreenController uses device status to color-code the display:
- **White (DEVICE_ONLINE)** - Device connected and receiving current data
- **Grey (DEVICE_STALE)** - Device disconnected but still trying to connect, OR connected but no recent data. Keeps last known values displayed.
- **Red (DEVICE_OFFLINE)** - Device is in backoff mode (gave up after 5 attempts). Shows zero values.

Battery meter fill also changes: normal colors when online, grey fill when stale, empty (black) when offline.

## Screen Interface

## Screen Interface

The screen is pretty self explanatory, but a few notes:
- Top wifi indicator is white if connected, or black (invisible) if not
- Top BLE indicator is:  Black=off, Grey=scanning, Yellow=partially connected, Blue=fully connected
- The two big rings are the current charge (outer) and draw (inner) in amps.  Charge could be solar or alternater.
- Big number in the middle is the current net flow.  e.g. if 4A draw, and 3A charge, then the number would show 1A.
- Below the big graph is current battery volts
- Two battery bar graphs on the left show state of charge for each SOK battery (Bat1 and Bat2)
- Below each battery bar: voltage (V) and hours remaining (H)

### Touch Interactions
- **Short tap anywhere** - Increases screen brightness (resets dim timer)
- **Long press center of screen** - Reset display (recovers from brownout/corruption)
- **Long press BLE region (top right)** - Toggle BLE on/off
- **Tap van/charger icon (bottom right)** - Show BT2 charge controller detail view with all properties:
  - Solar: voltage, current, power
  - Alternator: voltage, current, power
  - Battery: SOC, voltage, max charge current, controller temperature
  - Today's stats: amp hours, watt hours, peak current, peak power
  - Tap again to return to main screen
- **Tap battery icon** - Cycle through display modes:
  - **Combined mode** (no label): Shows averaged temperature and amps from both batteries. CMOS/DMOS indicators use AND logic (both must be on). Heater uses OR logic (on if either is heating).
  - **SOK 1 mode** (shows '1'): Displays only SOK battery 1 data
  - **SOK 2 mode** (shows '2'): Displays only SOK battery 2 data

### Tank Monitoring
- Two bar graphs show water and gas tank levels with days remaining (D) below each
- **Tank usage tracking**: Both water and gas tanks track usage patterns to predict remaining days
  - Updates every hour using weighted average (70% previous, 30% new measurement)
  - Calculates days remaining = current level / hourly percent used
  - More responsive to usage changes while filtering noise
- **Water tank**: Uses resistive sender (33Ω full → 240Ω empty) with 5V rail and 240Ω voltage divider on GPIO10
  - Auto-calibrates on fill detection (>0.3V increase in 5 seconds)
  - Applies calibration offset to both full and empty points for consistent accuracy
  - Compensates for voltage rail variations, resistor tolerances, and ADC reference drift
  - 10-sample averaging reduces noise from electrical interference
- **Gas tank**: Uses Force Sensing Resistor (FSR) with 1lb disposable propane bottles on GPIO13
  - Direct ADC-to-percentage mapping (ADC 1779=empty, ADC 2349=full, includes 379 offset for plumbing weight)
  - 40-sample averaging for stable readings, filters out failed reads

### Sparklines (Power Usage History)
Two sparkline graphs display power flow history:

**Night Sparkline** (moon icon, purple):
- Tracks overnight power usage from 10pm to 7am
- 54 data points at 10-minute intervals (9 hours total)
- Resets at 10pm each night to start fresh
- Shows net amp flow: positive = charging, negative = discharging
- The "Ah" number shows total amp-hours for the displayed period

**Day Sparkline** (calendar icon, yellow):
- Rolling 24-hour window of power usage
- 72 data points at 20-minute intervals
- Continuously shifts as new data arrives (no daily reset)
- Shows net amp flow pattern over the last 24 hours
- The "Ah" number shows total amp-hours for the displayed period

**How data is collected:**
1. Every second, the current net amps (charge - draw) is accumulated
2. At each interval (10 or 20 min), the average is calculated and added to the sparkline
3. When the sparkline is full, old data shifts out the left side as new data enters

### Other Display Elements
- Bottom icons are battery and charger controller (van icon).
- Charge controller shows solar and alternater amps - plus temperature of the controller itself
- Battery icon shows amps draw/charge, plus temperature. Displays mode-specific data based on selection.
- There are two small circles in the battery icon.  These are the DMOS and CMOS indicators, or Discharge and Charge "switch".  If they are green, it means the battery is charging and discharging normally.
- Below the battery icon a heater icon will appear if the BMS turns the heater on (in combined mode, shows if either battery is heating)

## Hardware

**ESP32S3 Dev Notes:**
- Due to the LovyanGFX library, ESP32 2.x core is needed (not 3.x)
- Be sure to enable USB CDC on boot if you want to print to serial
- Uses NimBLE-Arduino 2.x (not ArduinoBLE) for better ESP32-S3 compatibility
- Compile with: `arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc PowerMonitor.ino`

**WT32-SC01 Plus Touch Screen (by Wireless-Tag):**
- ESP32S3 processor  (use esp32s3 dev module for Arduino IDE)
- ST7796UI touch panel
- Uses library https://github.com/lovyan03/LovyanGFX with an update (LGFX_ST32-SC01Plus.hpp)
- Includes built in Wifi and BLE

**Supported BT devices as of 1/2026:**
- Renogy BT2 (BT-TH-xxxxxxxx)
- SOK BMS that ships with their batteries (SOK-xxxxxxx) - supports multiple batteries simultaneously

From [neilsheps Renogy-BT2-Reader](https://github.com/neilsheps/Renogy-BT2-Reader/tree/main) who's great work I've built on top of:

Renogy make a number of solar MPPT controllers, DC:DC converters and other devices.   Their more recent products use a bluetooth dongle known as a [BT2](https://www.renogy.com/bt-2-bluetooth-module/), but Renogy has never released any documentation on how to interrogate these devices.  By using Wireshark on an Android Phone and Renogy's BT app, I was able to decrypt the protocol used.  I built an Arduino library compatible with Adafruit's nrf52 series devices.  This library has been tested successfully with a [Renogy DCC30S device](https://www.renogy.com/dcc30s-12v-30a-dual-input-dc-dc-on-board-battery-charger-with-mppt/), and may work with other Renogy devices if the registers are known and decribed in the library header file.

## The Renogy BT2 Protocol

First, a BLE Central device scans for service 0xFFD0 and a manufacturer ID 0x7DE0 and attempts a connection.   There are two services and two characteristics that need to be set up
```
txService        0000ffd0-0000-1000-8000-00805f9b34fb     // Tx service
txCharacteristic 0000ffd1-0000-1000-8000-00805f9b34fb     // Tx characteristic. Sends data to the BT2

rxService        0000fff0-0000-1000-8000-00805f9b34fb     // Rx service
rxCharacteristic 0000fff1-0000-1000-8000-00805f9b34fb     // Rx characteristic. Receives notifications from the BT2
```
The data format is relatively simple once you read enough data.  All 2 byte numbers are big endian (MSB, then LSB).  A full list of registers and their functions is available [here](/resources).  An example data handshake looks like this:
```
ff 03 01 00  00 07 10 2a                                     Command sent to BT2 on characteristic FFD1
[]                                                           All commands seem to start with 0xFF
   []                                                        0x03 appears to be the command for "read"
      [___]                                                  Starting register to read (0x0100)
             [___]                                           How many registers to read (7)
                   [___]                                     Modbus 16 checksum, MSB first on bytes 0 - 5 inclusive

ff 03 0e 00 64 00 85 00 00 10 10 00 7a 00 00 00 00 31 68     Data response from BT2 through notification on characteristic FFF1
[]                                                           Replies also start with 0xFF
   []                                                        Acknowledges "read", I believe
      []                                                     Length of data response in bytes (usually 2x registers requested, so 14 here)
         [___]                                               Contents of register 0x0100 (Aux batt SOC, which is 100%)
               [___]                                         Contents of register 0x0101 (Aux batt voltage * 10, so 13.3v)
                     [___] [___] [___] [___] [___]           Contents of registers 0x0102 through 0x0106
                                                   [___]     Modbus 16 checksum on bytes 0 - 16 inclusive (in this case)


6d 61 69 6e 20 72 65 63 76 20 64 61 74 61 5b 66 66 5d 20 5b  20 byte ACK sent to BT2 on characteristic FFD1
 m  a  i  n     r  e  c  v     d  a  t  a  [  f  f  ]     [  it reads "main recv data[ff] [" and the ff corresponds to the first byte
                                                             of every 20 byte notification received; so a 50 byte datagram (3 notifications)
                                                             might trigger an ack sequence like "main recv data[ff] [", "main recv data[77] [",
                                                             and "main recv data[1e] [" depending on data received.  
                                                             The BT2 seems to respond OK without this ACK, but I do it also, as that's what the
                                                             Renogy BT app does
```

## The SOK BMS Protocol

SOK batteries ship with a Bluetooth-enabled BMS (Battery Management System). The protocol was reverse-engineered by capturing traffic from the SOK mobile app.

### BLE Services and Characteristics
Unlike the BT2, SOK uses a single service with two characteristics:
```
service          0000ffe0-0000-1000-8000-00805f9b34fb     // Main service for both TX and RX
txCharacteristic 0000ffe2-0000-1000-8000-00805f9b34fb     // TX - send commands (write without response only)
rxCharacteristic 0000ffe1-0000-1000-8000-00805f9b34fb     // RX - receive notifications
```

**Important:** The TX characteristic only supports `writeNoResponse` mode. Using `write` with response will fail.

### Command Format
Commands follow a simple structure:
```
ee <cmd> 00 00 00 <checksum>

ee       Prefix byte (all commands start with 0xEE)
<cmd>    Command byte (C1, C2, or C4)
00 00 00 Padding/parameters (always zeros in current implementation)
<checksum> Single-byte checksum (differs per command)
```

**Supported Commands:**
| Command | Bytes | Purpose | Response Packets |
|---------|-------|---------|------------------|
| C1 | `ee c1 00 00 00 ce` | Read CMOS/DMOS status and temperature | 2 (0xF0 + 0xF2) |
| C2 | `ee c2 00 00 00 46` | Read heating status | 2 (0xF0 + 0xF3) |
| C4 | `ee c4 00 00 00 4f` | Read protection status | 1 (0xF9) |

**Important:** Commands C1 and C2 each return **two separate notification packets** - you must wait for both before sending the next command. The first packet (0xF0) contains the base battery data (SOC, voltage, current), and the second packet contains command-specific data. This is unusual for BLE protocols where typically one command = one response.

Commands must be sent **sequentially** - wait for all response packets before sending next command. The code cycles through C1 → C2 → C4 repeatedly.

### Response Format
Responses come as notifications on the RX characteristic. All response packets start with `0xCC`:
```
cc <type> <data...>

cc       Response prefix
<type>   Packet type identifier
<data>   Variable length payload
```

**Two-Packet Response Pattern:**
Commands C1 and C2 return **two packets** each:
1. **Base packet (0xF0)** - Contains SOC, voltage, current, capacity, cycles
2. **Secondary packet (0xF2 or 0xF3)** - Contains command-specific data

Command C4 returns a **single packet (0xF9)** with protection status.

### Packet Types

#### Base Packet (0xF0) - Returned with C1 and C2
Contains primary battery data. All multi-byte values are **little-endian**.
```
Offset  Bytes  Description
------  -----  -----------
0       1      0xCC prefix
1       1      0xF0 packet type
2-3     2      Unknown/reserved
4       1      SOC (State of Charge) as percentage (0-100)
5-8     4      Voltage in 0.0001V units (divide by 10000 for volts)
9-12    4      Current in 0.001A units, signed (divide by 1000 for amps, negative = discharge)
13-16   4      Capacity in 0.001Ah units (divide by 1000 for amp-hours)
17-20   4      Cycle count
```

Example parsing:
```cpp
int soc = basePacketData[4];                                    // Direct percentage
float volts = bytesToInt(&basePacketData[5], 4, false) / 10000.0;   // e.g., 136500 → 13.65V
float amps = bytesToInt(&basePacketData[9], 4, true) / 1000.0;      // Signed, negative = discharging
float capacity = bytesToInt(&basePacketData[13], 4, false) / 1000.0; // e.g., 206000 → 206Ah
int cycles = bytesToInt(&basePacketData[17], 4, false);             // Charge/discharge cycles
```

#### CMOS/DMOS Packet (0xF2) - Response to C1
Contains charge/discharge MOSFET status and temperature.
```
Offset  Bytes  Description
------  -----  -----------
0       1      0xCC prefix
1       1      0xF2 packet type
2-18    17     Unknown/cell data
19-20   2      Temperature in 0.1°C units (divide by 10)
21      1      DMOS flag (discharge MOSFET: 1 = enabled)
22      1      CMOS flag (charge MOSFET: 1 = enabled)
```

#### Heating Packet (0xF3) - Response to C2
Contains heater status (SOK batteries have built-in heaters for cold weather).
```
Offset  Bytes  Description
------  -----  -----------
0       1      0xCC prefix
1       1      0xF3 packet type
2       1      Heating flag (1 = heater active)
```

#### Protection Packet (0xF9) - Response to C4
Contains protection status flags. This packet is monitored with a counter - if no 0xF9 packets received for 50+ read cycles, protection is assumed inactive.
```
Offset  Bytes  Description
------  -----  -----------
0       1      0xCC prefix
1       1      0xF9 packet type
2       1      Protection flag (non-zero = protection triggered)
```

### Timing and Sequence
```
1. Connect to SOK device (matches "SOK-" prefix during scan)
2. Discover service 0xFFE0
3. Subscribe to notifications on 0xFFE1
4. Send C1 command on 0xFFE2
5. Wait for 0xF0 base packet + 0xF2 secondary packet
6. Send C2 command
7. Wait for 0xF0 base packet + 0xF3 secondary packet  
8. Send C4 command
9. Wait for 0xF9 packet
10. Repeat from step 4
```

The code validates responses by:
- Checking 0xCC prefix byte
- Matching expected secondary packet type (0xF2 after C1, 0xF3 after C2)
- Tracking two-packet sequences (must receive both before sending next command)

### Data Freshness
- `SOK_BLE_STALE` (120 seconds) - Data considered stale if no updates received
- `lastHeardTime` updated on each successful notification
- `isCurrent()` returns false if data is stale