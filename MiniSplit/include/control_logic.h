#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

// Pure decision/encoding logic split out of main.c (2026-10-05) so it can be
// unit-tested on a host (test/host/) -- no FreeRTOS, logging or I/O in here.

#include <stdint.h>
#include <stdbool.h>
#include "tuya_client.h"
#include "ir_tcl112.h"

#ifdef __cplusplus
extern "C" {
#endif

// TCL112AC protocol mode values (see ../IR_PROTOCOL_REFERENCE.md's "State
// byte map") -- deliberately a separate encoding from Tuya's own "mode" DP
// (0=auto,1=cool,2=dry,3=fan,4=heat, see map_matter_mode_to_tuya()).
#define IR_MODE_HEAT 1
#define IR_MODE_DRY  2
#define IR_MODE_COOL 3
#define IR_MODE_FAN  7
#define IR_MODE_AUTO 8

// TCL112AC Fan values, state[8] bits 0-2 -- capture-confirmed for this unit
// (IR_PROTOCOL_REFERENCE.md's "Fan speed discrepancy"). `2` covers both
// Quiet and Low; they're only told apart by the Type 2 frame's Quiet bit,
// which this firmware always sends clear.
#define IR_FAN_AUTO 0
#define IR_FAN_LOW  2
#define IR_FAN_MED  3
#define IR_FAN_HIGH 5

#define MATTER_MODE_UNKNOWN 0xFF

uint8_t map_tuya_mode_to_matter(const tuya_device_status_t *device_status);
int8_t map_matter_mode_to_tuya(uint8_t matter_mode);
int8_t map_matter_mode_to_ir(uint8_t matter_mode);
uint8_t map_tuya_fan_speed_to_ir(uint8_t tuya_fan_speed);

void build_ir_state_frame(const tuya_device_status_t *status, bool power_on,
                          bool override_mode, uint8_t override_ir_mode,
                          bool override_setpoint, int16_t override_setpoint_c_x100,
                          uint8_t out_frame[IR_TCL112_FRAME_LEN],
                          uint8_t *out_ir_mode, int16_t *out_setpoint_c);

// Follow-Me heartbeat bits (state[4]/[6] bit 7 set, state[5] 0x20 clear,
// state[11] = ambient whole degrees C) -- see IR_PROTOCOL_REFERENCE.md.
void ir_frame_set_followme(uint8_t frame[IR_TCL112_FRAME_LEN], int8_t ambient_c);

// Fresh Air: the commanded value wins for FRESH_AIR_CONFIRM_WINDOW_MS after
// a command (Tuya's report can lag ~15 min), then the reported value does.
#define FRESH_AIR_CONFIRM_WINDOW_MS (15 * 60 * 1000)
bool fresh_air_effective(bool cmd_valid, bool cmd_value, uint32_t ms_since_cmd, bool reported);

// Mode reconciliation decision -- see the comment above MODE_MISMATCH_POLLS
// in main.c for the policy. main.c gathers the inputs and acts on the result.
#define MODE_MISMATCH_POLLS 2

typedef enum {
    MODE_RECONCILE_IN_SYNC,
    MODE_RECONCILE_ADOPT_UNKNOWN,
    MODE_RECONCILE_WAIT_POLLS,
    MODE_RECONCILE_ADOPT,
    MODE_RECONCILE_ADOPT_UNMAPPABLE,
    MODE_RECONCILE_WAIT_ECHO,
    MODE_RECONCILE_RESEND,
} mode_reconcile_action_t;

typedef struct {
    uint8_t actual;                   // unit's mode (Matter SystemModeEnum)
    uint8_t desired;                  // Desired card's mode, or MATTER_MODE_UNKNOWN
    bool command_pending;             // a mode/power command is queued
    bool unit_moved_last;             // unit's mode changed after Desired's last change
    bool actual_recently_commanded;   // we sent `actual` within the echo window
    bool desired_recently_commanded;  // we sent `desired` within the resend holdoff
} mode_reconcile_input_t;

// Updates *mismatch_polls (reset on in-sync/adopt, incremented on a
// mismatch); the caller resets it after a successful resend.
mode_reconcile_action_t mode_reconcile_decide(const mode_reconcile_input_t *in, uint8_t *mismatch_polls);

#ifdef __cplusplus
}
#endif

#endif
