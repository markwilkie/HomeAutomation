# SoilerGateway

**Version:** 0.9.0

A WiFi and LoRa gateway for soil moisture monitoring, designed for ESP32 microcontrollers.
Receives readings from remote `SoilerClient` sensor nodes over LoRa and publishes them to
Home Assistant via MQTT. See [HOME_ASSISTANT_MIGRATION_PLAN.md](../HOME_ASSISTANT_MIGRATION_PLAN.md)
for the full architecture and rationale — this used to report to a SmartThings hub and call the
Rachio API directly; that coupling has been removed. Zone mapping, moisture calibration, and
Rachio updates now live in Home Assistant automations instead of firmware.

## Features

- **LoRa Communication**: Receives soil moisture data from remote sensors using LoRa radio (915MHz)
- **MQTT / Home Assistant**: Publishes each sensor's reading to `soiler/<id>/state`, with a Last
  Will on `soiler/gateway/status` for availability
- **OLED Display**: Real-time status display using SSD1306 OLED
- **Over-The-Air Updates**: Support for OTA firmware updates
- **Remote Logging**: Papertrail integration for remote log monitoring

## Hardware Requirements

### ESP32 Board
- Heltec Wifi Lora 32(v2)   - https://docs.heltec.org/en/node/esp32/wifi_lora_32/index.html
- Board data: https://resource.heltec.cn/download/package_heltec_esp32_index.json
- NOTE: use 'WiFi LoRa 32(v2)', NOT 'Heltec Wifi Lora 32(v2)'
- NOTE: Do NOT install the heltec extended libraries as they'll conflict. 

### USB Drivers
The board's USB-to-serial chip is a Silicon Labs CP2102 (shows up as an unrecognized "Other device" in Windows until a driver is installed).

- Try Device Manager > right-click the device > "Update driver" > "Search automatically for drivers" first (Windows Update sometimes already has it).
- If that doesn't find it, download and install the CP210x USB to UART Bridge VCP Driver from Silicon Labs: https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads
- After installing and replugging the board, it should appear under "Ports (COM & LPT)" as a Silicon Labs CP210x device — select that COM port in the Arduino IDE to flash/monitor the board.

### Pin Configuration
```
OLED Display (SSD1306):
- SDA: GPIO4
- SCL: GPIO15
- RST: GPIO16

LoRa Module (SX1278):
- SCK: GPIO5
- MISO: GPIO19
- MOSI: GPIO27
- CS: GPIO18
- RESET: GPIO14
- IRQ: GPIO26
```

## Dependencies
#include <LoRa.h>    //https://github.com/sandeepmistry/arduino-LoRa
#include "SSD1306.h" //https://github.com/ThingPulse/esp8266-oled-ssd1306
#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient

NOTE: Do NOT use the newer Heltec libary

## Configuration
No `properties.h` file is needed anymore (it used to hold the Rachio API key and a config.json
URL - both gone now that Rachio/zone logic lives in Home Assistant, not firmware). MQTT broker
address/port are in `MyMqtt.h`.
