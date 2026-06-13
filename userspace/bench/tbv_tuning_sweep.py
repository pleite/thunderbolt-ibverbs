#!/usr/bin/env python3
"""Parametric sweep over the three key thunderbolt_ibverbs tunables.

Sweeps `nhi_interrupt_throttle_ns`, `native_fragment_striping`, and
`zcopy_min_bytes` across a small benchmark matrix and writes one CSV row per
(profile, case, direction) triple. Intended to be run against the same
strix↔strix four-rail hardware used by the full perftest suite so results are
directly comparable.

Each profile reloads the module on both hosts with the profiled parameters.
The reload uses the same `thunderbolt-ibverbs-reload-system` helper as the
main runner, which waits for the native service handshake before returning.

Usage:
  TBV_RDMA_CORE=<path> TBV_PERFTEST=<path> python3 tbv_tuning_sweep.py \\
    --server strix-1 --client strix-2 \\
    --output-dir /tmp/tuning-sweep \\
    [--profiles all|throttle|striping|zcopy] \\
    [--base-port 19800]

Typical nix-wrapped invocation:
  nix run .#tbv-tuning-sweep -- --server strix-1 --client strix-2 \\
    --output-dir bench/results/strix-2p-noiommu-2x40g/result
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

def _require_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        print(f"tbv-tuning-sweep: {name} is not set", file=sys.stderr)
        raise SystemExit(1)
    return value


RDMA_CORE = _require_env("TBV_RDMA_CORE")
PERFTEST = _require_env("TBV_PERFTEST")


# ---------------------------------------------------------------------------
# Profile definitions
# ---------------------------------------------------------------------------

# Base args shared by all profiles (linux_perf, four-rail, noiommu).
_BASE_MODULE_ARGS: dict[str, str] = {
    "profile": "linux_perf",
    "compat": "off",
    "tbnet": "prefer_rdma",
    "tbnet_identity": "off",
    "roce_netdev": "br0.lan",
    "lanes": "2",
    "bind_services": "1",
    "allocate_rings": "1",
    "start_rings": "1",
    "negotiate_native": "1",
    "enable_tunnels": "1",
    "native_data": "1",
    "apple_data": "0",
    "register_verbs": "1",
}


def _render_module_args(args: dict[str, str]) -> str:
    return " ".join(f"{k}={v}" for k, v in args.items())


def _profile(name: str, label: str, extra: dict[str, str]) -> dict:
    merged = {**_BASE_MODULE_ARGS, **extra}
    return {"name": name, "label": label, "module_args": merged}


# Throttle sweep: native_fragment_striping=0, zcopy_min_bytes=4096 held
# constant while nhi_interrupt_throttle_ns varies.
_THROTTLE_PROFILES = [
    _profile("throttle_off",  "throttle=0",      {"nhi_interrupt_throttle_ns": "0",      "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("throttle_25us", "throttle=25000",   {"nhi_interrupt_throttle_ns": "25000",  "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("throttle_50us", "throttle=50000",   {"nhi_interrupt_throttle_ns": "50000",  "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("throttle_100us","throttle=100000",  {"nhi_interrupt_throttle_ns": "100000", "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("throttle_200us","throttle=200000",  {"nhi_interrupt_throttle_ns": "200000", "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
]

# Striping sweep: nhi_interrupt_throttle_ns=50000 for throughput, zcopy=4096.
_STRIPING_PROFILES = [
    _profile("striping_off", "striping=0", {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("striping_on",  "striping=1", {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "1", "zcopy_min_bytes": "4096"}),
]

# Zero-copy threshold sweep: throttle=50000, striping=0.
_ZCOPY_PROFILES = [
    _profile("zcopy_off",    "zcopy=0",     {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "0", "zcopy_min_bytes": "0"}),
    _profile("zcopy_4k",     "zcopy=4096",  {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "0", "zcopy_min_bytes": "4096"}),
    _profile("zcopy_16k",    "zcopy=16384", {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "0", "zcopy_min_bytes": "16384"}),
    _profile("zcopy_64k",    "zcopy=65536", {"nhi_interrupt_throttle_ns": "50000", "native_fragment_striping": "0", "zcopy_min_bytes": "65536"}),
]

PROFILE_SETS: dict[str, list[dict]] = {
    "throttle": _THROTTLE_PROFILES,
    "striping": _STRIPING_PROFILES,
    "zcopy":    _ZCOPY_PROFILES,
    "all":      _THROTTLE_PROFILES + _STRIPING_PROFILES[1:] + _ZCOPY_PROFILES[1:],
}


# ---------------------------------------------------------------------------
# Benchmark cases
# ---------------------------------------------------------------------------

# Focused micro-matrix: enough points to see the effect of each tunable
# without waiting as long as the full 246-case perftest suite.
#
# BW: ib_write_bw at three representative sizes, two QP counts.
# LAT: ib_write_lat at three sizes, always qps=1.

BW_SIZES    = [4096, 65536, 1048576]
BW_QPS      = [1, 4]
LAT_SIZES   = [64, 4096, 65536]
BW_ITERS    = 500
LAT_ITERS   = 500


# ---------------------------------------------------------------------------
# CSV schema
# ---------------------------------------------------------------------------

CSV_FIELDS = [
    "timestamp_utc",
    "profile",
    "label",
    "nhi_interrupt_throttle_ns",
    "native_fragment_striping",
    "zcopy_min_bytes",
    "server",
    "client",
    "direction",
    "test",
    "size_bytes",
    "qps",
    "iters",
    "port",
    "status",
    "duration_s",
    "bw_peak_gbps",
    "bw_avg_gbps",
    "msg_rate_mpps",
    "lat_min_us",
    "lat_max_us",
    "lat_typical_us",
    "lat_avg_us",
    "lat_stdev_us",
    "lat_p99_us",
    "lat_p999_us",
    "error",
]


# ---------------------------------------------------------------------------
# SSH / process helpers
# ---------------------------------------------------------------------------

def _ssh_args(target: str, command: str, ssh_opts: list[str]) -> list[str]:
    base = ["ssh", "-o", "ConnectTimeout=8", "-o", "BatchMode=yes"]
    for opt in ssh_opts:
        base += ["-o", opt]
    base += [target, "sudo -n bash -lc " + shlex.quote(command)]
    return base


def _run_local(
    args: list[str],
    *,
    check: bool = True,
    timeout: int | None = None,
) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(proc.stdout[-2000:])
    return proc


def _run_ssh(
    host: str,
    command: str,
    ssh_opts: list[str],
    *,
    check: bool = True,
    timeout: int | None = None,
) -> subprocess.CompletedProcess[str]:
    return _run_local(_ssh_args(host, command, ssh_opts), check=check, timeout=timeout)


# ---------------------------------------------------------------------------
# Module reload
# ---------------------------------------------------------------------------

def reload_module(host: str, module_args: dict[str, str], ssh_opts: list[str]) -> None:
    args_str = _render_module_args(module_args)
    cmd = (
        "modprobe -r thunderbolt_ibverbs 2>/dev/null; "
        f"modprobe thunderbolt_ibverbs {args_str}"
    )
    _run_ssh(host, cmd, ssh_opts, check=True, timeout=30)
    # Give the native handshake time to complete.
    time.sleep(5)


# ---------------------------------------------------------------------------
# State check
# ---------------------------------------------------------------------------

def _speed_gbps(value: str) -> float | None:
    m = re.search(r"([0-9]+(?:\.[0-9]+)?)", value)
    return float(m.group(1)) if m else None


def assert_rails(
    host: str,
    ssh_opts: list[str],
    dev: str,
    expect_rails: int,
    expect_speed: str,
) -> None:
    peers = _run_ssh(
        host,
        "cat /sys/kernel/debug/thunderbolt_ibverbs/peers 2>/dev/null || true",
        ssh_opts,
        check=False,
        timeout=10,
    ).stdout
    rails = []
    for raw in peers.splitlines():
        line = raw.strip()
        if line.startswith("peer") and "backend=native" in line:
            continue
        if line.startswith("rail="):
            attrs = dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^ ]+)", line))
            if attrs.get("active") == "1" and attrs.get("data_ready") == "1":
                rails.append(attrs)
    if len(rails) != expect_rails:
        raise RuntimeError(
            f"{host}: expected {expect_rails} ready rails, saw {len(rails)}"
        )
    expected = _speed_gbps(expect_speed)
    if expected is not None:
        for rail in rails:
            observed = _speed_gbps(rail.get("link_speed", ""))
            if observed != expected:
                raise RuntimeError(
                    f"{host}: rail speed {rail.get('link_speed')} != {expect_speed}"
                )


# ---------------------------------------------------------------------------
# Perftest runners
# ---------------------------------------------------------------------------

def _perftest_cmd(
    bin_name: str,
    dev: str,
    gid_index: int,
    size: int,
    iters: int,
    qps: int,
    tx_depth: int,
    port: int,
    peer: str | None,
    timeout: int,
) -> str:
    parts = [
        f"LD_LIBRARY_PATH={shlex.quote(RDMA_CORE + '/lib')}",
        "timeout",
        shlex.quote(str(timeout)),
        shlex.quote(f"{PERFTEST}/bin/{bin_name}"),
        "-F",
        "-d", shlex.quote(dev),
        "-x", shlex.quote(str(gid_index)),
        "-s", shlex.quote(str(size)),
        "-n", shlex.quote(str(iters)),
        "-q", shlex.quote(str(qps)),
        "--tx-depth", shlex.quote(str(tx_depth)),
        "-p", shlex.quote(str(port)),
        "--report_gbits",
    ]
    if peer:
        parts.append(shlex.quote(peer))
    return " ".join(parts)


def _parse_bw(stdout: str) -> dict[str, str]:
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[0].isdigit() and parts[1].isdigit():
            return {
                "bw_peak_gbps": parts[2],
                "bw_avg_gbps": parts[3],
                "msg_rate_mpps": parts[4],
            }
    raise ValueError("could not parse bandwidth summary")


def _parse_lat(stdout: str) -> dict[str, str]:
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) >= 9 and parts[0].isdigit() and parts[1].isdigit():
            return {
                "lat_min_us": parts[2],
                "lat_max_us": parts[3],
                "lat_typical_us": parts[4],
                "lat_avg_us": parts[5],
                "lat_stdev_us": parts[6],
                "lat_p99_us": parts[7],
                "lat_p999_us": parts[8],
            }
    raise ValueError("could not parse latency summary")


def _run_pair(
    bin_name: str,
    parse_fn,
    server_host: str,
    client_host: str,
    ssh_opts: list[str],
    dev: str,
    gid_index: int,
    size: int,
    iters: int,
    qps: int,
    tx_depth: int,
    port: int,
    timeout: int,
    server_start_delay: float,
) -> tuple[str, dict[str, str], str]:
    server_cmd = _perftest_cmd(bin_name, dev, gid_index, size, iters, qps, tx_depth, port, None, timeout)
    client_cmd = _perftest_cmd(bin_name, dev, gid_index, size, iters, qps, tx_depth, port, server_host, timeout)

    server_proc = subprocess.Popen(
        _ssh_args(server_host, server_cmd, ssh_opts),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    time.sleep(server_start_delay)

    client_result = _run_local(
        _ssh_args(client_host, client_cmd, ssh_opts),
        check=False,
        timeout=timeout + 20,
    )
    try:
        server_stdout, _ = server_proc.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        server_proc.terminate()
        server_stdout, _ = server_proc.communicate(timeout=5)

    combined = f"-- client --\n{client_result.stdout}\n-- server --\n{server_stdout}"
    if client_result.returncode != 0:
        return "fail", {}, f"client rc={client_result.returncode}: {client_result.stdout.strip()[-500:]}"
    if server_proc.returncode != 0:
        return "fail", {}, f"server rc={server_proc.returncode}: {server_stdout.strip()[-500:]}"

    try:
        return "ok", parse_fn(client_result.stdout), ""
    except ValueError as exc:
        return "fail", {}, f"{exc}: {combined[-1000:]}"


# ---------------------------------------------------------------------------
# CSV helpers
# ---------------------------------------------------------------------------

def _open_csv(path: Path) -> tuple[object, csv.DictWriter]:
    path.parent.mkdir(parents=True, exist_ok=True)
    new_file = not path.exists() or path.stat().st_size == 0
    handle = path.open("a", newline="")
    writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
    if new_file:
        writer.writeheader()
    return handle, writer


def _empty_row(
    profile: dict,
    server: str,
    client: str,
    direction: str,
    test: str,
    size: int,
    qps: int,
    iters: int,
    port: int,
) -> dict[str, str]:
    ma = profile["module_args"]
    return {
        "timestamp_utc": dt.datetime.now(dt.UTC).isoformat(timespec="seconds"),
        "profile": profile["name"],
        "label": profile["label"],
        "nhi_interrupt_throttle_ns": ma.get("nhi_interrupt_throttle_ns", "0"),
        "native_fragment_striping":  ma.get("native_fragment_striping",  "0"),
        "zcopy_min_bytes":           ma.get("zcopy_min_bytes",           "0"),
        "server": server,
        "client": client,
        "direction": direction,
        "test": test,
        "size_bytes": str(size),
        "qps": str(qps),
        "iters": str(iters),
        "port": str(port),
        "status": "",
        "duration_s": "",
        "bw_peak_gbps": "",
        "bw_avg_gbps": "",
        "msg_rate_mpps": "",
        "lat_min_us": "",
        "lat_max_us": "",
        "lat_typical_us": "",
        "lat_avg_us": "",
        "lat_stdev_us": "",
        "lat_p99_us": "",
        "lat_p999_us": "",
        "error": "",
    }


# ---------------------------------------------------------------------------
# Main sweep logic
# ---------------------------------------------------------------------------

def _bw_tx_depth(size: int) -> int:
    if size >= 1048576:
        return 16
    if size >= 262144:
        return 32
    return 128


def run_sweep(args: argparse.Namespace) -> int:  # noqa: C901  (complex but linear)
    profiles_key = args.profiles
    if profiles_key not in PROFILE_SETS:
        print(
            f"tbv-tuning-sweep: unknown --profiles value {profiles_key!r}; "
            f"valid: {list(PROFILE_SETS)}",
            file=sys.stderr,
        )
        return 1
    selected_profiles = PROFILE_SETS[profiles_key]

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_path = output_dir / args.csv_name
    handle, writer = _open_csv(csv_path)

    ssh_opts: list[str] = args.ssh_option or []
    failures = 0
    port_counter = args.base_port

    try:
        for profile in selected_profiles:
            print(
                f"tbv-tuning-sweep: loading profile {profile['name']} "
                f"({profile['label']}) …",
                flush=True,
            )
            for host in (args.server, args.client):
                try:
                    reload_module(host, profile["module_args"], ssh_opts)
                except Exception as exc:
                    print(
                        f"tbv-tuning-sweep: reload failed on {host}: {exc}",
                        file=sys.stderr,
                    )
                    if args.stop_on_fail:
                        return 1
                    continue

            if args.expect_rails > 0:
                for host in (args.server, args.client):
                    try:
                        assert_rails(host, ssh_opts, args.dev, args.expect_rails, args.expect_speed)
                    except RuntimeError as exc:
                        print(f"tbv-tuning-sweep: {exc}", file=sys.stderr)
                        if args.stop_on_fail:
                            return 1

            # BW cases.
            for direction in (["forward"] if not args.both_directions else ["forward", "reverse"]):
                run_server = args.server if direction == "forward" else args.client
                run_client = args.client if direction == "forward" else args.server
                server_label = args.server if direction == "forward" else args.client
                client_label = args.client if direction == "forward" else args.server

                for qps in BW_QPS:
                    for size in BW_SIZES:
                        port_counter += 1
                        row = _empty_row(profile, server_label, client_label, direction, "ib_write_bw", size, qps, BW_ITERS, port_counter)
                        print(
                            f"  bw {direction} size={size} qps={qps} port={port_counter}",
                            flush=True,
                        )
                        t0 = time.monotonic()
                        status, metrics, err = _run_pair(
                            "ib_write_bw", _parse_bw,
                            run_server, run_client, ssh_opts,
                            args.dev, args.gid_index,
                            size, BW_ITERS, qps, _bw_tx_depth(size),
                            port_counter, args.timeout, args.server_start_delay,
                        )
                        row["duration_s"] = f"{time.monotonic() - t0:.3f}"
                        row["status"] = status
                        row.update(metrics)
                        row["error"] = err.replace("\n", " ")
                        writer.writerow(row)
                        handle.flush()
                        if status != "ok":
                            failures += 1
                            if args.stop_on_fail:
                                return 1

                # LAT cases — always qps=1.
                for size in LAT_SIZES:
                    port_counter += 1
                    row = _empty_row(profile, server_label, client_label, direction, "ib_write_lat", size, 1, LAT_ITERS, port_counter)
                    print(
                        f"  lat {direction} size={size} port={port_counter}",
                        flush=True,
                    )
                    t0 = time.monotonic()
                    status, metrics, err = _run_pair(
                        "ib_write_lat", _parse_lat,
                        run_server, run_client, ssh_opts,
                        args.dev, args.gid_index,
                        size, LAT_ITERS, 1, 128,
                        port_counter, args.timeout, args.server_start_delay,
                    )
                    row["duration_s"] = f"{time.monotonic() - t0:.3f}"
                    row["status"] = status
                    row.update(metrics)
                    row["error"] = err.replace("\n", " ")
                    writer.writerow(row)
                    handle.flush()
                    if status != "ok":
                        failures += 1
                        if args.stop_on_fail:
                            return 1

    finally:
        handle.close()

    print(f"tbv-tuning-sweep: wrote {csv_path}")
    if failures:
        print(f"tbv-tuning-sweep: {failures} rows failed", file=sys.stderr)
        return 1
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--server", required=True, help="SSH name/IP of the server host")
    p.add_argument("--client", required=True, help="SSH name/IP of the client host")
    p.add_argument(
        "--profiles",
        default="all",
        choices=list(PROFILE_SETS),
        help="Which profile group to sweep (default: all)",
    )
    p.add_argument("--output-dir", default=".", help="Directory for output CSV")
    p.add_argument(
        "--csv-name",
        default=f"tuning-{dt.datetime.now().strftime('%Y%m%d-%H%M%S')}.csv",
        help="Output CSV filename inside --output-dir",
    )
    p.add_argument("--dev", default="usb4_rdma0", help="RDMA device name")
    p.add_argument("--gid-index", type=int, default=1, help="GID index")
    p.add_argument("--expect-rails", type=int, default=4, help="Expected active rail count; 0 skips rail check")
    p.add_argument("--expect-speed", default="20Gb/s", help="Per-rail link speed, or 'any'")
    p.add_argument("--timeout", type=int, default=60, help="Per-case timeout in seconds")
    p.add_argument("--base-port", type=int, default=19800, help="Base TCP port (incremented per case)")
    p.add_argument("--server-start-delay", type=float, default=0.8, help="Seconds to wait after starting server before launching client")
    p.add_argument("--both-directions", action="store_true", help="Run each case in forward and reverse directions")
    p.add_argument("--stop-on-fail", action="store_true", help="Stop the sweep on the first failed case")
    p.add_argument("--ssh-option", action="append", default=[], metavar="OPT", help="Extra ssh -o option; may repeat")
    return p


def main() -> int:
    args = _build_parser().parse_args()
    return run_sweep(args)


if __name__ == "__main__":
    raise SystemExit(main())
