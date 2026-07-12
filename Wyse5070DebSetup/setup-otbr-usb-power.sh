#!/usr/bin/env bash
#
# setup-otbr-usb-power.sh
#
# otbr-agent crashed 7 times in ~30 hours (confirmed via full docker log
# history: "radio tx timeout" -> "Failed to communicate with RCP - no
# response from RCP during initialization" -> HandleRcpTimeout(), including
# a burst of 4 crashes within about 2 hours). The RCP (/dev/ttyUSB0, a CP210x)
# sits behind an internal USB hub (Realtek RTS5411) and the host's root USB2
# controller, and `lsusb -t` + sysfs showed both of those had
# power/control=auto (autosuspend-eligible) even though the leaf CP210x
# device itself already had autosuspend correctly disabled. If Linux
# suspends the hub during a quiet period, the resume latency needed to wake
# it for a time-sensitive spinel radio transaction can plausibly exceed
# OpenThread's RCP response deadline -- which matches this exact crash
# signature and its bursty-then-quiet pattern. Host load average was normal
# (~1.1) at the time, ruling out CPU contention as the cause.
#
# This disables autosuspend for the root USB2 controller (1d6b:0002 -- the
# generic Linux root hub ID, harmless to apply broadly) and the specific
# Realtek RTS5411 hub (0bda:5411) the RCP sits behind, via a udev rule so it
# survives reboots and re-enumeration (a plain `echo on > .../power/control`
# only takes effect until the next unplug/replug or reboot).
#
# Usage:
#   ./setup-otbr-usb-power.sh

set -euo pipefail

RULE_FILE="/etc/udev/rules.d/99-otbr-usb-no-autosuspend.rules"

echo "==> Installing udev rule to disable autosuspend on the OTBR RCP's USB chain"
sudo tee "${RULE_FILE}" > /dev/null <<'EOF'
# Disable USB autosuspend for the hub chain feeding the OTBR RCP radio
# (/dev/ttyUSB0). See setup-otbr-usb-power.sh for the full incident writeup.
SUBSYSTEM=="usb", ATTR{idVendor}=="1d6b", ATTR{idProduct}=="0002", ATTR{power/control}="on"
SUBSYSTEM=="usb", ATTR{idVendor}=="0bda", ATTR{idProduct}=="5411", ATTR{power/control}="on"
EOF

echo "==> Reloading udev rules"
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=1d6b --attr-match=idProduct=0002
sudo udevadm trigger --attr-match=idVendor=0bda --attr-match=idProduct=5411

echo "==> Applying immediately to already-attached devices (belt-and-suspenders --"
echo "    the trigger above should already cover this, but sysfs writes are cheap"
echo "    and idempotent)"
for dev in /sys/bus/usb/devices/*; do
  vendor_file="${dev}/idVendor"
  product_file="${dev}/idProduct"
  if [ -f "${vendor_file}" ] && [ -f "${product_file}" ]; then
    vendor=$(cat "${vendor_file}")
    product=$(cat "${product_file}")
    if { [ "${vendor}" = "1d6b" ] && [ "${product}" = "0002" ]; } || \
       { [ "${vendor}" = "0bda" ] && [ "${product}" = "5411" ]; }; then
      echo "on" | sudo tee "${dev}/power/control" > /dev/null
      echo "    ${dev} (${vendor}:${product}) -> $(cat "${dev}/power/control")"
    fi
  fi
done

echo ""
echo "==> Done. Verify any time with:"
echo "      lsusb -t"
echo "      cat /sys/bus/usb/devices/usb1/power/control      # expect: on"
echo "      cat /sys/bus/usb/devices/1-1/power/control        # expect: on"
