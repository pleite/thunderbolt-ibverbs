#!/usr/bin/env bash

set -euo pipefail

ns_server="tbv-ci-srv"
ns_client="tbv-ci-cli"
veth_server="tbv-ci-veth-srv"
veth_client="tbv-ci-veth-cli"
server_ip="198.18.0.1"
client_ip="198.18.0.2"
provider_type=""
server_dev=""
client_dev=""
tmp_dir=""
timeout_seconds="${TBV_CI_TIMEOUT_SECONDS:-60}"
server_startup_delay="${TBV_CI_SERVER_STARTUP_DELAY:-2}"
perftest_duration_seconds="${TBV_CI_PERFTEST_DURATION_SECONDS:-3}"
# Matches sysfs counter file paths whose basenames contain error-like keywords.
error_counter_regex="${TBV_CI_ERROR_COUNTER_REGEX:-.*/[^/]*(err|error|drop|discard|retry|timeout|fail|bad|invalid)[^/]*$}"

usage() {
	cat <<'EOF'
Usage:
  tools/ci/datapath-functional.sh

Creates a virtual two-node RDMA harness in Linux network namespaces, runs:
  - ib_write_bw
  - ib_send_bw
  - rping --validate
and fails if any RDMA error counters move.
EOF
}

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

cleanup() {
	ip netns del "$ns_server" >/dev/null 2>&1 || true
	ip netns del "$ns_client" >/dev/null 2>&1 || true
	if [[ -n "$tmp_dir" && -d "$tmp_dir" ]]; then
		rm -rf "$tmp_dir"
	fi
}
trap cleanup EXIT

require_root() {
	if [[ "$(id -u)" -ne 0 ]]; then
		exec sudo \
			TBV_CI_TIMEOUT_SECONDS="${TBV_CI_TIMEOUT_SECONDS:-}" \
			TBV_CI_SERVER_STARTUP_DELAY="${TBV_CI_SERVER_STARTUP_DELAY:-}" \
			TBV_CI_PERFTEST_DURATION_SECONDS="${TBV_CI_PERFTEST_DURATION_SECONDS:-}" \
			TBV_CI_ERROR_COUNTER_REGEX="${TBV_CI_ERROR_COUNTER_REGEX:-}" \
			bash "$0" "$@"
	fi
}

have_counter_files() {
	local dev="$1"
	find "/sys/class/infiniband/$dev/ports/1/counters" -maxdepth 1 -type f \
		-regextype posix-extended \
		-regex "$error_counter_regex" \
		-print 2>/dev/null | sort
}

snapshot_error_counters() {
	local out="$1"
	local dev
	local file
	local key
	local value
	: >"$out"
	for dev in "$server_dev" "$client_dev"; do
		while IFS= read -r file; do
			key="$(basename "$file")"
			if ! value="$(<"$file" 2>/dev/null)"; then
				printf 'WARN: failed reading counter file: %s\n' "$file" >&2
				value=0
			fi
			if [[ ! "$value" =~ ^[0-9]+$ ]]; then
				value=0
			fi
			printf '%s:%s=%s\n' "$dev" "$key" "$value" >>"$out"
		done < <(have_counter_files "$dev")
	done
	sort -o "$out" "$out"
}

assert_counter_deltas_zero() {
	local before="$1"
	local after="$2"
	local failed=0
	local before_value
	local after_value
	local delta
	local key

	while IFS='=' read -r key before_value; do
		after_value="$(awk -F= -v lookup="$key" '$1 == lookup { print $2; exit }' "$after")"
		after_value="${after_value:-$before_value}"
		[[ "$before_value" =~ ^[0-9]+$ ]] || before_value=0
		[[ "$after_value" =~ ^[0-9]+$ ]] || after_value=0
		delta=$((after_value - before_value))
		if ((delta != 0)); then
			printf 'ERROR: counter moved: %s delta=%+d (before=%s after=%s)\n' \
				"$key" "$delta" "$before_value" "$after_value" >&2
			failed=1
		fi
	done <"$before"

	((failed == 0)) || return 1
}

create_netns() {
	ip netns add "$ns_server"
	ip netns add "$ns_client"

	ip link add "$veth_server" type veth peer name "$veth_client"
	ip link set "$veth_server" netns "$ns_server"
	ip link set "$veth_client" netns "$ns_client"

	ip -n "$ns_server" addr add "$server_ip/24" dev "$veth_server"
	ip -n "$ns_client" addr add "$client_ip/24" dev "$veth_client"
	ip -n "$ns_server" link set lo up
	ip -n "$ns_client" link set lo up
	ip -n "$ns_server" link set "$veth_server" up
	ip -n "$ns_client" link set "$veth_client" up
}

setup_rdma_backend() {
	if modprobe rdma_rxe >/dev/null 2>&1; then
		provider_type="rxe"
		server_dev="tbv_ci_rxe_srv"
		client_dev="tbv_ci_rxe_cli"
		ip netns exec "$ns_server" rdma link add "$server_dev" type rxe netdev "$veth_server"
		ip netns exec "$ns_client" rdma link add "$client_dev" type rxe netdev "$veth_client"
		return 0
	fi

	if modprobe siw >/dev/null 2>&1; then
		provider_type="siw"
		server_dev="tbv_ci_siw_srv"
		client_dev="tbv_ci_siw_cli"
		ip netns exec "$ns_server" rdma link add "$server_dev" type siw netdev "$veth_server"
		ip netns exec "$ns_client" rdma link add "$client_dev" type siw netdev "$veth_client"
		return 0
	fi

	die "no software RDMA backend available (need rdma_rxe or siw)"
}

run_pair() {
	local title="$1"
	local tool="$2"
	shift 2
	local -a common_args=("$@")
	local base="${tool##*/}"
	local server_log="$tmp_dir/${base}.server.log"
	local client_log="$tmp_dir/${base}.client.log"

	printf '==> %s\n' "$title"
	timeout "$timeout_seconds" ip netns exec "$ns_server" \
		"$tool" "${common_args[@]}" -d "$server_dev" >"$server_log" 2>&1 &
	local server_pid="$!"
	sleep "$server_startup_delay"
	if ! kill -0 "$server_pid" >/dev/null 2>&1; then
		wait "$server_pid" || true
		cat "$server_log" >&2 || true
		die "$title server failed before client started"
	fi

	local client_status=0
	local server_status=0
	timeout "$timeout_seconds" ip netns exec "$ns_client" \
		"$tool" "${common_args[@]}" -d "$client_dev" "$server_ip" >"$client_log" 2>&1 || client_status=$?
	wait "$server_pid" || server_status=$?

	if ((client_status != 0 || server_status != 0)); then
		cat "$server_log" >&2 || true
		cat "$client_log" >&2 || true
		die "$title failed (server=$server_status client=$client_status)"
	fi
}

run_rping_validate() {
	local server_log="$tmp_dir/rping.server.log"
	local client_log="$tmp_dir/rping.client.log"
	printf '==> rping bit-verify\n'
	timeout "$timeout_seconds" ip netns exec "$ns_server" \
		rping -s -a "$server_ip" -C 64 -S 4096 -v -V >"$server_log" 2>&1 &
	local server_pid="$!"
	sleep "$server_startup_delay"
	if ! kill -0 "$server_pid" >/dev/null 2>&1; then
		wait "$server_pid" || true
		cat "$server_log" >&2 || true
		die "rping server failed before client started"
	fi

	local client_status=0
	local server_status=0
	timeout "$timeout_seconds" ip netns exec "$ns_client" \
		rping -c -a "$server_ip" -C 64 -S 4096 -v -V >"$client_log" 2>&1 || client_status=$?
	wait "$server_pid" || server_status=$?

	if ((client_status != 0 || server_status != 0)); then
		cat "$server_log" >&2 || true
		cat "$client_log" >&2 || true
		die "rping bit-verify failed (server=$server_status client=$client_status)"
	fi
}

main() {
	if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
		usage
		return 0
	fi
	require_root "$@"

	for cmd in ip rdma ib_write_bw ib_send_bw rping timeout; do
		command -v "$cmd" >/dev/null 2>&1 || die "$cmd not found"
	done

	modprobe ib_uverbs >/dev/null 2>&1 || true
	modprobe rdma_cm >/dev/null 2>&1 || true
	modprobe rdma_ucm >/dev/null 2>&1 || true

	create_netns
	setup_rdma_backend
	printf '==> backend: %s (%s,%s)\n' "$provider_type" "$server_dev" "$client_dev"

	tmp_dir="$(mktemp -d)"
	local before="$tmp_dir/counters.before"
	local after="$tmp_dir/counters.after"
	local -a perftest_common_args=(-R -F --report_gbits -D "$perftest_duration_seconds")
	snapshot_error_counters "$before"
	[[ -s "$before" ]] || die "no RDMA error counters found for $server_dev/$client_dev (check sysfs counters and TBV_CI_ERROR_COUNTER_REGEX)"

	run_pair "ib_write_bw" ib_write_bw "${perftest_common_args[@]}"
	run_pair "ib_send_bw" ib_send_bw "${perftest_common_args[@]}"
	run_rping_validate

	snapshot_error_counters "$after"
	assert_counter_deltas_zero "$before" "$after"

	printf '==> Data-path functional test OK\n'
}

main "$@"
