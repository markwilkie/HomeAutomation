# ThreadRouter

## What this is
A minimal ESP32-C6 Matter-over-Thread endpoint (`src/main.cpp`) whose only
real job is to join the household Thread mesh as a Full Thread Device
(router), extending coverage for other Thread devices -- originally built
to help MiniSplit's board, which has a long history of a marginal link to
OTBR (see `../MiniSplit/CLAUDE.md`). It exposes one no-op On/Off Plug-in
Unit endpoint purely so it commissions as a normal Matter node.

## How to commission a Thread device in this house (READ FIRST)
There are **two separate Thread networks** here, each with its own border
router and its own credentials:

| | OTBR (wyse) -- the one HA uses | SmartThings Hub |
|---|---|---|
| Network name | `WilkieMatterNet` | `ST-3011356111` |
| Ext PAN ID | `3954086294739573` | `3ea27d386512794d` |
| Partition ID | `0x2d2f19a4` | `0x4d6f48d3` |
| Border agent ext addr | `221a192c16105ba9` | `423bfb12db37cc91` |
| OMR prefix | `fde9:1db9:683f:1::/64` | `fd40:cb54:b1f7:1::/64` |

(Both advertise `_meshcop._udp` on the LAN -- re-check with avahi-browse or
the zeroconf snippet in this project's history if anything changes.)

### The procedure: commission from matter-server over BLE (no phone)
Step-by-step instructions are in **`../Wyse5070DebSetup/README.md` ->
"Adding a new Matter-over-Thread device"**, using
`../Wyse5070DebSetup/commission-thread-device.sh` (run on wyse with the
device's pairing code). It loads OTBR's dataset into matter-server, then
commissions over wyse's own Bluetooth into HA's fabric. The phone's Thread
credential store never gets involved. Verified 2026-10-04: this board became
Matter node 42, a router on partition `0x2d2f19a4`, and appeared in HA
automatically. For this board specifically: erase+flash it, then run the
script with code `34970112332`.

### Why not the phone (HA Companion app) -- currently broken on the Pixel
HA's Companion app on the Pixel does **not** commission the device itself
over BLE -- it hands that off to Google Play Services' Matter flow, which
provisions the Thread dataset **from Google's own Thread credential store**,
not from HA's `thread.datasets`. On 2026-10-02 that store preferred the
SmartThings network, so every device commissioned this way landed there,
and **Sync Thread credentials** refused to override it ("device prefers:
ST-3011356111"). Clearing Google Play Services data fixed the preferred
network but then broke phone commissioning entirely: the phone aborts with
"Failed to generate credentials" right after fetching the device's
attestation certificate, before sending any dataset. Not resolved -- use
the matter-server method above.

### Verify after commissioning
`docker exec otbr ot-ctl router table` (or `child table`) on wyse should
show the device, and its logs should show `Partition ID 0x2d2f19a4` (OTBR),
not `0x4d6f48d3` (SmartThings).

## Root cause of the 2026-10-02 commissioning failures (confirmed)
Every real BLE commissioning of this board landed on the SmartThings
network: Partition ID `0x4d6f48d3`, neighbor `423bfb12db37cc91`, and after
the first one matter-server reached the device at an `fd40:cb54:b1f7:1::`
address. All three match the SmartThings Hub's own `_meshcop._udp`
advertisement exactly. With the Hub powered off, the same commissioning
produced zero Parent Responses and a self-promoted isolated partition --
because the device had the SmartThings dataset and that network no longer
existed. The phone was handing out the wrong network's credentials; nothing
was wrong with the firmware, OTBR, or the radio.

A temporary bypass that hardcoded OTBR's dataset into `main.cpp` was used to
get the board onto OTBR during the investigation; it has been removed and
the board was commissioned normally via matter-server on 2026-10-04.

Theories investigated and ruled out on the way here (all wrong, kept so
nobody re-chases them):
- esp_matter 1.6.0 vs 1.5.0 (now pinned 1.5.0 to match MiniSplit anyway).
- Dataset content / TLV encoding / `otDatasetSetActiveTlvs` vs struct API.
- `_AttachToThreadNetwork()` call ordering.
- BLE/802.15.4 coexistence contention during the active BLE session.
- An earlier claim here that the SmartThings Hub shares OTBR's network key
  was also wrong -- different name and ext PAN ID, i.e. a different
  network entirely.
The mistake that cost the most time: debugging only from the device's side
and never checking *which network's* credentials it had actually been given.
Comparing the device's partition ID / ext addresses against each border
router's `_meshcop` advertisement settles it in one command.

## OpenThread persists role/partition state to NVS across reboots
`Settings------: Read NetworkInfo {rloc:..., role:router, ...}` at boot means
the device is restoring its previous role straight from NVS, and
`OpenThread attached to netif` fires within the first second without a real
attach. `idf.py flash` does not erase NVS. To force a genuine attach, run
`idf.py -p COM4 erase-flash` first.

## SmartThings Hub
Physically right next to wyse. Runs its own independent Thread network
(above). Powering it off orphans SmartThings-paired Thread devices, which
will then try to latch onto any other router sharing their credentials --
turn it back on promptly after any test.

## OTBR TX power (2026-10-02)
Found at 0 dBm (Sonoff Dongle Plus MG24 supports ~20 dBm) -- raised to 19 dBm
via `ot-ctl txpower 19`. **Runtime setting, not confirmed persistent** across
an OTBR container restart -- recheck `ot-ctl txpower` after any restart.
Mini-Split's RSSI (`sensor.minisplit_thread_rssi`) went from a sustained -97
to -106 dBm to -65 to -72 dBm over the following hours.

## OTBR has its own known reliability issue
`/mnt/data/appdata/otbr/watchdog.log` on wyse shows otbr-agent going
unresponsive and being watchdog-restarted every day or two (diagnostics in
`/mnt/data/appdata/otbr/diagnostics/unresponsive-*.log`). Unrelated to the
commissioning issue above.

## Diagnostic tooling notes
- The board's USB-Serial-JTAG console can wedge after many rapid open/close
  cycles (every capture returns identical stale bytes, or zero bytes). A
  software reset doesn't clear it; a full `idf.py -p COM4 flash` does.
- Do the reset and the serial capture in the same Python process
  (`subprocess` the esptool reset, then open the port) -- separate tool calls
  miss the early-boot window.
- Don't keyword-filter live logs during a commissioning attempt you can't
  easily repeat; capture everything to a file and grep afterward.
- `ot-ctl counters mac reset` + a tightly-timed before/after check isolates
  one attach attempt's effect on OTBR's radio counters.
