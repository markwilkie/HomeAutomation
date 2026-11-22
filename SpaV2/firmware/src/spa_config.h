// SpaV2 pins and tunables. Pin table: ../../PLAN.md "Pin assignment".
#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

// ---- Atlas Gravity pH / ORP via analog isolators (PWM, 3.3 V, direct) ----
#define PIN_PH              GPIO_NUM_4
#define PIN_ORP             GPIO_NUM_5
#define ORP_CAL_REF_MV      225.0f      // Atlas ORP calibration solution
#define PROBE_SAMPLE_MS     1000        // one averaged mV sample per probe per second
#define PROBE_WINDOW        30          // seconds averaged for a calibration point

// ---- DS18B20 water temperature ----
#define PIN_TEMP            GPIO_NUM_7
#define TEMP_PERIOD_MS      5000

// ---- Pump CT: SCT-013-030 (30 A -> 1 V RMS) on ADC1_CH6, 1.65 V bias ----
#define CT_ADC_CHANNEL      ADC_CHANNEL_6   // GPIO6
#define CT_A_PER_V          30.0f
#define CT_SAMPLES          1000            // 5 kHz x 200 ms = 12 cycles at 60 Hz
#define CT_PERIOD_US        200
// Clamp on the pump's white common wire (until 2026-11-22 it was on heater lead
// P4, ~7.8 A). Measured 2026-11-22: low 3.0 A, high 9.4 A, noise ~0.15 A.
// Thresholds sit well between noise and low speed. HA derives Low/High from
// the current (split at 6 A).
#define CT_ON_A             1.5f
#define CT_OFF_A            1.0f

// ---- pH/ORP gating (Phase 4) ----
#define PUMP_SETTLE_S       (5 * 60)        // publish pH/ORP only after the pump has run this long
#define CAL_LIVE_S          (10 * 60)       // a calibration press un-gates publishing for this long
#define PROBE_PUBLISH_MS    10000

// ---- SmarterSpa LEDs (active-low, 470k/470k dividers, no pulls) ----
// Wired order on the board (PLAN.md): GPIO19 yellow, 20 blue, 21 red, 22 white.
#define PIN_LED_YELLOW      GPIO_NUM_19
#define PIN_LED_BLUE        GPIO_NUM_20
#define PIN_LED_RED         GPIO_NUM_21
#define PIN_LED_WHITE       GPIO_NUM_22
#define LED_SAMPLE_US       1000            // 1 ms sampling (Phase 5)
#define LED_DEBOUNCE        5               // consecutive samples to accept a change (5 ms)
#define LED_STEADY_MS       5000            // a solid pattern must hold this long
#define LED_BLINK_MAX_MS    200             // a white pulse shorter than this is the standby blink
#define LED_STANDBY_GAP_MS  15000           // no standby blink for this long while idle = fault
#define LED_MENU_HOLD_MS    5000            // keep ignoring after white-solid (menu) ends
#define LED_FLASH_HOLD_MS   2500            // a colour lit within this long counts as on (flashing = boost cycle)
// Counted-flash readouts (yellow x100 + blue x10 + red x1, manual p. 14). Provisional until the
// SmarterSpa's flash timing is measured: the serial log prints each sequence's timings for that.
#define LED_READOUT_GAP_MS       1500       // no red/blue/yellow flash for this long ends a sequence
#define LED_READOUT_FLASH_MAX_MS 1000       // a flash lit longer than this is not part of a readout
#define LED_READOUT_MAX_MS       15000      // a sequence still going after this is a boost flash train
#define LED_READOUT_MAX_FLASHES  27         // 9 + 9 + 9 is the largest 3-digit readout
