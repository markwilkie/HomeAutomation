# SoilerGateway

**Version:** 0.9.0

A WiFi and LoRa gateway for soil moisture monitoring and irrigation control, designed for ESP32 microcontrollers. This project integrates with Rachio irrigation systems to provide automated soil moisture-based watering control.

## Features

- **LoRa Communication**: Receives soil moisture data from remote sensors using LoRa radio (915MHz)
- **WiFi Connectivity**: Connects to WiFi network for data transmission and web server functionality
- **Rachio Integration**: Automatically updates Rachio irrigation zones with soil moisture percentages
- **Web Server**: REST API endpoints for configuration and monitoring
- **OLED Display**: Real-time status display using SSD1306 OLED
- **Over-The-Air Updates**: Support for OTA firmware updates
- **Remote Logging**: Papertrail integration for remote log monitoring
- **Hub Communication**: Synchronizes with home automation hub for centralized control

## Hardware Requirements

### ESP32 Board
- Heltec Wifi Lora 32(v2)   - https://docs.heltec.org/en/node/esp32/wifi_lora_32/index.html
- Board data: https://resource.heltec.cn/download/package_heltec_esp32_index.json
- NOTE: use 'WiFi LoRa 32(v2)', NOT 'Heltec Wifi Lora 32(v2)'
- NOTE: Do NOT install the heltec extended libraries as they'll conflict. 

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

NOTE: Do NOT use the newer Heltec libary

## Configuration
Create a file called 'properties.h' with the following two defines in it:

#define RACHIO_API_KEY "put rachio api key here"
#define CONFIG_URL "https://raw.githubusercontent.com/markwilkie/HomeAutomation/master/Soiler/SoilerGateway/config.json"

#