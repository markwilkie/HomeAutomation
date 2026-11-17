# SpaV2 — Spa Water Monitor Rewrite (Matter over Thread)

Rewrite of the original SmartThings/ST_Anything spa monitor onto the same
**ESP32-C6-WROOM-1-N4 DevKitM-1** board used by `MiniSplit`, built with
**ESP-IDF + esp-matter**, joined to Home Assistant over **Matter/Thread**
(`WilkieMatterNet`).

Measures: **pH, ORP, water temperature**, plus **SmarterSpa salt-chlorine
generator state** (from the `saltview` reverse-engineering work) and **pump
running** state.

---

## 1. Background / existing assets

### Original firmware
- `SmartThings_based_solution/Arduino/Sketches/ST_Anything_SPA_ESP32WiFi/ST_Anything_SPA_ESP32WiFi.ino`
  - Classic ESP32, Wi-Fi, ST_Anything → SmartThings hub.
  - pH: Atlas Scientific **Gravity isolated analog pH** (`ph_iso_grav`), GPIO34, 120 s poll.
  - ORP: Atlas Scientific **Gravity isolated analog ORP** (`orp_iso_grav`), GPIO35, 120 s poll.
  - Temp: **DS18B20**, GPIO4, 60 s poll.
  - Calibration via serial: `PHCAL,4|7|10|CLEAR`, `ORPCAL,<mV>|CLEAR`; stored in EEPROM by the Atlas libs.
- Wrappers: `SmartThings_based_solution/Arduino/libraries/ST_Anything_Ph`, `ST_Anything_ORP`.
- ⚠️ The old sketch has the Wi-Fi SSID/password hard-coded in a public repo — rotate it; keep secrets out of the rewrite.

### saltview (SmarterSpa tap)
- `SpaV2/saltview/saltview.ino` — Arduino sketch that prints pins 2–9 on change.
  Used to map the SmarterSpa controller's 8-wire ribbon (Trilium note
  "Hottub V2 (Spa View)", Apr 2023).

| Ribbon wire | Arduino pin | Function | Polarity |
|---|---|---|---|
| 1 | 6 | White LED | active-low (high = off) |
| 2 | 7 | Button (DOWN?) | low = not pressed |
| 3 | 8 | Button (UP?) | low = not pressed |
| 4 | 9 | Button (BOOST?) | low = not pressed |
| 5 | 2 | Red LED | active-low |
| 6 | 3 | Blue LED | active-low |
| 7 | 4 | **Yellow** LED (physically yellow on the panel; the manual calls it **GREEN**) | active-low |
| 8 | 5 | +5 V | — |

Decision: **resistor dividers, 470 kΩ top / 470 kΩ bottom**, on the **4 LED lines only** (no power sense, no button inputs) (menu activity is filtered by white-solid + 5 s steady rule). Shared GND with SmarterSpa low-voltage side.

**Measured 2026-11-14 (red line, `test_leds`):** the LED lines are *not* 0/5 V logic.
- Unloaded at the ribbon: **5.5 V off, 3.4 V on**, a 2.1 V swing that never gets near 0 V.
- **High source impedance.** The original 56k/100k divider dragged red's off level from 5.5 V down to ~2.7 V (on ~2.5 V), leaving almost no swing. Yellow reading stuck-ON with 56k/100k is probably the same loading problem (not measured).
- With **470k/470k**, the line measured as "red" (GPIO20) sits at **~2.7 V off and ~1.7–2.0 V on**, and the C6 reads it cleanly in steady state. Red chattered with 56k/100k because its on level landed near the C6's actual switching point (~2.2 V observed).
- Margins are **empirical, not guaranteed**: the datasheet's V_IL ≤ 0.83 V / V_IH ≥ 2.48 V at 3.3 V are *not* met. If a line chatters in steady state, the guaranteed fix is an LM339 quad comparator at 3.3 V: high-value divider into IN+, ~1.1 V reference into IN−, 10k pull-up on the output (stays active-low). A 74LVC125A will **not** work, because it reads 3.4 V as high.
- Pulls must stay **off** (a ~45k internal pull-up beats a 235k divider). `test_leds` confirms `Pullup: 0, Pulldown: 0` on GPIO19–22 via `gpio_dump_io_configuration`.
- Red still chatters for ~1.4 s while the SmarterSpa powers up (slow supply ramp through the threshold). The decoder must filter this (Phase 5).
- All 4 lines are on 470k/470k (red measured; white/blue/yellow confirmed same values and read cleanly in `test_leds`).

### SmarterSpa LED semantics (ControlOMatic manual, ver. 06/27/16)
This unit has **no green LED**: where the manual says green, it means the **yellow** LED (GPIO19).

| Pattern | Meaning |
|---|---|
| Single flash every 10 s | Standby (manual says green, i.e. yellow; **captured unit flashes WHITE**, a ~10 ms pulse; trust the capture) |
| Solid BLUE | Generating, salt normal |
| Solid RED | Generating, salt low |
| Solid YELLOW | Generating, salt high |
| Solid RED + YELLOW | Shut down — salt over max |
| No lights (no standby blink) | No power / GFCI / supply fault |
| WHITE solid | User in power-change or zero-point edit mode (ignore) |
| YELLOW+BLUE flash | Boost confirmation (ignore) |
| Counted flashes (Y×100 + B×10 + R) | Numeric readout after button press (ignore, or decode in Phase 8) |

**Observed power-up (2026-11-14, `test_leds`):** WHITE+BLUE flash **together** ×5 (158 ms on / 477 ms period;
the manual says WHITE ×2), then WHITE → RED → BLUE → YELLOW at 528 ms each, then standby WHITE blinks
every 10 s. Ignore the whole sequence in the decoder.

Other facts: cycle starts when sensor < zero point; cycle length by power level
(L3 default = 2 h); electrode life ~7,000–10,000 production hours; DOWN during a
cycle shows electrode current (>160 too much salt, <125 + low output = add salt).

---

## 2. Target architecture

### Matter endpoint map
Implemented in `SpaV2/firmware/src/matter_device.cpp`. **Endpoint order is fixed**: HA names entities by it, and
`homeassistant/rename_entities.py` maps them to stable ids by endpoint number.

| EP | Value | Device type / cluster | Encoding | HA (after rename + package) |
|---|---|---|---|---|
| 1 | Water temp | Temperature Sensor | °C ×100 | `sensor.spa_water_temperature` |
| 2 | pH | Humidity Sensor (repurposed) | pH ×100 | `sensor.spa_ph` (device_class ph) |
| 3 | ORP | Humidity Sensor (repurposed) | mV + 2000 | `sensor.spa_orp` (mV), `sensor.spa_chlorine` (Low/Good/High) |
| 4 | pH/ORP readings live | Occupancy Sensor | 1 = current | `binary_sensor.spa_readings_live` |
| 5 | Pump running | Occupancy Sensor | CT hysteresis | `binary_sensor.spa_pump_running` |
| 6 | Pump current | Humidity Sensor (repurposed) | A ×100 | `sensor.spa_pump_current` |
| 7 | SmarterSpa state | Humidity Sensor (repurposed) | `spa_state_t` ×100 | `sensor.smarterspa_state` + binary sensors generating / salt low / salt high / salt shutdown / fault |
| 8 | Electrode hours | Humidity Sensor (repurposed) | whole hours | `sensor.spa_electrode_hours` |
| 9 | Calibration status | Humidity Sensor (repurposed) | `cal_status_t` ×100 | `sensor.spa_calibration_status` |
| 10–14 | Calibrate pH 7 / pH 4 / pH 10 / ORP / Clear | On/Off Plug-in Unit | momentary | `switch.spa_calibrate_*` (dashboard buttons) |

The SmarterSpa booleans are derived in HA from the one state code (fewer endpoints; one source of truth).
Booleans use **Occupancy Sensor**, not Contact/BooleanState: in this esp-matter version BooleanState's
StateValue is managed internally and `attribute::update()` can't set it (MiniSplit, `matter_device.cpp`).

**pH/ORP have no Matter cluster**, so they reuse MiniSplit's proven pattern: a **repurposed Humidity
Sensor** endpoint per value, decoded by an HA template sensor. MiniSplit already does this for compressor
load, outage reason and Thread RSSI (`MiniSplit/src/matter_device.cpp`; templates in
`Wyse5070DebSetup/homeassistant-config/configuration.yaml`, e.g. `sensor.minisplit_thread_rssi`).
Rules MiniSplit learned the hard way:
- **Never repurpose a Temperature Sensor:** HA converts it °C→°F and corrupts the value. Humidity has no unit conversion.
- **`MeasuredValue` is unsigned (uint16):** negative values need an offset (hence ORP + 2000). HA divides by 100 itself.
- **Numeric template sensors render `none` for invalid, never the string `unknown`:** the string fails HA's
  numeric check, and MiniSplit's RSSI sensor stuck on "unknown" for days because of it.

**HA presentation (templates in the same `configuration.yaml`):**
- **pH** → template sensor with `device_class: ph` showing plain **pH 7.45**.
- **ORP** → hidden raw-mV template sensor (for history/graphs) plus a **Low / Good / High** chlorine
  label. Thresholds live in HA so they can be tuned without reflashing. Starting points: **Low < 650 mV,
  Good 650–800, High > 800**, tuned against test strips (see Phase 3, ORP label tuning).

### Hardware
- ESP32-C6 DevKitM-1, Thread **FTD/router** (mains powered, no sleepy features). Board MAC **58:8C:81:5D:89:1C**: the former `ThreadRouter` board, repurposed 2026-11-14 (`test_leds/flash.ps1` refuses any other board).
- pH/ORP Gravity boards through the Atlas analog isolators (GRV-ISO, now sold as SRV-ISO) → GPIO4/GPIO5 as **digital PWM inputs** (no ADC). Isolator outputs are **3.3 V max**, so they connect directly (no divider, no filter), as on the old ESP32 (GPIO34/35).
- DS18B20 (1-Wire) → GPIO7 (digital only; no ADC on GPIO7), 4.7 kΩ pull-up.
- SmarterSpa ribbon → 470k/470k resistor dividers (shared GND) → 4 GPIO inputs (LEDs only).
- Pump CT clamp (SCT-013-030) → GPIO6 (ADC1), AC-coupled around a 1.65 V bias.


### Pin assignment (ESP32-C6-DevKitM-1)
| Signal | GPIO | Notes |
|---|---|---|
| pH (Gravity via isolator, PWM, 3.3 V) | GPIO4 | digital input, high-pulse timing (MCPWM capture); direct, no divider/filter |
| ORP (Gravity via isolator, PWM, 3.3 V) | GPIO5 | digital input, high-pulse timing (MCPWM capture); direct, no divider/filter |
| Pump CT clamp, SCT-013-030 (30 A → 1 V RMS, burden built in) | GPIO6 | ADC1_CH6; CT **red** lead (extended by a **purple** wire) → GPIO6; CT **white** lead (extended by a **blue** wire) → 1.65 V bias midpoint (10k/10k + 10 µF), never GND; one conductor only; RMS over 200 ms |
| Spare analog | GPIO2, GPIO3 (C6 ADC is GPIO0–6 only; GPIO4/5 are used as digital) | ADC1_CH2/3 |
| DS18B20 data | GPIO7 | 1-Wire via RMT (`espressif/ds18b20`); 4.7 kΩ pull-up to 3.3 V |
| SmarterSpa Yellow LED, manual's "green" (ribbon 7, **yellow** wire) | GPIO19 | 470k/470k divider, active-low, no pulls |
| SmarterSpa Blue LED (ribbon 6, **blue** wire) | GPIO20 | 470k/470k divider, active-low, no pulls |
| SmarterSpa Red LED (ribbon 5, **red** wire) | GPIO21 | 470k/470k divider, active-low, no pulls |
| SmarterSpa White LED (ribbon 1, **white** wire) | GPIO22 | 470k/470k divider, active-low, no pulls |
| Spare digital (Phase 8 BOOST drive) | GPIO23 | |
| SmarterSpa GND (**green** wire) | GND | common ground |

Harness (left→right): yellow, blue, red, white, green(GND). The ribbon→colour mapping above is correct; on the board the GPIO order is **reversed** from the original plan: yellow (green LED) → GPIO19, blue → GPIO20, red → GPIO21, white → GPIO22 (per the board wiring, 2026-11-14). **Confirmed 2026-11-15** with `test_leds` on this table: power-up W+B ×5, then lamp test white → red → blue → yellow (530 ms each), standby 10 ms blink on white every 10 s, then red steady (as seen by eye). Clean edges, no chatter.

Avoid: GPIO8 (strap + onboard RGB LED), GPIO9 (strap/BOOT button), GPIO15 (strap), GPIO12/13 (USB), GPIO16/17 (UART0 console).

---

## 3. Phases

Each phase ends with a testable exit criterion.

### Phase 0 — Hardware prep
- Build SmarterSpa interface: dividers on the 4 LED lines, common ground; verify LED-off lines read high at the GPIO with the divider attached. ✅ Done: 470k/470k on all 4 lines; levels and mapping verified with `test_leds` (see §1 measurements).
- Verify the isolator PWM on GPIO4/5: 3.3 V high level, ~80 µs period (the library treats an 80 µs high pulse as 100% = 2800 mV). A scope or the Phase 3 code's period log confirms it.
- Source DS18B20, pump CT clamp, enclosure.
- Rotate the Wi-Fi password exposed in the old sketch.
- **Exit:** parts on bench; pin assignment table committed to this file.

### Phase 1 — Skeleton + temperature
- Copy MiniSplit's ESP-IDF/esp-matter scaffolding (build/flash scripts, `partitions.csv`, `sdkconfig.defaults`, logging) into `SpaV2`.
- Strip BME280/Tuya code; add Temperature Sensor endpoint fed by DS18B20.
- Commission from wyse with `Wyse5070DebSetup/commission-thread-device.sh` (see `Wyse5070DebSetup/README.md`).
- **Exit:** commissioned on `WilkieMatterNet`; water temp visible in HA.

### Phase 2 — pH/ORP presentation check (short, the pattern is proven by MiniSplit)
- Add the pH + ORP repurposed Humidity Sensor endpoints publishing fake values (pH 7.45; ORP stepping
  600 → 700 → 850 mV).
- Add the HA templates: pH (`device_class: ph`), ORP raw mV (hidden), ORP Low/Good/High label.
- **Exit:** HA shows pH 7.45, and the ORP label steps Low → Good → High as the fake value crosses the thresholds.

### Phase 3 — Real pH & ORP
- **Use the existing Atlas Gravity libraries** (`atlas_gravity`: `ph_iso_grav`, `orp_iso_grav`, MIT, Atlas Scientific 2020;
  what the old sketch's `PS_Ph_Probe`/`PS_ORP_Probe` wrappers call; not vendored here, and copies are on GitHub, e.g.
  `abdisag1/atlas_gravity`). The **isolated** variants don't use the ADC. `read_voltage()` averages
  `pulseIn(pin, HIGH, 400)` over 3000 pulses and returns **mV = avg high time (µs) × 35**; if no pulses come
  (avg < 0.1 µs), the pin level decides: 80 µs if high, else 0. Everything else is the base classes, unchanged:
  - pH: `read_ph` is piecewise-linear through mid (pH 7) / low (pH 4) / high (pH 10) cal points in mV;
    cleared defaults mid 1500, low 2030, high 975.
  - ORP: `read_orp` = mV − 1500 − `cal_offset`; `cal(value)` sets `cal_offset = mV − (value + 1500)`.
- Port: vendor the library sources into a SpaV2 component and replace only the Arduino calls. `pulseIn` becomes
  MCPWM capture on GPIO4/5 (hardware edge timestamps; average the high-pulse width over 3000 pulses, ~0.25 s).
  Also log the period as a sanity check. `EEPROM` becomes NVS.
- Persist calibration in NVS (the libraries use Arduino EEPROM).

**Calibration plan.** The library math stays as is: pH calibration stores the probe's mV at pH 7 / 4 / 10,
and ORP calibration stores one offset against a reference solution.
- **Triggers:** HA buttons (momentary On/Off endpoints: *pH 7*, *pH 4*, *pH 10*, *ORP*, *Clear*). The
  serial console accepts the original commands as backup: `PHCAL,7|4|10|CLEAR`, `ORPCAL,<mV>|CLEAR`.
- **Procedure:** rinse the probe, put it in the solution, wait for it to settle, press the button.
  - **pH:** order **7 first, then 4, then 10** (mid point sets the centre; 4 and 10 set each slope).
    Two points (7 + 4) are enough for a spa (normal range 7.2–7.8); 10 is optional.
  - **ORP:** **optional**. It only corrects a fixed offset. If done: one point in a redox reference solution.
    The value comes from `ORP_CAL_REF_MV`, default **225 mV** (Atlas's ORP calibration solution);
    `ORPCAL,<mV>` on serial uses another value. ORP **cannot** be derived from pH (it depends mainly on
    free chlorine, plus pH, stabilizer/CYA and temperature), so there is no buffer-based shortcut.
  - **ORP label tuning (the step that matters):** the Low/Good/High boundaries are tuned in HA against test
    strips, which absorbs any probe offset plus this spa's stabilizer and pH:
    1. Run for 1–2 weeks. At each strip test, note free chlorine (ppm) next to the raw ORP mV in HA.
    2. Set **Good** to the ORP range where strips read in range (spa target ~1–3 ppm free chlorine);
       Low/High are below and above it.
    3. Re-check after big changes (stabilizer top-up, water change, probe replacement).
- **No calibration limits (decided 2026-11-15).** Stability and plausibility guards were built and then removed
  at the owner's call: the spa is electrically noisy and a rejected press was worse than an imperfect one. A press
  stores the 30 s mean as-is; only "no signal from the isolator" is refused. The serial log prints the 30 s mean,
  the drift over the window and the resulting slopes, so a bad point is visible and can be redone or cleared
  (`PHCAL,CLEAR` / the Clear button). Sanity reference: library defaults are 2030 / 1500 / 975 mV at
  pH 4 / 7 / 10 (≈175 board-mV per pH; the Gravity board amplifies the ~59 mV/pH glass electrode), and pH 4 must
  read above pH 7.
- **2026-11-15 first calibration:** ORP stored offset −356 mV (new probe read 1369 mV raw in 225 mV solution;
  steady). pH could not be calibrated: the pH channel swung 1435–2596 mV in buffer (pH 4 read *below* the pH 7
  point), so pH was cleared back to factory values pending the BNC-short test (probe vs board/isolator).
- **Feedback:** the *Calibration status* endpoint reports *Calibrating...* at each press, then OK / cleared /
  no signal (decoded by an HA template; the unused *unstable* / *out of range* codes stay reserved). Always
  changing state lets HA react to every press.
- **Cal mode bypasses pump gating:** while calibrating, the probe is in a cup, not the spa. A cal press
  enables live pH/ORP publishing for 10 min regardless of the Phase 4 pump gate.
- **Stored in NVS.** The board has no clock, so HA keeps the dates: an automation stamps
  `input_datetime.spa_last_ph_calibration` / `_orp_` when a press ends in OK.
- **Recommended interval:** pH monthly; ORP every 3 months, or whenever ORP drifts from what test strips suggest.
- **Exit:** matches calibration solutions within tolerance (pH ±0.1, ORP ±10 mV); cal survives reboot.

### Phase 4 — Pump detection & gating
- CT clamp → Pump Running (Occupancy Sensor) + Pump current (A).
- RMS of 1000 samples at 5 kHz (200 ms, 12 cycles), hysteresis `CT_ON_A` / `CT_OFF_A` in `firmware/src/spa_config.h`.
  Heater test 2026-11-14: noise floor **0.30–0.34 A** (10 mV RMS); fan ~0.4 A; heater low 7.4 A, high 11.0 A.
  The old 0.5 / 0.3 A thresholds sat in the noise (stuck ON after the load stopped).
  **Pump measured 2026-11-15: low speed 2.6 A, high speed 8.0 A.** Thresholds **on > 1.5 A, off < 1.0 A**:
  ~4× the noise floor, with margin below low speed. HA's *Spa pump speed* template reads Off / Low / High from
  the current (Low/High split at 5 A, midway).
- Publish pH/ORP only after pump has run ≥5 min; hold last good value otherwise (`binary_sensor.spa_readings_live`).
- **Exit:** pH/ORP update only during pump runs.

### Phase 5 — SmarterSpa decoder
- **Timer-sampled inputs, not edge interrupts:** read GPIO19–22 every 1 ms with a short majority/
  debounce filter (e.g. 3–5 consistent samples). Catches the ~10 ms standby blink; ignores power-up
  chatter, which overflowed a 512-entry interrupt queue in `test_leds` (27k events dropped).
- State machine: standby / generating-OK / generating-low / generating-high / salt-shutdown / fault.
  - Solid state accepted only after ~5 s steady.
  - Ignore while WHITE solid (menu modes) and for 5 s after it ends. (Button lines aren't wired, so no
    button-change rule; counted-flash readouts and boost flashes never hold 5 s, so they're ignored anyway.)
  - Fault = no standby blink for >15 s while not generating, **or all four lines "lit" for >15 s**: with the
    SmarterSpa unpowered every line sits at 0 V, which reads as all LEDs on (seen in `test_leds`).
  - Implemented in `firmware/src/spa_leds.c`; also keeps a lifetime electrode-hours counter in NVS (Phase 7 item).
- Expose one SmarterSpa state code (endpoint 7); HA derives the booleans (see the endpoint map).
- **Exit:** boost cycle and power cycle produce correct states; button presses at the unit cause no false states.

### Phase 6 — Home Assistant side
- Package `Wyse5070DebSetup/homeassistant-config/packages/spa.yaml` (templates, threshold helpers,
  `history_stats` generating + pump runtime 24 h, calibration date stamps, alerts), deployed by that folder's
  `deploy.sh`. Entity renames and the dashboard: `SpaV2/homeassistant/` (steps in its README).
- Alerts (persistent notifications): salt shutdown (5 min), fault / no power (10 min), salt low (30 min),
  pH outside the helper range for 30 min while live, chlorine label **Low** for 1 h while live.
- **Spa** sidebar dashboard (`/spa-dashboard`, from `spa_dashboard.yaml`): tiles, pH gauge, 24 h graphs, details,
  calibration buttons and threshold helpers.
- **Exit:** alerts fire on test values.

### Phase 7 — Hardening & install
- Task watchdog, esp-matter OTA, behavior on Thread loss.
- NVS lifetime electrode-hours counter (wear tracking vs. 7–10k h life).
- Install, in-situ calibration, 1–2 week soak.
- **Exit:** two weeks stable with no manual intervention.

### Phase 8 (optional) — Active control
- Drive buttons via high-side switches (P-FET/PNP from 5 V, or opto across contacts): DOWN during a cycle → decode flash count → electrode current (real salt proxy).
- BOOST from HA.

## 4. Data log (read before judging pH/ORP trends)

All times PST. Values before each change aren't comparable with values after it.

| When | Change | Effect on history |
|---|---|---|
| 2026-11-16 ~8:45 pm | Drained and refilled with fresh (unchlorinated) water; salt added that night | Water chemistry reset |
| 2026-11-16 8:52 pm | HA pH offset +3.9 set to 0 | `sensor.spa_ph` stops being raw + 3.9 |
| 2026-11-17 ~7:55 am | **pH and ORP isolator wires swapped at GPIO4/GPIO5** (they had been reversed since install). Readings resumed 7:55:56 | Before: "pH" was really the ORP probe and "ORP" the pH probe. Firmware ORP cal (offset −357 mV) was taken on the pH probe and is still stored |
| 2026-11-17 8:04:30 am | HA offsets set to match a strip: pH −0.6 (strip ~7.1 vs 7.72), ORP +265 mV (usual strip sanitizer level → mid "Good", 725). ORP "low" threshold reset 895 → 650 | ORP history shows a few seconds of −541 at 8:04:20–28 (helper created at its minimum) |

Pending: proper buffer calibration (clear cal, pH 7/4, ORP 225 mV), then both HA offsets back to 0. Add a row here when it's done.
