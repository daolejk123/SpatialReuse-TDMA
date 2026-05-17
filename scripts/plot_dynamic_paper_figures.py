#!/usr/bin/env python3
"""Build paper-ready dynamic-topology figures from one or more benchmark suites."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


DEFAULT_SCENARIO_ORDER = [
    "N12_grid_manet_pedestrian_edge_toggle",
    "N12_grid_manet_pedestrian_bridge_break",
    "N12_grid_manet_pedestrian_node_rejoin",
    "N12_grid_manet_pedestrian_node_dropout",
]
DEFAULT_METHOD_ORDER = [
    "B_masked_debt_adaptive",
    "ppo_baseline",
    "greedy_stdma_2hop",
    "heuristic_only",
]
SCENARIO_LABELS = {
    "N12_grid_manet_pedestrian_edge_toggle": "Edge Toggle",
    "N12_grid_manet_pedestrian_bridge_break": "Bridge Break",
    "N12_grid_manet_pedestrian_node_rejoin": "Node Rejoin",
    "N12_grid_manet_pedestrian_node_dropout": "Node Dropout",
}
METHOD_LABELS = {
    "B_masked_debt_adaptive": "B'",
    "ppo_baseline": "PPO",
    "greedy_stdma_2hop": "Greedy-STDMA-2hop",
    "heuristic_only": "Heuristic",
}


def seed_key(seed_text: str) -> int:
    text = seed_text.removeprefix("seed")
    return int(text) if text.isdigit() else 0


def discover_runs(
    suite_dirs: list[Path],
    scenarios: set[str] | None,
    methods: set[str] | None,
) -> list[dict[str, object]]:
    runs: list[dict[str, object]] = []
    seen: set[tuple[str, str, int, Path]] = set()
    for suite_dir in suite_dirs:
        for fairness_path in sorted(suite_dir.glob("*/*/seed*/metrics/fairness.csv")):
            scenario, method, seed_text, _, _ = fairness_path.relative_to(suite_dir).parts
            if scenarios and scenario not in scenarios:
                continue
            if methods and method not in methods:
                continue
            seed = seed_key(seed_text)
            key = (scenario, method, seed, fairness_path.resolve())
            if key in seen:
                continue
            seen.add(key)
            runs.append(
                {
                    "suite_dir": suite_dir,
                    "scenario": scenario,
                    "method": method,
                    "seed": seed,
                    "path": fairness_path,
                }
            )
    return runs


def load_run_dataframe(fairness_path: Path) -> pd.DataFrame:
    df = pd.read_csv(fairness_path)
    df["pdr"] = np.where(
        df["sum_arrivals"] > 0,
        df["sum_delta_packets"] / df["sum_arrivals"],
        np.nan,
    )
    df["throughput"] = df["sum_delta_packets"].astype(float)
    return df[["frame", "pdr", "throughput"]]


def aggregate_per_frame(seed_dfs: list[pd.DataFrame]) -> pd.DataFrame:
    merged = pd.concat(seed_dfs, ignore_index=True)
    return merged.groupby("frame", as_index=False)[["pdr", "throughput"]].mean()


def collect_data(runs: list[dict[str, object]]) -> dict[tuple[str, str], list[pd.DataFrame]]:
    data: dict[tuple[str, str], list[pd.DataFrame]] = {}
    for run in runs:
        key = (str(run["scenario"]), str(run["method"]))
        data.setdefault(key, []).append(load_run_dataframe(Path(run["path"])))
    return data


def ordered(items: set[str], preferred: list[str]) -> list[str]:
    front = [item for item in preferred if item in items]
    tail = sorted(item for item in items if item not in preferred)
    return front + tail


def has_recovery(scenario: str) -> bool:
    return "node_dropout" not in scenario


def build_phase_summary(
    data: dict[tuple[str, str], list[pd.DataFrame]],
    scenarios: list[str],
    methods: list[str],
    perturb_frame: int,
    recovery_frame: int,
) -> pd.DataFrame:
    rows: list[dict[str, object]] = []
    for scenario in scenarios:
        for method in methods:
            seed_dfs = data.get((scenario, method))
            if not seed_dfs:
                continue
            agg = aggregate_per_frame(seed_dfs)
            pre = agg["frame"] <= perturb_frame
            perturb = (agg["frame"] > perturb_frame) & (agg["frame"] <= recovery_frame)
            recovery = agg["frame"] > recovery_frame
            pdr_pre = float(agg.loc[pre, "pdr"].mean())
            pdr_perturb = float(agg.loc[perturb, "pdr"].mean())
            pdr_recovery = (
                float(agg.loc[recovery, "pdr"].mean())
                if has_recovery(scenario) and recovery.any()
                else math.nan
            )
            throughput_pre = float(agg.loc[pre, "throughput"].mean())
            throughput_perturb = float(agg.loc[perturb, "throughput"].mean())
            throughput_recovery = (
                float(agg.loc[recovery, "throughput"].mean())
                if has_recovery(scenario) and recovery.any()
                else math.nan
            )
            rows.append(
                {
                    "scenario": scenario,
                    "scenario_label": SCENARIO_LABELS.get(scenario, scenario),
                    "method": method,
                    "method_label": METHOD_LABELS.get(method, method),
                    "n_seeds": len(seed_dfs),
                    "pdr_pre": pdr_pre,
                    "pdr_perturb": pdr_perturb,
                    "pdr_recovery": pdr_recovery,
                    "pdr_perturb_delta_pct": (
                        (pdr_perturb - pdr_pre) / pdr_pre * 100 if pdr_pre else math.nan
                    ),
                    "throughput_pre": throughput_pre,
                    "throughput_perturb": throughput_perturb,
                    "throughput_recovery": throughput_recovery,
                    "throughput_perturb_delta_pct": (
                        (throughput_perturb - throughput_pre) / throughput_pre * 100
                        if throughput_pre
                        else math.nan
                    ),
                }
            )
    return pd.DataFrame(rows)


def plot_pdr_grid(
    data: dict[tuple[str, str], list[pd.DataFrame]],
    scenarios: list[str],
    methods: list[str],
    out_stem: Path,
    perturb_frame: int,
    recovery_frame: int,
    window: int,
) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(12.6, 7.6), sharex=True, sharey=True)
    colors = {
        "B_masked_debt_adaptive": "#1f77b4",
        "ppo_baseline": "#d62728",
        "greedy_stdma_2hop": "#ff7f0e",
        "heuristic_only": "#2ca02c",
    }
    for idx, (ax, scenario) in enumerate(zip(axes.flatten(), scenarios)):
        for method in methods:
            seed_dfs = data.get((scenario, method))
            if not seed_dfs:
                continue
            agg = aggregate_per_frame(seed_dfs)
            series = agg["pdr"].rolling(window, min_periods=1).mean()
            ax.plot(
                agg["frame"],
                series,
                label=METHOD_LABELS.get(method, method),
                color=colors.get(method),
                linewidth=1.7,
            )
        ax.axvline(perturb_frame, color="#d62728", linestyle="--", alpha=0.55)
        if has_recovery(scenario):
            ax.axvline(recovery_frame, color="#2ca02c", linestyle="--", alpha=0.55)
        ax.set_title(SCENARIO_LABELS.get(scenario, scenario))
        ax.grid(alpha=0.25)
        ax.set_xlabel("Frame" if idx >= 2 else "")
        ax.set_ylabel("Packet delivery ratio" if idx % 2 == 0 else "")
    handles, labels = axes.flatten()[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=4, frameon=False)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    for suffix in (".png", ".pdf"):
        fig.savefig(out_stem.with_suffix(suffix), dpi=180 if suffix == ".png" else None)
    plt.close(fig)


def plot_phase_grid(summary: pd.DataFrame, scenarios: list[str], methods: list[str], out_stem: Path) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(12.6, 7.6), sharey=True)
    phase_cols = [
        ("pdr_pre", "Pre"),
        ("pdr_perturb", "Perturb"),
        ("pdr_recovery", "Recovery"),
    ]
    width = 0.18
    method_count = len(methods)
    colors = {
        "B_masked_debt_adaptive": "#1f77b4",
        "ppo_baseline": "#d62728",
        "greedy_stdma_2hop": "#ff7f0e",
        "heuristic_only": "#2ca02c",
    }
    x = np.arange(len(phase_cols))
    for idx, (ax, scenario) in enumerate(zip(axes.flatten(), scenarios)):
        subset = summary[summary["scenario"] == scenario]
        for idx, method in enumerate(methods):
            row = subset[subset["method"] == method]
            if row.empty:
                continue
            values = [float(row.iloc[0][col]) for col, _ in phase_cols]
            offset = (idx - (method_count - 1) / 2) * width
            ax.bar(
                x + offset,
                values,
                width=width,
                label=METHOD_LABELS.get(method, method),
                color=colors.get(method),
            )
        ax.set_title(SCENARIO_LABELS.get(scenario, scenario))
        ax.set_xticks(x, [label for _, label in phase_cols])
        ax.set_ylabel("Packet delivery ratio" if idx % 2 == 0 else "")
        if not has_recovery(scenario):
            ax.text(x[-1], 0.006, "N/A", ha="center", va="bottom", fontsize=9, color="#666666")
        ax.grid(axis="y", alpha=0.25)
    handles, labels = axes.flatten()[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=4, frameon=False)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    for suffix in (".png", ".pdf"):
        fig.savefig(out_stem.with_suffix(suffix), dpi=180 if suffix == ".png" else None)
    plt.close(fig)


def markdown_table(summary: pd.DataFrame, scenarios: list[str], methods: list[str]) -> str:
    lines = [
        "| Scenario | Method | Seeds | PDR pre | PDR perturb | PDR recovery | Throughput pre | Throughput perturb | Throughput recovery |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for scenario in scenarios:
        for method in methods:
            row = summary[(summary["scenario"] == scenario) & (summary["method"] == method)]
            if row.empty:
                continue
            item = row.iloc[0]
            values = [
                item["scenario_label"],
                item["method_label"],
                str(int(item["n_seeds"])),
                f"{item['pdr_pre']:.4f}",
                f"{item['pdr_perturb']:.4f}",
                "" if math.isnan(item["pdr_recovery"]) else f"{item['pdr_recovery']:.4f}",
                f"{item['throughput_pre']:.4f}",
                f"{item['throughput_perturb']:.4f}",
                "" if math.isnan(item["throughput_recovery"]) else f"{item['throughput_recovery']:.4f}",
            ]
            lines.append("| " + " | ".join(values) + " |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite_dirs", nargs="+", type=Path, required=True)
    parser.add_argument("--output_dir", type=Path, required=True)
    parser.add_argument("--scenarios", nargs="*", default=DEFAULT_SCENARIO_ORDER)
    parser.add_argument("--methods", nargs="*", default=DEFAULT_METHOD_ORDER)
    parser.add_argument("--perturb_at_frame", type=int, default=20000)
    parser.add_argument("--recovery_at_frame", type=int, default=40000)
    parser.add_argument("--window", type=int, default=500)
    args = parser.parse_args()

    suite_dirs = [path.resolve() for path in args.suite_dirs]
    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = discover_runs(suite_dirs, set(args.scenarios), set(args.methods))
    if not runs:
        raise SystemExit("no matching dynamic runs found")
    data = collect_data(runs)
    scenarios = ordered({scenario for scenario, _ in data}, list(args.scenarios))
    methods = ordered({method for _, method in data}, list(args.methods))
    summary = build_phase_summary(
        data,
        scenarios,
        methods,
        args.perturb_at_frame,
        args.recovery_at_frame,
    )

    plot_pdr_grid(
        data,
        scenarios,
        methods,
        out_dir / "dynamic_pdr_grid",
        args.perturb_at_frame,
        args.recovery_at_frame,
        args.window,
    )
    plot_phase_grid(summary, scenarios, methods, out_dir / "dynamic_phase_grid")
    summary.to_csv(out_dir / "dynamic_phase_summary.csv", index=False)
    (out_dir / "dynamic_phase_summary.md").write_text(
        markdown_table(summary, scenarios, methods),
        encoding="utf-8",
    )
    print(f"[PLOT] {out_dir / 'dynamic_pdr_grid.png'}")
    print(f"[PLOT] {out_dir / 'dynamic_phase_grid.png'}")
    print(f"[SUMMARY] {out_dir / 'dynamic_phase_summary.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
