#!/usr/bin/env python3
"""Run the two-node smoke regression suite (vLLM transport + perftest verbs)."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import shlex
import shutil
import subprocess
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hosts", default=os.environ.get("TBV_REGRESSION_HOSTS", ""), help="H1,H2")
    parser.add_argument("--iface", default=os.environ.get("TBV_REGRESSION_IFACE", "eno1"))
    parser.add_argument("--transport", default=os.environ.get("TBV_REGRESSION_TRANSPORT", "native"))
    parser.add_argument("--wrapper", default=os.environ.get("TBV_REGRESSION_WRAPPER", ""))
    parser.add_argument("--out-root", default=os.environ.get("TBV_REGRESSION_OUT_ROOT", "thunderbolt-ibverbs/results/regression"))
    parser.add_argument("--tag", default=os.environ.get("TBV_REGRESSION_TAG", "smoke"))
    parser.add_argument("--run-id", default="")
    parser.add_argument("--skip-vllm", action="store_true")
    parser.add_argument("--skip-perftest", action="store_true")
    parser.add_argument("--vllm-smoke-bin", default=os.environ.get("TBV_VLLM_SMOKE_BIN", "tbv_vllm_smoke.sh"))
    parser.add_argument("--perftest-bin", default=os.environ.get("TBV_PERFTEST_BIN", ""))
    parser.add_argument("--baseline-csv", default="")
    parser.add_argument("--require-baseline", action="store_true")
    parser.add_argument("--bw-drop-pct", type=float, default=float(os.environ.get("TBV_REGRESSION_BW_DROP_PCT", "7.5")))
    parser.add_argument("--lat-rise-pct", type=float, default=float(os.environ.get("TBV_REGRESSION_LAT_RISE_PCT", "12.5")))
    parser.add_argument("--perftest-only", action="append", default=[
        "bw.*size4096.qps1",
        "bw.uc.*size4096.qps1",
        "lat.*size64",
        "lat.uc.*size64",
    ])
    parser.add_argument("--perftest-extra-arg", action="append", default=[])
    return parser.parse_args()


def run_logged(cmd: list[str], log_path: Path) -> tuple[int, str]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w") as handle:
        handle.write("$ " + " ".join(shlex.quote(token) for token in cmd) + "\n\n")
        handle.flush()
        proc = subprocess.run(cmd, stdout=handle, stderr=subprocess.STDOUT, text=True)
    return proc.returncode, " ".join(shlex.quote(token) for token in cmd)


def resolve_perftest_bin(raw: str) -> list[str]:
    if raw:
        return shlex.split(raw)
    if shutil.which("tbv-perftest"):
        return ["tbv-perftest"]
    if shutil.which("nix") and Path("flake.nix").exists():
        return ["nix", "run", ".#tbv-perftest", "--"]
    raise RuntimeError("could not find tbv-perftest; set --perftest-bin or TBV_PERFTEST_BIN")


def resolve_baseline(args: argparse.Namespace, out_root: Path, run_dir: Path) -> Path | None:
    if args.baseline_csv:
        return Path(args.baseline_csv)
    latest_success = out_root / "latest-success"
    if not latest_success.exists():
        return None
    try:
        previous = latest_success.resolve()
    except OSError:
        return None
    if previous == run_dir:
        return None
    candidate = previous / "perftest-smoke.csv"
    return candidate if candidate.exists() else None


def to_float(row: dict[str, str], key: str) -> float | None:
    value = row.get(key, "").strip()
    if not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def compare_with_baseline(current_csv: Path, baseline_csv: Path, bw_drop_pct: float, lat_rise_pct: float) -> dict[str, Any]:
    baseline_rows = {
        (row.get("case", ""), row.get("direction", "")): row
        for row in load_rows(baseline_csv)
        if row.get("status") == "ok"
    }
    failures: list[dict[str, Any]] = []
    compared = 0
    missing = 0

    for row in load_rows(current_csv):
        if row.get("status") != "ok":
            continue
        kind = row.get("kind", "")
        key = (row.get("case", ""), row.get("direction", ""))
        base = baseline_rows.get(key)
        if base is None:
            missing += 1
            continue

        if kind == "bw":
            metric = "bw_avg_gbps"
            current = to_float(row, metric)
            baseline = to_float(base, metric)
            if current is None or baseline is None or baseline <= 0:
                continue
            compared += 1
            floor = baseline * (1.0 - (bw_drop_pct / 100.0))
            if current < floor:
                failures.append({
                    "case": key[0],
                    "direction": key[1],
                    "metric": metric,
                    "baseline": baseline,
                    "current": current,
                    "allowed_floor": floor,
                })
        elif kind == "lat":
            metric = "lat_typical_us"
            current = to_float(row, metric)
            baseline = to_float(base, metric)
            if current is None or baseline is None or baseline <= 0:
                continue
            compared += 1
            ceiling = baseline * (1.0 + (lat_rise_pct / 100.0))
            if current > ceiling:
                failures.append({
                    "case": key[0],
                    "direction": key[1],
                    "metric": metric,
                    "baseline": baseline,
                    "current": current,
                    "allowed_ceiling": ceiling,
                })

    return {
        "baseline_csv": str(baseline_csv),
        "compared_rows": compared,
        "missing_baseline_rows": missing,
        "regressions": failures,
        "ok": len(failures) == 0,
    }


def update_symlink(link: Path, target: Path) -> None:
    link.parent.mkdir(parents=True, exist_ok=True)
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(target.resolve())


def now_utc_iso() -> str:
    return dt.datetime.now(dt.UTC).isoformat(timespec="seconds").replace("+00:00", "Z")


def main() -> int:
    args = parse_args()
    if not args.hosts:
        raise SystemExit("--hosts is required (or set TBV_REGRESSION_HOSTS)")
    if args.skip_vllm and args.skip_perftest:
        raise SystemExit("cannot skip both --skip-vllm and --skip-perftest")

    run_id = args.run_id or dt.datetime.now(dt.UTC).strftime("%Y%m%d-%H%M%S")
    out_root = Path(args.out_root)
    run_dir = out_root / run_id
    run_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        "run_id": run_id,
        "started_utc": now_utc_iso(),
        "hosts": args.hosts,
        "iface": args.iface,
        "transport": args.transport,
        "tag": args.tag,
        "run_dir": str(run_dir),
        "steps": {},
        "status": "ok",
        "error": "",
    }

    perftest_csv = run_dir / "perftest-smoke.csv"
    perftest_jsonl = run_dir / "perftest-smoke.jsonl"

    if not args.skip_vllm:
        vllm_cmd = [args.vllm_smoke_bin, "--hosts", args.hosts, "--iface", args.iface, "--transport", args.transport, "--log-root", str(run_dir / "vllm")]
        if args.wrapper:
            vllm_cmd.extend(["--wrapper", args.wrapper])
        rc, cmdline = run_logged(vllm_cmd, run_dir / "vllm.log")
        manifest["steps"]["vllm"] = {"ok": rc == 0, "code": rc, "command": cmdline, "log": str(run_dir / "vllm.log")}
        if rc != 0:
            manifest["status"] = "fail"
            manifest["error"] = "vLLM transport smoke failed"

    if manifest["status"] == "ok" and not args.skip_perftest:
        perftest_cmd = resolve_perftest_bin(args.perftest_bin) + [
            "--hosts", args.hosts,
            "--directions", "both",
            "--tag", args.tag,
            "--stop-on-fail",
            "--csv", str(perftest_csv),
            "--jsonl", str(perftest_jsonl),
        ]
        for pattern in args.perftest_only:
            perftest_cmd.extend(["--only", pattern])
        for extra in args.perftest_extra_arg:
            perftest_cmd.extend(shlex.split(extra))
        rc, cmdline = run_logged(perftest_cmd, run_dir / "perftest.log")
        manifest["steps"]["perftest"] = {
            "ok": rc == 0,
            "code": rc,
            "command": cmdline,
            "log": str(run_dir / "perftest.log"),
            "csv": str(perftest_csv),
            "jsonl": str(perftest_jsonl),
        }
        if rc != 0:
            manifest["status"] = "fail"
            manifest["error"] = "perftest smoke failed"

    regression_report: dict[str, Any] = {
        "ok": True,
        "skipped": True,
        "reason": "no perftest run",
    }
    if manifest["status"] == "ok" and not args.skip_perftest:
        baseline = resolve_baseline(args, out_root, run_dir)
        if baseline is None:
            if args.require_baseline:
                manifest["status"] = "fail"
                manifest["error"] = "no baseline CSV available for regression check"
            regression_report = {"ok": not args.require_baseline, "skipped": True, "reason": "no baseline CSV available"}
        else:
            report = compare_with_baseline(perftest_csv, baseline, args.bw_drop_pct, args.lat_rise_pct)
            regression_report = {
                "ok": report["ok"],
                "skipped": False,
                "bw_drop_pct": args.bw_drop_pct,
                "lat_rise_pct": args.lat_rise_pct,
                **report,
            }
            if not report["ok"]:
                manifest["status"] = "fail"
                manifest["error"] = "perftest metrics regressed vs baseline"
    manifest["steps"]["regression"] = regression_report

    manifest["finished_utc"] = now_utc_iso()
    (run_dir / "regression.json").write_text(json.dumps(regression_report, indent=2, sort_keys=True) + "\n")
    (run_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    update_symlink(out_root / "latest", run_dir)
    if manifest["status"] == "ok":
        update_symlink(out_root / "latest-success", run_dir)

    print(f"tbv-regression: wrote {run_dir / 'manifest.json'}")
    print(f"tbv-regression: wrote {run_dir / 'regression.json'}")
    return 0 if manifest["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
