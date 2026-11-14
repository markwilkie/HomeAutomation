"""Tests for the daily brief's deterministic code (no model, no network).

Run on wyse with the deployed venv, which setup-daily-brief-oai.sh does
before copying anything:
    /mnt/data/appdata/daily-brief-oai/app/venv/bin/python -m pytest -q Wyse5070DebSetup/daily-brief/tests
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
for sub in ("azure", "shared"):
    sys.path.insert(0, str(ROOT / sub))
