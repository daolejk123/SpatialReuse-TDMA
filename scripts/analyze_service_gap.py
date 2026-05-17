#!/usr/bin/env python3
"""Reconstruct service-gap distributions from recorded benchmark metrics."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import DefaultDict, Iterable


def _percentile(values: list[int], pct: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return float(values[0])
    rank = pct * (len(values) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(values) - 1)
    frac = rank - lo
    return values[lo] * (1.0 - frac) + values[hi] * frac


def _bounded_cost(value: float, threshold: float, ramp: float) -> float:
    if ramp <= 0:
        return 0.0
    return min(1.0, max(0.0, (value - threshold) / ramp))


def _iter_metric_dirs(roots: Iterable[Path]) -> Iterable[Path]:
    for root in roots:
        yield from root.glob("**/metrics")


def _load_qt_by_frame(metrics_dir: Path) -> dict[tuple[int, int], float]:
    qt_by_frame: dict[tuple[int, int], float] = {}
    with (metrics_dir / "frame_metrics.csv").open(newline="") as handle:
        for row in csv.DictReader(handle):
            qt_by_frame[(int(row["frame"]), int(row["nodeId"]))] = float(row["Qt"])
    return qt_by_frame


def _reconstruct_gaps(metrics_dir: Path) -> list[int]:
    qt_by_frame = _load_qt_by_frame(metrics_dir)
    prev_success: dict[int, int] = {}
    gap_by_node: DefaultDict[int, int] = defaultdict(int)
    gaps: list[int] = []
    with (metrics_dir / "slot_stats.csv").open(newline="") as handle:
        for row in csv.DictReader(handle):
            frame = int(row["frame"])
            node_id = int(row["nodeId"])
            total_success = int(row["totalSuccessfulTx"])
            previous_total = prev_success.get(node_id, total_success)
            served = total_success > previous_total
            qt = qt_by_frame.get((frame, node_id), 0.0)
            if served or qt <= 0.0:
                gap_by_node[node_id] = 0
            else:
                gap_by_node[node_id] += 1
            prev_success[node_id] = total_success
            if qt > 0.0:
                gaps.append(gap_by_node[node_id])
    return gaps


def _labels(metrics_dir: Path) -> tuple[str, str, str, str]:
    seed_dir = metrics_dir.parent
    method_dir = seed_dir.parent
    scenario_dir = method_dir.parent
    root = scenario_dir.parent
    return root.name, scenario_dir.name, method_dir.name, seed_dir.name


def _summarize(
    roots: list[Path],
    method_filter: set[str],
    service_threshold: float,
    service_ramp: float,
    starvation_threshold: float,
    starvation_ramp: float,
) -> list[dict[str, object]]:
    grouped: DefaultDict[tuple[str, str, str], list[int]] = defaultdict(list)
    seeds: DefaultDict[tuple[str, str, str], set[str]] = defaultdict(set)
    for metrics_dir in _iter_metric_dirs(roots):
        slot_stats = metrics_dir / "slot_stats.csv"
        frame_metrics = metrics_dir / "frame_metrics.csv"
        if not slot_stats.exists() or not frame_metrics.exists():
            continue
        root, scenario, method, seed = _labels(metrics_dir)
        if method_filter and method not in method_filter:
            continue
        key = (root, scenario, method)
        grouped[key].extend(_reconstruct_gaps(metrics_dir))
        seeds[key].add(seed)

    rows: list[dict[str, object]] = []
    for (root, scenario, method), values in sorted(grouped.items()):
        ordered = sorted(values)
        service_mean = statistics.fmean(
            _bounded_cost(v, service_threshold, service_ramp) for v in ordered
        )
        starvation_mean = statistics.fmean(
            _bounded_cost(v, starvation_threshold, starvation_ramp) for v in ordered
        )
        rows.append(
            {
                "suite": root,
                "scenario": scenario,
                "method": method,
                "seeds": len(seeds[(root, scenario, method)]),
                "samples": len(ordered),
                "mean": statistics.fmean(ordered),
                "p50": _percentile(ordered, 0.50),
                "p75": _percentile(ordered, 0.75),
                "p90": _percentile(ordered, 0.90),
                "p95": _percentile(ordered, 0.95),
                "p99": _percentile(ordered, 0.99),
                "share_gt_5": sum(v > 5 for v in ordered) / len(ordered),
                "service_cost_mean": service_mean,
                "starvation_cost_mean": starvation_mean,
            }
        )
    return rows


def _print_table(rows: list[dict[str, object]]) -> None:
    header = (
        "suite",
        "scenario",
        "method",
        "seeds",
        "samples",
        "mean",
        "p50",
        "p75",
        "p90",
        "p95",
        "p99",
        "share_gt_5",
        "service_cost_mean",
        "starvation_cost_mean",
    )
    writer = csv.DictWriter(
        sys.stdout,
        fieldnames=list(header),
        lineterminator="\n",
    )
    writer.writeheader()
    for row in rows:
        writer.writerow(
            {
                key: f"{value:.6f}" if isinstance(value, float) else value
                for key, value in row.items()
            }
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Reconstruct queued-node service-gap distributions from metrics CSVs."
    )
    parser.add_argument("roots", nargs="+", type=Path, help="benchmark suite root(s)")
    parser.add_argument("--methods", nargs="*", default=[], help="optional method filter")
    parser.add_argument("--service_threshold", type=float, default=80.0)
    parser.add_argument("--service_ramp", type=float, default=1200.0)
    parser.add_argument("--starvation_threshold", type=float, default=300.0)
    parser.add_argument("--starvation_ramp", type=float, default=1800.0)
    args = parser.parse_args()
    rows = _summarize(
        roots=args.roots,
        method_filter=set(args.methods),
        service_threshold=args.service_threshold,
        service_ramp=args.service_ramp,
        starvation_threshold=args.starvation_threshold,
        starvation_ramp=args.starvation_ramp,
    )
    _print_table(rows)


if __name__ == "__main__":
    main()
