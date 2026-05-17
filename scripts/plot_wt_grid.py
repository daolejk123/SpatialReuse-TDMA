#!/usr/bin/env python3
"""Plot service-debt Wt-threshold grid comparisons across benchmark suites."""

from __future__ import annotations

import argparse
import csv
import math
import re
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


PLOT_SPECS = [
    ("pre_sent_candidate_ratio", "Pre sent / candidate", "ratio"),
    ("perturb_sent_candidate_ratio", "Perturb sent / candidate", "ratio"),
    ("packet_delivery_ratio", "Packet delivery ratio", "ratio"),
    ("starvation_ratio", "Starvation ratio", "ratio"),
]


def read_suite_config(suite_dir: Path) -> dict[str, str]:
    cfg: dict[str, str] = {}
    cfg_path = suite_dir / "suite_config.tsv"
    if not cfg_path.exists():
        raise FileNotFoundError(f"missing suite config: {cfg_path}")
    with cfg_path.open(encoding="utf-8") as f:
        next(f, None)
        for line in f:
            key, value = line.rstrip("\n").split("\t", 1)
            cfg[key] = value
    return cfg


def seed_key(path: Path) -> int:
    text = path.name.removeprefix("seed")
    return int(text) if text.isdigit() else 0


def iter_runs(
    suite_dir: Path,
    scenarios: set[str] | None = None,
    methods: set[str] | None = None,
):
    for frame_path in sorted(suite_dir.glob("*/*/seed*/metrics/frame_metrics.csv")):
        scenario, method, seed_text, _, _ = frame_path.relative_to(suite_dir).parts
        if scenarios and scenario not in scenarios:
            continue
        if methods and method not in methods:
            continue
        seed = seed_key(Path(seed_text))
        yield scenario, method, seed, frame_path


def phase_sent_candidate_ratio(frame_path: Path) -> dict[str, float]:
    totals: dict[str, list[float]] = defaultdict(lambda: [0.0, 0.0])
    with frame_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            phase = row.get("event_phase", "")
            if phase not in {"pre", "perturb"}:
                continue
            totals[phase][0] += float(row.get("req_sent", 0.0) or 0.0)
            totals[phase][1] += float(row.get("req_candidates", 0.0) or 0.0)
    out: dict[str, float] = {}
    for phase in ("pre", "perturb"):
        sent, candidates = totals[phase]
        out[f"{phase}_sent_candidate_ratio"] = sent / candidates if candidates else math.nan
    return out


def load_run_metrics(suite_dir: Path) -> dict[tuple[str, str, int], dict[str, float]]:
    summary_path = suite_dir / "summaries" / "metrics_summary.csv"
    if not summary_path.exists():
        raise FileNotFoundError(f"missing metrics summary: {summary_path}")
    rows: dict[tuple[str, str, int], dict[str, float]] = {}
    with summary_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = (row["scenario"], row["method"], int(row["seed"]))
            rows[key] = {
                "packet_delivery_ratio": float(row["packet_delivery_ratio"]),
                "starvation_ratio": float(row["starvation_ratio"]),
            }
    return rows


def run_wt_threshold(run_dir: Path, suite_default: float) -> float:
    python_log = run_dir / "python.log"
    if not python_log.exists():
        return suite_default
    match = re.search(
        r"wt_threshold=([0-9]+(?:\.[0-9]+)?)",
        python_log.read_text(encoding="utf-8", errors="ignore"),
    )
    return float(match.group(1)) if match else suite_default


def collect_rows(
    suite_dirs: list[Path],
    scenarios: set[str] | None = None,
    methods: set[str] | None = None,
) -> pd.DataFrame:
    rows: list[dict[str, object]] = []
    for suite_dir in suite_dirs:
        cfg = read_suite_config(suite_dir)
        wt_raw = cfg.get("service_debt_wt_threshold", "")
        suite_wt = float(wt_raw) if wt_raw != "" else 0.0
        run_metrics = load_run_metrics(suite_dir)
        for scenario, method, seed, frame_path in iter_runs(suite_dir, scenarios, methods):
            key = (scenario, method, seed)
            metrics = run_metrics.get(key)
            if metrics is None:
                continue
            wt = run_wt_threshold(frame_path.parents[1], suite_wt)
            rows.append(
                {
                    "suite": suite_dir.name,
                    "wt_threshold": wt,
                    "scenario": scenario,
                    "method": method,
                    "seed": seed,
                    **phase_sent_candidate_ratio(frame_path),
                    **metrics,
                }
            )
    if not rows:
        raise ValueError("no usable runs found")
    return pd.DataFrame(rows)


def aggregate_rows(rows: pd.DataFrame) -> pd.DataFrame:
    metric_cols = [name for name, _, _ in PLOT_SPECS]
    summary = (
        rows.groupby(["wt_threshold", "scenario", "method"], as_index=False)[metric_cols]
        .agg(["mean", "std"])
        .reset_index()
    )
    return flatten_summary(summary)


def plot_grid(summary: pd.DataFrame, out_path: Path) -> None:
    series_keys = sorted({(row["scenario"], row["method"]) for _, row in summary.iterrows()})
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), sharex=True)
    axes = axes.flatten()

    for ax, (metric, title, _) in zip(axes, PLOT_SPECS):
        for scenario, method in series_keys:
            subset = summary[
                (summary["scenario"] == scenario) & (summary["method"] == method)
            ].sort_values("wt_threshold")
            x = subset["wt_threshold"]
            y = subset[f"{metric}_mean"]
            yerr = subset[f"{metric}_std"].fillna(0.0)
            label = scenario if len({m for _, m in series_keys}) == 1 else f"{scenario} / {method}"
            ax.errorbar(x, y, yerr=yerr, marker="o", linewidth=1.8, capsize=3, label=label)
        ax.set_title(title)
        ax.set_xlabel("service_debt_wt_threshold")
        ax.grid(alpha=0.3)
        ax.set_ylim(bottom=0)
    axes[0].legend(loc="best", fontsize=8)
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def flatten_summary(summary: pd.DataFrame) -> pd.DataFrame:
    flat = summary.copy()
    flat.columns = [
        "_".join(str(part) for part in col if part)
        if isinstance(col, tuple)
        else str(col)
        for col in flat.columns
    ]
    if "index" in flat.columns:
        flat = flat.drop(columns=["index"])
    return flat


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite_dirs", nargs="+", type=Path, required=True)
    parser.add_argument("--output_dir", type=Path, required=True)
    parser.add_argument("--scenarios", nargs="*", default=None)
    parser.add_argument("--methods", nargs="*", default=None)
    args = parser.parse_args()

    suite_dirs = [path.resolve() for path in args.suite_dirs]
    rows = collect_rows(
        suite_dirs,
        set(args.scenarios) if args.scenarios else None,
        set(args.methods) if args.methods else None,
    )
    summary = aggregate_rows(rows)

    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    rows.to_csv(out_dir / "wt_grid_runs.csv", index=False)
    summary.to_csv(out_dir / "wt_grid_summary.csv", index=False)
    plot_grid(summary, out_dir / "wt_grid_comparison.png")
    print(f"[SUMMARY] {out_dir / 'wt_grid_summary.csv'}")
    print(f"[PLOT] {out_dir / 'wt_grid_comparison.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
