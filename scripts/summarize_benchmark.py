#!/usr/bin/env python3
"""Summarize benchmark_suite.py output directories."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from pathlib import Path

from summarize_ablation_metrics import (
    RUN_FIELDS,
    SUMMARY_METRICS,
    fmt,
    summarize_run,
)


BENCH_RUN_FIELDS = [
    "scenario",
    "num_nodes",
    "topology_mode",
    "method",
    "seed",
    *[f for f in RUN_FIELDS if f not in {"scenario", "num_nodes", "topology_mode", "group", "seed"}],
]


def seed_key(path: Path) -> int:
    text = path.name.replace("seed", "")
    return int(text) if text.isdigit() else 0


def discover_runs(root: Path):
    for scenario_dir in sorted(root.iterdir()):
        if not scenario_dir.is_dir() or scenario_dir.name == "summaries":
            continue
        for method_dir in sorted(scenario_dir.iterdir()):
            if not method_dir.is_dir():
                continue
            for seed_dir in sorted(method_dir.glob("seed*"), key=seed_key):
                seed_text = seed_dir.name.replace("seed", "")
                if seed_text.isdigit():
                    yield scenario_dir.name, method_dir.name, int(seed_text), seed_dir


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def mean_std(values: list[float]) -> tuple[float, float]:
    vals = [v for v in values if not math.isnan(v)]
    if not vals:
        return math.nan, math.nan
    if len(vals) == 1:
        return vals[0], math.nan
    return statistics.fmean(vals), statistics.stdev(vals)


def make_summary(rows: list[dict[str, object]], keys: list[str]) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    groups = sorted({tuple(str(row.get(k, "")) for k in keys) for row in rows})
    for group_values in groups:
        selected = [
            row for row in rows
            if tuple(str(row.get(k, "")) for k in keys) == group_values
        ]
        out: dict[str, object] = {key: value for key, value in zip(keys, group_values)}
        out["runs"] = len(selected)
        out["complete_runs"] = sum(1 for row in selected if row.get("complete") in (True, "True", "true"))
        if "scenario" in keys:
            first = selected[0]
            out["num_nodes"] = first.get("num_nodes", "")
            out["topology_mode"] = first.get("topology_mode", "")
        for metric in SUMMARY_METRICS:
            mean, std = mean_std([float(row.get(metric, math.nan)) for row in selected])
            out[f"{metric}_mean"] = mean
            out[f"{metric}_std"] = std
        output.append(out)
    attach_relative_gains(output)
    return output


def attach_relative_gains(rows: list[dict[str, object]]) -> None:
    baselines: dict[tuple[str, str], float] = {}
    for row in rows:
        scenario = str(row.get("scenario", ""))
        method = str(row.get("method", ""))
        if method in {"baseline", "plain_tdma"}:
            baselines[(scenario, method)] = float(row.get("goodput_per_slot_mean", math.nan))
    for row in rows:
        scenario = str(row.get("scenario", ""))
        goodput = float(row.get("goodput_per_slot_mean", math.nan))
        for baseline in ("baseline", "plain_tdma"):
            base = baselines.get((scenario, baseline), baselines.get(("", baseline), math.nan))
            value = goodput / base if base and not math.isnan(base) else math.nan
            row[f"goodput_gain_vs_{baseline}"] = value


def markdown_table(rows: list[dict[str, object]]) -> str:
    headers = [
        "场景",
        "方法",
        "完整run",
        "goodput/slot",
        "PDR",
        "MAC效率",
        "queue_slope",
        "Wt_P95",
        "Jain尾100",
        "starvation",
        "gain_vs_baseline",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join([":---", ":---"] + ["---:" for _ in headers[2:]]) + "|",
    ]
    for row in rows:
        lines.append(
            "| "
            + " | ".join(
                [
                    str(row.get("scenario", "")),
                    str(row.get("method", "")),
                    f"{row.get('complete_runs', 0)}/{row.get('runs', 0)}",
                    fmt(float(row.get("goodput_per_slot_mean", math.nan)), 4),
                    fmt(float(row.get("packet_delivery_ratio_mean", math.nan)), 4),
                    fmt(float(row.get("mac_efficiency_mean", math.nan)), 4),
                    fmt(float(row.get("queue_stability_slope_mean", math.nan)), 4),
                    fmt(float(row.get("Wt_p95_mean", math.nan)), 4),
                    fmt(float(row.get("jain_tx_tail100_mean", math.nan)), 4),
                    fmt(float(row.get("starvation_ratio_mean", math.nan)), 4),
                    fmt(float(row.get("goodput_gain_vs_baseline", math.nan)), 4),
                ]
            )
            + " |"
        )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="Benchmark root, e.g. logs/benchmark_suite_YYYYMMDD_HHMMSS")
    parser.add_argument("--no-markdown", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    summary_dir = root / "summaries"
    rows: list[dict[str, object]] = []
    for scenario, method, seed, run_dir in discover_runs(root):
        row = summarize_run(scenario, method, seed, run_dir)
        row["method"] = method
        row.pop("group", None)
        rows.append(row)
    if not rows:
        raise SystemExit(f"No benchmark runs found under {root}")

    write_csv(summary_dir / "metrics_summary.csv", rows, BENCH_RUN_FIELDS)

    method_rows = make_summary(rows, ["method"])
    method_fields = ["method", "runs", "complete_runs"]
    for metric in SUMMARY_METRICS:
        method_fields.extend([f"{metric}_mean", f"{metric}_std"])
    method_fields.extend(["goodput_gain_vs_baseline", "goodput_gain_vs_plain_tdma"])
    write_csv(summary_dir / "metrics_method_summary.csv", method_rows, method_fields)

    scenario_rows = make_summary(rows, ["scenario", "method"])
    scenario_fields = ["scenario", "num_nodes", "topology_mode", "method", "runs", "complete_runs"]
    for metric in SUMMARY_METRICS:
        scenario_fields.extend([f"{metric}_mean", f"{metric}_std"])
    scenario_fields.extend(["goodput_gain_vs_baseline", "goodput_gain_vs_plain_tdma"])
    write_csv(summary_dir / "metrics_scenario_method_summary.csv", scenario_rows, scenario_fields)

    md = markdown_table(scenario_rows)
    (summary_dir / "comparison_table.md").write_text(md, encoding="utf-8")
    if not args.no_markdown:
        print(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
