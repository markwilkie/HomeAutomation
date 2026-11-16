// Weather station pins. Same GPIO numbers on the DevKitC and the FireBeetle,
// so switching boards is a rebuild (build.ps1 -Board), not a code change.
// Pin plan and reasons: ../PLAN.md "Pin plan".
//
// Checked 2026-11-16 against ESP-IDF 5.4.1 soc/esp32c6 (adc_channel.h,
// rtc_io_periph.c, soc_caps.h SOC_RTCIO_PIN_COUNT 8, SOC_ADC_PERIPH_NUM 1):
//   - LP IO (usable by the LP core, holdable through deep sleep): GPIO0-7.
//   - ADC1 channel n is GPIOn for n = 0-6. The C6 has no ADC2.
// Avoid: strapping pins 8, 9, 15 for inputs; 12/13 (USB D-/D+).
#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "hal/adc_types.h"

// ---- Decided (PLAN.md pin table) ----
#define PIN_BATTERY_SENSE   GPIO_NUM_0      // ADC1_CH0; FireBeetle onboard 1M/1M divider
#define ADC_CH_BATTERY      ADC_CHANNEL_0
#define PIN_WIND_DIR        GPIO_NUM_2      // ADC1_CH2, vane divider
#define ADC_CH_WIND_DIR     ADC_CHANNEL_2
#define PIN_MUX_COMMON      GPIO_NUM_3      // ADC1_CH3, 74HC4051 common: LDR / UV1-3
#define ADC_CH_MUX          ADC_CHANNEL_3
#define PIN_WIND_PULSE      GPIO_NUM_4      // LP IO, anemometer (falling edges)
#define PIN_RAIN_PULSE      GPIO_NUM_5      // LP IO, rain gauge (rising edges)
#define PIN_BATT_NTC        GPIO_NUM_6      // ADC1_CH6, battery thermistor (cold cutoff)
#define ADC_CH_BATT_NTC     ADC_CHANNEL_6
#define PIN_BUCK_EN         GPIO_NUM_7      // LP IO, solar buck enable; held through sleep

// ---- Still TBD (PLAN.md): pick from header pins exposed on both boards ----
// I2C SDA/SCL (BME280), MUX EN/SEL0/SEL1, moisture, vane power enable.

// ---- Per board ----
#if CONFIG_WS_BOARD_FIREBEETLE
#define BOARD_NAME          "FireBeetle 2 ESP32-C6"
#define PIN_STATUS_LED      GPIO_NUM_15     // onboard LED (plain, active high)
#define STATUS_LED_RGB      0
#define HAS_BATTERY_SENSE   1
#else
#define BOARD_NAME          "ESP32-C6-DevKitC-1"
#define PIN_STATUS_LED      GPIO_NUM_8      // onboard WS2812 RGB LED
#define STATUS_LED_RGB      1
#define HAS_BATTERY_SENSE   0               // no divider on GPIO0: battery reading is a stub
#endif
