#!/usr/bin/env bash
#
# setup-bluetooth.sh
# Installs and enables BlueZ (host Bluetooth stack) on wilkie-home-server so
# the matter-server container can do BLE commissioning of Matter devices.
#
# This host has no Bluetooth radio of its own -- it needs a USB dongle. This
# script expects one to already be plugged in (tested against an ASUS
# USB-BT500, USB ID 0b05:190e, but detection isn't limited to that ID -- any
# adapter the kernel's btusb driver picks up will do).
#
# Unlike OTBR/Zigbee2MQTT, this isn't a Docker container -- matter-server
# talks to Bluetooth via the host's D-Bus (see the /run/dbus mount in
# setup-matter-server.sh's docker-compose.yml), so BlueZ has to be running
# directly on the host, not containerized.
#
# Usage:
#   ./setup-bluetooth.sh
#
# Re-running this script is safe: apt-get install is idempotent, and
# enabling an already-enabled service is a no-op.

set -euo pipefail

echo "==> Checking for a USB Bluetooth adapter..."
if ! lsusb | grep -iq "bluetooth\|0b05:190e"; then
  echo "!! No Bluetooth adapter found in 'lsusb' output. Is it plugged in?"
  echo "   Run 'lsusb' yourself to check -- this script only warns, doesn't block,"
  echo "   in case detection just doesn't recognize your specific dongle's string."
else
  echo "==> Found adapter:"
  lsusb | grep -i "bluetooth\|0b05:190e"
fi

echo "==> Installing bluez (if not already installed)..."
sudo apt-get update -qq
sudo apt-get install -y bluez

echo "==> Enabling and starting bluetooth.service..."
sudo systemctl enable bluetooth
sudo systemctl restart bluetooth

echo "==> Waiting for the adapter to register..."
sleep 3

echo ""
echo "==> bluetoothd status:"
systemctl status bluetooth --no-pager | head -10

echo ""
echo "==> Adapter list (bluetoothctl):"
bluetoothctl list

echo ""
echo "==> Done."
echo ""
echo "    IMPORTANT: matter-server was likely already running before BlueZ"
echo "    existed on this host. Its D-Bus connection to bluetoothd needs a"
echo "    fresh container start to pick this up:"
echo ""
echo "      docker restart matter-server"
echo "      docker logs matter-server --tail 30"
echo ""
echo "    Look for it acquiring a Bluetooth adapter in those logs (vs. the"
echo "    earlier 'no bluetoothd/D-Bus socket found' warning path)."
