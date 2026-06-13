#!/usr/bin/env python3
"""Summarize tbv-perftest CSV output.

The runner writes large JSONL files with full stdout/stderr and telemetry
snapshots. This script intentionally consumes only the compact CSV sidecar so
benchmark reports can be checked into Git without carrying those raw artifacts.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def num(row: dict[str, str], key: str) -> float | None:
    value = row.get(key, "")
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def load_rows(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                row["_source"] = str(path)
                rows.append(row)
    return rows


def compare_rows(
    rows: list[dict[str, str]],
    baseline_rows: list[dict[str, str]],
    bw_drop_pct: float,
    lat_rise_pct: float,
    require_baseline_match: bool,
) -> dict[str, object]:
    baseline = {
        (row.get("case", ""), row.get("direction", "")): row
        for row in baseline_rows
        if row.get("status") == "ok"
    }
    regressions: list[dict[str, object]] = []
    missing: list[tuple[str, str]] = []
    compared = 0

    for row in rows:
        if row.get("status") != "ok":
            continue
        key = (row.get("case", ""), row.get("direction", ""))
        base = baseline.get(key)
        if base is None:
            missing.append(key)
            continue

        kind = row.get("kind", "")
        if kind == "bw":
            metric = "bw_avg_gbps"
            current = num(row, metric)
            reference = num(base, metric)
            if current is None or reference is None or reference <= 0:
                continue
            compared += 1
            floor = reference * (1.0 - (bw_drop_pct / 100.0))
            if current < floor:
                regressions.append(
                    {
                        "case": key[0],
                        "direction": key[1],
                        "metric": metric,
                        "baseline": reference,
                        "current": current,
                        "limit": floor,
                        "kind": "floor",
                    }
                )
        elif kind == "lat":
            metric = "lat_typical_us"
            current = num(row, metric)
            reference = num(base, metric)
            if current is None or reference is None or reference <= 0:
                continue
            compared += 1
            ceiling = reference * (1.0 + (lat_rise_pct / 100.0))
            if current > ceiling:
                regressions.append(
                    {
                        "case": key[0],
                        "direction": key[1],
                        "metric": metric,
                        "baseline": reference,
                        "current": current,
                        "limit": ceiling,
                        "kind": "ceiling",
                    }
                )

    ok = len(regressions) == 0 and compared > 0
    if require_baseline_match and missing:
        ok = False

    return {
        "ok": ok,
        "compared_rows": compared,
        "missing_baseline_rows": len(missing),
        "missing_cases": missing,
        "regressions": regressions,
    }


def print_row_counts(rows: list[dict[str, str]]) -> None:
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["plan"]].append(row)

    print("## Row counts")
    for plan in sorted(grouped):
        plan_rows = grouped[plan]
        failures = sum(1 for row in plan_rows if row["status"] != "ok")
        print(f"- {plan}: {len(plan_rows)} rows, {failures} non-ok")
    print()


def print_bandwidth(rows: list[dict[str, str]]) -> None:
    best: dict[tuple[str, str, str], dict[str, str]] = {}
    for row in rows:
        if row["status"] != "ok" or row["kind"] != "bw":
            continue
        key = (row["plan"], row["verb"], row["direction"])
        value = num(row, "bw_avg_gbps")
        if value is None:
            continue
        old = best.get(key)
        if old is None or value > (num(old, "bw_avg_gbps") or 0):
            best[key] = row

    print("## Peak bandwidth")
    for key in sorted(best):
        row = best[key]
        print(
            "- {plan} {direction} ib_{verb}_bw: {bw:.2f} Gb/s "
            "({case}, size={size}, qps={qps})".format(
                plan=row["plan"],
                direction=row["direction"],
                verb=row["verb"],
                bw=num(row, "bw_avg_gbps") or 0,
                case=row["case"],
                size=row["size_bytes"],
                qps=row["qps"],
            )
        )
    print()


def print_latency(rows: list[dict[str, str]]) -> None:
    latency = [
        row
        for row in rows
        if row["status"] == "ok" and row["kind"] == "lat"
    ]

    print("## Latency")
    for plan in sorted({row["plan"] for row in latency}):
        print(f"### {plan}")
        print("| size | direction | verb | typical us | avg us | p99 us | p99.9 us | max us |")
        print("|---:|---|---|---:|---:|---:|---:|---:|")
        for row in sorted(
            (row for row in latency if row["plan"] == plan),
            key=lambda row: (
                int(row["size_bytes"]),
                row["direction"],
                row["verb"],
            ),
        ):
            print(
                "| {size} | {direction} | {verb} | {typ:.2f} | {avg:.2f} | "
                "{p99:.2f} | {p999:.2f} | {maxv:.2f} |".format(
                    size=row["size_bytes"],
                    direction=row["direction"],
                    verb=row["verb"],
                    typ=num(row, "lat_typical_us") or 0,
                    avg=num(row, "lat_avg_us") or 0,
                    p99=num(row, "lat_p99_us") or 0,
                    p999=num(row, "lat_p999_us") or 0,
                    maxv=num(row, "lat_max_us") or 0,
                )
            )
        print()


def print_regressions(report: dict[str, object], baseline: Path) -> None:
    print(f"## Regression check vs {baseline}")
    print(f"- compared rows: {report['compared_rows']}")
    print(f"- missing baseline rows: {report['missing_baseline_rows']}")
    print(f"- status: {'ok' if report['ok'] else 'fail'}")
    print()

    regressions = report["regressions"]
    if regressions:
        print("### Regressions")
        for row in regressions:
            limit_label = "min" if row["kind"] == "floor" else "max"
            print(
                "- {case} {direction} {metric}: baseline={baseline:.2f}, current={current:.2f}, {limit_label}={limit:.2f}".format(
                    case=row["case"],
                    direction=row["direction"],
                    metric=row["metric"],
                    baseline=row["baseline"],
                    current=row["current"],
                    limit=row["limit"],
                    limit_label=limit_label,
                )
            )
        print()

    missing = report["missing_cases"]
    if missing:
        print("### Missing baseline rows")
        for case, direction in missing:
            print(f"- {case} {direction}")
        print()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--bw-drop-pct", type=float, default=7.5)
    parser.add_argument("--lat-rise-pct", type=float, default=12.5)
    parser.add_argument("--require-baseline-match", action="store_true")
    parser.add_argument("csv", nargs="+", type=Path)
    args = parser.parse_args()

    rows = load_rows(args.csv)
    print("# Perftest Summary")
    print()
    print_row_counts(rows)
    print_bandwidth(rows)
    print_latency(rows)
    if args.baseline is None:
        return 0

    report = compare_rows(
        rows,
        load_rows([args.baseline]),
        args.bw_drop_pct,
        args.lat_rise_pct,
        args.require_baseline_match,
    )
    print_regressions(report, args.baseline)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
