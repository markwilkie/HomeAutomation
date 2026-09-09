/**
 * @file ir_frame_decode.h
 * @brief Decode a captured TCL112AC RMT-RX symbol stream back into the
 *        14-byte frame it encodes -- the receive-side mirror of
 *        ../../../src/ir_tcl112.c's transmit encoding.
 *
 * Lives in this test app, not the shipped driver: ir_tcl112.c is
 * deliberately TX-only (see its own header doc) since MiniSplit's firmware
 * never needs to receive IR. This decoder exists purely to let the
 * loopback test verify what actually went out over IR, independent of
 * ir_tcl112.c's own idea of what it sent.
 */

#ifndef IR_FRAME_DECODE_H
#define IR_FRAME_DECODE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/rmt_types.h"

#define IR_FRAME_DECODE_LEN 14

/**
 * @brief Decode a captured symbol stream into a 14-byte TCL112AC frame.
 *
 * @param symbols Captured RMT-RX symbols, header symbol first.
 * @param num_symbols Number of captured symbols.
 * @param out_frame Destination for the decoded 14 bytes.
 * @return true if the header matched and enough symbols were present to
 *         decode a full frame; false otherwise (logged with the reason).
 *         A false return means the capture wasn't a clean loopback frame
 *         (wiring, polarity, or line-of-sight problem) -- it does not by
 *         itself mean the TX encoding is wrong.
 */
bool ir_frame_decode(const rmt_symbol_word_t *symbols, size_t num_symbols,
                     uint8_t out_frame[IR_FRAME_DECODE_LEN]);

#endif // IR_FRAME_DECODE_H
