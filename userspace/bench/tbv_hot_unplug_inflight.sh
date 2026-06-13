#!/usr/bin/env bash
set -euo pipefail

hosts=${TBV_HOT_UNPLUG_HOSTS:-}
dev=${TBV_HOT_UNPLUG_DEV:-usb4_rdma0}
duration=${TBV_HOT_UNPLUG_DURATION:-20}
warmup=${TBV_HOT_UNPLUG_WARMUP:-3}
unplug_host=${TBV_HOT_UNPLUG_UNPLUG_HOST:-}
unplug_cmd=${TBV_HOT_UNPLUG_CMD:-}
ssh_cmd=${TBV_SSH:-ssh}
ssh_opts=${TBV_SSH_OPTS:-"-o BatchMode=yes -o ControlMaster=no -S none -o ConnectTimeout=10"}
out_dir=${TBV_HOT_UNPLUG_OUT:-/tmp/tbv-hot-unplug}
sender_args=${TBV_HOT_UNPLUG_SENDER_ARGS:-"--report_gbits --size 4096 --tx-depth 128"}

usage() {
	cat <<EOF
Usage: tbv_hot_unplug_inflight.sh --hosts H1,H2 --unplug-cmd CMD [options]

Runs ib_send_bw with in-flight traffic, triggers hot-unplug, and verifies the
sender exits (no silent hang waiting for completions).

Options:
  --hosts H1,H2             Required sender,receiver SSH hosts
  --dev DEV                 RDMA device name on both hosts (default: $dev)
  --duration SECONDS        ib_send_bw duration (default: $duration)
  --warmup SECONDS          Delay before unplug trigger (default: $warmup)
  --unplug-host HOST        Host where unplug command runs (default: sender)
  --unplug-cmd CMD          Required command to trigger hot-unplug
  --out DIR                 Output directory (default: $out_dir)
EOF
}

while (($#)); do
	case "$1" in
	--hosts) hosts=$2; shift 2 ;;
	--dev) dev=$2; shift 2 ;;
	--duration) duration=$2; shift 2 ;;
	--warmup) warmup=$2; shift 2 ;;
	--unplug-host) unplug_host=$2; shift 2 ;;
	--unplug-cmd) unplug_cmd=$2; shift 2 ;;
	--out) out_dir=$2; shift 2 ;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		echo "unknown option: $1" >&2
		usage >&2
		exit 2
		;;
	esac
done

[[ -n "$hosts" ]] || {
	echo "--hosts is required" >&2
	exit 2
}
[[ -n "$unplug_cmd" ]] || {
	echo "--unplug-cmd is required" >&2
	exit 2
}

IFS=, read -r -a host_list <<< "$hosts"
if ((${#host_list[@]} != 2)); then
	echo "expected --hosts H1,H2, got: $hosts" >&2
	exit 2
fi
sender=${host_list[0]}
receiver=${host_list[1]}
if [[ -z "$unplug_host" ]]; then
	unplug_host=$sender
fi
read -r -a ssh_opts_array <<< "$ssh_opts"

run_remote() {
	local host=$1
	shift
	"$ssh_cmd" "${ssh_opts_array[@]}" "$host" "$@"
}

mkdir -p "$out_dir"
server_log="$out_dir/ib_send_bw.server.log"
client_log="$out_dir/ib_send_bw.client.log"
unplug_log="$out_dir/unplug.log"
remote_server_log="/tmp/tbv-hot-unplug.server.log"

cleanup() {
	run_remote "$receiver" "pkill -f 'ib_send_bw.*--ib-dev $dev' || true" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "tbv-hot-unplug: sender=$sender receiver=$receiver unplug_host=$unplug_host dev=$dev"

run_remote "$receiver" "pkill -f 'ib_send_bw.*--ib-dev $dev' || true"
run_remote "$receiver" "nohup ib_send_bw --ib-dev '$dev' --duration '$duration' $sender_args >'$remote_server_log' 2>&1 </dev/null &"
sleep 1

run_remote "$sender" "ib_send_bw '$receiver' --ib-dev '$dev' --duration '$duration' $sender_args" \
	>"$client_log" 2>&1 &
client_pid=$!

sleep "$warmup"
if ! run_remote "$unplug_host" "$unplug_cmd" >"$unplug_log" 2>&1; then
	echo "hot-unplug command failed; see $unplug_log" >&2
	wait "$client_pid" || true
	exit 1
fi

# Give ib_send_bw extra time to emit terminal CQ/error output after duration.
deadline=$((SECONDS + duration + 30))
while kill -0 "$client_pid" >/dev/null 2>&1; do
	if ((SECONDS >= deadline)); then
		break
	fi
	sleep 1
done
if kill -0 "$client_pid" >/dev/null 2>&1; then
	echo "ib_send_bw sender did not exit after hot-unplug; probable lost completion" >&2
	exit 1
fi
wait "$client_pid" || true

run_remote "$receiver" "cat '$remote_server_log' 2>/dev/null || true" \
	>"$server_log" || true

if ! grep -Eq "completion with error|Failed status|BW average|MsgRate" "$client_log"; then
	echo "sender log does not contain expected completion output: $client_log" >&2
	exit 1
fi

echo "tbv-hot-unplug: PASS (logs in $out_dir)"
