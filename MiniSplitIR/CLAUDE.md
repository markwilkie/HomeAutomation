# MiniSplit IR Follow-Me Bridge

## Blocking prerequisite
Milestone 1 (real IR protocol capture + measured follow-me fallback timeout)
must be done by the user on physical hardware before any Device B send logic
is written. Do not guess at or fabricate a protocol/timeout — wait for
captured data (see [instructions.txt](instructions.txt)).

## State model (Device B)
- The AC's IR protocol carries the full state in every frame — there is no
  delta/partial command. Never send anything but the complete serialized
  shadow state `{power, mode, setpoint, fan_speed, swing, followme_enabled}`.
- A Matter attribute write updates one field of the in-memory shadow state;
  the IR transmission always serializes and sends all fields.
- Persist shadow state to NVS so a reboot doesn't risk sending a stale/wrong
  frame before the next real command arrives.

## Command send heuristics (Device B)
Apply all of these before transmitting — do not wire Matter attribute writes
1:1 to IR sends:
- Debounce rapid writes (~300–500ms) and coalesce to one send of final values.
- Skip sends within the unit's own setpoint step resolution of current state.
- Dedup against shadow state — don't retransmit an echo of what's already set.
- Enforce a minimum inter-command spacing floor (unit needs cooldown between
  accepted frames).
- A command send resets/delays the next follow-me heartbeat tick by one
  interval — only one thing can be on the IR LED at a time, and an
  immediately-following heartbeat would be redundant anyway.

## Follow-me heartbeat (Device B)
- Heartbeat interval = roughly half the Milestone-1 measured fallback
  timeout.
- Uses last-known-good value from the Matter subscription to Device A —
  never a fresh poll.
- If Device A's subscription goes stale (no updates for ~2x its normal
  report interval), **stop** the heartbeat and let the unit fall back to
  its internal sensor. Do not keep transmitting a frozen temperature — that
  fail-safe is intentional, same as the real remote losing power.

## Relationship to ../MiniSplit/
[../MiniSplit/](../MiniSplit/) is a separate esp-matter project bridging
this same AC's Tuya cloud status/commands to Matter. That project's Tuya
command path is what this one exists to bypass — Tuya status GETs there are
fine to keep running read-only in parallel; do not remove them as part of
this project's work.
