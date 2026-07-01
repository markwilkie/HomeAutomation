#!/bin/bash
# Build and flash script for the Dormer Window State Matter sensor.
set -e

PROJECT_NAME="dormer-window-state"
# This project targets the ESP32-C6 (RISC-V, WiFi 6 + BLE 5 + 802.15.4/Thread).
TARGET="${1:-esp32c6}"

echo "=========================================="
echo "Dormer Window State Build Script"
echo "=========================================="
echo "Target: $TARGET"
echo

if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Run from project root."
    exit 1
fi

echo "Setting ESP32 target to: $TARGET"
idf.py set-target "$TARGET"

echo "Running menuconfig (optional - press 'Q' to skip)..."
echo "Configure under 'Dormer Window State Configuration':"
echo "  - WINDOW_SENSOR_GPIO (which pin the reed switch is wired to)"
echo "  - WINDOW_SENSOR_ACTIVE_LOW (wiring polarity)"
read -p "Run menuconfig? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    idf.py menuconfig
fi

echo
echo "Building firmware..."
idf.py build

echo
echo "Ready to flash. Connect your ESP32-C6 and press Enter."
read -r

# Default port: COM6 on Windows; override as arg 2 (e.g. /dev/ttyUSB0 on Linux).
PORT="${2:-COM6}"
echo "Flashing to $PORT..."
idf.py -p "$PORT" flash

echo
read -p "Start serial monitor? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    idf.py -p "$PORT" monitor
fi

echo "Done!"
