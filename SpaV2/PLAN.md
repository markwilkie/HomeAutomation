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
| 7 | 4 | "Yellow" LED — actually the **GREEN** LED (matches manual power-up order W,R,B,G) | active-low |
| 8 | 5 | +5 V | — |

Decision: **resistor dividers, 470 kΩ top / 470 kΩ bottom**, on the **4 LED lines only** (no power sense, no button inputs) (menu activity is filtered by white-solid + 5 s steady rule). Shared GND with SmarterSpa low-voltage side.

**Measured 2026-11-14 (red line, `test_leds`):** the LED lines are *not* 0/5 V logic.
- Unloaded at the ribbon: **5.5 V off, 3.4 V on**, a 2.1 V swing that never gets near 0 V.
- **High source impedance.** The original 56k/100k divider dragged red's off level from 5.5 V down to ~2.7 V (on ~2.5 V), leaving almost no swing. Green reading stuck-ON with 56k/100k is probably the same loading problem (not measured).
- With **470k/470k**, GPIO20 sits at **~2.7 V off and ~1.7–2.0 V on**, and the C6 reads it cleanly in steady state. Red chattered with 56k/100k because its on level landed near the C6's actual switching point (~2.2 V observed).
- Margins are **empirical, not guaranteed**: the datasheet's V_IL ≤ 0.83 V / V_IH ≥ 2.48 V at 3.3 V are *not* met. If a line chatters in steady state, the guaranteed fix is an LM339 quad comparator at 3.3 V: high-value divider into IN+, ~1.1 V reference into IN−, 10k pull-up on the output (stays active-low). A 74LVC125A will **not** work, because it reads 3.4 V as high.
- Pulls must stay **off** (a ~45k internal pull-up beats a 235k divider). `test_leds` confirms `Pullup: 0, Pulldown: 0` on GPIO19–22 via `gpio_dump_io_configuration`.
- Red still chatters for ~1.4 s while the SmarterSpa powers up (slow supply ramp through the threshold). The decoder must filter this (Phase 5).
- **TODO:** confirm white/blue/green are also on 470k/470k (only red was measured and swapped).

### SmarterSpa LED semantics (ControlOMatic manual, ver. 06/27/16)
| Pattern | Meaning |
|---|---|
| Single flash every 10 s | Standby (manual says GREEN; **captured unit flashes WHITE**, a ~10 ms pulse; trust the capture) |
| Solid BLUE | Generating, salt normal |
| Solid RED | Generating, salt low |
| Solid GREEN | Generating, salt high |
| Solid RED + GREEN | Shut down — salt over max |
| No lights (no standby blink) | No power / GFCI / supply fault |
| WHITE solid | User in power-change or zero-point edit mode (ignore) |
| GREEN+BLUE flash | Boost confirmation (ignore) |
| Counted flashes (G×100 + B×10 + R) | Numeric readout after button press (ignore, or decode in Phase 8) |

**Observed power-up (2026-11-14, `test_leds`):** WHITE+BLUE flash **together** ×5 (158 ms on / 477 ms period;
the manual says WHITE ×2), then WHITE → RED → BLUE → GREEN at 528 ms each, then standby WHITE blinks
every 10 s. Ignore the whole sequence in the decoder.

Other facts: cycle starts when sensor < zero point; cycle length by power level
(L3 default = 2 h); electrode life ~7,000–10,000 production hours; DOWN during a
cycle shows electrode current (>160 too much salt, <125 + low output = add salt).

---

## 2. Target architecture

### Matter endpoint map
| Endpoint | Device type / cluster | Value |
|---|---|---|
| Water temp | Temperature Sensor / Temperature Measurement | °C ×100 |
| ORP | Electrical Sensor / Electrical Power Measurement `Voltage` | mV |
| pH | Electrical Sensor / Electrical Power Measurement `Voltage` | pH ×1000 (HA template rescales) |
| Pump running | Contact Sensor / Boolean State | CT clamp |
| SmarterSpa: Generating | Boolean State | |
| SmarterSpa: Salt Low | Boolean State | |
| SmarterSpa: Salt High | Boolean State | |
| SmarterSpa: Salt Shutdown | Boolean State | alert |
| SmarterSpa: Fault / no power | Boolean State | alert |
| Calibration buttons (pH4, pH7, pH10, ORP, Clear) | On/Off (momentary) | auto-reset to off |

**Open risk:** pH/ORP have no standard Matter cluster that HA renders (not
verified exhaustively). Mapping onto Electrical Power Measurement `Voltage` is
unconfirmed for a sensor-only device in HA — Phase 2 de-risks this.

### Hardware
- ESP32-C6 DevKitM-1, Thread **FTD/router** (mains powered, no sleepy features). Board MAC **58:8C:81:5D:89:1C**: the former `ThreadRouter` board, repurposed 2026-11-14 (`test_leds/flash.ps1` refuses any other board).
- pH/ORP Gravity iso boards → GPIO4/GPIO5 (ADC1).
- DS18B20 (1-Wire) → any free GPIO, 4.7 kΩ pull-up.
- SmarterSpa ribbon → 470k/470k resistor dividers (shared GND) → 4 GPIO inputs (LEDs only).
- Pump CT clamp → ADC1 pin (or comparator → digital input).


### Pin assignment (ESP32-C6-DevKitM-1)
| Signal | GPIO | Notes |
|---|---|---|
| pH (Gravity iso analog out) | GPIO4 | ADC1_CH4 |
| ORP (Gravity iso analog out) | GPIO5 | ADC1_CH5 |
| Pump CT clamp (conditioned) | GPIO6 | ADC1_CH6 (pad JTAG unused; USB-JTAG on 12/13) |
| Spare analog | GPIO2, GPIO3 | ADC1_CH2/3 |
| DS18B20 data | GPIO18 | 4.7 kΩ pull-up to 3.3 V |
| SmarterSpa White LED (ribbon 1, **white** wire) | GPIO19 | 470k/470k divider, active-low, no pulls |
| SmarterSpa Red LED (ribbon 5, **red** wire) | GPIO20 | 470k/470k divider, active-low, no pulls |
| SmarterSpa Blue LED (ribbon 6, **blue** wire) | GPIO21 | 470k/470k divider, active-low, no pulls |
| SmarterSpa Green LED (ribbon 7, **yellow** wire) | GPIO22 | 470k/470k divider, active-low, no pulls |
| Spare digital (Phase 8 BOOST drive) | GPIO23 | |
| SmarterSpa GND (**green** wire) | GND | common ground |

Harness (left→right): yellow, blue, red, white, green(GND). Color→LED mapping **verified 2026-11-14** with `SpaV2/test_leds`: the power-up step printed WHITE → RED → BLUE → GREEN on GPIO19 → 20 → 21 → 22, so no swaps are needed.

Avoid: GPIO8 (strap + onboard RGB LED), GPIO9 (strap/BOOT button), GPIO15 (strap), GPIO12/13 (USB), GPIO16/17 (UART0 console).

---

## 3. Phases

Each phase ends with a testable exit criterion.

### Phase 0 — Hardware prep
- Build SmarterSpa interface: dividers on the 4 LED lines, common ground; verify LED-off lines read high at the GPIO with the divider attached. ✅ Done for red with 470k/470k, and all 4 lines + mapping verified with `test_leds` (see §1 measurements). Remaining: confirm the divider values on white/blue/green.
- Verify Atlas Gravity pH/ORP output range vs. C6 ADC range at chosen supply voltage.
- Source DS18B20, pump CT clamp, enclosure.
- Rotate the Wi-Fi password exposed in the old sketch.
- **Exit:** parts on bench; pin assignment table committed to this file.

### Phase 1 — Skeleton + temperature
- Copy MiniSplit's ESP-IDF/esp-matter scaffolding (build/flash scripts, `partitions.csv`, `sdkconfig.defaults`, logging) into `SpaV2`.
- Strip BME280/Tuya code; add Temperature Sensor endpoint fed by DS18B20.
- Commission from wyse with `Wyse5070DebSetup/commission-thread-device.sh` (see `Wyse5070DebSetup/README.md`).
- **Exit:** commissioned on `WilkieMatterNet`; water temp visible in HA.

### Phase 2 — Matter de-risk spike (pH/ORP rendering)
- Add ORP + pH endpoints on Electrical Power Measurement publishing fake values.
- Add HA template sensor rescaling pH.
- **Exit:** HA displays both. If not, choose fallback cluster here before writing sensor code.

### Phase 3 — Real pH & ORP
- ESP-IDF ADC oneshot + curve-fitting calibration; average 32–64 samples per reading.
- Port Atlas Gravity pH/ORP conversion + calibration math to C; persist cal in NVS.
- Calibration via On/Off momentary endpoints; serial console as backup (same commands as original).
- **Exit:** matches calibration solutions within tolerance; cal survives reboot.

### Phase 4 — Pump detection & gating
- CT clamp → Pump Running Boolean State.
- Publish pH/ORP only after pump has run ≥5 min; hold last good value otherwise.
- **Exit:** pH/ORP update only during pump runs.

### Phase 5 — SmarterSpa decoder
- **Timer-sampled inputs, not edge interrupts:** read GPIO19–22 every 1 ms with a short majority/
  debounce filter (e.g. 3–5 consistent samples). Catches the ~10 ms standby blink; ignores power-up
  chatter, which overflowed a 512-entry interrupt queue in `test_leds` (27k events dropped).
- State machine: standby / generating-OK / generating-low / generating-high / salt-shutdown / fault.
  - Solid state accepted only after ~5 s steady.
  - Ignore while WHITE solid (menu modes) and for a few seconds after any button-line change.
  - Fault = no standby blink for >15 s while not generating.
- Expose Boolean State endpoints listed above.
- **Exit:** boost cycle and power cycle produce correct states; button presses at the unit cause no false states.

### Phase 6 — Home Assistant side
- `history_stats`: generating hours (24 h), pump runtime (24 h).
- Alerts: salt shutdown, fault, pH out of range, ORP low.
- Dashboard card.
- **Exit:** alerts fire on test values.

### Phase 7 — Hardening & install
- Task watchdog, esp-matter OTA, behavior on Thread loss.
- NVS lifetime electrode-hours counter (wear tracking vs. 7–10k h life).
- Install, in-situ calibration, 1–2 week soak.
- **Exit:** two weeks stable with no manual intervention.

### Phase 8 (optional) — Active control
- Drive buttons via high-side switches (P-FET/PNP from 5 V, or opto across contacts): DOWN during a cycle → decode flash count → electrode current (real salt proxy).
- BOOST from HA.
