/**
 * @file ir_tcl112.c
 * @brief RMT-based IR transmitter for the TCL112AC protocol.
 *
 * Wire encoding and checksum are documented and verified in
 * ../IR_PROTOCOL_REFERENCE.md. Uses ESP-IDF 5.x's driver/rmt_tx (not the
 * legacy driver/rmt.h) -- builds the entire 114-symbol waveform (1 header +
 * 112 data bits + 1 footer) into a plain array and replays it with the RMT
 * "copy" encoder, rather than pulling in IRremoteESP8266/Arduino for this
 * (see PLAN.md Milestone 1's rationale: this is a small, fully-characterized
 * fixed waveform, not worth an Arduino dependency).
 *
 * Build- and flash-verified against real ESP32-C6 hardware on 2026-09-07 via
 * test_apps/ir_loopback (a standalone RMT-only project, LED-to-receiver
 * optical loopback on one board -- see its README.md). First hardware run
 * decoded 111 of 112 data bits correctly but consistently got the
 * checksum's top bit wrong -- traced to a missing footer mark (see
 * IR_TCL112_GAP_US below), not a bug in the bit encoding itself. Fixed and
 * re-verified: 5/5 consecutive sends decoded back byte-for-byte identical,
 * including the checksum.
 */

#include "ir_tcl112.h"

#include <stdbool.h>

#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "IR_TCL112";

// Wire timing, in microseconds -- see IR_PROTOCOL_REFERENCE.md's "Wire
// encoding" section. Sourced from IRremoteESP8266's own constants
// (kTcl112AcHdrMark/HdrSpace/BitMark/OneSpace/ZeroSpace), which matched
// this project's own measured capture ranges closely enough to transmit at
// nominal -- this is the sender side, not a receiver needing tolerance.
#define IR_TCL112_HDR_MARK_US  3000
#define IR_TCL112_HDR_SPACE_US 1650
#define IR_TCL112_BIT_MARK_US  500
#define IR_TCL112_ONE_SPACE_US 1050
#define IR_TCL112_ZERO_SPACE_US 325

// Trailing footer mark + gap sent after the 112th data bit -- found missing
// from this file by test_apps/ir_loopback's receive side (2026-09-07): with
// no footer, a real receiver has no closing edge to bound the last bit's
// space against, so it reads as truncated garbage regardless of its actual
// value. Confirmed against IRremoteESP8266's own sendTcl112Ac()
// (src/ir_Tcl.cpp), which calls sendGeneric() with footermark=
// kTcl112AcBitMark (the same 500us bit-mark constant, not a new value) and
// gap=kTcl112AcGap=kDefaultMessageGap -- which that library's own source
// comments "just a guess" for, so IR_TCL112_GAP_US carries the same
// uncertainty here. That guess doesn't affect correctness though: a
// receiver only needs the footer *mark's* rising edge to close out the
// preceding bit's space -- the gap duration after it is unobserved by
// anything in this project's single-frame sends.
#define IR_TCL112_GAP_US 100000

#define IR_TCL112_CARRIER_HZ 38000
#define IR_TCL112_CARRIER_DUTY 0.33f

// 1 header symbol + 8 bits/byte * 14 bytes + 1 footer symbol = 114 symbols.
#define IR_TCL112_NUM_BIT_SYMBOLS (IR_TCL112_FRAME_LEN * 8)
#define IR_TCL112_NUM_SYMBOLS (IR_TCL112_NUM_BIT_SYMBOLS + 2)

#define IR_TCL112_RMT_RESOLUTION_HZ 1000000  // 1 tick = 1us

static rmt_channel_handle_t s_tx_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

uint8_t ir_tcl112_checksum(const uint8_t frame[IR_TCL112_FRAME_LEN]) {
    // Type 2 (special/quiet) frames add a 0x0F offset; Type 1 (normal)
    // frames don't. See IR_PROTOCOL_REFERENCE.md's "Checksum" section --
    // verified against five real captured frames of both types.
    const bool is_special = (frame[3] == 0x02);
    uint32_t sum = is_special ? 0x0F : 0x00;
    for (int i = 0; i < IR_TCL112_FRAME_LEN - 1; i++) {
        sum += frame[i];
    }
    return (uint8_t)(sum & 0xFF);
}

esp_err_t ir_tcl112_init(void) {
    if (s_tx_chan != NULL) {
        ESP_LOGW(TAG, "ir_tcl112_init() called twice without deinit -- ignoring");
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = IR_TCL112_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_TCL112_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        },
    };
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = IR_TCL112_CARRIER_HZ,
        .duty_cycle = IR_TCL112_CARRIER_DUTY,
    };
    err = rmt_apply_carrier(s_tx_chan, &carrier_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_apply_carrier failed: %s", esp_err_to_name(err));
        return err;
    }

    // rmt_copy_encoder_config_t has no members in this IDF version -- no
    // initializer needed (an empty-brace initializer triggers "excess
    // elements in struct initializer" on gcc for a genuinely empty struct).
    rmt_copy_encoder_config_t copy_encoder_config;
    err = rmt_new_copy_encoder(&copy_encoder_config, &s_copy_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "IR TX ready on GPIO %d", IR_TCL112_GPIO);
    return ESP_OK;
}

esp_err_t ir_tcl112_deinit(void) {
    esp_err_t err = ESP_OK;
    if (s_tx_chan != NULL) {
        err = rmt_disable(s_tx_chan);
        rmt_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    if (s_copy_encoder != NULL) {
        rmt_del_encoder(s_copy_encoder);
        s_copy_encoder = NULL;
    }
    return err;
}

// Fills one mark+space symbol. level0=1 (carrier on) for the mark, level1=0
// (carrier off) for the space -- the RMT carrier feature modulates level=1
// segments automatically once enabled via rmt_apply_carrier(), so this file
// only deals in mark/space durations, not raw 38kHz toggling.
static inline void fill_symbol(rmt_symbol_word_t *sym, uint32_t mark_us, uint32_t space_us) {
    sym->level0 = 1;
    sym->duration0 = mark_us;
    sym->level1 = 0;
    sym->duration1 = space_us;
}

esp_err_t ir_tcl112_send(uint8_t frame[IR_TCL112_FRAME_LEN]) {
    if (s_tx_chan == NULL || s_copy_encoder == NULL) {
        ESP_LOGE(TAG, "ir_tcl112_send() called before ir_tcl112_init()");
        return ESP_ERR_INVALID_STATE;
    }

    // Never trust a caller-supplied checksum -- recompute fresh every send
    // (see header doc: PLAN.md Milestone 2's pre-send refresh means the
    // other 13 bytes can legitimately change between sends).
    frame[13] = ir_tcl112_checksum(frame);

    rmt_symbol_word_t symbols[IR_TCL112_NUM_SYMBOLS];
    fill_symbol(&symbols[0], IR_TCL112_HDR_MARK_US, IR_TCL112_HDR_SPACE_US);

    // 112 data bits, LSB-first per byte, byte order state[0] first (see
    // IR_PROTOCOL_REFERENCE.md's "Wire encoding" section).
    int sym_idx = 1;
    for (int byte_idx = 0; byte_idx < IR_TCL112_FRAME_LEN; byte_idx++) {
        uint8_t byte = frame[byte_idx];
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            const bool bit_is_one = (byte >> bit_idx) & 0x1;
            const uint32_t space_us = bit_is_one ? IR_TCL112_ONE_SPACE_US : IR_TCL112_ZERO_SPACE_US;
            fill_symbol(&symbols[sym_idx], IR_TCL112_BIT_MARK_US, space_us);
            sym_idx++;
        }
    }

    // Footer: closes out the last data bit's space with a real edge to
    // measure against (see IR_TCL112_GAP_US's comment above) -- without
    // this, a real receiver can't tell how long that final space was.
    fill_symbol(&symbols[sym_idx], IR_TCL112_BIT_MARK_US, IR_TCL112_GAP_US);
    sym_idx++;

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,  // Send once.
    };
    esp_err_t err = rmt_transmit(s_tx_chan, s_copy_encoder, symbols, sizeof(symbols), &transmit_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        return err;
    }

    // Block until the frame is actually on the wire before returning --
    // callers (Milestone 2's command handling) shouldn't proceed as if the
    // send completed until it has.
    err = rmt_tx_wait_all_done(s_tx_chan, 1000);  // milliseconds, not ticks
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_wait_all_done timed out: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}
