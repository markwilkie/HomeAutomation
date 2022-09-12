
### Overview
This is a ESP based weather station commmunicating via WiFi with Samsung Smarththings Edge drivers which run on the hub itself.

It's intended to be self-sufficient with a solar panel and large capacitors for power.  Using capacitors came as an inspiration from: https://moteino.blogspot.com/p/5v-solar-power-supply.html?m=1  

Last updated Sept 11 (version 1.02.xx)

### Specifications
- Board: [FireBeetle from dfrobot](https://wiki.dfrobot.com/FireBeetle_ESP32_IOT_Microcontroller(V3.0)__Supports_Wi-Fi_&_Bluetooth__SKU__DFR0478)
    - Draws .15ma (milliamps) while deep sleeping
- 2X 250F Capacitors in series for power
    - Note: Using the FireBeetle board, it's possible to simply attach a LIPO battery to the board for even longer lasting performance
- Temp, pressure, humidity: BME280 from Adafruit
- Air quality sensor: PMS5003
- UV sensor: GY-8511
- Wind speed: https://www.makerfabs.com/wiki/index.php?title=Anemometer 
- Wind direction: Weather vane from an old (old) weather station 
- Rain gauge: Tipper from an old (old) weather station 
- Brightness: A LDR

### Prerequisites
- https://arduinojson.org/?utm_source=meta&utm_medium=library.properties
- https://github.com/adafruit/Adafruit_BME280_Library
- https://github.com/finitespace/BME280  (used only for environment calculations)
- ULP - Steps in https://github.com/duff2013/ulptool
	- Path is C:\Users\<username>\Documents\ArduinoData\packages\esp32
	- Add recipe hooks to  platform.local.txt  (C:\Users\mawilkie\AppData\Local\Arduino15\packages\firebeetle32\hardware\esp32\0.1.1)
		- Add recipe hook before copying platform.local.txt  https://github.com/duff2013/ulptool/issues/78
			- recipe.hooks.core.prebuild.01.pattern.windows=cmd /c if exist "{build.source.path}\*.s" copy /y "{build.source.path}\*.s" "{build.path}\sketch\"
		- Add recipe hook for version increment
			- recipe.hooks.sketch.prebuild.02.pattern.windows=cmd /c if exist {build.source.path}\incrementversion.bat {build.source.path}\incrementversion.bat {build.path} {build.source.path} {build.project_name}
	- Install Python 2.7  https://www.python.org/downloads/release/python-2718/  (for ULP tool chain)
	- Add python to your path  (c:\python27?)

### Board Setup:
- FireBeetle  http://download.dfrobot.top/FireBeetle/package_esp32_index.json
- ESP DOIT Dev Kit: https://dl.espressif.com/dl/package_esp32_index.json
- VS Code and OTA:  http://arduinoetcetera.blogspot.com/2019/12/visual-studio-code-for-arduino-ota.html

### Principles (of sort)
- All calculation and conversions done on the ESP.  Min/Max/Avg are done on the hub because there's more memory, and some capabilities already do this so it's more consistent.

### Basic Handshaking/Sync Flow

First Boot
1. ESP boots and has no way to contact the hub because the driver ports are unknown
1. ESP then puts itself into "handshaking mode" where it waits for a /handshake GET from the each hub driver.
1. The hub will notice that it hasn't hear from the ESP in the alotted time, and call /handshake
1. Handshake will set the epoch, ip and port for each driver
1. Handshake mode will continue until each driver (weather, wind, rain, and admin) have checked in via /handshake
1. Handshake mode is then exited and the ESP goes to deep sleep for the set amount of time (600 seconds?)

Each Wakeup  (like a new boot)
1. ESP checks to see if this is boot from deep sleep or first boot
1. Assuming it's a wake, ESP contacts hub with a GET call to /settings to retrieve the latest epoch and if it should go into wifi only mode
1. Sensors are read
1. Epoch (and wifi only flag) updated from hub using a GET message
1. Assuming its time, messages are posted to the hub
1. Note that the PMS sensor (air quality) only reads and post every hour because it's such a power hog

When Hub Port Changes (this happens when driver is updated or hub is rebooted)
1. ESP notices this condition by erroring out it's POST command
1. Once that happens, it goes into "handshake mode" where the ESP listens for /handshake
1. The hub will notice that it hasn't hear from the ESP in the alotted time, and call /handshake

### Logging
- Papertrail  (see logger.h for setup)

### Power Management
- The ESP and sensors are powered by three sources:
  - Solar panel connected via protection circuits to the capacitors to charge them
  - 2 Capacitors routed through a boost circuit (Adafruit MiniBoost 5V @ 1A - TPS61023), then connected to board via USB
  - A1000mAh LiPO battery connected via built in power/charging port on Fire Beetle board
- This particlar board (Fire Beetle) will charge the LiPO if USB power is over ~3.4 volts.
- All onboard voltage measurements are from the capacitors pre-boost circuit
- If cap voltage drops below certain thresholds, the following occurs based on DEFINES:
  - PMSMINVOLTAGE - Below this value, no further readings are taken from the PMS air sensor
  - SWITCHTOTOBAT - Below this value, boost circuit is disabled, leaving the battery to power things.  (this is because battery charging will drain the caps unecessarily)
  - SWITCHTOTOCAP - Above this, boost circuit is re-enabled - which will also charge the battery
  - POWERSAVERVOLTAGE - Below this value, long sleeps are enabled and no PMS reads are allowed.  
- The PMS air sensor is very power hungry (>120ma) so we minimize how often readings are taken.  (AIRREADTIME)

### ULP

### API Reference (server on ESP client)

- /handshake   (syncWithHub())
["hubAddress"]
["epoch"]
["deviceName"]
["hubPort"] - Hub port is always changing, so we need the latest  (this is for the device)

### Client side GET messages

- /settings  (syncSettings())
  ["epoch"] - Gets current epoch from hub (ESP does this periodically when it wakes up)
  ["wifionly_flag"] - When set (true/false), it puts the ESP in a mode where the WiFi is always on so that over-the-air updates can happen

### Client side POST messages  (as of 9/2/2022)

- /weather
    doc["temperature"] Read from BME280
    doc["humidity"] 
    doc["pressure"] 
    doc["dew_point"] Calculated
    doc["heat_index"] Calculated

    doc["voltage"] = Read at the capacitors through a 50/50 voltage divider
    doc["ldr"] = Calculated to approx lux
    doc["moisture"] = "wet" or "dry"
    doc["uv"] = Calculated to uv index    

    doc["pm25"] standard PM 2.5
    doc["pm100"] 
    doc["pm25AQI"] Calculated AQI 
    doc["pm25Label"] "Good", "Moderate", etc.... 
    doc["pm100AQI"] 
    doc["pm100Label"] 
    doc["pms_read_time"]

    doc["current_time"] Always epoch

- /wind
    doc["wind_speed"] in knots
    doc["wind_direction"] in degrees
    doc["wind_direction_label"] N, NW, etc
    doc["wind_gust"] Calculated based on the length of the shorted pulse  (clever)
    doc["current_time"] 

- /rain
    doc["rain_rate"] current rate
    doc["moisture"] "wet" or "dry"
    doc["current_time"] 
  
- /admin
    doc["hubWeatherPort"] port for the driver on the hub (it changes every boot/refresh)
    doc["hubWindPort"] 
    doc["hubRainPort"] 
    doc["voltage"] voltage at the caps
    doc["wifi_strength"] RSSI value
    doc["firmware_version"] auto incremented every build
	doc["heap_frag"] = heap fragmentation in percentage
	doc["wifi_only_flag"] true if we're in Wifi Only mode
	doc["batter_pwr_flag"] true if running off battery instead of capacitors via boost		
	doc["pms_read_time"] last time pms sensor was read 
	doc["cpu_reset_code"] code of why the ESP rebooted
	doc["cpu_reset_reason"] 
    doc["current_time"] 

### Hardware interfaces
- Power
	- Solar Panel - 10W(Watts) 12V(Volts) Monocrystalline  (about 1A)
	- Adafruit MiniBoost 5V @ 1A - TPS61023  (capacitors --> PMS5003 and board via USB)
	- 1000mAh LiPO battery connected directly to Fire Beetle board
- i2c
	- BME280 humidity/temp/pressure- 3.6 µA  (gpio 21 and 22)
- ULP co-processer counting pulses
	- Wind speed
		- sample:  https://github.com/Makerfabs/Project-Wind-weather-station/blob/master/Example/Anemometer_demo/Anemometer_demo.ino
		- info: https://www.makerfabs.com/wiki/index.php?title=Anemometer
	- Rain
- ADC
	- Voltage divider
		- Cap voltage  (2x10K Resistors should do the trick)  ran out of ADC
		- Moisture sensor - sensor up top, then 1M resistor below resulting in 2.5V and up when 300K (lightly damp) and lower (wetter).
		- Light level (3K for normal light, 200 for direct sunlight, 1+ Mega Ohms for dark)
			- w/ 10K    3K=2.5V, 300=3.2v, and 1M=.03v
	- Wind direction - BL=3.3v, GR=Gnd, YLW=voltage
	- GY-8511 UV breakout - straight voltage
- UART for air quality
	- https://how2electronics.com/interfacing-pms5003-air-quality-sensor-arduino/
	- https://github.com/markwilkie/HomeAutomation/blob/master/rf24_based_solution-deprecated/Sonoff/FactoryATMEGAFirmware/sonoff_sc/pms5003.ino 


### Wiring / Hookup
Wiring:
- ESP32 (power)
	- VIN - 5v+ from caps via booster
	- Ground - from caps
	- VDD 3.3V
	- 1000mAh LiPO battery also connected via the built in battery port (this also charges battery)
	- Solar Panel --> Capacitors 
- 4 pair from attached
	- BME280 (temperature, pressure, humidity)
		- Orange - VCC from ESP board
		- Orange/White - Ground
		- Blue - SDA (GPIO21)
		- Blue/White - SCK (GPIO22)
	- PMS5003 (air quality)
		- Brown - 5v from booster
		- Brown/White - Ground
		- Green - TX (GPIO16 - URT2)
		- Green/White - SET (GPIO25)   - low=sleep, high=enabled
- 3 pair from wind vane (+ splice)
	- Wind Vane
		- Blue - 3.3v
		- Green - ground
		- Yellow - signal (GPIO36)
	- Rain
		- Red - 3.3v  (shared ground)
		- White-->10K pull down-->red-->GPIO26 - pulse count on high
	- Moisture
		- Black-->1M pull down-->white-->GPIO10  (is a digital input)
- 3 pair from UV
	- Red - VIN
	- Yellow - 3.3v
	- Green - Ground
	- Blue - out (GPIO34)
	- Black - Enable (GPIO27)
- LDR 
	- White-->10K divider-->yellow-->GPIO15  (shares power w/ Red)
- Anemometer
	- Brown - 3.3v
	- Black - Ground
	- Green - PNP pulse -not used
	- Blue - NPNP pulse (GPIO14)  -interrupt on low
- Voltage monitoring
	- Caps-->yellow-->10K/10K divider-->yellow-->GPIO39
	- VCC pin-->yellow-->2K4/3K3 divider-->yellow-->GPIO35
- Boost circuit (Adafruit MiniBoost 5V @ 1A - TPS61023)
	- Enable --> GPIO12  (high is enabled)

### Capacity for 2x250F caps in series
- Approx 25mah capacity  (https://sparks.gogo.co.nz/capacitor-formulae.html)
	- Could perhaps extend this to 70mah by using a boost circuit  (this would support up to a 3.5ma draw for 18 hours)
- Target 1.5ma draw to work in the winter (6 hours of daylight)
	- We're drawing .15ma during sleep!!