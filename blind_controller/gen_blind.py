#!/usr/bin/env python3
"""Generate the raw RF timing arrays for ANY A-OK blind channel (1-16).

The 5 captured remote channels revealed the full A-OK frame format (see
ADDING_A_BLIND.md). Every command is a 9-byte frame:

    [03][C0][C1][00][CHANNEL_hi][CHANNEL_lo? ...]   <-- 16-bit one-hot channel
    actually: byte3:byte4 = 16-bit one-hot channel (blindN sets bit N-1)
    byte5=01  byte6=00  byte7=command(DOWN=02, UP=01)  byte8=checksum(sum&0xFF)

This tool builds blindN by taking the PROVEN-WORKING, even-aligned blind2
capture and swapping only the pulse-pairs whose bits differ. It validates that
every generated frame (a) decodes back to the intended bytes and (b) leaves all
physical pulse-pairs clean (one short + one long), so the output is guaranteed
transmittable.

Usage:
    python gen_blind.py 7              # print blind7_down/blind7_up C arrays
    python gen_blind.py 7 >> blind_data.h   # append (then remove the old #endif)

Always confirms the method against all 5 known channels before emitting.
"""
import sys
import re
from pathlib import Path

HEADER = Path(__file__).with_name("blind_data.h")
H = HEADER.read_text()

CMD_DOWN, CMD_UP = 0x02, 0x01
BASE_DOWN, BASE_UP = "blind2_down", "blind2_up"   # even-aligned, proven working


def parse(name):
    m = re.search(name + r"\[\d+\]\s*=\s*\{([^}]*)\}", H)
    if not m:
        raise SystemExit(f"array {name} not found in blind_data.h")
    return [int(x) for x in re.findall(r"\d+", m.group(1))]


def clean(a, b):
    lo, hi = min(a, b), max(a, b)
    return lo < 440 and hi > 480


def frame_starts(s):
    out = []
    for i, v in enumerate(s):
        if 4500 <= v <= 5500 and i + 2 + 144 < len(s) and all(100 < x < 1000 for x in s[i+2:i+2+40]):
            out.append(i + 2)
    return out


def decode(s, st, nb=72):
    return "".join("1" if s[st+2*k] > s[st+2*k+1] else "0" for k in range(nb))


def to_bytes(bits):
    return [int(bits[i:i+8], 2) for i in range(0, 72, 8)]


def bits_from_bytes(by):
    return "".join(format(b, "08b") for b in by)


def frame_bytes(n, cmd):
    """9-byte frame for blind n (1-16), given command byte."""
    onehot = 1 << (n - 1)                 # blind1->bit0 ... blind16->bit15
    by = [0x03, 0xC0, 0xC1, (onehot >> 8) & 0xFF, onehot & 0xFF, 0x01, 0x00, cmd, 0]
    by[8] = sum(by[:8]) & 0xFF            # checksum = sum of bytes 0..7
    return by


def generate(base_name, target_bytes):
    s = parse(base_name)[:]
    starts = frame_starts(s)
    if starts[0] % 2 != 0:
        raise SystemExit(f"{base_name} is not even-aligned; pick an even base")
    base = decode(s, starts[0])
    target = bits_from_bytes(target_bytes)
    flips = [i for i in range(72) if base[i] != target[i]]
    edited = 0
    for st in starts:
        if st % 2 or st + 2*72 > len(s):
            continue
        if any(not clean(s[st+2*k], s[st+2*k+1]) for k in range(72)):
            continue
        for bp in flips:
            i = st + 2*bp
            s[i], s[i+1] = s[i+1], s[i]
        edited += 1
    if edited == 0:
        raise SystemExit("no clean frames were edited - check the base array")
    return s


def validate(name, arr, target_bytes):
    target = bits_from_bytes(target_bytes)
    for st in frame_starts(arr):
        if st % 2 or st + 2*72 > len(arr):
            continue
        if any(not clean(arr[st+2*k], arr[st+2*k+1]) for k in range(72)):
            raise SystemExit(f"{name}: physical pair not clean @frame {st}")
        if decode(arr, st) != target:
            raise SystemExit(f"{name}: frame {st} does not decode to target")


def self_check():
    """Confirm the model reproduces all 5 known channels before generating."""
    for n in range(1, 6):
        for d, cmd in (("down", CMD_DOWN), ("up", CMD_UP)):
            real = to_bytes(decode(parse(f"blind{n}_{d}"), frame_starts(parse(f"blind{n}_{d}"))[0]))
            if real != frame_bytes(n, cmd):
                raise SystemExit(f"self-check FAILED on blind{n}_{d}: model {frame_bytes(n,cmd)} != real {real}")


def emit(name, arr, by):
    cmd = "Down" if name.endswith("down") else "Up"
    hexbytes = " ".join("%02X" % b for b in by)
    lines = [f"// {name.replace('_',' ').title()} (DERIVED via gen_blind.py)",
             f"// Frame bytes: {hexbytes}  (16-bit one-hot channel, checksum = sum&0xFF)",
             f"unsigned int {name}[{len(arr)}] = {{"]
    row = []
    body = []
    for v in arr:
        row.append(f"{v:>5}")
        if len(row) == 8:
            body.append("  " + ", ".join(row) + ",")
            row = []
    if row:
        body.append("  " + ", ".join(row))
    if body[-1].endswith(","):
        body[-1] = body[-1][:-1]
    return "\n".join(lines + body + ["};"])


def main():
    if len(sys.argv) != 2 or not sys.argv[1].isdigit():
        raise SystemExit("usage: python gen_blind.py <blind_number 1-16>")
    n = int(sys.argv[1])
    if not 1 <= n <= 16:
        raise SystemExit("blind number must be 1-16 (16-bit one-hot channel)")

    self_check()  # aborts if the model can't reproduce the 5 known channels

    down_by, up_by = frame_bytes(n, CMD_DOWN), frame_bytes(n, CMD_UP)
    down = generate(BASE_DOWN, down_by)
    up = generate(BASE_UP, up_by)
    validate(f"blind{n}_down", down, down_by)
    validate(f"blind{n}_up", up, up_by)

    print(emit(f"blind{n}_down", down, down_by))
    print()
    print(emit(f"blind{n}_up", up, up_by))


if __name__ == "__main__":
    main()
