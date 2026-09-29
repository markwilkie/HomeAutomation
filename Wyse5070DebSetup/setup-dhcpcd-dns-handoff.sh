#!/usr/bin/env bash
#
# setup-dhcpcd-dns-handoff.sh
# Stops dhcpcd from managing /etc/resolv.conf, so Tailscale's MagicDNS proxy
# (100.100.100.100) is the sole, stable owner of it.
#
# Why: dhcpcd (managing enp1s0 -- see /etc/dhcpcd.conf, no nohook directives
# by default) rewrites /etc/resolv.conf back to the LAN router
# (192.168.15.1, from the pfSense DHCP lease) on every renewal/rebind.
# tailscaled notices, logs a "trample: resolv.conf changed from what we
# expected" warning, and overwrites it back to 100.100.100.100 -- this fight
# was observed recurring every 2-4 minutes continuously in the journal, not
# just at boot (see https://tailscale.com/s/dns-fight, which tailscaled
# itself links in that warning).
#
# This is a *deeper* cause of the DNS breakage than the boot-order race
# setup-docker-wait-for-dns.sh addresses: that script only waits for DNS to
# work once, at docker.service startup. If dhcpcd wins one of these
# skirmishes at the exact moment a container is created (any time, not just
# boot), that container's embedded DNS resolver bakes in the broken
# (LAN-router-only, non-Tailscale) config anyway. Both fixes matter --
# neither is a substitute for the other.
#
# The fix: `nohook resolv.conf` in /etc/dhcpcd.conf tells dhcpcd to leave
# /etc/resolv.conf alone entirely (it still acquires/renews the IP lease
# normally -- this only disables its DNS-file-writing hook), leaving
# tailscaled as the sole writer.
#
# Usage (needs sudo -- run interactively, not from an unattended script):
#   sudo bash ./setup-dhcpcd-dns-handoff.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Must run as root (sudo bash $0)" >&2
  exit 1
fi

CONF="/etc/dhcpcd.conf"
IFACE="enp1s0"

if grep -qE '^\s*nohook\s+resolv\.conf' "${CONF}"; then
  echo "==> ${CONF} already has 'nohook resolv.conf', nothing to change"
else
  echo "==> Appending 'nohook resolv.conf' to ${CONF}"
  {
    echo ""
    echo "# Added by setup-dhcpcd-dns-handoff.sh ($(date -I)):"
    echo "# leave /etc/resolv.conf to tailscaled -- see that script's header"
    echo "# comment for why (dhcpcd/tailscaled were fighting over it)."
    echo "nohook resolv.conf"
  } >> "${CONF}"
fi

echo "==> Rebinding ${IFACE} so dhcpcd picks up the new hook config now"
echo "    (renews the DHCP lease in place -- does not change the IP;"
echo "    a sub-second interruption is possible, SSH should survive it)"
dhcpcd --rebind "${IFACE}"

sleep 2
echo ""
echo "==> Current /etc/resolv.conf:"
cat /etc/resolv.conf
echo ""
echo "==> Done. Watch for a bit to confirm it stays pointed at Tailscale:"
echo "      watch -n5 cat /etc/resolv.conf"
echo "    Or check journalctl for the fight stopping:"
echo "      journalctl -u tailscaled --since '5 min ago' | grep trample"
echo "    (should produce no new matches going forward)"
