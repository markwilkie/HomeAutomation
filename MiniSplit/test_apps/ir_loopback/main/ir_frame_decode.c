/**
 * @file ir_frame_decode.c
 * @brief See ir_frame_decode.h.
 *
 * Timing constants duplicated from ../../../src/ir_tcl112.c (that file
 * doesn't expose them via its public header -- they're a TX-only
 * implementation detail there) and from ../../../IR_PROTOCOL_REFERENCE.md's
 * "Wire encoding" section, which remains the source of truth if these ever
 * need re-deriving.
 */

#include "ir_frame_decode.h"

#include "esp_log.h"

static const char *TAG = "IR_DECODE";

#define HDR_MARK_US 3000
#define HDR_SPACE_US 1650
#define ONE_SPACE_US 1050
#define ZERO_SPACE_US 325
// Midpoint between ZERO_SPACE_US and ONE_SPACE_US -- classifies a captured
// bit's space duration as 0 or 1.
#define BIT_SPACE_THRESHOLD_US ((ONE_SPACE_US + ZERO_SPACE_US) / 2)
// Generous tolerance for the header check only -- the bit threshold above
// is a simple midpoint split instead, since 0/1 spaces are far enough apart
// (325us vs 1050us) that no separate tolerance band is needed.
#define HDR_TOLERANCE_US 400

static bool within(uint32_t value, uint32_t nominal, uint32_t tolerance) {
    uint32_t lo = (nominal > tolerance) ? (nominal - tolerance) : 0;
    uint32_t hi = nominal + tolerance;
    return value >= lo && value <= hi;
}

bool ir_frame_decode(const rmt_symbol_word_t *symbols, size_t num_symbols,
                     uint8_t out_frame[IR_FRAME_DECODE_LEN]) {
    const size_t expected_symbols = 1 + IR_FRAME_DECODE_LEN * 8;
    if (num_symbols < expected_symbols) {
        ESP_LOGE(TAG,
                 "Captured %u symbols, need %u for a full frame -- frame "
                 "truncated (receiver not powered, wrong RX GPIO, or LED "
                 "not aimed at the receiver?)",
                 (unsigned)num_symbols, (unsigned)expected_symbols);
        return false;
    }

    // Header check. A mismatch with otherwise-plausible-looking numbers
    // (e.g. values swapped between duration0/duration1) usually means the
    // RX channel's invert_in assumption in loopback_test.c is backwards for
    // this receiver module, not that the encoding itself is wrong.
    uint32_t hdr_mark = symbols[0].duration0;
    uint32_t hdr_space = symbols[0].duration1;
    if (!within(hdr_mark, HDR_MARK_US, HDR_TOLERANCE_US) ||
        !within(hdr_space, HDR_SPACE_US, HDR_TOLERANCE_US)) {
        ESP_LOGE(TAG,
                 "Header mismatch: mark=%uus space=%uus (want ~%uus/~%uus)",
                 (unsigned)hdr_mark, (unsigned)hdr_space,
                 (unsigned)HDR_MARK_US, (unsigned)HDR_SPACE_US);
        return false;
    }

    for (int byte_idx = 0; byte_idx < IR_FRAME_DECODE_LEN; byte_idx++) {
        uint8_t byte = 0;
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            const rmt_symbol_word_t *sym = &symbols[1 + byte_idx * 8 + bit_idx];
            if (sym->duration1 >= BIT_SPACE_THRESHOLD_US) {
                byte |= (uint8_t)(1u << bit_idx);
            }
        }
        out_frame[byte_idx] = byte;
    }
    return true;
}
