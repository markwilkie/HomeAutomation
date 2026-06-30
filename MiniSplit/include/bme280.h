/**
 * @file bme280.h
 * @brief Minimal BME280 temperature / humidity / pressure driver (I2C master)
 *
 * Uses the ESP-IDF 5.x i2c_master driver. Performs full Bosch fixed-point
 * compensation for temperature, humidity, and pressure.
 */

#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Bus / device configuration (override via build defines if needed) -------
#ifndef BME280_I2C_PORT
#define BME280_I2C_PORT 0          // I2C_NUM_0
#endif
#ifndef BME280_I2C_SDA_GPIO
#define BME280_I2C_SDA_GPIO 6      // ESP32-C6 default SDA
#endif
#ifndef BME280_I2C_SCL_GPIO
#define BME280_I2C_SCL_GPIO 7      // ESP32-C6 default SCL
#endif
#ifndef BME280_I2C_ADDR
#define BME280_I2C_ADDR 0x76       // 0x76 (SDO->GND) or 0x77 (SDO->VDD)
#endif
#ifndef BME280_I2C_FREQ_HZ
#define BME280_I2C_FREQ_HZ 100000  // 100 kHz standard mode
#endif

/** @brief One compensated environmental reading. */
typedef struct {
    float temperature_c;   // degrees Celsius
    float humidity_pct;    // relative humidity, percent
    float pressure_hpa;    // pressure, hectopascals (mbar)
} bme280_reading_t;

/**
 * @brief Initialize the I2C bus and the BME280 sensor.
 *
 * Verifies the chip ID, reads factory calibration, and configures the sensor
 * for normal-mode forced sampling (1x oversampling, IIR filter off).
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if the chip ID does not match.
 */
esp_err_t bme280_init(void);

/**
 * @brief Take a single compensated reading.
 *
 * @param out Destination for the reading (must not be NULL).
 * @return ESP_OK on success.
 */
esp_err_t bme280_read(bme280_reading_t *out);

/**
 * @brief Whether bme280_init() previously succeeded.
 */
bool bme280_is_present(void);

#ifdef __cplusplus
}
#endif

#endif // BME280_H
