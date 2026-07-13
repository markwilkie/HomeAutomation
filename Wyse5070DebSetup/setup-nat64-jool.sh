#!/usr/bin/env bash
#
# setup-nat64-jool.sh
#
# Configures Jool (kernel-level SIIT/NAT64) as the NAT64 gateway for Thread
# devices behind the OTBR container (setup-mg24.sh), replacing OTBR's own
# built-in userspace NAT64 translator, which has an unresolved bug in this
# deployment -- see setup-mg24.sh for the full writeup.
#
# This script:
#   1. Installs jool-dkms/jool-tools + matching kernel headers if missing,
#      loads the jool kernel module, and persists it across reboots.
#   2. Waits for otbr-agent to compute its NAT64 prefix (needs its own
#      prefix manager running first to discover it).
#   3. Disables otbr-agent's own NAT64 (prefix manager + translator are a
#      single combined on/off toggle in OpenThread's API -- there's no way
#      to keep just the prefix manager), then manually re-adds the IDENTICAL
#      prefix to Thread Network Data, so devices already relying on it via
#      DNS64 don't need to be reconfigured or recommissioned.
#   4. Creates/refreshes a Jool NAT64 instance using that same prefix as
#      pool6, and this host's real IP as pool4 (so Jool's own NAPT produces
#      a directly routable source address -- no separate MASQUERADE rule
#      needed).
#   5. Installs a systemd service so all of this reapplies automatically
#      after every reboot (none of steps 2-4 persist on their own -- they're
#      runtime kernel/process state, not config files).
#
# PREREQUISITE: setup-mg24.sh must have already started the otbr container
# and it must have joined/formed the Thread network (so a NAT64 prefix
# exists to discover).
#
# Usage:
#   ./setup-nat64-jool.sh                      # apply now (also called by setup-mg24.sh)
#   sudo systemctl status nat64-jool.service    # check boot-time reapplication

set -euo pipefail

CONTAINER_NAME="otbr"
JOOL_INSTANCE="nat64"
POOL4_PORT_RANGE="61001-65535"

# The boot-time systemd service (installed below) runs this script as root
# with no TTY. `sudo` there fails ("a terminal is required to read the
# password") since root still has to authenticate to invoke it. Skip sudo
# entirely when already root; only use it for interactive runs by a
# non-root user.
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

# Jool's userspace CLI needs CAP_NET_ADMIN. We run it via a privileged
# container chrooted into the real host filesystem, so it uses the actual
# host binary/kernel module but with the container's elevated capabilities --
# avoids needing an interactive sudo password for every single jool command.
jool_host() {
  docker run --rm --privileged --network host -v /:/hostfs alpine chroot /hostfs "$@"
}

echo "==> Ensuring jool-dkms and jool-tools are installed"
if ! dpkg -l jool-tools 2>/dev/null | grep -q '^ii'; then
  ${SUDO} apt-get update -qq
  ${SUDO} apt-get install -y "linux-headers-$(uname -r)" jool-dkms jool-tools
fi

echo "==> Ensuring the jool kernel module is loaded"
if ! lsmod | grep -q '^jool '; then
  ${SUDO} modprobe jool
fi

# Persist across reboots if not already listed
if ! grep -qx "jool" /etc/modules 2>/dev/null; then
  echo "jool" | ${SUDO} tee -a /etc/modules > /dev/null
  echo "    Added jool to /etc/modules for persistence"
fi

echo "==> Waiting for otbr-agent to compute its NAT64 prefix..."
NAT64_PREFIX=""
for i in $(seq 1 30); do
  NAT64_PREFIX=$(docker exec "${CONTAINER_NAME}" sh -c "ot-ctl br nat64prefix" 2>/dev/null \
    | grep -oE '^Local: [0-9a-fA-F:]+/[0-9]+' | awk '{print $2}' || true)
  if [ -n "${NAT64_PREFIX}" ]; then
    break
  fi
  sleep 2
done

if [ -z "${NAT64_PREFIX}" ]; then
  echo "!! Timed out waiting for otbr-agent to compute a NAT64 prefix." >&2
  echo "   Check 'docker logs ${CONTAINER_NAME}' and that Thread has formed/joined." >&2
  exit 1
fi
echo "==> Discovered NAT64 prefix: ${NAT64_PREFIX}"

echo "==> Disabling otbr-agent's own NAT64 (prefix manager + translator are a single toggle)"
docker exec "${CONTAINER_NAME}" sh -c "ot-ctl nat64 disable" > /dev/null

echo "==> Re-adding the same prefix to Thread Network Data manually"
# Matches the flags OTBR's own prefix manager used ("sn low": stable, nat64,
# low preference) so this is indistinguishable from network data's point of
# view -- devices that already discovered this prefix via DNS64 keep working.
docker exec "${CONTAINER_NAME}" sh -c "ot-ctl route add ${NAT64_PREFIX} sn low" > /dev/null
docker exec "${CONTAINER_NAME}" sh -c "ot-ctl netdata register" > /dev/null

echo "==> Determining this host's primary outbound IP"
HOST_IP=$(ip route get 1.1.1.1 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}')
if [ -z "${HOST_IP}" ]; then
  echo "!! Could not determine host IP" >&2
  exit 1
fi
echo "==> Using ${HOST_IP} as Jool's pool4 address"

echo "==> Configuring Jool NAT64 instance '${JOOL_INSTANCE}'"
jool_host jool instance remove "${JOOL_INSTANCE}" > /dev/null 2>&1 || true
# The kernel module needs a moment to tear down the removed instance's
# netfilter hooks; re-adding immediately after can intermittently fail with
# "This namespace lacks an instance named 'nat64'" (seen in practice).
sleep 2
jool_host jool instance add "${JOOL_INSTANCE}" --pool6 "${NAT64_PREFIX}" --netfilter
# pool4 takes one protocol per call.
jool_host jool -i "${JOOL_INSTANCE}" pool4 add "${HOST_IP}" "${POOL4_PORT_RANGE}" --tcp
jool_host jool -i "${JOOL_INSTANCE}" pool4 add "${HOST_IP}" "${POOL4_PORT_RANGE}" --udp
jool_host jool -i "${JOOL_INSTANCE}" pool4 add "${HOST_IP}" "${POOL4_PORT_RANGE}" --icmp

echo ""
echo "==> Jool instance status:"
jool_host jool instance display

# ---- Install systemd service for boot-time persistence --------------------
# None of the above survives a reboot on its own (module load aside): the
# NAT64-disable + manual route re-add is otbr-agent runtime state, and
# Jool's instance/pool6/pool4 config lives only in the running kernel
# module. This script must re-run after every boot, once Docker (and the
# otbr container) are back up -- hence the systemd unit below.
SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
SERVICE_FILE="/etc/systemd/system/nat64-jool.service"
echo ""
echo "==> Installing systemd service for boot-time persistence"
${SUDO} tee "${SERVICE_FILE}" > /dev/null <<EOF
[Unit]
Description=Configure Jool NAT64 for the OTBR Thread network
After=docker.service
Requires=docker.service

[Service]
Type=oneshot
ExecStart=${SCRIPT_PATH}
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

${SUDO} systemctl daemon-reload
${SUDO} systemctl enable nat64-jool.service > /dev/null 2>&1

echo ""
echo "==> Done."
echo "    Jool instance: ${JOOL_INSTANCE}"
echo "    pool6 (IPv6):  ${NAT64_PREFIX}"
echo "    pool4 (IPv4):  ${HOST_IP} ports ${POOL4_PORT_RANGE}"
echo ""
echo "    Check status any time with:"
echo "      docker run --rm --privileged --network host -v /:/hostfs alpine chroot /hostfs jool instance display"
echo "      docker run --rm --privileged --network host -v /:/hostfs alpine chroot /hostfs jool -i ${JOOL_INSTANCE} session display --udp"
echo "      sudo systemctl status nat64-jool.service"
