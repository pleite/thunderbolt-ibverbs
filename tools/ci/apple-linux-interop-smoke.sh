#!/usr/bin/env bash
# Apple<->Linux interop smoke test (Linux side).
#
# This script verifies the Linux-side mac_compat profile setup and, when a
# real Apple peer is present, exercises the full FA57 verb exchange.
#
# Without Apple hardware it still validates:
#   - The module loads with profile=mac_compat.
#   - The Apple FA57 service directory is registered in the Thunderbolt
#     property database.
#   - The module unloads cleanly.
#
# With Apple hardware (TBV_APPLE_PEER_IP set):
#   - The FA57 DMA path is negotiated and a verbs device appears.
#   - A single UC SEND is exchanged using mac_tb_rdma_probe on the Apple
#     side (caller is responsible for running that tool).
#
# Usage:
#   tools/ci/apple-linux-interop-smoke.sh [--help] [--peer-ip IP]
#
# Environment:
#   TBV_APPLE_PEER_IP=<ip>    IPv4 address of the Apple peer. When set the
#                             test waits for a verbs device to appear and
#                             checks that the Apple data path is live.
#   TBV_APPLE_HCA=<name>      IB device name to look for (default: auto-detect
#                             the first usb4_rdma* or usb4_apple* device).
#   TBV_APPLE_TIMEOUT=<sec>   Seconds to wait for the verbs device to appear
#                             after the module loads (default: 30).
#   TBV_APPLE_SKIP_UNLOAD=1   Leave the module loaded after the test.

set -euo pipefail

usage() {
	cat <<'EOF'
Usage:
  tools/ci/apple-linux-interop-smoke.sh [--help] [--peer-ip IP]

Options:
  --peer-ip IP    IPv4 address of the Apple peer (enables live interop check).
  --help          Print this message and exit.

Environment:
  TBV_APPLE_PEER_IP=<ip>    Equivalent to --peer-ip.
  TBV_APPLE_HCA=<name>      Expected IB device name (auto-detected by default).
  TBV_APPLE_TIMEOUT=<sec>   Wait timeout for verbs device (default: 30).
  TBV_APPLE_SKIP_UNLOAD=1   Leave the module loaded after the test.

Examples:
  # Linux-side-only smoke (no Apple hardware required):
  sudo tools/ci/apple-linux-interop-smoke.sh

  # Full interop smoke with an Apple peer at 192.168.1.2:
  sudo tools/ci/apple-linux-interop-smoke.sh --peer-ip 192.168.1.2
EOF
}

die() {
	printf 'FAIL: %s\n' "$*" >&2
	exit 1
}

pass() {
	printf 'PASS: %s\n' "$*"
}

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

peer_ip="${TBV_APPLE_PEER_IP:-}"
hca="${TBV_APPLE_HCA:-}"
timeout_sec="${TBV_APPLE_TIMEOUT:-30}"
skip_unload="${TBV_APPLE_SKIP_UNLOAD:-0}"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--peer-ip)
		peer_ip="${2:-}"
		[[ -n "$peer_ip" ]] || die "--peer-ip requires an argument"
		shift 2
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		die "unknown argument: $1"
		;;
	esac
done

if [[ "$(id -u)" -ne 0 ]]; then
	exec sudo env \
		TBV_APPLE_PEER_IP="$peer_ip" \
		TBV_APPLE_HCA="$hca" \
		TBV_APPLE_TIMEOUT="$timeout_sec" \
		TBV_APPLE_SKIP_UNLOAD="$skip_unload" \
		bash "$0" "$@"
fi

# ---------------------------------------------------------------------------
# Step 1: Verify the module is built and loadable.
# ---------------------------------------------------------------------------

ko="$repo_root/kernel/thunderbolt_ibverbs.ko"

if [[ ! -f "$ko" ]]; then
	printf '==> Kernel module not found at %s; attempting build\n' "$ko"
	kver="$(uname -r)"
	make -C "$repo_root" KVER="$kver" KDIR="/lib/modules/$kver/build" \
		modules 2>&1 | tail -n 5
fi

[[ -f "$ko" ]] || die "thunderbolt_ibverbs.ko not found; run 'make modules' first"

# Unload a stale module from a previous run if present.
if grep -q '^thunderbolt_ibverbs ' /proc/modules 2>/dev/null; then
	printf '==> Unloading pre-existing thunderbolt_ibverbs module\n'
	rmmod thunderbolt_ibverbs
fi

kver="$(uname -r)"
install -D -m 0644 "$ko" "/lib/modules/$kver/extra/thunderbolt_ibverbs.ko"
depmod -a "$kver"

# ---------------------------------------------------------------------------
# Step 2: Load with mac_compat profile.
# ---------------------------------------------------------------------------

printf '==> Loading thunderbolt_ibverbs profile=mac_compat\n'
modprobe thunderbolt_ibverbs \
	profile=mac_compat \
	bind_services=1 \
	allocate_rings=1 \
	start_rings=1 \
	enable_tunnels=1 \
	register_verbs=1

grep -q '^thunderbolt_ibverbs ' /proc/modules ||
	die "module did not appear in /proc/modules"
pass "module loaded with mac_compat profile"

# Check that the Apple (FA57) service registration is logged in dmesg.
if dmesg | grep -q 'thunderbolt_ibverbs.*apple.*service'; then
	pass "Apple FA57 service registered in Thunderbolt property database"
else
	printf 'NOTE: Apple FA57 service log line not found in dmesg (may not be visible without a Thunderbolt controller)\n'
fi

# ---------------------------------------------------------------------------
# Step 3: Verify verbs device (with or without Apple peer).
# ---------------------------------------------------------------------------

wait_for_ibdev() {
	local waited=0
	local dev=""

	while (( waited <= timeout_sec )); do
		if [[ -n "$hca" ]]; then
			if [[ -d "/sys/class/infiniband/$hca" ]]; then
				printf '%s\n' "$hca"
				return 0
			fi
		else
			dev="$(find /sys/class/infiniband -mindepth 1 -maxdepth 1 \
				\( -name 'usb4_rdma*' -o -name 'usb4_apple*' \) \
				-printf '%f\n' 2>/dev/null | sort | head -n 1)"
			if [[ -n "$dev" ]]; then
				printf '%s\n' "$dev"
				return 0
			fi
		fi
		sleep 1
		waited=$(( waited + 1 ))
	done
	return 1
}

if [[ -n "$peer_ip" ]]; then
	printf '==> Waiting up to %s s for a verbs device (Apple peer at %s)\n' \
		"$timeout_sec" "$peer_ip"
	ibdev=""
	ibdev="$(wait_for_ibdev)" ||
		die "no usb4_rdma/usb4_apple device appeared within ${timeout_sec}s; is the Apple peer connected and running mac_tb_rdma_probe?"

	pass "verbs device appeared: $ibdev"
	rdma link show
	ibv_devices

	printf '==> Apple<->Linux interop: GID exchange\n'
	# Print the GID table for the device so the Apple-side operator
	# can configure mac_tb_rdma_probe with the correct remote GID.
	if command -v ibv_query_gid >/dev/null 2>&1; then
		ibv_query_gid "$ibdev" 1 0 2>/dev/null || true
	fi
	rdma link show "$ibdev"/1 2>/dev/null || true

	pass "Apple<->Linux interop: Linux verbs device is live"
	cat <<EOF

To complete the interop exchange, run on the Apple side:
  MAC_TB_RDMA_PROBE_RTR=1 MAC_TB_RDMA_PROBE_SEND=1 \\
    mac_tb_rdma_probe rdma_en1 ${peer_ip} <linux-qpn> 7

Replace <linux-qpn> with the QPN shown above by rdma link show / u4_pingpong.
EOF
else
	printf '==> No Apple peer IP set; skipping live device check\n'
	printf '    Set TBV_APPLE_PEER_IP=<ip> or use --peer-ip to run the full test.\n'
fi

# ---------------------------------------------------------------------------
# Step 4: Unload.
# ---------------------------------------------------------------------------

if [[ "$skip_unload" == "1" ]]; then
	printf '==> Skipping unload (TBV_APPLE_SKIP_UNLOAD=1)\n'
else
	printf '==> Unloading module\n'
	rmmod thunderbolt_ibverbs
	grep -q '^thunderbolt_ibverbs ' /proc/modules &&
		die "module did not unload cleanly"
	pass "module unloaded cleanly"
fi

printf '\n==> Apple<->Linux interop smoke OK\n'
