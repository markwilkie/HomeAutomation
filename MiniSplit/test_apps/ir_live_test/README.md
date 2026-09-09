# IR live test

Standalone ESP-IDF app that points `../../src/ir_tcl112.c` (the real TX
driver, already build/flash-verified by `../ir_loopback`) at the actual
MiniSplit AC unit instead of a loopback receiver, and walks through a fixed
list of commands interactively so a human can confirm each one lands.

Deliberately a separate ESP-IDF project, same reasoning as `../ir_loopback`:
only needs `esp_driver_rmt` (for TX) and `esp_driver_uart` (for interactive
console input), so it builds and flashes in seconds instead of pulling in
the full Matter/Thread/esp_matter stack. `components/ir_tcl112/` references
the real `../../src/ir_tcl112.c` and `../../include/ir_tcl112.h` by relative
path (not a copy), so this always tests the actual shipped driver.

Every command is built from the base/template frame in
[../../IR_PROTOCOL_REFERENCE.md](../../IR_PROTOCOL_REFERENCE.md)'s
"Base/template frame for Milestone 2" section -- a real, checksum-verified
capture of the user's own remote -- overwriting only the fields this
project actually controls (Power, Mode, Setpoint, Fan). Every other field
(Light, Swing, Health, Turbo, Timers, ...) rides along as the unit's own
captured default, not a guess.

## Wiring

Same as the real deployment (and `../ir_loopback`'s TX side): IR LED
(through its transistor stage) on **GPIO 3** (`IR_TCL112_GPIO`'s default),
aimed at the AC unit's own IR receiver window this time, not a test
receiver module.

## Build / flash

```powershell
cd C:\Users\Administrator\Documents\GitHub\HomeAutomation\MiniSplit\test_apps\ir_live_test
idf.py set-target esp32c6
idf.py -p COM6 build flash monitor
```

(Swap `COM6` for whatever port Device Manager shows.)

## What it does

For each of 12 commands (power on/off, every Mode, every Fan speed, min/max
Setpoint -- all built off the base frame above) the monitor shows:

```
[3/12] Mode: Heat, 24C, Fan Auto
  Frame: 23 CB 26 01 00 64 01 07 00 00 00 00 84 XX
Ready -- press Enter to transmit.
```

Press Enter once you're pointed at the unit and watching it. It transmits,
then asks:

```
Did the unit respond correctly? (y/n):
```

Type `y` or `n` and press Enter. After all 12 commands, it prints a
pass/fail summary and offers to run the whole list again -- useful for
re-checking a command that failed once you've fixed wiring/aim, or for
retesting after a driver change.

## Notes

- This app blocks on serial input (`getchar()`/`fgets()` over the console
  UART), so it needs a real terminal (`idf.py monitor` or equivalent) with
  the ability to type into it -- it does not work with a log-only viewer.
- `Setpoint` is a no-op in Auto mode (see `IR_PROTOCOL_REFERENCE.md`) --
  expect Auto-mode steps to not visibly change the setpoint display even
  when the frame's Setpoint byte differs from the previous command.
- A "Power OFF" step won't visibly demonstrate Mode/Fan/Setpoint fields
  changing -- it's testing that Power itself lands, not the other fields
  carried along in the same frame.
