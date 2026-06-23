#!/bin/bash
# Build and flash script for MiniSplit Matter Bridge

set -e

PROJECT_NAME="minisplit-matter-bridge"
TARGET="${1:-esp32s3}"

echo "=========================================="
echo "MiniSplit Matter Bridge Build Script"
echo "=========================================="
echo "Target: $TARGET"
echo

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Run from project root."
    exit 1
fi

# Set target
echo "Setting ESP32 target to: $TARGET"
idf.py set-target $TARGET

# Configure
echo "Running menuconfig (optional - press 'Q' to skip)..."
echo "Configure:"
echo "  - WiFi SSID/Password in src/main.c"
echo "  - Tuya credentials in CONFIG.md"
read -p "Run menuconfig? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    idf.py menuconfig
fi

# Build
echo
echo "Building firmware..."
idf.py build

# Flash
echo
echo "Ready to flash. Connect your ESP32 and press Enter."
read

PORT="${2:-/dev/ttyUSB0}"
echo "Flashing to $PORT..."
idf.py flash -p $PORT

# Monitor
echo
read -p "Start serial monitor? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    idf.py monitor -p $PORT
fi

echo "Done!"
