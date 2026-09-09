/**
 * @file ir_tcl112.h
 * @brief RMT-based IR transmitter for the TCL112AC protocol (Pioneer
 *        WT012GLUI25FVQ mini split).
 *
 * Wire encoding, checksum, and the full state byte map are documented in
 * ../IR_PROTOCOL_REFERENCE.md -- this driver only implements the transmit
 * side (build a 14-byte frame's checksum, send it once over IR). Frame
 * *construction* (which bits mean what) belongs to the caller (Milestone 2
 * in ../PLAN.md), not this file.
 */

#ifndef IR_TCL112_H
#define IR_TCL112_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO driving the IR LED (through a transistor stage -- see PLAN.md
// Milestone 1's hardware note). Placeholder: the physical wire-run from
// this board's enclosure to the AC unit's IR receiver window hasn't been
// finalized yet, so this default is a guess, not a confirmed wiring fact.
// Override via build define once the real pin is chosen.
#ifndef IR_TCL112_GPIO
#define IR_TCL112_GPIO 3
#endif

#define IR_TCL112_FRAME_LEN 14

/**
 * @brief Initialize the RMT TX channel and 38kHz carrier for IR transmit.
 *
 * Call once at startup. Safe to call again after ir_tcl112_deinit().
 *
 * @return ESP_OK on success.
 */
esp_err_t ir_tcl112_init(void);

/**
 * @brief Release the RMT TX channel.
 */
esp_err_t ir_tcl112_deinit(void);

/**
 * @brief Compute the TCL112AC checksum for a 14-byte frame.
 *
 * Algorithm confirmed against IRremoteESP8266's IRTcl112Ac::calcChecksum()
 * and independently verified against five real captured frames (see
 * ../IR_PROTOCOL_REFERENCE.md) -- not a guess.
 *
 * @param frame The 14-byte frame. frame[13] is ignored (not read) -- the
 *              checksum is computed over frame[0..12] plus the Type-2
 *              offset below, never over a stale/prior checksum byte.
 * @return The checksum byte (goes in frame[13]).
 */
uint8_t ir_tcl112_checksum(const uint8_t frame[IR_TCL112_FRAME_LEN]);

/**
 * @brief Transmit one 14-byte TCL112AC frame over IR.
 *
 * Overwrites frame[13] with a freshly computed checksum before sending --
 * callers should never precompute or cache it themselves (see PLAN.md
 * Milestone 2: every send must reflect the current merged state, so a
 * stale checksum would silently corrupt whatever changed since the last
 * send). Blocks until the RMT hardware finishes transmitting.
 *
 * @param frame The 14-byte frame to send. Mutated in place (checksum byte
 *              only).
 * @return ESP_OK on success.
 */
esp_err_t ir_tcl112_send(uint8_t frame[IR_TCL112_FRAME_LEN]);

#ifdef __cplusplus
}
#endif

#endif // IR_TCL112_H
