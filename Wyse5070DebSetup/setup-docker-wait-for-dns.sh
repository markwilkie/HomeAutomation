#!/usr/bin/env bash
#
# setup-docker-wait-for-dns.sh
# Makes docker.service wait for working DNS resolution before starting.
#
# Why: on 2026-09-29 this host rebooted (likely a power interruption -- the
# previous boot's journal stops mid-stream with no clean shutdown logged,
# then an ~11.5 hour gap before the next boot). Docker started before
# Tailscale's MagicDNS proxy (100.100.100.100) had finished pulling its
# resolver config from the coordination server. `tailscaled.service` was
# already reporting "active" at that point -- systemd unit-active only means
# the process started, not that MagicDNS is actually answering queries yet --
# so ordering docker.service `After=tailscaled.service` alone would NOT have
# caught this.
#
# Docker's embedded per-container DNS resolver (127.0.0.11) captures its
# upstream nameserver list from the host's /etc/resolv.conf once, at the
# time each container/network is created, and never refreshes it for
# already-running containers. Since /etc/resolv.conf here always points at
# Tailscale's 100.100.100.100 (see other scripts in this repo), any
# container created while that proxy isn't yet answering gets a broken
# resolver baked in permanently, until restarted. That's exactly what
# happened to mcp-gateway-monarch/todo/trilium at this boot -- 8+ hours of
# "Temporary failure in name resolution" on every outbound API call, until
# manually restarted.
#
# Fix: an ExecStartPre wait-loop that polls actual DNS resolution (not just
# service-active state) for up to 30s before letting docker.service start.
# Fails open (exits 0) on timeout rather than blocking boot indefinitely --
# if DNS is genuinely down for a longer stretch, we'd rather docker start
# late/degraded than have it (and everything depending on it -- Caddy, HA,
# Zigbee2MQTT, Z-Wave JS UI) not start at all.
#
# Usage (needs sudo -- run interactively, not from an unattended script):
#   sudo bash ./setup-docker-wait-for-dns.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Must run as root (sudo bash $0)" >&2
  exit 1
fi

WAIT_SCRIPT="/usr/local/sbin/wait-for-dns.sh"
DROPIN_DIR="/etc/systemd/system/docker.service.d"
DROPIN_FILE="${DROPIN_DIR}/wait-for-dns.conf"

echo "==> Writing ${WAIT_SCRIPT}"
tee "${WAIT_SCRIPT}" > /dev/null <<'EOF'
#!/usr/bin/env bash
# Waits up to 30s for real DNS resolution to work, then exits 0 regardless
# (fails open -- see setup-docker-wait-for-dns.sh for why).
for i in $(seq 1 30); do
  if getent hosts github.com >/dev/null 2>&1; then
    logger -t wait-for-dns "DNS working after ${i}s, proceeding"
    exit 0
  fi
  sleep 1
done
logger -t wait-for-dns "DNS still not resolving after 30s, proceeding anyway (fail-open)"
exit 0
EOF
chmod 755 "${WAIT_SCRIPT}"

echo "==> Writing ${DROPIN_FILE}"
mkdir -p "${DROPIN_DIR}"
tee "${DROPIN_FILE}" > /dev/null <<EOF
[Unit]
After=tailscaled.service network-online.target
Wants=tailscaled.service network-online.target

[Service]
ExecStartPre=${WAIT_SCRIPT}
EOF

echo "==> Reloading systemd"
systemctl daemon-reload

echo ""
echo "==> Done. Takes effect on the next boot of docker.service (not applied"
echo "    retroactively -- this run does NOT restart docker, which would"
echo "    bounce every container on the host)."
echo "    Verify after the next reboot with:"
echo "      systemctl status docker (should be started after tailscaled)"
echo "      journalctl -t wait-for-dns --boot"
