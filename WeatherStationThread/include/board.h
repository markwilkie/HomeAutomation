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
// GPIO6: spare (battery thermistor cut 2026-11-25; cold cutoff uses BME280).
//        ADC1_CH6 / LP IO. Internal pull-up is on after reset (MTCK): disable it for ADC use.
#define PIN_CHARGE_EN       GPIO_NUM_7      // LP IO, solar charge switch (2N7002 gate): high = charging,
                                            // 100K pull-up = charging by default; held through sleep

// ---- Digital (decided 2026-11-25, Mark kept the 74HC4051) ----
// Pins on both headers (FireBeetle V1.1 schematic P1/P2, DevKitC J1/J3):
// 1-9, 15-23. Minus strapping 8/9/15 (also LEDs/BOOT), 16/17 (DevKitC UART0
// console) leaves 1 and 18-23 -- all used here, none spare.
#define PIN_I2C_SDA         GPIO_NUM_19     // BME280; FireBeetle header label SDA
#define PIN_I2C_SCL         GPIO_NUM_20     // BME280; FireBeetle header label SCL
#define PIN_MUX_EN          GPIO_NUM_21     // active low, also gates UV/LDR power (PNP); ext 100K pull-up = off; held in sleep
#define PIN_MUX_SEL0        GPIO_NUM_22
#define PIN_MUX_SEL1        GPIO_NUM_23
#define PIN_MOISTURE        GPIO_NUM_18     // digital in, external 1M pull-down
#define PIN_VANE_PWR        GPIO_NUM_1      // high = power the vane divider; ext pull-down = off; held in sleep.
                                            // FireBeetle: only wired to the GDI connector's LCD_CS (free with no display)

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
