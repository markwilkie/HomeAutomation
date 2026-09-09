# IR loopback test

Standalone ESP-IDF app that build/flash-verifies `../../src/ir_tcl112.c`
(the RMT-based TX driver, written without a working toolchain available at
the time -- see that file's header) by sending a known frame out the IR LED
and decoding it back on an IR receiver module wired to the same board. This
is Milestone 1's real-hardware check, before the driver ever gets pointed
at the actual AC unit -- see [../../PLAN.md](../../PLAN.md).

Deliberately a separate ESP-IDF project, not a mode bolted into MiniSplit's
main firmware: it only needs `esp_driver_rmt`, so it builds and flashes in
seconds instead of pulling in the full Matter/Thread/esp_matter stack for
every iteration. `components/ir_tcl112/` references the real
`../../src/ir_tcl112.c` and `../../include/ir_tcl112.h` by relative path
(not a copy), so this always tests the actual shipped driver.

## Wiring

- **IR LED** (through its transistor stage, same as the real deployment) on
  **GPIO 3** -- `ir_tcl112.h`'s `IR_TCL112_GPIO` default, confirmed as the
  real wiring for this test.
- **IR receiver module** (e.g. HX-M121, same part as
  [../../../MiniSplitIR/capture_tools](../../../MiniSplitIR/capture_tools))
  DAT pin on **GPIO 2** -- matches that existing convention. VCC/GND per the
  module's own requirements (3.3V for the HX-M121).
- Aim the LED at the receiver's sensor window -- a few centimeters away,
  roughly facing each other. This is an **optical** loopback (through the
  real 38kHz carrier and the receiver's demodulator), not an electrical
  short between the two GPIOs -- it validates the actual waveform ir_tcl112
  puts on the wire, not just RMT symbol timing.

## Build / flash

Same toolchain as the main MiniSplit project -- see
[../../WINDOWS_TOOLCHAIN_SETUP.md](../../WINDOWS_TOOLCHAIN_SETUP.md) if it
isn't installed yet.

```powershell
cd C:\Users\Administrator\Documents\GitHub\HomeAutomation\MiniSplit\test_apps\ir_loopback
idf.py set-target esp32c6
idf.py -p COM6 build flash monitor
```

(Swap `COM6` for whatever port Device Manager shows.)

## Expected output

Repeats every ~3 seconds so you can watch it live while adjusting LED/
receiver alignment:

```
I (xxx) IR_LOOPBACK: IR loopback test: TX GPIO 3 (IR LED) -> receiver -> RX GPIO 2 (DAT)
I (xxx) IR_TCL112: IR TX ready on GPIO 3
I (xxx) IR_LOOPBACK: Raw capture (113 symbols):
I (xxx) IR_LOOPBACK:   [  0] mark=3000us space=1650us
  ...
I (xxx) IR_LOOPBACK: PASS: decoded frame matches sent frame exactly
```

## If it fails

- **"no capture within 2s"** -- receiver not powered, wrong RX GPIO, LED not
  aimed at it, or LED/transistor stage not actually driving. Check power
  first (measure 3.3V at the receiver's VCC pin).
- **Header mismatch with plausible-but-wrong numbers** (e.g. mark and space
  look swapped) -- flip `invert_in` in `main/loopback_test.c`'s RX channel
  config. Different receiver modules invert polarity differently; this
  hasn't been confirmed against real hardware yet (see that file's
  file-level NOTE).
- **Frame truncated (fewer than 113 symbols captured)** -- usually the same
  causes as "no capture," just partial (e.g. receiver briefly loses lock,
  or the LED moved out of alignment mid-frame).
- **Decoded frame differs from sent frame, but header and length are fine**
  -- this is the case that actually indicates an ir_tcl112.c encoding bug
  (bit order, timing constant, byte order) rather than a wiring/polarity
  problem. The per-byte diff in the log output points at which bits are
  wrong.
- **`rmt_new_rx_channel` fails with an out-of-memory-style error** -- this
  app's RX channel and `ir_tcl112.c`'s TX channel together are asking for
  more RMT symbol memory than the chip has free across two channels. Lower
  `IR_RX_MEM_BLOCK_SYMBOLS` in `main/loopback_test.c` and use
  `rmt_receive_config_t`'s `flags.en_partial_rx` instead of raising it
  further (not implemented here -- see that file's top comment).

## Once this passes

The point of this test is narrow: confirm `ir_tcl112.c` actually builds for
this toolchain and puts a correctly-encoded frame on the wire. It does not
confirm the real AC unit accepts that frame (Milestone 1's other open item:
confirm the physical wire-run distance and mount at the AC's IR receiver
window) or exercise frame *construction* (Milestone 2, which lives in
`matter_device.cpp`, not here). Both stay open after this test passes.
