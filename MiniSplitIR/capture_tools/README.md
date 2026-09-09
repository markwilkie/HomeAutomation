# Milestone 1 capture tools

Sketches used for the IR protocol capture step (see [../instructions.txt](../instructions.txt),
Milestone 1). Flashed to a spare ESP32-C6 dev board with an HX-M121 IR receiver
wired to GPIO2 (`VCC`→3.3V, `GND`→GND, `DAT`→GPIO2).

- `IRrecvDumpV2/` — the actual capture sketch (IRremoteESP8266 library example,
  copied here with `kRecvPin` set to 2). This is what's flashed during the real
  capture sessions.
- `RawPinTest/` — a minimal diagnostic sketch that bypasses the IRremoteESP8266
  library entirely and just prints GPIO2's raw digital level on change. Used to
  isolate a receiver/wiring problem from a library/RMT-driver problem when
  IRrecvDumpV2 wasn't decoding anything.

## Build gotcha found the hard way

Compiling for `esp32:esp32:esp32c6` with default board options gives a board
whose Arduino `Serial` is bound to the physical UART0 pins, not the USB port —
"USB CDC On Boot" defaults to **Disabled** on this board definition. The ROM
bootloader's boot banner still prints over the native USB-JTAG-serial
regardless (so it looks like the port is working), but nothing the app itself
prints via `Serial` ever reaches USB.

**Fix:** compile/flash with the FQBN option explicitly enabled:
```
arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc <sketch dir>
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc <sketch dir>
```
In the Arduino IDE: Tools → USB CDC On Boot → **Enabled**.

With CDC-on-boot enabled, the sketch's `while (!Serial) delay(50);` then
genuinely waits for the host to assert DTR before proceeding — a plain serial
reader that never raises DTR will see the board sit there forever after the
ROM banner. If you're scripting a serial connection (not using a normal
terminal app, which asserts DTR by default), do a controlled reset-to-run
pulse using **RTS only** (DTR held low throughout, since DTR doubles as the
boot-mode strap and toggling both together risks landing in the ROM
downloader instead of running the app), then raise DTR *after* the reset has
settled to let `setup()` proceed.
