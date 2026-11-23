#!/bin/bash
# Build the MiniSplit Matter bridge with plain idf.py (Linux/macOS/WSL, or an ESP-IDF shell).
# On Windows prefer build.ps1 / flash.ps1 (job cap, right ESP-IDF install,
# MAC-checked flashing). Never runs set-target on an existing project: it
# wipes build/ (full rebuild) and regenerates sdkconfig. See tools/ESP_BUILDS.md.
set -e
cd "$(dirname "$0")"
[ -f sdkconfig ] || idf.py set-target esp32c6
idf.py build
