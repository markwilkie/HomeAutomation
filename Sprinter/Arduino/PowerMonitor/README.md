# Bluetooth power monitor
Arduino library to interrogate BT devices and display the results on a small touch screen

The screen is pretty self explanatory, but a few notes:
- Top wifi indicator is white if connected, or black (invisible) if not
- Top BLE indicator is:  Black=off, Grey=scanning, Yellow=partially connected, Blue=fully connected
- The two big rings are the current charge (outer) and draw (inner) in amps.  Charge could be solar or alternater.
- Big number in the middle is the current net flow.  e.g. if 4A draw, and 3A charge, then the number would show 1A.
- Below the big graph is current battery volts
- Graph on left is battery state of charge
- At the bottom, time in hours is shown based on the capacity (according to the BMS) and current draw
- Graph on the right is water tank level
- There are two spark lines which show net amp flow.  The night spark is from 10pm to 7am (PST), and the day spark is last 24 hours.
- The numbers by the spark lines are the sum of the spark in amp hours  (e.g. night spark could show 12, which would mean 12ah were used)
- Bottom icons are battery and charger controller (van icon).
- Charge controller shows solar and alternater amps - plus temperature of the controller itself
- Battery shows amps draw/charge, plus temperature of the battery itself.
- There are two small circles in the battery icon.  These are the DMOS and CMOS indicators, or Discharge and Charge "switch".  If they are green, it means the battery is charging and discharging normally.
- Below the battery icon a heater icon will appear if the BMS turns the heater on

Small touch screen is the WT32-SC01 Plus by Wireless-Tag:
- ESP32S3 processor
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

---

## Recent Features (June 2025)

### Dual SOK Battery Support (December 2025)
- The system now supports two SOK batteries simultaneously.
- Create multiple `SOKReader` instances with different periphery names:
  ```cpp
  SOKReader sokReader1;  // First battery (default name: "SOK-AA12487")
  SOKReader sokReader2("SOK-AA12488");  // Second battery (custom name)
  ```
- The system automatically:
  - Scans for and connects to both batteries
  - Polls each battery in round-robin fashion with the BT2 reader
  - Combines battery data for display:
    - SOC: Averaged between both batteries
    - Voltage: Averaged between both batteries
    - Current (Amps): Summed from both batteries
    - Capacity: Summed from both batteries
- Each battery is independently monitored and logged
- Update the second battery's periphery name in the constructor to match your device

### Water Tank Analog Level Sensing
- The water tank level is now read using an analog input (GPIO10/ADC1_CH9 on ESP32-S3).
- The analog pin reads a voltage from a variable resistor (33–240Ω range).
- 0.57V is mapped to 0% (empty), and 2.73V is mapped to 100% (full).
- The code automatically constrains the output to 0–100%.

### Pump Monitoring
- A new `Pump` class monitors if the pump is running using an analog current sensor.
- If the pump runs for more than 1 minute, an output pin will begin to flash (toggle every 500ms).
- While the pump is running but before the 1-minute threshold, the output pin stays HIGH.
- The `Pump` class requires regular calls to its `update()` method (e.g., in your main loop).

### Example Usage

```cpp
// WaterTank usage
WaterTank tank;
int percentFull = tank.readAnalogLevel(); // 0-100%
