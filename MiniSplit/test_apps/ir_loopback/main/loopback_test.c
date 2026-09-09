/**
 * @file loopback_test.c
 * @brief Standalone RMT loopback test for ir_tcl112.c: send a known frame
 *        out the IR LED and decode it back on an IR receiver module wired
 *        to the same board, to build/flash-verify the TX driver before it
 *        ever gets pointed at the real AC unit.
 *
 * Wiring (see this directory's README.md for the full writeup):
 *   - IR LED (through its transistor stage) on GPIO 3 (IR_TCL112_GPIO,
 *     ir_tcl112.h's default) -- same pin the real MiniSplit firmware uses.
 *   - IR receiver module DAT pin on GPIO 2 (IR_RX_GPIO below), matching the
 *     wiring convention already established in ../../capture_tools' HX-M121
 *     setup. VCC/GND per the receiver module's own requirements.
 *   - LED physically aimed at the receiver's sensor window (optical
 *     loopback, not an electrical short) -- this exercises the real 38kHz
 *     carrier and the receiver's demodulator, not just RMT timing.
 *
 * Build- and flash-verified against real ESP32-C6 hardware on 2026-09-07.
 * First flash hit `rmt_new_rx_channel(): no free rx channels`: the ESP32-C6
 * has only 2 RX-capable RMT channels, each with 48 words of dedicated
 * memory (`SOC_RMT_MEM_WORDS_PER_CHANNEL`, esp32c6/soc_caps.h) -- the
 * original `IR_RX_MEM_BLOCK_SYMBOLS 128` request exceeded what a single RX
 * channel can ever be given on this chip, regardless of what ir_tcl112.c's
 * TX channel was using. Fixed by keeping `mem_block_symbols` at 48 (one
 * channel's actual HW allocation) and setting `rmt_receive_config_t`'s
 * `flags.en_partial_rx`, which ESP32-C6 supports
 * (`SOC_RMT_SUPPORT_RX_PINGPONG`, confirmed against
 * esp_driver_rmt/src/rmt_rx.c) -- the driver ping-pongs 48-symbol hardware
 * chunks into this file's larger IR_RX_SYMBOL_BUF_LEN host buffer. Traced
 * through that source: since the host buffer here is sized comfortably
 * above the 113-symbol frame, `on_recv_done` still only fires once per
 * frame (with `flags.is_last` true) rather than once per chunk -- no
 * multi-callback accumulation needed in this file.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"

#include "ir_tcl112.h"
#include "ir_frame_decode.h"

static const char *TAG = "IR_LOOPBACK";

#ifndef IR_RX_GPIO
#define IR_RX_GPIO 2
#endif

// 48 = SOC_RMT_MEM_WORDS_PER_CHANNEL on ESP32-C6 -- the actual hardware
// allocation for one RX-capable channel, confirmed against
// esp32c6/soc_caps.h. Do not raise this; it's a hardware ceiling per
// channel, not a tunable buffer size (see the file-level NOTE above).
#define IR_RX_MEM_BLOCK_SYMBOLS 48
// The real capture buffer: sized comfortably above the 113 symbols (1
// header + 112 data bits) one full frame needs. flags.en_partial_rx below
// lets the driver ping-pong the 48-symbol hardware chunks into this larger
// buffer.
#define IR_RX_SYMBOL_BUF_LEN 128

#define IR_RX_RESOLUTION_HZ 1000000  // 1 tick = 1us, matches ir_tcl112.c's TX resolution.
#define IR_RX_SIGNAL_MIN_NS 1000     // 1us -- filters switching noise shorter than any real symbol (shortest is 325us).
#define IR_RX_SIGNAL_MAX_NS 5000000  // 5ms -- above the 3000us header mark, well below "transmission has ended".

static QueueHandle_t s_rx_queue;
static rmt_symbol_word_t s_rx_buf[IR_RX_SYMBOL_BUF_LEN];

static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t channel,
                                  const rmt_rx_done_event_data_t *edata,
                                  void *user_data) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user_data, edata, &woken);
    return woken == pdTRUE;
}

static void log_raw_symbols(const rmt_symbol_word_t *symbols, size_t count) {
    // Mirrors capture_tools/IRrecvDumpV2's raw dump -- lets a mismatch be
    // eyeballed manually even when ir_frame_decode() can't make sense of it.
    ESP_LOGI(TAG, "Raw capture (%u symbols):", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  [%3u] mark=%uus space=%uus", (unsigned)i,
                 (unsigned)symbols[i].duration0, (unsigned)symbols[i].duration1);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "IR loopback test: TX GPIO %d (IR LED) -> receiver -> RX GPIO %d (DAT)",
              IR_TCL112_GPIO, IR_RX_GPIO);

    ESP_ERROR_CHECK(ir_tcl112_init());

    rmt_rx_channel_config_t rx_chan_config = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RX_RESOLUTION_HZ,
        .mem_block_symbols = IR_RX_MEM_BLOCK_SYMBOLS,
        .flags = {
            // Demodulating IR receiver modules (HX-M121 and similar
            // TSOP-style parts) idle HIGH and pull LOW during a carrier
            // burst -- the opposite of how ir_tcl112.c's fill_symbol()
            // treats a mark (level=1). invert_in normalizes that so a
            // captured symbol's duration0 lines up with "mark" the same
            // way ir_tcl112.c wrote it. NOT build/flash-verified -- if
            // ir_frame_decode()'s header check fails with otherwise
            // plausible-looking numbers, flip this first.
            .invert_in = true,
        },
    };
    rmt_channel_handle_t rx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));

    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_done_cb };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, s_rx_queue));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    // Known-good test frame: Type 1, Cool, 21C, Fan Medium, isTcl bit set --
    // arbitrary but built entirely from fields IR_PROTOCOL_REFERENCE.md
    // marks as independently confirmed (not the sourced-but-unconfirmed
    // ones), so a mismatch can only mean an encode/decode/wiring bug, never
    // an unconfirmed-bit guess.
    static const uint8_t kTestFrame[IR_TCL112_FRAME_LEN] = {
        0x23, 0xCB, 0x26,  // fixed header
        0x01,              // MsgType = Type 1 (normal/full-state)
        0x00,              // Follow Me off
        0x00,              // Power/Timer bits clear
        0x03,              // Mode = Cool
        0x0A,              // Setpoint: 31 - 0x0A = 21C
        0x03,              // Fan = Medium
        0x00, 0x00, 0x00,  // Timers / Follow-Me temp -- unused here
        0x80,              // isTcl bit set (every real capture shows this set)
        0x00,              // checksum -- overwritten by ir_tcl112_send()
    };
    uint8_t expected_frame[IR_TCL112_FRAME_LEN];
    memcpy(expected_frame, kTestFrame, IR_TCL112_FRAME_LEN);
    expected_frame[13] = ir_tcl112_checksum(kTestFrame);

    while (1) {
        rmt_receive_config_t rx_config = {
            .signal_range_min_ns = IR_RX_SIGNAL_MIN_NS,
            .signal_range_max_ns = IR_RX_SIGNAL_MAX_NS,
            .flags = {
                // Required: the 48-symbol hardware channel can't hold a
                // 113-symbol frame on its own. See the file-level NOTE.
                .en_partial_rx = true,
            },
        };
        ESP_ERROR_CHECK(rmt_receive(rx_chan, s_rx_buf, sizeof(s_rx_buf), &rx_config));

        // rmt_receive() returns once the RX channel is armed, not once a
        // symbol has arrived -- this is just a safety margin before firing
        // the send, not a fix for a real race.
        vTaskDelay(pdMS_TO_TICKS(20));

        uint8_t send_frame[IR_TCL112_FRAME_LEN];
        memcpy(send_frame, kTestFrame, IR_TCL112_FRAME_LEN);
        esp_err_t send_err = ir_tcl112_send(send_frame);
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "ir_tcl112_send failed: %s", esp_err_to_name(send_err));
        }

        rmt_rx_done_event_data_t rx_data;
        if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "FAIL: no capture within 2s -- check receiver power/wiring and "
                          "that the LED is aimed at it");
        } else {
            log_raw_symbols(rx_data.received_symbols, rx_data.num_symbols);

            uint8_t decoded[IR_TCL112_FRAME_LEN];
            if (ir_frame_decode(rx_data.received_symbols, rx_data.num_symbols, decoded)) {
                if (memcmp(decoded, expected_frame, IR_TCL112_FRAME_LEN) == 0) {
                    ESP_LOGI(TAG, "PASS: decoded frame matches sent frame exactly");
                } else {
                    ESP_LOGE(TAG, "FAIL: decoded frame differs from sent frame");
                    for (int i = 0; i < IR_TCL112_FRAME_LEN; i++) {
                        if (decoded[i] != expected_frame[i]) {
                            ESP_LOGE(TAG, "  byte[%d]: sent 0x%02X, decoded 0x%02X",
                                     i, expected_frame[i], decoded[i]);
                        }
                    }
                }
            }
        }

        // Repeat periodically so the test can be watched live over
        // `idf.py monitor` while adjusting LED/receiver alignment.
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
