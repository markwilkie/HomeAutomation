#!/bin/bash
# WSL2/Linux fallback only — for normal Windows development use
# setup-toolchain.ps1 instead (native Windows build works fine for this
# project; see WINDOWS_TOOLCHAIN_SETUP.md).
#
# Installs ESP-IDF v5.4.1 + build prerequisites for the ESP32-C6 inside
# WSL2 Ubuntu (or native Linux), for the rare case a build step needs a
# Linux host (e.g. host-side chip-tool/chip-cert/ZAP tooling).
#
# Safe to re-run: skips the clone if ESP-IDF is already checked out.
set -e

IDF_VERSION="${IDF_VERSION:-v5.4.1}"
IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
TARGET="${TARGET:-esp32c6}"

echo "=========================================="
echo "ESP-IDF $IDF_VERSION setup for $TARGET"
echo "=========================================="

echo
echo "== 1. Installing build prerequisites (apt) =="
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0

echo
echo "== 2. Fetching ESP-IDF $IDF_VERSION into $IDF_DIR =="
if [ -d "$IDF_DIR/.git" ]; then
    echo "Already present, skipping clone."
else
    mkdir -p "$(dirname "$IDF_DIR")"
    git clone -b "$IDF_VERSION" --recursive https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

echo
echo "== 3. Installing tools for target: $TARGET =="
( cd "$IDF_DIR" && ./install.sh "$TARGET" )

echo
echo "== 4. Wiring up shell helpers (~/.bashrc) =="
ALIAS_LINE="alias get_idf=\". $IDF_DIR/export.sh\""
CCACHE_LINE="export IDF_CCACHE_ENABLE=1"
grep -qxF "$ALIAS_LINE" ~/.bashrc 2>/dev/null || echo "$ALIAS_LINE" >> ~/.bashrc
grep -qxF "$CCACHE_LINE" ~/.bashrc 2>/dev/null || echo "$CCACHE_LINE" >> ~/.bashrc

cat <<EOF

Done.

Open a NEW terminal (so ~/.bashrc changes take effect), then:

  get_idf                                # source the IDF environment
  mkdir -p ~/projects
  cp -r /mnt/c/Users/Administrator/Documents/GitHub/HomeAutomation/DormerWindowState ~/projects/
  cd ~/projects/DormerWindowState
  idf.py set-target $TARGET
  idf.py build

See WINDOWS_TOOLCHAIN_SETUP.md for flashing (requires usbipd-win to forward
the COM port into WSL2).
EOF
