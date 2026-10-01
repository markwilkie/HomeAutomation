#!/usr/bin/env python3
"""Decode a RawPinTest.ino capture transcript (as saved by capture_serial.ps1)
into mark/space durations and, where a TCL112 header is found, bytes.

Pure stdlib -- runs on any host, independent of the ESP32 board. Input is
just text lines of the form "pin2=<0|1> t=<micros>"; everything else in the
transcript (prompts, CAPTURE_DONE/CAPTURE_END) is ignored.

The receiver module idles HIGH and pulls LOW during an IR carrier burst
(confirmed in test_apps/ir_loopback's loopback_test.c), so an interval where
the pin was held LOW is a mark (burst) and HIGH is a space (gap). Timing
constants below are duplicated from src/ir_tcl112.c / IR_PROTOCOL_REFERENCE.md
-- re-derive from there if this ever needs updating.

Usage:
  python decode_raw_capture.py capture.log
  python decode_raw_capture.py capture.log --dump-raw
"""

import argparse
import re
import sys

HDR_MARK_US = 3000
HDR_SPACE_US = 1650
BIT_MARK_US = 500
ONE_SPACE_US = 1050
ZERO_SPACE_US = 325
BIT_SPACE_THRESHOLD_US = (ONE_SPACE_US + ZERO_SPACE_US) // 2
HDR_TOLERANCE_US = 400
FRAME_LEN_BYTES = 14
FRAME_LEN_BITS = FRAME_LEN_BYTES * 8
# 1 header + 112 bits + 1 footer = 114 symbols/frame, 2 edges (mark+space)
# per symbol. A clean N-frame capture (e.g. 2 for a Type2+Type1 pair) should
# total N * EDGES_PER_FRAME edges exactly -- used as a loud sanity check
# since a short count silently decodes as a truncated-frame warning deep in
# per-frame output otherwise (how esp_capture/follow_me_enable.log and
# esp_capture/power_off.log's missing tails were originally found, 2026-10-01).
EDGES_PER_FRAME = (1 + FRAME_LEN_BITS + 1) * 2

EDGE_RE = re.compile(r"pin2=([01])\s+t=(\d+)")


def parse_edges(text):
    edges = []
    for line in text.splitlines():
        m = EDGE_RE.search(line)
        if m:
            edges.append((int(m.group(2)), int(m.group(1))))
    return edges


def intervals_from_edges(edges):
    """Return list of (kind, duration_us, start_t) -- kind is 'MARK' or 'SPACE'.

    Interval i (between edges[i-1] and edges[i]) was held at the level set
    by edges[i-1], since that's the edge that drove the pin to that level.
    """
    out = []
    for i in range(1, len(edges)):
        t_prev, level_prev = edges[i - 1]
        t_cur, _ = edges[i]
        duration = t_cur - t_prev
        kind = "MARK" if level_prev == 0 else "SPACE"
        out.append((kind, duration, t_prev))
    return out


def within(value, nominal, tolerance):
    return (nominal - tolerance) <= value <= (nominal + tolerance)


def find_headers(intervals):
    """Indices into `intervals` where a MARK/SPACE pair matches the TCL112 header."""
    starts = []
    for i in range(len(intervals) - 1):
        k0, d0, _ = intervals[i]
        k1, d1, _ = intervals[i + 1]
        if (k0 == "MARK" and within(d0, HDR_MARK_US, HDR_TOLERANCE_US) and
                k1 == "SPACE" and within(d1, HDR_SPACE_US, HDR_TOLERANCE_US)):
            starts.append(i)
    return starts


def decode_frame(intervals, header_idx):
    """Try to decode 14 bytes starting after the header at `header_idx`.
    Returns (bytes_or_None, detail_lines)."""
    detail = []
    bit_start = header_idx + 2  # first bit's mark is right after header's space
    needed = FRAME_LEN_BITS * 2  # each bit = one MARK + one SPACE interval
    if bit_start + needed > len(intervals):
        detail.append(f"  truncated: need {needed} more intervals, have {len(intervals) - bit_start}")
        return None, detail

    frame = bytearray(FRAME_LEN_BYTES)
    for byte_idx in range(FRAME_LEN_BYTES):
        byte_val = 0
        for bit_idx in range(8):
            idx = bit_start + (byte_idx * 8 + bit_idx) * 2
            mk, mk_dur, _ = intervals[idx]
            sp, sp_dur, _ = intervals[idx + 1]
            if mk != "MARK" or sp != "SPACE":
                detail.append(f"  byte {byte_idx} bit {bit_idx}: expected MARK/SPACE, "
                               f"got {mk}/{sp} -- edge likely dropped, capture unreliable from here on")
                return None, detail
            if not within(mk_dur, BIT_MARK_US, 200):
                detail.append(f"  byte {byte_idx} bit {bit_idx}: bit-mark {mk_dur}us far from "
                               f"nominal {BIT_MARK_US}us")
            bit_val = 1 if sp_dur >= BIT_SPACE_THRESHOLD_US else 0
            byte_val |= bit_val << bit_idx
        frame[byte_idx] = byte_val
    return bytes(frame), detail


def checksum_ok(frame):
    is_special = frame[3] == 0x02
    expected = (sum(frame[0:13]) + (0x0F if is_special else 0x00)) & 0xFF
    return expected, frame[13] == expected


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture_file", help="transcript saved by capture_serial.ps1")
    ap.add_argument("--dump-raw", action="store_true",
                     help="print every mark/space interval (for a low-level timing diff "
                          "against the TX firmware's fill_symbol() output)")
    args = ap.parse_args()

    with open(args.capture_file, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    edges = parse_edges(text)
    if not edges:
        print(f"No 'pin2=<0|1> t=<micros>' lines found in {args.capture_file}", file=sys.stderr)
        sys.exit(1)

    intervals = intervals_from_edges(edges)
    print(f"{len(edges)} edges -> {len(intervals)} intervals")

    if args.dump_raw:
        print("\n-- raw intervals --")
        for i, (kind, dur, t) in enumerate(intervals):
            print(f"  [{i:4d}] t={t:>9d} {kind:5s} {dur:5d}us")

    headers = find_headers(intervals)
    if not headers:
        print("\nNo TCL112 header (mark~3000us/space~1650us) found. "
              "Capture may be noise, a different protocol, or wrong polarity "
              "(try inverting the MARK/SPACE assignment in intervals_from_edges).")
        sys.exit(0)

    expected_edges = len(headers) * EDGES_PER_FRAME
    if len(edges) != expected_edges:
        short_by = expected_edges - len(edges)
        if short_by > 0:
            print(f"\n*** WARNING: {len(edges)} edges captured, expected {expected_edges} for "
                  f"{len(headers)} frame(s) -- {short_by} edges SHORT. Capture likely truncated "
                  f"(stopped before the transmission finished, or the transmitter itself cut off "
                  f"early -- see the per-frame 'truncated' detail below). ***")
        else:
            print(f"\n*** WARNING: {len(edges)} edges captured, expected {expected_edges} for "
                  f"{len(headers)} frame(s) -- {-short_by} edges EXTRA. Capture likely includes "
                  f"noise or a partial extra frame beyond what was decoded below. ***")

    print(f"\n{len(headers)} header(s) found -- decoding each as a TCL112 frame:")
    for n, h in enumerate(headers):
        print(f"\n-- frame {n} (header at interval {h}) --")
        frame, detail = decode_frame(intervals, h)
        for line in detail:
            print(line)
        if frame is None:
            continue
        hex_str = " ".join(f"{b:02X}" for b in frame)
        expected_csum, ok = checksum_ok(frame)
        msg_type = frame[3]
        print(f"  bytes: {hex_str}")
        print(f"  MsgType=0x{msg_type:02X} ({'Type 2/special' if msg_type == 0x02 else 'Type 1' if msg_type == 0x01 else 'other'})")
        print(f"  checksum: frame=0x{frame[13]:02X} expected=0x{expected_csum:02X} "
              f"{'OK' if ok else 'MISMATCH'}")


if __name__ == "__main__":
    main()
