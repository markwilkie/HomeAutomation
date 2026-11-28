# Weather Station — Thread Rewrite Plan (v6, 2026-11-16)

Rewrite of [../SmartThings_Edge/Arduino/WeatherStation/](../SmartThings_Edge/Arduino/WeatherStation/)
(original ESP32 FireBeetle, Arduino, Wi-Fi → SmartThings Edge driver) as an
ESP-IDF project on the **ESP32-C6**, reporting over **Thread** instead of
Wi-Fi. Develop on an **ESP32-C6-DevKitC-1-N8**, deploy on a **DFRobot
FireBeetle 2 ESP32-C6 (DFR1075)**.

The old Arduino project stays in place, untouched, as the reference for
calibration constants, wind-direction thresholds, and wiring until this
project is deployed and verified. Do not delete it as part of this work.

**Decisions (Mark, 2026-11-16) — see "Decisions" at the bottom:**
transport **not final** — a reachability test (M4a) picks, in order of
preference: direct MQTT over a custom DNS-free transport, a UDP relay, or
Matter (direct MQTT failed in MiniSplit at the name-lookup step); wind in knots, rain in inches; same
anemometer/vane/rain gauge as the old station; add a cold-charge cutoff;
keep all three UV sensors and the moisture sensor; serve OTA images from an
existing web server on wyse; firmware-only cold cutoff accepted; Thread network
is `WilkieMatterNet`; report every 120 s by day, less often 22:00–06:00 and
on low battery; keep the old gust method. No open items remain.

---

## Scope

**Keep (port from the old project):**

| Sensor | Interface | Old pin(s) | Notes |
|---|---|---|---|
| BME280 (temp, humidity, pressure; dew point, heat index computed) | I2C | SDA 21, SCL 22 | Use forced mode, read once per cycle |
| Anemometer (wind speed) | Pulse, LP core | GPIO14 | NPN output, active low. 20 pulses/rev, `WINDFACTOR 3.914639` (see `WindRain.h`) |
| Wind gust | Derived from shortest wind-pulse interval | — | Old logic: `ULP::getULPShortestWindPulseTime()` + `GUSTFACTOR`. Keep, see Milestone 3 |
| Rain gauge (tipping bucket) | Pulse, LP core | GPIO26 | Active high with 10K pulldown. `RAINFACTOR 0.0102` per tip. **Raise the pulldown** (≥1M, or the pin's internal pull-down): a bucket resting with the switch closed draws 330 µA through 10K, more than the whole sleep budget |
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
   The old "protection circuits" (photo 2026-11-27) are two round GSD-026
   35.5 mm boards soldered on the supercap terminals: per-cell shunt
   balancers (U1 + Q2 DPAK dumping into R4-R6 low-ohm resistors) that clamp
   each cap at ~2.7 V. The panel feeds the caps directly; there is no
   regulator. They go with the caps and can't feed VCC, hence the PlusRoc.
2. **Battery: keep the existing 1000 mAh LiPo**, connected to the FireBeetle
   battery connector. Budget check (calculated, not measured): a Thread SED
   in light sleep at ~0.25 mA average is ~6 mAh/day → months of autonomy
   with zero sun. Even at 1 mA average it is ~40 days. Capacity is not the
   constraint; cold-weather charging is (see §5).
3. **Solar: keep the 10 W / 12 V panel; step it down with a PlusRoc
   PR4A063 (Mark, 2026-11-25)** — potted, waterproof 15 W car converter,
   "12V/24V" in, 5 V 3 A out, 4 wires, no enable pin. Output goes to
   FireBeetle header VCC (P1 pin 1), the 4.5–6 V solar input, through the
   charge switch (§5). Why it fits: the panel is ~17 V working, ~21–22 V
   open-circuit, up to ~24 V on cold mornings; 3 A is far above the
   CN3165's ~540 mA, so it never runs near its limit; potted suits the pole.
   Its idle current comes from the panel, not the battery (FireBeetle D4/Q1
   stop back-feed), so it doesn't matter.
   **Before wiring:** confirm the listing's input range reaches ≥30 V, and
   measure its output with no load — must be ≤6.0 V (typically 5.0–5.2 V).
   Rejected: Pololu D36V6F5 (600 mA too tight), D36V28F5 (fine, $24, not
   sealed), S13V20F5 (22 V max input), S7V7F5 (11.8 V max), 7805 linear
   (~6.5 W heat at full charge, no enable).
4. **Gate sensor power.** The old build powered the wind vane, anemometer
   and BME280 from 3.3 V continuously. The vane is a resistor divider and can
   draw hundreds of µA depending on direction (verify against the vane's
   resistor table) — power it from a GPIO-driven high-side switch and only
   enable it during the read. Keep the existing PNP-gated MUX/UV/LDR supply
   (EN low = on). Anemometer quiescent current is unknown — measure it (Decision 3);
   it must stay powered because it is counted in sleep.
   **Every power-control line must hold its state through sleep.** In deep
   sleep (and in light sleep with peripheral power-down) ordinary digital
   GPIOs fall back to their reset state and can float:
   - **MUX/UV/LDR enable (PNP, active low):** a floating line can switch the
     sensors on for the whole sleep. Add an external pull-up (100K) so the
     default is *off*, and `gpio_hold_en()` it high before sleeping.
   - **Vane power enable:** external pull-down (default off), held off in sleep.
   - Check each line's sleep state with a meter in M5, not just on paper.
5. **Cold-charge cutoff (decided: add it).** LiPo must not charge below
   0 °C, and charging is hardware-driven by the CN3165, so cut the solar
   input instead:
   - **Charge switch on the 5 V side** (the PlusRoc has no enable pin),
     between the converter's 5 V output and FireBeetle VCC:
     ```
     PlusRoc 5V ──┬── AO3401 P-FET source ── drain ──► FireBeetle VCC
                  └── 100K ── P-FET gate ── 2N7002 N-FET drain; source → GND
     GPIO7 ── 2N7002 gate, 100K pull-up to 3V3
     ```
     GPIO7 high (or floating, via the pull-up) → N-FET on → P-FET on →
     charging. GPIO7 low → cut off. A P-FET alone won't do: 3.3 V on its
     gate can't turn it off with 5 V on the source. The P-FET's body diode
     points VCC → converter, so nothing flows to VCC when it's off.
     Opening the switch removes FireBeetle VCC, which stops charging; the
     board keeps running from the battery.
   - Drive it from **GPIO7 (an LP IO pin)**. The cutoff matters most while
     the station sleeps, which is nearly all the time, so GPIO7 must be
     driven *during* sleep: hold it with `gpio_hold_en()` before every sleep
     and release it after waking to change it (every GPIO floats in light
     sleep otherwise, and the pull-up would turn charging back on).
   - **Temperature source: the BME280's air temperature (Mark, 2026-11-25 —
     battery thermistor cut).** Air in the radiation shield is not the
     cell's temperature (the box lags on cold mornings and runs warm in
     sun), so the thresholds carry extra margin.
   - Firmware: open the charge switch below **4 °C air**, re-enable above **7 °C
     air** (hysteresis), Kconfig values. Check every report cycle. If the
     BME280 read fails, keep the previous state. Retune the margin after
     M8 if the box temperature turns out to track air closely.
   - **Fail-safe default:** pull EN to the *enabled* state in hardware, so
     if firmware hangs, charging continues rather than the station dying.
     This means the cutoff only works while firmware runs (including sleep,
     thanks to the hold) — **accepted by Mark 2026-11-16**; no hardware
     cutoff.
   - Report the cutoff state in every report.
6. **Measure, don't assume:** final average current is measured on the
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
single 3 MB `factory` partition. If M4a picks plain Thread, the image should
be well under a slot; if it picks Matter, "Matter + two OTA slots in 4 MB" is
unproven — measure the image (`idf.py size`) before committing to this table.

**Mechanism:**
- **Trigger** (depends on the M4a transport):
  - B1 (direct MQTT): retained `weatherstation/cmd/ota` topic with firmware
    version + URL + SHA-256, read each wake; cleared by the device after it
    marks the new image valid.
  - B2 (relay): the relay's reply carries the same fields.
  - Matter: Matter OTA needs an OTA Provider on the fabric. Confirm what
    HA's matter-server offers before relying on it.
- **Download:** on an update, the device switches to rx-on-when-idle (stays
  awake, no SED polling delay), downloads over HTTP from wyse on the
  Thread interface, verifies the hash, reboots. Same idea as the old
  `wifionly_flag`.
- **DNS warning (from MiniSplit):** `esp_https_ota` goes through esp-tls,
  the same layer that failed to connect to an IP literal in MiniSplit
  (`getaddrinfo()` → `EAI_FAIL`). If it hits the same bug, fetch the image
  with a raw lwIP socket (or via the relay) and write it with the lower-level
  `esp_ota_begin/write/end` API, which needs no networking of its own.
- **The file has to be served from somewhere on wyse.** Before adding a new
  container, check whether an existing one (Caddy, the dashboard's web
  server) can serve a static `firmware/` directory on the LAN. If a new
  service is needed, follow repo `CLAUDE.md`: compose file written by a
  checked-in setup script, `restart: unless-stopped`, dashboard card and a
  port-table row in the same change. **Decided (Mark, 2026-11-16): reuse an
  existing web server** — check Caddy and the dashboard's server first; only
  add a container if neither can serve a static LAN-only `firmware/` dir.

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
| **Spare** | 6 | Was the battery thermistor (cut 2026-11-25). ADC1_CH6, LP IO, holdable. Has an internal pull-up after reset (MTCK, Table 2-2 note 5) — disable it if used for ADC. FireBeetle: GDI connector pin, free with no display |
| Charge switch (cold cutoff) | 7 | Drives the 2N7002 gate of the P-FET switch, high = charging. LP IO, held through sleep, see Power §5. 100K pull-up to 3V3 = charging by default |
| I2C SDA / SCL (BME280) | 19 / 20 | FireBeetle header labels SDA/SCL |
| MUX EN (active low; also gates UV/LDR power) | 21 | External 100K pull-up (off), held in sleep — Power §4 |
| MUX SEL0, SEL1 | 22, 23 | |
| Moisture (digital) | 18 | 1M pull-down as before |
| Vane power enable | 1 | External pull-down (off), held in sleep — Power §4 |

**Pin check (2026-11-25)** against the FireBeetle V1.1 schematic (headers
P1/P2) and the DevKitC-1 user guide (J1/J3):
- Headers on both boards: GPIO1–9, 15–23. FireBeetle has no GPIO10/11 (QFN32
  chip); the DevKitC doesn't break out GPIO14. FireBeetle GPIO0 is the
  onboard 1M/1M battery divider and is not on the header.
- **GPIO1 is free on the FireBeetle** — it only goes to the GDI connector's
  LCD_CS. (Was "unconfirmed" above.)
- Free digital pins after strapping 8/9/15 and the DevKitC console 16/17:
  1 and 18–23, all used. With the thermistor cut, **GPIO6 is the one spare**.
- **Why the 74HC4051 stays (Mark, 2026-11-25):** ADC is GPIO0–6 only, and
  0/4/5 are taken (battery, wind, rain), and GPIO1 is a digital output
  (vane power), leaving 3 ADC pins (2, 3, 6) for 5 analog signals. Vane (2)
  gets its own; LDR + UV1–3 share the mux (3). An ADS1115 on I2C was the
  alternative (frees 21–23); not chosen.

**Datasheet check (2026-11-25, ESP32-C6 datasheet v1.6 + ESP-IDF 5.4.1
source).** The mapping works; these are the rules it depends on:
- **Strapping GPIO4/5 (MTMS/MTDI) are harmless:** they only select SDIO
  clock edges (Table 3-4), latched at reset; SPI boot needs only GPIO9=1
  (Table 3-3). A rain bucket resting closed at power-up is fine.
- **JTAG pins GPIO4–7 are free:** with unburned eFuses JTAG comes from
  USB-Serial/JTAG and GPIO15 is ignored (Table 3-7).
- **ADC:** ADC1_CH0–6 = GPIO0–6, no ADC2 (Table 2-9). ATTEN3 (12 dB)
  range 0–3300 mV, ±40 mV after calibration (Table 5-6) — battery divider
  ≤2.1 V and vane ≤3.3 V fit. Datasheet accuracy is with the radio off:
  read the ADC before the Thread exchange, not during it.
- **GPIO0/1 are the 32 kHz crystal pins**; the FireBeetle has no 32 kHz
  crystal, so they're free — but the LP timer then runs on the internal
  RC slow clock, which drifts with temperature. Scale gust intervals by
  the measured slow-clock calibration (`esp_clk_slowclk_cal_get()`).
- **Every GPIO floats in light sleep** unless held: ESP-IDF's
  `CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND` (on in our sdkconfig) sets every
  pin's sleep config to disabled/floating. So the external pull-up on MUX
  EN (21) and pull-down on vane power (1) are **required**; the charge
  switch (7) must be `gpio_hold_en`'d; and **hold MUX SEL0/SEL1 (22/23) low** in
  sleep too — floating CMOS select inputs on an always-powered 4051 can
  draw current. All GPIOs on the C6 are holdable through deep sleep
  (`GPIO_HOLD_MASK`, `SOC_GPIO_SUPPORT_HOLD_IO_IN_DSLP`). Configure the
  pulse pins through `rtc_gpio_*` (LP IO), not `gpio_*`. Verify each
  line's sleep level with a meter in M5.
- **Reset states (Table 2-2):** GPIO18–23 have a weak pull-up *at reset*
  (MUX EN stays off — good). GPIO1 and GPIO7 have no pull, so the
  external resistors decide: vane off, charging on (fail-safe).
- **LP core:** wind and rain share one LP IO interrupt and the example's
  `ulp_lp_core_gpio_clear_intr_status()` clears all pins; read
  `rtcio_ll_get_interrupt_status()` first to tell GPIO4 from GPIO5.
- **Wind pull-up current:** the anemometer's NPN output sits low whenever
  the rotor stops in the "on" position; with the internal ~45K pull-up
  that's ~73 µA continuously. Use an external 470K–1M pull-up (like the
  rain pulldown fix) and measure the resting current in M3.
- **Sensor box power (checked 2026-11-25, FireBeetle V1.1 schematic):**
  - Header **VCC (P1 pin 1) is the 5 V/solar *input***, not an output.
    Q1 (AO3407, gate = VUSB, R11 pull-down) joins VCC to VCC1, the CN3165
    charger input; USB feeds VCC1 through D2. The battery reaches only the
    3V3 buck: VBAT → Q2 (AO3407, gate = VCC1, R10 pull-down) → TPS62A02,
    with D4 stopping VCC1 → buck back-feed. So **on battery alone VCC is
    ~0 V**, and it also drops whenever the cold cutoff opens the charge
    switch. **Never power sensors from VCC.**
  - **3V3 (P1 pin 2 / P2 pin 2) is always on** from battery or input:
    TPS62A02, EN tied to VIN, Vout = 0.6 × (1 + 453K/100K) ≈ 3.32 V. Feed
    the sensor box, BME280, vane and anemometer from 3V3. Below ~3.4 V
    battery the buck runs in dropout and 3V3 follows the battery.
  - **Everything runs on 3.3 V (Mark, 2026-11-25).** The PMS5003 was the
    only 5 V load — old readme: "VDD 3.3V which powers all sensors except
    for the PMS5003 (which is 5v) and LDR/UV which is an output pin". The
    74HC4051 (2–6 V) and GY-8511/ML8511 (2.7–3.6 V) are fine at 3.3 V, and
    3.3 V SEL logic is then within spec. The old readme's "Red - VIN" to
    the sensor box is wrong: Mark confirmed (2026-11-27) the box is already
    on the 3.3 V rail. Keep it there; it must stay on 3V3 (the 2N2907 PNP
    switch only turns off if its emitter is at the GPIO high level).
    Sensor box photo (2026-11-27): Perma-Proto with the 74HC4051 (NXP DIP),
    a 2N2907 PNP high-side switch for UV/LDR power, ~5 resistors, no
    regulator.

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

### M0 — Resolve open items
Done 2026-11-16 (see Decisions). M1 can start.

### M1 — Scaffold (DevKitC)
- ESP-IDF project, target esp32c6, 4 MB flash, partition table above.
- `board.h`, the three sdkconfig.defaults files, build/flash scripts.
- Blink + log over both consoles; confirm the secondary USB-Serial/JTAG
  console setting so the same build logs on the FireBeetle.
- **Status 2026-11-16:** scaffold written and built for the DevKitC
  (`build.ps1`; `-Board firebeetle` for the deploy board). App image 182 KB
  of a 1920 KB slot.
- **Done 2026-11-25 on the DevKitC** (MAC `AC:EB:E6:D2:94:0C`, recorded in
  `flash.ps1`; QFN40, 8 MB flash, chip rev v0.2). 4 MB image boots from
  `ota_0`; LED blinks; boot log and heartbeat appear on both ports — UART
  bridge (CP210x, COM10 on this PC) and native USB-Serial/JTAG (COM9).

### M2 — Sensors, awake (DevKitC)
- BME280 over I2C (forced mode). Dew point and heat index ported from the
  old handler.
  Board is the Adafruit BME280 (STEMMA QT version, photo 2026-11-27):
  wired to VIN, GND, SCK, SDI only, so SDO floats on the board's pull-up →
  **I2C address 0x77**, not 0x76. VIN takes 3–5 V through the board's own
  LDO and level shifters; the old readme says it's fed 3.3 V (Orange). At
  3.3 V in, the LDO drops out and the sensor sees ~3.1 V: still in spec.
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
    the old `wind_low_tick_cnt` approach (Decision 9).
  - **Gust factor:** the old code had two fudge factors. `ULP.h`'s
    `GUSTFACTOR 2.3` turned ULP loop ticks (`ULPSLEEP` 40 µs) into real time —
    it corrected the old ULP loop's actual period, so **drop it**: LP timer
    timestamps are already real µs. Keep `WindRain.h`'s `GUSTFACTOR .225`
    (one pulse → rev/s) as the starting value, then check gusts against the
    old station side by side in M8 and retune.
  - Rain (GPIO5): count rising edges.
  - **Debounce in ms, not µs.** The example's debounce is ~0.6 µs; reed
    switches bounce for milliseconds. Ignore edges within N ms of the last
    counted edge (start ~5 ms rain, ~1–2 ms wind; tune on hardware). At
    20 pulses/rev a strong wind is a few hundred Hz — make sure the wind
    debounce window doesn't eat real pulses.
  - Shared counters read and reset atomically by the HP core each report.
- Verify counting continues across **both** light sleep and deep sleep.
- Bench test with a function generator or a second board toggling pins.

### M4a — Transport reachability test (do this first, before any M4 code)
**Why:** in MiniSplit, a direct MQTT client from a Thread-only C6 to mosquitto
was built and never connected (see `../MiniSplit/PLAN.md`, "Architecture
change, 2026-09-07"): esp-tls/lwIP did a DNS lookup even for IP literals
(`getaddrinfo()` returned `EAI_FAIL`), and OTBR's DNS64 only resolves public
names. Both failures were in the **name-lookup step**, not the network path.
This test checks whether the network path works when that step is skipped.

**Test:** a minimal ESP-IDF app on the DevKitC that joins `WilkieMatterNet`
(dataset from `ot-ctl dataset active -x` in the `otbr` container, written to
NVS, never committed). It uses **raw lwIP sockets** only — `sockaddr_in6`
filled by `inet6_aton`, no `getaddrinfo`, no esp-tls — and tries, against
each target address below:
- **TCP:** `connect()` to port **1883** (mosquitto). Success = connection
  established; then send a hand-built MQTT CONNECT packet and check for a
  CONNACK (that also proves the broker credentials).
- **UDP:** one datagram to a test port, plus a reply back to the device.

Target addresses, in this order:
1. **wyse's own IPv6 address** on the LAN/infrastructure link (`ip -6 addr`
   on wyse; prefer a stable ULA or another address routable from the
   Thread mesh). Try this first — it needs no NAT64.
2. The NAT64-embedded form of `192.168.15.30` (`c0a8:0f1e` appended to the
   NAT64 /96 prefix). The prefix is **not** the well-known `64:ff9b::/96` —
   otbr-agent computes it and `setup-nat64-jool.sh` re-publishes it; read the
   live value with `docker exec otbr sh -c "ot-ctl netdata show"`. Note this
   translates back to wyse itself (hairpin); whether Jool delivers that
   locally is unverified — that's what the test finds out.
3. The OpenThread CLI `ping` to both addresses, as a control.

On wyse: watch `docker logs mosquitto` (or `mosquitto_sub -v -t '#'`) for
the TCP/MQTT test, `nc -u -l -6 <port>` for UDP. Mosquitto must be listening
on IPv6 for target 1 — check its listener config in `setup-mosquitto.sh`
before concluding a failure is a routing problem.

**Record the result in this file**, then pick the transport:
- **TCP to 1883 works (either address) →** M4 Option B1: direct MQTT with a
  custom esp-mqtt transport. Preferred.
- **TCP fails but UDP works →** M4 Option B2: UDP relay on wyse.
- **Neither works →** stop and tell Mark; default to M4 Option A (Matter),
  the proven route in this house.

### M4 shared requirements (B1 and B2)
- **Thread:** OpenThread SED on `WilkieMatterNet`.
- **Home Assistant:** MQTT discovery. Retained config at
  `homeassistant/sensor/weatherstation/<id>/config` for every field, with
  device class and units (wind in **knots**, rain in **inches**, pressure,
  temperature, humidity, lux, UV index, battery V, charge cutoff), grouped
  under one HA device. State on `weatherstation/state` as one JSON object.
  Set `expire_after` so stale data shows unavailable. Republish discovery
  config on boot and when the firmware version changes, not every cycle.
- **Payload:** the fields from the old `/weather`, `/wind`, `/rain`, `/admin`
  posts minus PMS and cap/boost fields, plus battery voltage,
  charge-cutoff state, firmware version, reset reason, Thread
  RSSI/LQI and parent RLOC16.
- **Report interval:** `CYCLETIME` configurable via Kconfig and overridable
  remotely (retained `weatherstation/cmd/interval` in B1, relay reply in
  B2). **Default 120 s** (Decision 8).
- **Night interval (Decision 8):** 22:00–06:00 local, report every
  **600 s**. The device has no reliable clock (no SNTP: same name-lookup
  problem as MQTT), so **Home Assistant owns the schedule**: an automation
  publishes the retained interval topic at 22:00 (600) and 06:00 (120), and
  once at HA start for whichever period it is. B2: the relay applies the
  same schedule in its reply. Matter fallback: HA writes the interval
  attribute instead. The device just uses the latest interval it was given.
- **Low-battery mode (Decision 10):** below a battery-voltage threshold
  (old firmware: 3.55 V → 1.5 h sleep), stretch the report interval; start
  with 3.55 V / 3600 s, hysteresis back to normal ~0.1 V higher, both
  Kconfig. The interval used is the **longest** of the remote (day/night)
  interval and the low-battery interval. LP-core counting continues regardless, so no wind/rain pulses
  are lost — they're reported in the next (longer) cycle. Report the mode.
- **Credentials:** dedicated mosquitto user for the station (see
  `setup-mosquitto.sh`; password from wyse's `.env`, never the repo or a
  placeholder). Stored in NVS or a gitignored sdkconfig fragment.

### M4 Option B1 — Direct MQTT, custom transport (preferred)
- Use `esp-mqtt`, but **not** its built-in `mqtt://` transport (that path
  goes through esp-tls → `getaddrinfo()` → the MiniSplit failure).
- Create a custom transport: `esp_transport_init()`, then
  `esp_transport_set_func()` with your own connect/read/write/poll/close
  callbacks. `connect` opens an `AF_INET6` socket and `connect()`s straight
  to the M4a-proven IPv6 literal (kept in Kconfig/NVS), no name lookup.
  Pass the handle in `esp_mqtt_client_config_t.network.transport`.
  Note: the client takes ownership and destroys it in
  `esp_mqtt_client_destroy()` — don't free it yourself. Verify the exact
  callback signatures against the ESP-IDF version in use.
- Per report cycle: connect → publish state (QoS 1) → read retained
  `weatherstation/cmd/#` (interval, OTA) → disconnect → sleep. Don't hold
  the TCP session across SED sleep. Measure how long the connect/publish
  cycle keeps the radio awake (it's several round trips over a sleepy
  link — temporarily raise the SED poll rate during the exchange if it's
  slow).
- Device publishes its own HA discovery config. No new service on wyse.

### M4 Option B2 — UDP relay (fallback if only UDP works)
- **Device:** each report cycle, send one JSON datagram (raw lwIP socket,
  IPv6 literal from M4a), wait briefly for the relay's reply, retry up to N
  times, then sleep. Keep the payload within one datagram — measure it.
- **Relay on wyse:** small service (Python, pinned versions) that receives
  the datagram, validates an HMAC in the payload (the relay port is on the
  LAN), publishes to mosquitto (including HA discovery), and replies with an
  ack + interval + "update available" flag.
- Deploy per repo `CLAUDE.md`: compose file written by a checked-in setup
  script, `restart: unless-stopped`, dashboard card and a port-table row in
  the same change; show logs before calling it live.

### M4 Option A — Matter over Thread (fallback if M4a fails)
- Commission with `Wyse5070DebSetup/commission-thread-device.sh`, not the
  phone (repo `CLAUDE.md`).
- Standard clusters: Temperature, Relative Humidity, Pressure, Illuminance
  Measurement, Power Source (battery).
- No standard clusters for wind speed, wind direction, rain or UV (as far as
  the M0 research found — re-verify against the current Matter spec). Ask
  Mark how to carry them before building: e.g. the MiniSplit trick of
  borrowing a writable numeric cluster per value, or HA template sensors.
- Check image size immediately; this decides whether OTA fits (OTA section).

### M5 — Sleep strategy and power (DevKitC, relative numbers)
- Default: light sleep + automatic power management with the LP core
  counting, per Espressif's guidance that a standard SED should use light
  sleep (deep sleep forces a reboot and Thread re-attach every wake).
- Measure average current on J5 for: light-sleep SED, and deep sleep with
  re-attach every cycle. Pick the lower one that keeps reports reliable.
  Record both numbers here.

### M6 — OTA (DevKitC)
- Implement per the OTA section (image served by an existing web server on wyse). Test: update, bad image rollback,
  power loss mid-download.

### M7 — Port to FireBeetle
- Build with `sdkconfig.defaults.firebeetle`. Verify battery ADC on GPIO0
  against a multimeter and calibrate the divider factor.
- Wire the power path: panel → PlusRoc (5 V) → charge switch → VCC (P1
  pin 1); LiPo on battery connector (P4); sensors on 3V3.
- **Measure average current on battery** with a meter in series. This is the
  number that counts. Record it here.
- Confirm console via USB-Serial/JTAG reconnects after wake (or accept no
  console and rely on reported diagnostics).
- Weatherproof enclosure; reuse the existing sensor wiring harness where
  possible (see wiring section of the old readme).
- **Rewire list** (old FireBeetle ESP32 DFR0478 → FireBeetle 2 C6). Colors
  are from the old readme; confirm each one with a meter before moving it,
  and tag every wire with its old GPIO before pulling it off the header.

  | Wire (old readme) | Old | New | Change |
  |---|---|---|---|
  | BME280 Blue (SDA) | 21 | 19 | |
  | BME280 Blue/White (SCL) | 22 | 20 | |
  | BME280 Orange / Orange-White | 3V3 / GND | 3V3 / GND | |
  | Anemometer Blue (NPN pulse) | 14 | 4 | Brown 3V3, Black GND unchanged; Green unused |
  | Rain White (pulse) | 26 | 5 | Swap the 10K pull-down on the perma-proto for 1M |
  | Vane Yellow (signal) | 36 | 2 | |
  | Vane Blue (3.3 V) | 3V3 | 1 (`PIN_VANE_PWR`) | Add an external pull-down (100K) |
  | Moisture White | 13 | 18 | 1M pull-down stays |
  | Sensor box Black (MUX EN) | 27 | 21 | Add 100K pull-up to 3V3 (none in the old wiring) |
  | Sensor box Yellow / White (SEL) | 2 / 5 | 22 / 23 | Readme doesn't say which color is SEL0; not visible in the sensor box photo. Swapping them only swaps UV2 and UV3 (LDR = 11 and UV1 = 00 are symmetric), so check S0 = pin 11 with a meter or just swap in board.h if UV2/UV3 look reversed |
  | Sensor box Blue (MUX out) | 34 | 3 | |
  | Sensor box Red (power) | 3V3 | 3V3 | Already on 3.3 V (Mark, 2026-11-27; readme's "VIN" is wrong). No change |
  | Cap divider 10K/10K → 39 | 39 | — | Remove (caps gone) |
  | VCC divider 2K4/3K3 → 35 | 35 | — | Remove; C6 board has its own on GPIO0 (and the 2K4/3K3 divider draws ~0.7 mA) |
  | PMS5003 Brown, Green, Green/White | 5V, 16, 25 | — | Remove |
  | MiniBoost EN | 12 | — | Remove with the MiniBoost |
  | Charge switch gate | — | 7 | New (power §5) |

### M8 — Deploy and retire the old station
- Run side-by-side with the old station for a few days if possible; compare
  temp, wind and rain.
- Then remove the SmartThings Edge weather driver usage, and move the old
  Arduino project to an `archive/` note or delete it — **ask Mark first.**

---

## Parts to buy

- [x] PlusRoc PR4A063 12/24 V → 5 V converter (arrived 2026-11-27)
- [ ] AO3401 P-FET, SOT-23 (charge switch, power §5)
- [ ] 2N7002 N-FET, SOT-23 (charge switch)
- [ ] 100K resistors ×4: charge switch (2), vane power pull-down, MUX EN pull-up
- [ ] 1M resistor: rain pull-down (replaces the 10K)
- [ ] Stainless screws for the enclosures. Lids thread into brass inserts;
      size and count still to be measured from the existing screws.

---

## Decisions (Mark, 2026-11-16)

1. **Transport:** originally "plain Thread + MQTT". Revised the same day
   after reviewing MiniSplit's failed direct-MQTT attempt: M4a reachability
   test decides. Order of preference (Mark, 2026-11-16): direct MQTT with a
   custom esp-mqtt transport that connects to an IPv6 literal (skips the
   failing lookup) → UDP relay → Matter.
2. **Units:** wind in knots, rain in inches (`RAINFACTOR 0.0102` per tip).
3. **Hardware:** same anemometer, wind vane and rain gauge as the old
   station. Their quiescent current, 3.3 V operation and the vane resistor
   table still have to be **measured** in M2/M3. Record the results here.
4. **Cold-charge cutoff: add it.** P-FET charge switch on held LP IO GPIO7
   after the PlusRoc converter (Power §3, §5; was buck EN until the
   converter choice on 2026-11-25).
   Firmware-only cutoff accepted, no hardware cutoff. **Revised
   2026-11-25:** battery thermistor cut; the cutoff uses the BME280 air
   temperature with extra margin (off < 4 °C, on > 7 °C).
5. **Sensors:** keep all three GY-8511 UV sensors (report each, plus the
   max as `uv`) and the moisture sensor.
6. **OTA images:** served by an existing web server on wyse (was open item B).
7. **Thread network:** OTBR's `WilkieMatterNet`, the one HA uses (was open
   item D). The SmartThings hub's `ST-3011356111` is not an option.

8. **Report interval:** 120 s by day; less often 22:00–06:00 (600 s, set by
   an HA automation) and on low battery (Decision 10); the longest applies.
   Was open item A; if the measured battery draw in M7 is too high, raise
   the day value.
9. **Gust:** keep the old shortest-pulse method (Mark: "it's working pretty
   good now"); recalibrate the factor per M3. Was open item E.
10. **Low-battery mode:** yes, longer report interval below a threshold (M4
    shared requirements). Was open item F.

## Still open

None. New questions go here — ask Mark, don't guess.
