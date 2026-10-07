#!/usr/bin/env python3
"""Append one MiniSplit manual-override record to /config/minisplit/overrides.csv.

Called by shell_command.minisplit_log_override (configuration.yaml) from the
"MiniSplit Manual Override Detect" automation, 2026-10-06. Long-term record of
what was chosen by hand vs. what the seasonal schedule wanted, to tune the
winter/summer targets -- the recorder only keeps ~10 days.
"""
import csv
import os
import sys

PATH = "/config/minisplit/overrides.csv"
FIELDS = ["time", "chosen_f", "schedule_f", "outdoor_7d_f", "room_f", "season_pct", "mode", "source"]

values = sys.argv[1:]
if len(values) != len(FIELDS):
    sys.exit(f"expected {len(FIELDS)} values, got {len(values)}: {values}")
new = not os.path.exists(PATH)
with open(PATH, "a", newline="") as f:
    w = csv.writer(f)
    if new:
        w.writerow(FIELDS)
    w.writerow(values)
