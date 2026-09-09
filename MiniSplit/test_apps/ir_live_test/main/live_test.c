/**
 * @file live_test.c
 * @brief Interactive test app: transmit real TCL112AC commands from
 *        ir_tcl112.c at the actual MiniSplit unit (not a loopback receiver --
 *        see test_apps/ir_loopback for that) and have a human confirm each
 *        one landed.
 *
 * Every command is built from the base/template frame captured off the
 * user's own remote (IR_PROTOCOL_REFERENCE.md's "Base/template frame for
 * Milestone 2" section), overwriting only the fields this project actually
 * controls -- Power (state[5] bit 0x04), Mode (state[6] bits 0-3), Setpoint
 * (state[7]), Fan (state[8] bits 0-2) -- exactly as that doc specifies, so
 * every still-unconfirmed field (Light/Swing/Health/Turbo/Timers/etc.) rides
 * along as the unit's own natural default rather than a guess.
 *
 * Flow per command: print the command and its raw frame bytes, print
 * "Ready" and block until Enter is pressed (so the tester can get the unit
 * in view first), transmit, then ask "Did the unit respond correctly?" and
 * record the y/n answer. A pass/fail summary prints after the full list,
 * then offers to run through it again.
 *
 * SEND_TYPE2_COMPANION (below) sends a real captured Type 2 "special/quiet"
 * frame immediately before each Type 1 command, mirroring real remote
 * behavior -- see kTypeTwoCompanionFrame's comment. Toggle to 0 to A/B test
 * whether the AC actually needs it.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "sdkconfig.h"

#include "ir_tcl112.h"

// TCL112AC Mode values (state[6] bits 0-3) -- independently confirmed by
// this project's own captures, see IR_PROTOCOL_REFERENCE.md's state byte map.
enum {
    MODE_HEAT = 1,
    MODE_DRY = 2,
    MODE_COOL = 3,
    MODE_FAN = 7,
    MODE_AUTO = 8,
};

// TCL112AC Fan values (state[8] bits 0-2) -- this unit's real firmware
// values, not the generic library's (see "Fan speed discrepancy" in
// IR_PROTOCOL_REFERENCE.md): 1 is never observed, Quiet and Low share 2.
enum {
    FAN_AUTO = 0,
    FAN_QUIET_LOW = 2,
    FAN_MEDIUM = 3,
    FAN_HIGH = 5,
};

typedef struct {
    const char *description;
    bool power_on;
    uint8_t mode;
    uint8_t setpoint_c;
    uint8_t fan;
} test_step_t;

static const test_step_t kTestSteps[] = {
    {"Power ON  (Fan mode, 20C, Fan Auto -- base template as-is)", true, MODE_FAN, 20, FAN_AUTO},
    {"Power OFF", false, MODE_FAN, 20, FAN_AUTO},
    {"Mode: Heat, 24C, Fan Auto", true, MODE_HEAT, 24, FAN_AUTO},
    {"Mode: Cool, 24C, Fan Auto", true, MODE_COOL, 24, FAN_AUTO},
    {"Mode: Dry,  24C, Fan Auto", true, MODE_DRY, 24, FAN_AUTO},
    {"Mode: Auto, 24C, Fan Auto", true, MODE_AUTO, 24, FAN_AUTO},
    {"Fan: Quiet/Low, Cool 24C", true, MODE_COOL, 24, FAN_QUIET_LOW},
    {"Fan: Medium,    Cool 24C", true, MODE_COOL, 24, FAN_MEDIUM},
    {"Fan: High,      Cool 24C", true, MODE_COOL, 24, FAN_HIGH},
    {"Setpoint: 16C (minimum), Cool, Fan Auto", true, MODE_COOL, 16, FAN_AUTO},
    {"Setpoint: 31C (maximum), Cool, Fan Auto", true, MODE_COOL, 31, FAN_AUTO},
    {"Power OFF (end of test)", false, MODE_FAN, 20, FAN_AUTO},
};
#define NUM_TEST_STEPS (sizeof(kTestSteps) / sizeof(kTestSteps[0]))

// Base/template frame, verbatim from IR_PROTOCOL_REFERENCE.md -- a real,
// checksum-verified capture of the user's own remote. Milestone 2's rule:
// build every outgoing command from this array, overwriting only the
// fields below, so every field this project doesn't model yet keeps the
// unit's own real default instead of a zeroed guess.
static const uint8_t kBaseFrame[IR_TCL112_FRAME_LEN] = {
    0x23, 0xCB, 0x26, 0x01, 0x00, 0x64, 0x07, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x84, 0x0F,
};

// Every real remote button-press sends this Type 2 "special/quiet" frame
// immediately before the Type 1 state frame above -- see
// IR_PROTOCOL_REFERENCE.md's "Protocol identity" and "Type 2 frame"
// sections, and ../../MiniSplitIR/captures/protocol_capture.md's raw
// session log. This driver has so far only ever sent Type 1 alone; the doc
// flags that as an explicitly unconfirmed gap ("only worth revisiting if
// the AC ever appears to need the paired Type 2 frame to accept a command
// -- not observed so far"), which is exactly what a first real live-unit
// test (this app) is for.
//
// state[6] is a real capture-confirmed step counter that just keeps
// incrementing across presses without gating acceptance (see the capture
// log's Fan-speed session) -- there's no single "correct" value to
// reproduce, so this is one verbatim real capture ("everyday" Type 2,
// constant across ordinary mode-cycle presses) rather than a synthesized
// guess. Checksum recomputed fresh by ir_tcl112_send() as always.
#define SEND_TYPE2_COMPANION 1
static const uint8_t kTypeTwoCompanionFrame[IR_TCL112_FRAME_LEN] = {
    0x23, 0xCB, 0x26, 0x02, 0x00, 0x40, 0x20, 0x00, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x48,
};

static void build_frame(uint8_t out[IR_TCL112_FRAME_LEN], bool power_on, uint8_t mode,
                         uint8_t setpoint_c, uint8_t fan) {
    memcpy(out, kBaseFrame, IR_TCL112_FRAME_LEN);

    if (power_on) {
        out[5] |= 0x04;
    } else {
        out[5] &= (uint8_t)~0x04;
    }

    out[6] = (uint8_t)((out[6] & ~0x0F) | (mode & 0x0F));

    // Temp = 31 - (state[7] & 0x0F) -- see IR_PROTOCOL_REFERENCE.md's state
    // byte map. Silently ignored by the unit in Auto mode.
    out[7] = (uint8_t)(31 - setpoint_c);

    out[8] = (uint8_t)((out[8] & ~0x07) | (fan & 0x07));

    // out[13] (checksum) is recomputed fresh by ir_tcl112_send() itself.
}

// This board has no separate UART-to-USB bridge chip -- the only physical
// PC connection is the chip's native USB-Serial-JTAG peripheral (see
// sdkconfig.defaults, which makes it the primary console so stdin reads
// actually reach it). Without installing its interrupt-driven driver, reads
// are non-blocking at the VFS layer (usb_serial_jtag_vfs.c's own file
// comment: "simple non-blocking... driver") -- getchar()/fgets() would
// return EOF immediately instead of blocking for a keypress. Installing the
// driver and switching to it (mirrors the UART-based
// example_configure_stdin_stdout() pattern in ESP-IDF's own
// examples/common_components/protocol_examples_common/stdin_out.c, just
// against the USB-Serial-JTAG API instead of UART) gives real blocking reads.
static void configure_stdin_stdout(void) {
    if (usb_serial_jtag_is_driver_installed()) {
        return;
    }
    setvbuf(stdin, NULL, _IONBF, 0);
    usb_serial_jtag_driver_config_t usj_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_config));
    usb_serial_jtag_vfs_use_driver();
}

// Blocks until Enter is pressed, echoing keystrokes back (the raw UART VFS
// doesn't echo on its own -- unlike a full linenoise console).
static void wait_for_enter(void) {
    int c;
    do {
        c = getchar();
        if (c != EOF) {
            putchar(c);
        }
    } while (c != '\n' && c != '\r');
    printf("\n");
}

// Blocks until a line starting with y/Y/n/N is entered; reprompts otherwise.
static bool prompt_yes_no(const char *prompt) {
    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        int answer = 0;
        int c;
        do {
            c = getchar();
            if (c == EOF) {
                continue;
            }
            putchar(c);
            if (answer == 0 && (c == 'y' || c == 'Y' || c == 'n' || c == 'N')) {
                answer = c;
            }
        } while (c != '\n' && c != '\r');
        printf("\n");

        if (answer == 'y' || answer == 'Y') {
            return true;
        }
        if (answer == 'n' || answer == 'N') {
            return false;
        }
        printf("Please answer y or n.\n");
    }
}

void app_main(void) {
    configure_stdin_stdout();
    ESP_ERROR_CHECK(ir_tcl112_init());

    printf("\nIR live test -- transmits real commands at the MiniSplit unit.\n");
    printf("Point the IR LED at the unit's receiver window before starting.\n");

    while (1) {
        bool step_passed[NUM_TEST_STEPS];
        int pass_count = 0;

        for (size_t i = 0; i < NUM_TEST_STEPS; i++) {
            const test_step_t *step = &kTestSteps[i];

            uint8_t frame[IR_TCL112_FRAME_LEN];
            build_frame(frame, step->power_on, step->mode, step->setpoint_c, step->fan);

            printf("\n[%u/%u] %s\n", (unsigned)(i + 1), (unsigned)NUM_TEST_STEPS, step->description);
#if SEND_TYPE2_COMPANION
            printf("  Type2:");
            for (int b = 0; b < IR_TCL112_FRAME_LEN; b++) {
                printf(" %02X", kTypeTwoCompanionFrame[b]);
            }
            printf("\n");
#endif
            printf("  Frame:");
            for (int b = 0; b < IR_TCL112_FRAME_LEN; b++) {
                printf(" %02X", frame[b]);
            }
            printf("\n");

            printf("Ready -- press Enter to transmit.\n");
            wait_for_enter();

#if SEND_TYPE2_COMPANION
            uint8_t type2_frame[IR_TCL112_FRAME_LEN];
            memcpy(type2_frame, kTypeTwoCompanionFrame, IR_TCL112_FRAME_LEN);
            esp_err_t type2_err = ir_tcl112_send(type2_frame);
            if (type2_err != ESP_OK) {
                printf("  ir_tcl112_send (Type2) failed: %s -- counting as a fail.\n",
                       esp_err_to_name(type2_err));
                step_passed[i] = false;
                continue;
            }
#endif

            esp_err_t err = ir_tcl112_send(frame);
            if (err != ESP_OK) {
                printf("  ir_tcl112_send failed: %s -- counting as a fail.\n", esp_err_to_name(err));
                step_passed[i] = false;
                continue;
            }

            step_passed[i] = prompt_yes_no("Did the unit respond correctly? (y/n): ");
            if (step_passed[i]) {
                pass_count++;
            }
        }

        printf("\n===== Summary: %d/%u passed =====\n", pass_count, (unsigned)NUM_TEST_STEPS);
        for (size_t i = 0; i < NUM_TEST_STEPS; i++) {
            printf("  [%s] %s\n", step_passed[i] ? "PASS" : "FAIL", kTestSteps[i].description);
        }

        if (!prompt_yes_no("\nRun through all commands again? (y/n): ")) {
            break;
        }
    }

    printf("\nDone.\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
