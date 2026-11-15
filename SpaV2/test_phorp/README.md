# SpaV2 test_phorp — Atlas Gravity pH / ORP via isolator PWM, DS18B20, pump CT

Standalone ESP-IDF app (no Matter/Thread/Wi-Fi) that reads the Atlas Gravity pH and ORP
boards through their analog isolators, the way Atlas's `atlas_gravity` library
(`ph_iso_grav` / `orp_iso_grav`) does. See PLAN.md Phase 3.

| GPIO | Signal |
|---|---|
| 4 | pH isolator PWM out (3.3 V, direct) |
| 5 | ORP isolator PWM out (3.3 V, direct) |
| 6 | Pump CT, SCT-013-030 (30 A → 1 V RMS), biased to ~1.65 V by 10k/10k + 10 µF |
| 7 | DS18B20 data (4.7k pull-up to 3.3 V; internal pull-up also on as a backstop) |

The isolator PWM's high time is measured with MCPWM capture (12.5 ns). The library's
conversions are used unchanged: `mV = avg high µs × 35`, then pH (cleared defaults
mid 1500 / low 2030 / high 975 mV) and ORP (`mV − 1500`, no offset). **Uncalibrated.**

## Build / flash / monitor

```powershell
cd SpaV2\test_phorp
.\build.ps1
.\flash.ps1                  # SpaV2 board only (MAC 58:8C:81:5D:89:1C); optional -Port COMn
. C:\esp\v5.4.1\esp-idf\export.ps1; idf.py -p COMn monitor
```

## Expected output (every 2 s)

```
    4073  | pH  high= 39.36us period= 81.34us ( 48.4%, 24913 pulses) mV= 1377.7 -> pH  7.70 | ORP high= 24.47us period= 83.38us ( 29.4%, 26056 pulses) mV=  856.6 -> ORP  -643.4 mV
```

(First run, 2026-11-14, probes in storage solution.) The period should be about 80 µs,
because the library's ×35 assumes it. `NO PULSES` means the isolator output isn't reaching the pin. `Temp [no DS18B20 on GPIO7]`
means the 1-Wire search found nothing; it retries every 5 s. The CT section samples GPIO6 at 5 kHz
for 200 ms and prints the bias (should be ~1650 mV), RMS, amps (RMS × 30) and pump ON/OFF
(provisional thresholds: on > 0.5 A, off < 0.3 A).
