# Environmental Sensors & Extra Matter Endpoints

This document covers the standalone temperature and humidity sensor endpoints
exposed to the Matter controller (Home Assistant), and the optional **BME280**
sensor that drives them.

These endpoints exist specifically so the readings are usable as **automation
triggers / conditions** — independent of the thermostat's `LocalTemperature`
(which not every controller reliably exposes as a standalone temperature for
automations; this was true of SmartThings on the [legacy
path](COMMISSIONING_GUIDE.md#legacy-smartthings-wifi-build) and is the reason
these dedicated endpoints exist at all).

## Three distinct temperatures

It's easy to conflate these, so to be explicit — the device exposes three
separate temperature readings, from two different physical sensors:

| Reading | Source | Where it lives |
|---|---|---|
| Indoor (mini-split) | The Tuya mini-split's own internal sensor (`temp_current`) | Endpoint 1 (root), Thermostat cluster's `LocalTemperature` |
| Indoor (BME280) | The optional BME280 wired to the ESP32 | Endpoint 2, standalone Temperature Sensor (`matter_update_aux_temperature`) |
| Outdoor (mini-split) | The Tuya mini-split's outdoor unit sensor (`ure` DP) | Endpoint 4, standalone Temperature Sensor (`matter_update_outdoor_temperature`) |

The indoor BME280 endpoint is **not** a fallback/mirror of the mini-split's indoor
reading — it is always the BME280's own value, and only updates while a BME280 is
detected. If no BME280 is fitted, that endpoint simply stays at its default and the
mini-split's own indoor reading remains available via the Thermostat (endpoint 1).

## Matter endpoints

The device publishes these endpoints (IDs are assigned at init and printed in the
boot log):

| Endpoint | Matter device type | Cluster / attribute | Exposed as |
|----------|--------------------|---------------------|------------|
| 1 (root) | Thermostat (0x0301) | On/Off, Thermostat | Thermostat / AC controls; indoor temp from the mini-split |
| **2** | Temperature Sensor (0x0302) | `TemperatureMeasurement.MeasuredValue` | **Temperature Measurement** — indoor temp from the BME280 |
| **3** | Humidity Sensor (0x0307) | `RelativeHumidityMeasurement.MeasuredValue` | **Relative Humidity Measurement** — from the BME280 |
| 4 | Temperature Sensor (0x0302) | `TemperatureMeasurement.MeasuredValue` | Outdoor temp from the mini-split |
| 5 | Humidity Sensor (repurposed) | `RelativeHumidityMeasurement.MeasuredValue` (%RH x100, exact match for 0-100%) | Compressor load %, see [TUYA_DP_REFERENCE.md](TUYA_DP_REFERENCE.md) |
| 6 | Occupancy Sensor (repurposed) | `OccupancySensing.Occupancy` (bit 0) | Compressor running (binary_sensor in HA, rename from "Occupancy"/Detected-Clear as desired) |

Boot log line (endpoint IDs will vary):

```
MATTER_DEVICE: Matter device initialized: thermostat_ep=1 temp_sensor_ep=2 humidity_sensor_ep=3 outdoor_temp_sensor_ep=4 compressor_ep=5 compressor_running_ep=6
```

### Units

| Reading | Matter unit | Example |
|---------|-------------|---------|
| Temperature | int16, °C × 100 | `2200` = 22.00 °C |
| Humidity | uint16, %RH × 100 | `5000` = 50.00 %RH |

### Update API (`include/matter_device.h`)

```c
// Temperature sensor endpoint (°C × 100)
void matter_update_aux_temperature(int16_t temp_c);

// Humidity sensor endpoint (%RH × 100)
void matter_update_aux_humidity(uint16_t humidity_centi_pct);
```

Both are implemented in [src/matter_device.cpp](src/matter_device.cpp) and update the
corresponding measurement cluster on their dedicated endpoint.

## BME280 sensor

A minimal BME280 driver provides real temperature, humidity, and pressure
readings. It uses the ESP-IDF 5.x `i2c_master` driver and performs the full Bosch
fixed-point compensation from the BME280 datasheet.

- Driver: [src/bme280.c](src/bme280.c) / [include/bme280.h](include/bme280.h)
- Build requirement: component `esp_driver_i2c` (added in [src/CMakeLists.txt](src/CMakeLists.txt))

### Wiring (ESP32-C6)

| BME280 pin | ESP32-C6 | Notes |
|------------|----------|-------|
| VCC / VIN  | 3V3      | 3.3 V only |
| GND        | GND      | |
| SDA        | GPIO6    | `BME280_I2C_SDA_GPIO` |
| SCL        | GPIO7    | `BME280_I2C_SCL_GPIO` |
| SDO        | GND      | Sets I2C address `0x76` (tie to VDD for `0x77`) |

The driver enables the ESP32's internal pull-ups; external 4.7 kΩ pull-ups on SDA/SCL
are recommended for longer wiring.

### Configuration

Defaults live in [include/bme280.h](include/bme280.h) and can be overridden with build
defines (e.g. in `src/CMakeLists.txt` via `target_compile_definitions`):

| Define | Default | Purpose |
|--------|---------|---------|
| `BME280_I2C_PORT` | `0` | I2C port number |
| `BME280_I2C_SDA_GPIO` | `6` | SDA pin |
| `BME280_I2C_SCL_GPIO` | `7` | SCL pin |
| `BME280_I2C_ADDR` | `0x76` | I2C address (`0x76` or `0x77`) |
| `BME280_I2C_FREQ_HZ` | `100000` | I2C clock |

### Driver API

```c
esp_err_t bme280_init(void);                 // probes chip ID, reads calibration
esp_err_t bme280_read(bme280_reading_t *out); // temperature_c, humidity_pct, pressure_hpa
bool      bme280_is_present(void);            // true after a successful init
```

## Runtime behavior (`src/main.c`)

- `bme280_init()` runs after Matter commissioning. If a BME280 is detected, an
  `env_task` FreeRTOS task is started.
- `env_task` reads the BME280 every `ENV_POLL_INTERVAL_MS` (30 s) and calls
  `matter_update_aux_temperature()` and `matter_update_aux_humidity()`.
- This Temperature Sensor endpoint is dedicated to the BME280's own indoor reading —
  it is deliberately kept separate from the mini-split's own indoor reading (which
  lives on the Thermostat's `LocalTemperature`, endpoint 1) so the two never get
  conflated. If no BME280 is fitted, this endpoint simply stays unset/default; there
  is no fallback to the Tuya reading.

Example boot log when the sensor is present:

```
BME280: BME280 initialized (addr=0x76 sda=6 scl=7)
MAIN: Environment sensor task started (BME280, interval: 30000ms)
MAIN: BME280: 22.41°C  47.8%RH  1012.6 hPa
```

## Using the readings in Home Assistant automations

1. After flashing, the two new sensors appear as entities on the Matter device.
2. In Home Assistant, create an automation with a **Trigger**/**Condition** using the
   temperature or humidity entity (e.g. "If humidity rises above 60%, turn on the
   mini-split in Dry mode").

> **Re-commissioning note:** adding these endpoints changes the device's Matter
> composition. If the temperature/humidity entities do not appear on an
> already-paired device, remove the device from Home Assistant and re-add it after
> flashing the new firmware.

Using the [legacy SmartThings path](COMMISSIONING_GUIDE.md#legacy-smartthings-wifi-build)
instead? Same idea, via a SmartThings routine's **If** condition — same re-commissioning caveat
applies (remove/re-add in the SmartThings app).

## Pressure

The BME280 also measures barometric pressure (`pressure_hpa`), logged by `env_task`.
Matter has no standard pressure sensor device type, so it is not exposed to the
controller; it is available in the firmware via `bme280_read()` if needed.
