# Bluetooth power monitor
Arduino library to interrogate BT devices and display the results on a small touch screen

The screen is pretty self explanatory, but a few notes:
- Top wifi indicator is white if connected, or black (invisible) if not
- Top BLE indicator is:  Black=off, Grey=scanning, Yellow=partially connected, Blue=fully connected
- The two big rings are the current charge (outer) and draw (inner) in amps.  Charge could be solar or alternater.
- Big number in the middle is the current net flow.  e.g. if 4A draw, and 3A charge, then the number would show 1A.
- Below the big graph is current battery volts
- Two battery bar graphs on the left show state of charge for each SOK battery (Bat1 and Bat2)
- Below each battery bar: voltage (V) and hours remaining (H)
- At the bottom, the battery icon can be tapped to cycle through three display modes:
  - **Combined mode** (no label): Shows averaged temperature and amps from both batteries. CMOS/DMOS indicators use AND logic (both must be on). Heater uses OR logic (on if either is heating).
  - **SOK 1 mode** (shows '1'): Displays only SOK battery 1 data
  - **SOK 2 mode** (shows '2'): Displays only SOK battery 2 data
- Graph on the right is water tank level - tap to see detailed water level view, tap again to return
- **Water tank monitoring**: Uses resistive sender (33Ω full → 240Ω empty) with 5V rail and 240Ω voltage divider
  - Auto-calibrates on fill detection (>0.3V increase in 5 seconds)
  - Applies calibration offset to both full and empty points for consistent accuracy
  - Compensates for voltage rail variations, resistor tolerances, and ADC reference drift
- Two bar graphs show water and gas tank levels with days remaining (D) below each
- **Tank usage tracking**: Both water and gas tanks track usage patterns to predict remaining days
  - Updates every hour using weighted average (70% previous, 30% new measurement)
  - Calculates days remaining = current level / hourly percent used
  - More responsive to usage changes while filtering noise
- **Water tank monitoring**: Uses resistive sender (33Ω full → 240Ω empty) with 5V rail and 240Ω voltage divider on GPIO10
  - Auto-calibrates on fill detection (>0.3V increase in 5 seconds)
  - Applies calibration offset to both full and empty points for consistent accuracy
  - Compensates for voltage rail variations, resistor tolerances, and ADC reference drift
  - 10-sample averaging reduces noise from electrical interference
- **Gas tank monitoring**: Uses Force Sensing Resistor (FSR) with 1lb disposable propane bottles on GPIO11
  - GPIO11 requires WiFi to be disabled during reads due to hardware conflict on ESP32-S3
  - Direct ADC-to-percentage mapping (ADC 1779=empty, ADC 2349=full, includes 379 offset for plumbing weight)
  - 40-sample averaging for stable readings
- There are two spark lines which show net amp flow.  The night spark is from 10pm to 7am (PST), and the day spark is last 24 hours.
- The numbers by the spark lines are the sum of the spark in amp hours  (e.g. night spark could show 12, which would mean 12ah were used)
- Bottom icons are battery and charger controller (van icon).
- Charge controller shows solar and alternater amps - plus temperature of the controller itself
- Battery icon shows amps draw/charge, plus temperature. Displays mode-specific data based on selection.
- There are two small circles in the battery icon.  These are the DMOS and CMOS indicators, or Discharge and Charge "switch".  If they are green, it means the battery is charging and discharging normally.
- Below the battery icon a heater icon will appear if the BMS turns the heater on (in combined mode, shows if either battery is heating)

ESP32S3 Dev Notes:
- Due to the LovyanGFX library, ESP32 2.x core is needed (not 3.x)
- Be sure to enable USB CDC on boot if you want to print to serial

Small touch screen is the WT32-SC01 Plus by Wireless-Tag:
- ESP32S3 processor  (use esp32s3 dev module for Arduino IDE)
- ST7796UI touch panel
- Uses library https://github.com/lovyan03/LovyanGFX with an update (LGFX_ST32-SC01Plus.hpp)
- Includes built in Wifi and BLE

Supported BT devices as of 9/2023:
- Renogy BT2
- SOK bms that ships w/ their batteries

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