# SpaV2 test_leds — SmarterSpa LED line check

Standalone ESP-IDF app (no Matter/Thread/Wi-Fi) that checks the 4 SmarterSpa LED
lines reach the ESP32-C6-DevKitM-1 through the 470k/470k dividers (see PLAN.md §1 for why) (Phase 0 in `../PLAN.md`).

| GPIO | LED | Wire |
|---|---|---|
| 19 | WHITE | white (ribbon 1) |
| 20 | RED | red (ribbon 5) |
| 21 | BLUE | blue (ribbon 6) |
| 22 | GREEN | yellow (ribbon 7) |

Active-low (0 = LED lit), no internal pulls, common GND with the SmarterSpa. Boot output and
every 60 s it prints a register dump of the 4 pins (`Pullup: 0, Pulldown: 0` expected).

## Build / flash / monitor (Windows PowerShell)

Uses the same plain-upstream ESP-IDF v5.4.1 as MiniSplit (`C:\esp\v5.4.1\esp-idf`).

```powershell
cd SpaV2\test_leds
.\build.ps1

# flash.ps1 refuses any board but SpaV2's (MAC 58:8C:81:5D:89:1C, the former
# ThreadRouter board).
.\flash.ps1                  # optional: -Port COMn

# Monitor (needs an interactive terminal; Ctrl+] to exit)
. C:\esp\v5.4.1\esp-idf\export.ps1
idf.py -p COMn monitor
```

## Expected output

On boot, the raw level of every pin. With the SmarterSpa powered and idle, all should be
`1[off]`. A `0[ON]` on a pin whose LED is dark means the divider pulls the "off" level
below the C6's input-high threshold (see the ⚠️ in PLAN.md):

```
Boot levels:  WHITE(GPIO19)=1[off]  RED(GPIO20)=1[off]  BLUE(GPIO21)=1[off]  GREEN(GPIO22)=1[off]
```

Power-cycle the SmarterSpa. Each transition prints as `<ms since boot>  <LED> ON/OFF`:

```
   12034  WHITE ON
   12290  WHITE OFF (on for 256 ms)
   12540  WHITE ON
   12795  WHITE OFF (on for 255 ms)
   13050  WHITE ON          <- then WHITE, RED, BLUE, GREEN one at a time
   ...
   14100  RED ON
   ...
   22400  WHITE ON          <- standby: one short WHITE flash every ~10 s
   22437  WHITE OFF (on for 37 ms)
```

(Timings above are illustrative.) Every 10 s there's a summary line:

```
   30000  --- 10 s summary:  WHITE=off tr=2  RED=off tr=0 <-- NO TRANSITIONS  BLUE=off tr=0 <-- NO TRANSITIONS  GREEN=off tr=0 <-- NO TRANSITIONS
```

In standby only WHITE should move, so `NO TRANSITIONS` on RED/BLUE/GREEN is normal
there. It matters during the power-up sequence: any LED that stays at zero then isn't
reaching its GPIO.

Other messages:
- `WARNING: <LED> chattering: >20 edges in 100 ms`: the line is sitting near the
  threshold. Measure it at the GPIO and consider 100k/220k or a 74LVC125A.
- `(+N no-change edges)`: an interrupt fired but the level read back unchanged, from a
  pulse shorter than ISR latency or a bounce.
- `events DROPPED`: the queue overflowed, almost certainly from chatter.
- `state resynced from pin`: the tracked state disagreed with the pin at summary time.

## If the order is wrong

The expected power-up order is WHITE+BLUE together ×5 (this unit; the manual says WHITE ×2), then WHITE → RED → BLUE → GREEN.
Verified correct on 2026-11-14. The app does
**not** remap pins. If, say, the line printed as `RED` lights third and the one printed as
`BLUE` lights second, those two wires are swapped. Fix that deliberately in the harness or
in PLAN.md's pin table, not here by accident.
