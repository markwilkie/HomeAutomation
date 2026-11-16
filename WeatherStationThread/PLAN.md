# Weather Station — Thread Rewrite Plan (v1, 2026-11-16)

Rewrite of [../SmartThings_Edge/Arduino/WeatherStation/](../SmartThings_Edge/Arduino/WeatherStation/)
(original ESP32 FireBeetle, Arduino, Wi-Fi → SmartThings Edge driver) as an
ESP-IDF project on the **ESP32-C6**, reporting over **Thread** instead of
Wi-Fi. Develop on an **ESP32-C6-DevKitC-1-N8**, deploy on a **DFRobot
FireBeetle 2 ESP32-C6 (DFR1075)**.

The old Arduino project stays in place, untouched, as the reference for
calibration constants, wind-direction thresholds, and wiring until this
project is deployed and verified. Do not delete it as part of this work.

**Before writing code, read "Open questions" at the bottom.** Q1 (transport)
decides the shape of Milestone 4 and the OTA approach. Do not pick an
answer for Mark; ask him.

---

## Scope

**Keep (port from the old project):**

| Sensor | Interface | Old pin(s) | Notes |
|---|---|---|---|
| BME280 (temp, humidity, pressure; dew point, heat index computed) | I2C | SDA 21, SCL 22 | Use forced mode, read once per cycle |
| Anemometer (wind speed) | Pulse, LP core | GPIO14 | NPN output, active low. 20 pulses/rev, `WINDFACTOR 3.914639` (see `WindRain.h`) |
| Wind gust | Derived from shortest wind-pulse interval | — | Old logic: `ULP::getULPShortestWindPulseTime()` + `GUSTFACTOR`. Keep, see Milestone 3 |
| Rain gauge (tipping bucket) | Pulse, LP core | GPIO26 | Active high with 10K pulldown. `RAINFACTOR 0.0102` per tip |
| Wind direction (vane) | ADC | GPIO36 | 8-point threshold table in `WindRainHandler.ino` `calcWindDirection()` — port the thresholds verbatim, then recalibrate on the C6's ADC |
| LDR (lux) + 3x GY-8511 UV | ADC via 74HC4051 MUX | MUX out GPIO34, EN 27 (active low), SEL 2/5 | Lux formula and UV-index conversion in `ADCHandler` |
| Moisture (wet/dry) | Digital | GPIO13 | 1M pulldown |
| Battery voltage | ADC | GPIO35 (old VCC divider) | On FireBeetle: GPIO0 via onboard 1M/1M divider |

**Drop:**
- PMS5003 air quality sensor, its 5V supply and `SET` pin, all `pm*` fields.
- Supercapacitors, Adafruit MiniBoost, boost-enable pin, cap-voltage ADC,
  and all cap/boost switching logic (`SWITCHTOTOBAT`, `SWITCHTOTOCAP`,
  `PMSMINVOLTAGE`). These existed only to feed the PMS5003's 5V draw.
- The SmartThings handshake/hub-port/HTTP server protocol.
- Papertrail logging over Wi-Fi.

---

## Hardware decisions (already made)

**Chip: ESP32-C6.** Hard requirement is counting wind and rain pulses
while asleep. The C6's LP RISC-V core does this (ESP-IDF example
`system/ulp/lp_core/gpio_intr_pulse_counter`), and keeps the project in the
same ESP-IDF toolchain as MiniSplit/ThreadRouter. Considered and rejected:
ESP32-H2 (no LP core), nRF54L15 (TIMER counter mode needs HFCLK, ~37 µA
measured; would need a CPU wake per pulse), EFR32MG24 (good hardware PCNT in
EM2, but only one PCNT instance, no pulse timing for gust, and a new SDK).

**Dev board: ESP32-C6-DevKitC-1-N8.** Second USB port (UART bridge) keeps
the console alive across sleep/wake, and the J5 header allows module
current measurement. Build for **4 MB flash** on it anyway, so the image
always fits the FireBeetle.

**Deploy board: FireBeetle 2 ESP32-C6, V1.1.** ESP32-C6FH4 (4 MB in-package
flash, not upgradeable). Onboard CN3165 solar Li-ion charger (VIN 4.5–6 V,
~540 mA charge current per schematic R9), TPS62A02 buck for 3V3, battery
sense on GPIO0 through a 1M/1M divider (~2 µA constant drain, acceptable).
Rated deep-sleep 36 µA (V1.1). Schematic:
https://img.dfrobot.com.cn/wikicn/5d57611a3416442fa39bffca/048c45084604c50576f027c7b97e80b4.pdf

---

## Power recommendation

With the PMS5003 gone, the station's average draw is set by the C6's sleep
mode plus sensor quiescent current, not by bursts of 5V load.

1. **Remove the supercaps and MiniBoost.** Nothing needs 5V anymore.
2. **Battery: keep the existing 1000 mAh LiPo**, connected to the FireBeetle
   battery connector. Budget check (calculated, not measured): a Thread SED
   in light sleep at ~0.25 mA average is ~6 mAh/day → months of autonomy
   with zero sun. Even at 1 mA average it is ~40 days. Capacity is not the
   constraint; cold-weather charging is (see Q5).
3. **Solar: keep the 10 W / 12 V panel and add a small buck converter to
   ~5.0 V into FireBeetle VIN.** The FireBeetle's solar input is 4.5–6 V, so
   the 12 V panel cannot connect directly. A buck keeps the existing panel
   and mount. Alternative: swap to a 5–6 V panel, but check its *open-circuit*
   voltage stays under 6 V, which many "6 V" panels exceed. Buck converter
   quiescent current must be low (sub-100 µA) or it runs only off panel
   power — verify the part chosen does not back-feed or drain the battery
   at night.
4. **Gate sensor power.** The old build powered the wind vane, anemometer
   and BME280 from 3.3 V continuously. The vane is a resistor divider and can
   draw hundreds of µA depending on direction (verify against the vane's
   resistor table) — power it from a GPIO-driven high-side switch and only
   enable it during the read. Keep the existing PNP-gated MUX/UV/LDR supply
   (EN low = on). Anemometer quiescent current is unknown — measure it (Q4);
   it must stay powered because it is counted in sleep.
5. **Measure, don't assume:** final average current is measured on the
   FireBeetle running from battery (Milestone 7). DevKitC readings are only
   useful for relative comparisons — its UART bridge and LEDs swamp the chip.

---

## OTA recommendation

**Do OTA, with two app slots, and design for it from Milestone 1.** The
station is on a pole; reflashing over USB means a ladder.

Partition table (4 MB), starting point — adjust after the M2 image-size
check:

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
otadata,  data, ota,     0xf000,   0x2000
phy_init, data, phy,     0x11000,  0x1000
ota_0,    app,  ota_0,   0x20000,  0x1E0000
ota_1,    app,  ota_1,   0x200000, 0x1E0000
```

That is ~1.875 MB per slot. **Note: none of the existing 4 MB C6 projects in
this repo (MiniSplit, MiniSplitIR, ThreadRouter) use OTA** — they all use a
single 3 MB `factory` partition. So "Matter + OTA fits in 4 MB" is unproven
here. If Q1 lands on Matter, check the image size at the end of M2 before
committing to this table. A plain-Thread build (no Matter stack) should be
far smaller, but measure it.

**Mechanism (depends on Q1):**
- Plain Thread: `esp_https_ota` (or plain HTTP on the LAN) from a file
  served on wyse, over the Thread interface. An "OTA pending" flag in the
  normal report acknowledgment tells the device to stay awake (rx-on-when-idle,
  short poll) for the download, then reboot. Same idea as the old
  `wifionly_flag`.
- Matter: Matter OTA requires an OTA Provider on the fabric. Confirm what
  HA's matter-server offers before relying on it.

Always enable app rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`):
mark the new image valid only after it has joined Thread and delivered one
successful report.

---

## Pin plan (shared by both boards)

Same GPIO numbers on DevKitC and FireBeetle so switching boards is a
rebuild, not a code change. Keep all pin numbers in `include/board.h`.

Constraints (**VERIFY each against the ESP32-C6 datasheet/TRM before
wiring** — taken from memory and from the FireBeetle schematic extract):
- LP-core GPIO interrupts only on LP IO pins, GPIO0–GPIO7.
- ADC1 channels are on GPIO0–GPIO6 (the C6 has no ADC2).
- Avoid strapping pins GPIO8, GPIO9, GPIO15; avoid GPIO12/13 (native USB
  D-/D+). FireBeetle: GPIO0 = battery sense, GPIO15 = onboard LED,
  GPIO6/7 shared with the GDI display connector (free if no display),
  GPIO1 assignment unconfirmed — don't use it.
- DevKitC: GPIO8 = addressable RGB LED.

| Function | GPIO | Why |
|---|---|---|
| Battery sense (ADC) | 0 | FireBeetle onboard divider; stub on DevKitC |
| Wind direction (ADC) | 2 | ADC1, free on both |
| MUX common: LDR / UV (ADC) | 3 | ADC1, free on both |
| Wind pulse (LP IO) | 4 | LP IO; FireBeetle label LP_RX (LP UART unused) |
| Rain pulse (LP IO) | 5 | LP IO; FireBeetle label LP_TX |
| Spare LP IO | 6, 7 | Reserve for a future pulse input |
| I2C SDA / SCL (BME280) | TBD | Pick from header pins exposed on **both** boards, outside the above — check the FireBeetle pinout |
| MUX EN, SEL0, SEL1 | TBD | Any free digital GPIO exposed on both |
| Moisture (digital) | TBD | Any free digital GPIO |
| Vane power enable | TBD | New, see Power §4 |

---

## Project layout

New top-level directory `WeatherStationThread/`, ESP-IDF, target `esp32c6`,
following MiniSplit's conventions (`build.ps1`/`flash.ps1`, `version.txt`,
`sdkconfig.defaults`).

```
WeatherStationThread/
  CMakeLists.txt
  partitions.csv
  sdkconfig.defaults              # shared: 4MB flash, LP core, OpenThread SED, PM, rollback
  sdkconfig.defaults.devkitc      # console on UART0 (bridge)
  sdkconfig.defaults.firebeetle   # console on USB-Serial/JTAG, battery ADC on
  include/board.h                 # pins + HAS_BATTERY_SENSE, per board via Kconfig/ifdef
  main/                           # HP core app
  ulp/                            # LP core program (C, replaces ulp_pulses.s)
```

Board selection: `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.firebeetle" build`.

---

## Milestones

### M0 — Answer the open questions
Blocking for M4 and the OTA mechanism. M1–M3 can start without them.

### M1 — Scaffold (DevKitC)
- ESP-IDF project, target esp32c6, 4 MB flash, partition table above.
- `board.h`, the three sdkconfig.defaults files, build/flash scripts.
- Blink + log over both consoles; confirm the secondary USB-Serial/JTAG
  console setting so the same build logs on the FireBeetle.

### M2 — Sensors, awake (DevKitC)
- BME280 over I2C (forced mode). Dew point and heat index ported from the
  old handler.
- ADC oneshot with calibration (curve fitting) for: wind direction, MUX
  (LDR + UV1–3, select via SEL0/SEL1, EN low), battery (DevKitC stub).
  Port the resample/tolerance logic from `ADCHandler` only if readings are
  noisy on the C6 — don't port it blindly.
- Wind-direction thresholds: start from the old table, then recalibrate —
  the C6 ADC's range/attenuation differs from the original ESP32.
- Moisture digital read.
- **Image-size check:** record the app binary size in this file. Matters for
  OTA slot sizing.

### M3 — LP-core pulse counting (DevKitC)
- LP core program in C, based on `gpio_intr_pulse_counter`:
  - Wind (GPIO4): count falling edges; record the **shortest interval**
    between edges since last read (LP timer timestamps) for gust, matching
    the old `wind_low_tick_cnt` approach.
  - Rain (GPIO5): count rising edges.
  - **Debounce in ms, not µs.** The example's debounce is ~0.6 µs; reed
    switches bounce for milliseconds. Ignore edges within N ms of the last
    counted edge (start ~5 ms rain, ~1–2 ms wind; tune on hardware). At
    20 pulses/rev a strong wind is a few hundred Hz — make sure the wind
    debounce window doesn't eat real pulses.
  - Shared counters read and reset atomically by the HP core each report.
- Verify counting continues across **both** light sleep and deep sleep.
- Bench test with a function generator or a second board toggling pins.

### M4 — Thread transport (DevKitC)
Depends on Q1.
- Join OTBR's `WilkieMatterNet` as a **Sleepy End Device**, not Mark's
  SmartThings network.
- Report every `CYCLETIME` (default 600 s, revisit — Q3). Payload: the
  fields from the old `/weather`, `/wind`, `/rain` posts minus PMS, plus
  battery voltage, firmware version, reset reason, RSSI/LQI, parent RLOC.
- Entities show up in Home Assistant with units and device classes.

### M5 — Sleep strategy and power (DevKitC, relative numbers)
- Default: light sleep + automatic power management with the LP core
  counting, per Espressif's guidance that a standard SED should use light
  sleep (deep sleep forces a reboot and Thread re-attach every wake).
- Measure average current on J5 for: light-sleep SED, and deep sleep with
  re-attach every cycle. Pick the lower one that keeps reports reliable.
  Record both numbers here.

### M6 — OTA (DevKitC)
- Implement per the OTA section and Q1. Test: update, bad image rollback,
  power loss mid-download.

### M7 — Port to FireBeetle
- Build with `sdkconfig.defaults.firebeetle`. Verify battery ADC on GPIO0
  against a multimeter and calibrate the divider factor.
- Wire the power path: panel → buck (5.0 V) → VIN; LiPo on battery connector.
- **Measure average current on battery** with a meter in series. This is the
  number that counts. Record it here.
- Confirm console via USB-Serial/JTAG reconnects after wake (or accept no
  console and rely on reported diagnostics).
- Weatherproof enclosure; reuse the existing sensor wiring harness where
  possible (see wiring section of the old readme).

### M8 — Deploy and retire the old station
- Run side-by-side with the old station for a few days if possible; compare
  temp, wind and rain.
- Then remove the SmartThings Edge weather driver usage, and move the old
  Arduino project to an `archive/` note or delete it — **ask Mark first.**

---

## Open questions (ask Mark before M4)

1. **Transport: Matter, or plain Thread + MQTT/CoAP?** As far as I could
   find, Matter has standard clusters for temperature, humidity, pressure,
   illuminance and battery (Power Source), but I could not find standard
   clusters for wind speed, wind direction, rain or UV — those would be
   vendor-specific and likely invisible in HA's Matter integration (verify).
   Options:
   - (a) Matter over Thread for the standard sensors + vendor clusters for
     the rest. Uses the existing commissioning script. Biggest image; OTA in
     4 MB unproven.
   - (b) Plain OpenThread SED sending UDP/CoAP or MQTT to wyse (mosquitto
     already runs there; NAT64 is on OTBR) and HA MQTT discovery for all
     entities. Smallest image, easiest OTA, all fields first-class in HA.
     Needs a way to give the device the Thread dataset (NVS-provisioned or
     Thread joiner/commissioner).
   - (c) ESPHome over Thread. Would need a custom external component for
     LP-core counting; ESPHome Thread + deep sleep has known rough edges.
   Tentative lean: (b), moderate confidence — confirm with Mark.
2. **Units.** Old firmware reports wind in knots. Keep knots (sailing use),
   or mph/m/s? Rain per tip `0.0102` — inches? Confirm.
3. **Report interval.** Keep 600 s, or shorten now that Thread is cheap per
   message (e.g. 60–120 s for wind)? Shorter = more radio time.
4. **Anemometer and rain gauge hardware:** same units as the old station
   (Makerfabs anemometer, existing vane and bucket)? Anemometer quiescent
   current and supply range (does it run at 3.3 V)? Vane resistor table?
5. **Cold-weather charging.** Li-ion/LiPo must not charge below 0 °C, and
   the CN3165 has no battery temperature input as far as the schematic
   extract shows (verify). Charging is hardware-driven, so firmware alone
   can't stop it. Options: accept the risk (freezes are infrequent here);
   switch VIN with a GPIO-driven MOSFET and cut solar input when the BME280
   (or a thermistor at the battery) reads below ~0 °C; or add a hardware
   NTC cutoff.
6. **UV sensors:** keep all three GY-8511s, or one? (Old code reported the
   max of three.)
7. **Moisture sensor:** still installed and useful?
8. **Gust definition:** keep the shortest-pulse method, or switch to the
   standard 3-second-average peak? The latter needs periodic LP-core
   sampling, not just per-pulse timestamps.
9. **Power-saver mode:** the old firmware stretched sleep to 1.5 h below
   3.55 V. Keep an equivalent low-battery mode?
10. **Which Thread network** — confirm OTBR's `WilkieMatterNet`, not the
    SmartThings hub's network.
