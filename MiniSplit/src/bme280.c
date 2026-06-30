/**
 * @file bme280.c
 * @brief Minimal BME280 driver implementation (ESP-IDF 5.x i2c_master).
 *
 * Compensation math is the Bosch reference fixed-point implementation from the
 * BME280 datasheet (rev 1.x), section 4.2.3.
 */

#include "bme280.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BME280";

// Registers
#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_DATA       0xF7  // press(3) temp(3) hum(2) = 8 bytes
#define BME280_REG_CALIB_TP   0x88  // 26 bytes (T1..T3, P1..P9, 0xA0, H1)
#define BME280_REG_CALIB_H    0xE1  // 7 bytes (H2..H6)

#define BME280_CHIP_ID        0x60
#define BME280_RESET_WORD     0xB6
#define BME280_I2C_TIMEOUT_MS 1000

// Calibration data
typedef struct {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1;
    int16_t  H2;
    uint8_t  H3;
    int16_t  H4, H5;
    int8_t   H6;
} bme280_calib_t;

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bme280_calib_t s_calib;
static int32_t s_t_fine;
static bool s_present = false;

static esp_err_t bme280_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), BME280_I2C_TIMEOUT_MS);
}

static esp_err_t bme280_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, BME280_I2C_TIMEOUT_MS);
}

static esp_err_t bme280_read_calibration(void)
{
    uint8_t tp[26];
    uint8_t h[7];
    esp_err_t err = bme280_read_regs(BME280_REG_CALIB_TP, tp, sizeof(tp));
    if (err != ESP_OK) {
        return err;
    }
    err = bme280_read_regs(BME280_REG_CALIB_H, h, sizeof(h));
    if (err != ESP_OK) {
        return err;
    }

    s_calib.T1 = (uint16_t)(tp[0] | (tp[1] << 8));
    s_calib.T2 = (int16_t)(tp[2] | (tp[3] << 8));
    s_calib.T3 = (int16_t)(tp[4] | (tp[5] << 8));
    s_calib.P1 = (uint16_t)(tp[6] | (tp[7] << 8));
    s_calib.P2 = (int16_t)(tp[8] | (tp[9] << 8));
    s_calib.P3 = (int16_t)(tp[10] | (tp[11] << 8));
    s_calib.P4 = (int16_t)(tp[12] | (tp[13] << 8));
    s_calib.P5 = (int16_t)(tp[14] | (tp[15] << 8));
    s_calib.P6 = (int16_t)(tp[16] | (tp[17] << 8));
    s_calib.P7 = (int16_t)(tp[18] | (tp[19] << 8));
    s_calib.P8 = (int16_t)(tp[20] | (tp[21] << 8));
    s_calib.P9 = (int16_t)(tp[22] | (tp[23] << 8));
    // tp[24] is 0xA0 (unused), tp[25] is H1
    s_calib.H1 = tp[25];

    s_calib.H2 = (int16_t)(h[0] | (h[1] << 8));
    s_calib.H3 = h[2];
    s_calib.H4 = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
    s_calib.H5 = (int16_t)((h[5] << 4) | (h[4] >> 4));
    s_calib.H6 = (int8_t)h[6];

    return ESP_OK;
}

// Returns temperature in 0.01 degC. Sets s_t_fine for pressure/humidity.
static int32_t bme280_compensate_temp(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)s_calib.T1 << 1))) * ((int32_t)s_calib.T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)s_calib.T1)) *
              ((adc_T >> 4) - ((int32_t)s_calib.T1))) >> 12) *
            ((int32_t)s_calib.T3)) >> 14;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8;
}

// Returns pressure in Q24.8 Pa (i.e. Pa = value / 256).
static uint32_t bme280_compensate_press(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)s_t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)s_calib.P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.P5) << 17);
    var2 = var2 + (((int64_t)s_calib.P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.P3) >> 8) +
           ((var1 * (int64_t)s_calib.P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.P1) >> 33;
    if (var1 == 0) {
        return 0; // avoid divide-by-zero
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.P7) << 4);
    return (uint32_t)p;
}

// Returns humidity in Q22.10 %RH (i.e. %RH = value / 1024).
static uint32_t bme280_compensate_hum(int32_t adc_H)
{
    int32_t v = s_t_fine - 76800;
    v = ((((adc_H << 14) - (((int32_t)s_calib.H4) << 20) - (((int32_t)s_calib.H5) * v)) +
          16384) >> 15) *
        (((((((v * ((int32_t)s_calib.H6)) >> 10) *
             (((v * ((int32_t)s_calib.H3)) >> 11) + 32768)) >> 10) + 2097152) *
              ((int32_t)s_calib.H2) + 8192) >> 14);
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)s_calib.H1)) >> 4);
    if (v < 0) {
        v = 0;
    }
    if (v > 419430400) {
        v = 419430400;
    }
    return (uint32_t)(v >> 12);
}

esp_err_t bme280_init(void)
{
    s_present = false;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BME280_I2C_PORT,
        .scl_io_num = BME280_I2C_SCL_GPIO,
        .sda_io_num = BME280_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_I2C_ADDR,
        .scl_speed_hz = BME280_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t chip_id = 0;
    err = bme280_read_regs(BME280_REG_ID, &chip_id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(err));
        return err;
    }
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip ID 0x%02X (expected 0x%02X)", chip_id, BME280_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    // Soft reset, then wait for NVM copy to complete.
    bme280_write_reg(BME280_REG_RESET, BME280_RESET_WORD);
    vTaskDelay(pdMS_TO_TICKS(10));

    err = bme280_read_calibration();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration: %s", esp_err_to_name(err));
        return err;
    }

    // ctrl_hum must be written before ctrl_meas. 1x oversampling for humidity.
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01);
    // config: standby 1000ms (0b101<<5), IIR filter off, SPI 3-wire off.
    bme280_write_reg(BME280_REG_CONFIG, 0xA0);
    // ctrl_meas: temp 1x (0b001<<5), press 1x (0b001<<2), normal mode (0b11).
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x27);

    s_present = true;
    ESP_LOGI(TAG, "BME280 initialized (addr=0x%02X sda=%d scl=%d)",
             BME280_I2C_ADDR, BME280_I2C_SDA_GPIO, BME280_I2C_SCL_GPIO);
    return ESP_OK;
}

esp_err_t bme280_read(bme280_reading_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t d[8];
    esp_err_t err = bme280_read_regs(BME280_REG_DATA, d, sizeof(d));
    if (err != ESP_OK) {
        return err;
    }

    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];

    int32_t t = bme280_compensate_temp(adc_T);     // 0.01 degC
    uint32_t p = bme280_compensate_press(adc_P);   // Q24.8 Pa
    uint32_t h = bme280_compensate_hum(adc_H);     // Q22.10 %RH

    out->temperature_c = t / 100.0f;
    out->pressure_hpa = (p / 256.0f) / 100.0f;     // Pa -> hPa
    out->humidity_pct = h / 1024.0f;
    return ESP_OK;
}

bool bme280_is_present(void)
{
    return s_present;
}
