#!/usr/bin/env python3
"""动态扰动 benchmark 套件的窗口切分 + 恢复速度可视化。

读取 `logs/benchmark_*/<scenario>/<method>/seed*/metrics/fairness.csv` 与对应
`frame_metrics.csv`，按 perturb_at_frame / recovery_at_frame 切三段窗口
（pre / perturb / recovery），逐 seed 聚合后输出：

1. 每个 scenario 一张 4 子图（PDR / Jain / Queue / Throughput）时序图，
   方法用颜色区分，纵线标扰动 / 恢复事件。
2. summary.csv：逐 (scenario, method, metric) 输出 pre/perturb/recovery 均值
   与恢复时长（rolling-100 均值首次回到 pre × thresh 的相对帧数）。

用法：
    python scripts/plot_dynamic_recovery.py \\
        --suite_dir logs/benchmark_dynamic_perturb_v5_20260515_113708
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


METRIC_SPECS = [
    ("pdr", "Packet Delivery Ratio", "higher_better"),
    ("jain_tx", "Jain Fairness (tx)", "higher_better"),
    ("queue", "Total Queue Depth", "lower_better"),
    ("throughput", "Throughput (pkts/frame)", "higher_better"),
]


def load_suite_config(suite_dir: Path) -> dict:
    cfg = {}
    cfg_path = suite_dir / "suite_config.tsv"
    if cfg_path.exists():
        for line in cfg_path.read_text().splitlines()[1:]:
            parts = line.split("\t", 1)
            if len(parts) == 2:
                cfg[parts[0]] = parts[1]
    return cfg


def discover_runs(suite_dir: Path, scenarios: list[str] | None, methods: list[str] | None):
    runs = []
    for fpath in sorted(suite_dir.glob("*/*/seed*/metrics/fairness.csv")):
        rel = fpath.relative_to(suite_dir)
        scenario, method, seed_str, _, _ = rel.parts
        if scenarios and scenario not in scenarios:
            continue
        if methods and method not in methods:
            continue
        try:
            seed = int(seed_str.replace("seed", ""))
        except ValueError:
            continue
        runs.append({"scenario": scenario, "method": method, "seed": seed, "path": fpath})
    return runs


def load_run_dataframe(fairness_path: Path) -> pd.DataFrame:
    df = pd.read_csv(fairness_path)
    df["pdr"] = np.where(df["sum_arrivals"] > 0,
                         df["sum_delta_packets"] / df["sum_arrivals"], np.nan)
    df["queue"] = df["sum_queue"].astype(float)
    df["throughput"] = df["sum_delta_packets"].astype(float)
    return df[["frame", "pdr", "jain_tx", "queue", "throughput"]]


def aggregate_per_frame(seed_dfs: list[pd.DataFrame]) -> pd.DataFrame:
    if not seed_dfs:
        return pd.DataFrame(columns=["frame"] + [m for m, _, _ in METRIC_SPECS])
    merged = pd.concat(seed_dfs, ignore_index=True)
    return merged.groupby("frame", as_index=False)[
        [m for m, _, _ in METRIC_SPECS]].mean()


def compute_recovery_time(agg: pd.DataFrame, metric: str, direction: str,
                          pre_mean: float, perturb_frame: int,
                          recovery_frame: int, thresh: float, win: int) -> float:
    """返回首次回到 thresh × pre_mean 的相对帧数（相对 recovery_frame）。

    无法判定时返回 NaN。
    """
    if np.isnan(pre_mean):
        return float("nan")
    target = pre_mean * thresh
    rec = agg[agg["frame"] > recovery_frame]
    if len(rec) < win:
        return float("nan")
    rolling = rec[metric].rolling(win, min_periods=win).mean()
    if direction == "higher_better":
        mask = rolling >= target
    else:
        mask = rolling <= target
    valid = rec["frame"][mask]
    if valid.empty:
        return float("nan")
    return float(valid.iloc[0] - recovery_frame)


def plot_scenario(scenario: str, methods: list[str], data: dict, out_dir: Path,
                  perturb_frame: int, recovery_frame: int, has_recovery: bool,
                  window: int) -> Path:
    fig, axs = plt.subplots(2, 2, figsize=(14, 8), sharex=True)
    axs = axs.flatten()
    colors = plt.rcParams["axes.prop_cycle"].by_key()["color"]
    for mi, method in enumerate(methods):
        agg = aggregate_per_frame(data[(scenario, method)])
        if agg.empty:
            continue
        color = colors[mi % len(colors)]
        for ax, (metric, title, _) in zip(axs, METRIC_SPECS):
            series = agg[metric].rolling(window, min_periods=1).mean()
            ax.plot(agg["frame"], series, label=method, color=color, linewidth=1.5)
            ax.set_title(title)
    for ax, (metric, _, _) in zip(axs, METRIC_SPECS):
        ax.axvline(perturb_frame, color="red", linestyle="--", alpha=0.6,
                   label=f"perturb@{perturb_frame}")
        if has_recovery:
            ax.axvline(recovery_frame, color="green", linestyle="--", alpha=0.6,
                       label=f"recovery@{recovery_frame}")
        ax.grid(alpha=0.3)
        ax.set_xlabel("frame")
    axs[0].legend(loc="best", fontsize=8)
    fig.suptitle(f"{scenario}  (rolling window = {window} frames)")
    fig.tight_layout()
    out_path = out_dir / f"{scenario}.png"
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--suite_dir", required=True, type=Path,
                    help="benchmark suite 输出目录")
    ap.add_argument("--output_dir", type=Path, default=None,
                    help="图与 CSV 输出目录（默认 suite_dir/plots_dynamic_recovery）")
    ap.add_argument("--window", type=int, default=500,
                    help="时序图 rolling-mean 窗口（帧）")
    ap.add_argument("--recovery_window", type=int, default=100,
                    help="判定恢复时的 rolling-mean 窗口（帧）")
    ap.add_argument("--recovery_thresh", type=float, default=0.95,
                    help="恢复阈值：rolling avg 回到 pre × thresh 视作恢复")
    ap.add_argument("--scenarios", nargs="*", default=None)
    ap.add_argument("--methods", nargs="*", default=None)
    ap.add_argument("--perturb_at_frame", type=int, default=None,
                    help="覆盖 suite_config 中的 perturb_at_frame")
    ap.add_argument("--recovery_at_frame", type=int, default=None,
                    help="覆盖 suite_config 中的 recovery_at_frame")
    args = ap.parse_args()

    suite_dir = args.suite_dir.resolve()
    if not suite_dir.is_dir():
        print(f"[ERR] suite_dir 不存在: {suite_dir}", file=sys.stderr)
        return 1

    cfg = load_suite_config(suite_dir)
    perturb_frame = args.perturb_at_frame or int(cfg.get("perturb_at_frame", 20000))
    recovery_frame = args.recovery_at_frame or int(cfg.get("recovery_at_frame", 40000))

    out_dir = (args.output_dir or (suite_dir / "plots_dynamic_recovery")).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = discover_runs(suite_dir, args.scenarios, args.methods)
    if not runs:
        print(f"[ERR] 未在 {suite_dir} 找到任何 fairness.csv", file=sys.stderr)
        return 1
    print(f"[INFO] 共发现 {len(runs)} 个 run；perturb@{perturb_frame} recovery@{recovery_frame}")
    print(f"[INFO] 输出目录: {out_dir}")

    data: dict[tuple[str, str], list[pd.DataFrame]] = {}
    for r in runs:
        df = load_run_dataframe(r["path"])
        data.setdefault((r["scenario"], r["method"]), []).append(df)

    scenarios = sorted({s for s, _ in data})
    summary_rows = []

    for scenario in scenarios:
        methods_here = sorted({m for s, m in data if s == scenario})
        agg_dfs = {m: aggregate_per_frame(data[(scenario, m)]) for m in methods_here}

        has_recovery = any(
            (not agg.empty) and agg["frame"].max() > recovery_frame
            for agg in agg_dfs.values()
        ) and "node_dropout" not in scenario

        plot_path = plot_scenario(scenario, methods_here, data, out_dir,
                                  perturb_frame, recovery_frame, has_recovery,
                                  args.window)
        print(f"[PLOT] {scenario} → {plot_path.name}")

        for method in methods_here:
            agg = agg_dfs[method]
            if agg.empty:
                continue
            n_seeds = len(data[(scenario, method)])
            pre_mask = agg["frame"] <= perturb_frame
            pert_mask = (agg["frame"] > perturb_frame) & (agg["frame"] <= recovery_frame)
            rec_mask = agg["frame"] > recovery_frame
            for metric, _, direction in METRIC_SPECS:
                pre_mean = float(agg.loc[pre_mask, metric].mean())
                pert_mean = float(agg.loc[pert_mask, metric].mean())
                rec_mean = float(agg.loc[rec_mask, metric].mean()) if rec_mask.any() else float("nan")
                rec_time = compute_recovery_time(
                    agg, metric, direction, pre_mean,
                    perturb_frame, recovery_frame,
                    args.recovery_thresh, args.recovery_window
                ) if has_recovery else float("nan")
                summary_rows.append({
                    "scenario": scenario,
                    "method": method,
                    "metric": metric,
                    "direction": direction,
                    "n_seeds": n_seeds,
                    "pre_mean": pre_mean,
                    "perturb_mean": pert_mean,
                    "recovery_mean": rec_mean,
                    "perturb_delta_pct": (pert_mean - pre_mean) / pre_mean * 100
                                          if pre_mean else float("nan"),
                    "recovery_time_frames": rec_time,
                })

    summary_df = pd.DataFrame(summary_rows)
    summary_path = out_dir / "summary.csv"
    summary_df.to_csv(summary_path, index=False)
    print(f"[SUMMARY] {summary_path} ({len(summary_df)} 行)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
