# Weather Station (Thread rewrite)

Plan: [PLAN.md](PLAN.md). Read it fully before writing code.

## Blocking prerequisite
M1–M4 are unblocked (transport decided: Thread + MQTT). Items in
PLAN.md "Still open" (A–F) need Mark's answer before the parts they affect. Do not
choose for Mark — ask.

## Rules
- Build for 4 MB flash on every board, including the 8 MB DevKitC.
- All pin numbers live in `include/board.h`. Same GPIOs on both boards.
- Items marked VERIFY in PLAN.md (LP IO range, ADC channels, FireBeetle
  pins) must be checked against the ESP32-C6 datasheet/TRM or the FireBeetle
  schematic before wiring or coding against them.
- The old project in `../SmartThings_Edge/Arduino/WeatherStation/` is the
  reference for constants and wiring. Don't modify or delete it.
- Record measured numbers (image size, sleep currents) back into PLAN.md.
