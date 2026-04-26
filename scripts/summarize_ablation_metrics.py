#!/usr/bin/env python3
"""Summarize PPO and network metrics from an ablation run directory."""

from __future__ import annotations

import argparse
import collections
import csv
import math
import re
import statistics
from pathlib import Path
from typing import Iterable


PPO_RE = re.compile(
    r"^\[PPO\]\s+frame=\s*(?P<frame>\d+)\s+update=\s*(?P<update>\d+)\s+"
    r"avg_r=(?P<avg_r>[+-]?\d+(?:\.\d+)?)\s+"
    r"L_actor=(?P<L_actor>[+-]?\d+(?:\.\d+)?)\s+"
    r"L_critic=(?P<L_critic>[+-]?\d+(?:\.\d+)?)\s+"
    r"entropy=(?P<entropy>[+-]?\d+(?:\.\d+)?)\s+"
    r"ent=(?P<ent>[+-]?\d+(?:\.\d+)?)"
    r"(?:\s+heur_dev=(?P<heur_dev>[+-]?\d+(?:\.\d+)?))?"
)

GROUP_ORDER = ["baseline", "D", "B", "DB"]

RUN_FIELDS = [
    "scenario",
    "num_nodes",
    "topology_mode",
    "group",
    "seed",
    "complete",
    "ppo_frame",
    "ppo_update",
    "avg_r",
    "entropy",
    "L_critic",
    "heur_dev",
    "final_frame",
    "total_packets",
    "total_tx",
    "total_requests",
    "tx_success_rate",
    "packets_per_frame",
    "goodput_per_slot",
    "packet_delivery_ratio",
    "drop_or_backlog_rate",
    "mac_efficiency",
    "concurrent_success_degree",
    "spatial_reuse_gain",
    "jain_tx_mean",
    "jain_tx_tail100",
    "queue_tail100",
    "queue_stability_slope",
    "request_rate_mean",
    "slot_util_mean",
    "ctrl_collision_mean",
    "control_pressure",
    "req_pressure_mean",
    "Wt_p95",
    "fair_goodput",
    "starvation_ratio",
    "energy_proxy",
    "metrics_dir",
]

SUMMARY_METRICS = [
    "avg_r",
    "entropy",
    "L_critic",
    "heur_dev",
    "total_packets",
    "total_tx",
    "total_requests",
    "tx_success_rate",
    "packets_per_frame",
    "goodput_per_slot",
    "packet_delivery_ratio",
    "drop_or_backlog_rate",
    "mac_efficiency",
    "concurrent_success_degree",
    "spatial_reuse_gain",
    "jain_tx_mean",
    "jain_tx_tail100",
    "queue_tail100",
    "queue_stability_slope",
    "request_rate_mean",
    "slot_util_mean",
    "ctrl_collision_mean",
    "control_pressure",
    "req_pressure_mean",
    "Wt_p95",
    "fair_goodput",
    "starvation_ratio",
    "energy_proxy",
]

SCENARIO_RE = re.compile(r"^N(?P<num_nodes>\d+)_(?P<topology>.+)$")


def fnum(value: str | float | int | None) -> float:
    if value in (None, ""):
        return math.nan
    return float(value)


def fmt(value: float | str | None, digits: int = 4) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if math.isnan(value):
        return ""
    if abs(value) >= 100:
        return f"{value:.1f}"
    return f"{value:.{digits}f}"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def percentile(values: list[float], pct: float) -> float:
    vals = sorted(v for v in values if not math.isnan(v))
    if not vals:
        return math.nan
    if len(vals) == 1:
        return vals[0]
    rank = (len(vals) - 1) * pct / 100.0
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return vals[int(rank)]
    frac = rank - low
    return vals[low] * (1.0 - frac) + vals[high] * frac


def linear_slope(points: list[tuple[float, float]]) -> float:
    clean = [(x, y) for x, y in points if not math.isnan(x) and not math.isnan(y)]
    if len(clean) < 2:
        return math.nan
    xs = [p[0] for p in clean]
    ys = [p[1] for p in clean]
    x_mean = statistics.fmean(xs)
    y_mean = statistics.fmean(ys)
    denom = sum((x - x_mean) ** 2 for x in xs)
    if denom == 0:
        return math.nan
    return sum((x - x_mean) * (y - y_mean) for x, y in clean) / denom


def last_ppo(path: Path) -> dict[str, float]:
    last: dict[str, float] | None = None
    if not path.exists():
        return {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = PPO_RE.match(line)
        if not m:
            continue
        last = {
            "ppo_frame": fnum(m.group("frame")),
            "ppo_update": fnum(m.group("update")),
            "avg_r": fnum(m.group("avg_r")),
            "entropy": fnum(m.group("entropy")),
            "L_critic": fnum(m.group("L_critic")),
            "heur_dev": fnum(m.group("heur_dev")),
        }
    return last or {}


def sim_completed(path: Path) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8", errors="ignore")
    return "Simulation time limit reached" in text and "End." in text


def summarize_slot_stats(path: Path, num_slots: float = math.nan, total_arrivals: float = math.nan) -> dict[str, float]:
    if not path.exists():
        return {}
    final_frame = 0
    last: list[dict[str, str]] = []
    generated_cols: list[str] | None = None
    tail_by_node: dict[str, collections.deque[tuple[int, float]]] = collections.defaultdict(lambda: collections.deque(maxlen=101))
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if generated_cols is None:
                generated_cols = [k for k in row if k.startswith("totalGenerated")]
            frame = int(float(row["frame"]))
            if frame > final_frame:
                final_frame = frame
                last = [row]
            elif frame == final_frame:
                last.append(row)
            tail_by_node[row["nodeId"]].append((frame, fnum(row["totalSuccessfulPackets"])))
    if not last:
        return {}
    total_packets = sum(fnum(r["totalSuccessfulPackets"]) for r in last)
    total_tx = sum(fnum(r["totalSuccessfulTx"]) for r in last)
    total_requests = sum(fnum(r["totalSlotRequests"]) for r in last)
    if math.isnan(total_arrivals):
        generated_cols = generated_cols or []
        total_arrivals = sum(fnum(r[col]) for r in last for col in generated_cols)
    tail_start = max(0, final_frame - 100)
    starved = 0
    for node_rows in tail_by_node.values():
        ordered = [(frame, packets) for frame, packets in node_rows if frame >= tail_start]
        if not ordered:
            continue
        first = ordered[0][1]
        last_packets = ordered[-1][1]
        if last_packets - first <= 0:
            starved += 1
    final_nodes = len(last)
    goodput_per_slot = (
        total_packets / (final_frame * num_slots)
        if final_frame and num_slots and not math.isnan(num_slots)
        else math.nan
    )
    return {
        "final_frame": float(final_frame),
        "total_packets": total_packets,
        "total_tx": total_tx,
        "total_requests": total_requests,
        "tx_success_rate": total_tx / total_requests if total_requests else math.nan,
        "packets_per_frame": total_packets / final_frame if final_frame else math.nan,
        "goodput_per_slot": goodput_per_slot,
        "packet_delivery_ratio": total_packets / total_arrivals if total_arrivals else math.nan,
        "mac_efficiency": total_packets / total_requests if total_requests else math.nan,
        "starvation_ratio": starved / final_nodes if final_nodes else math.nan,
        "slot_final_nodes": float(final_nodes),
    }


def summarize_fairness(path: Path) -> dict[str, float]:
    if not path.exists():
        return {}
    count = 0
    jain_sum = 0.0
    total_arrivals = 0.0
    total_delta_packets = 0.0
    tail: collections.deque[dict[str, str]] = collections.deque(maxlen=100)
    last_row: dict[str, str] | None = None
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            count += 1
            jain_sum += fnum(row["jain_tx"])
            total_arrivals += fnum(row["sum_arrivals"])
            total_delta_packets += fnum(row["sum_delta_packets"])
            tail.append(row)
            last_row = row
    if not last_row:
        return {}
    queue_tail100 = statistics.fmean(fnum(r["sum_queue"]) for r in tail)
    final_queue = fnum(last_row["sum_queue"])
    return {
        "fairness_final_frame": fnum(last_row["frame"]),
        "jain_tx_mean": jain_sum / count if count else math.nan,
        "jain_tx_tail100": statistics.fmean(fnum(r["jain_tx"]) for r in tail),
        "queue_tail100": queue_tail100,
        "queue_stability_slope": linear_slope([(fnum(r["frame"]), fnum(r["sum_queue"])) for r in tail]),
        "total_arrivals": total_arrivals,
        "total_delta_packets": total_delta_packets,
        "final_queue": final_queue,
        "fairness_rows": float(count),
    }


def summarize_frame_metrics(path: Path) -> dict[str, float]:
    if not path.exists():
        return {}
    count = 0
    req_sent = 0.0
    req_candidates = 0.0
    ctrl_sum = 0.0
    wt_values: list[float] = []
    num_slots = math.nan
    util_sum = 0.0
    util_count = 0
    last_row: dict[str, str] | None = None
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            count += 1
            req_sent += fnum(row["req_sent"])
            req_candidates += fnum(row["req_candidates"])
            ctrl_sum += fnum(row["Cctrl"])
            wt_values.append(fnum(row["Wt"]))
            if row.get("Bown"):
                num_slots = max(num_slots if not math.isnan(num_slots) else 0.0, float(len(row["Bown"])))
                util_sum += row["Bown"].count("1")
                util_count += 1
            last_row = row
    if not last_row:
        return {}
    slot_util_mean = (util_sum / util_count / num_slots) if util_count and num_slots and not math.isnan(num_slots) else math.nan
    ctrl_collision_mean = ctrl_sum / count if count else math.nan
    req_pressure_mean = req_sent / (count * num_slots) if count and num_slots and not math.isnan(num_slots) else math.nan
    control_pressure = ctrl_collision_mean / num_slots if num_slots and not math.isnan(num_slots) else math.nan
    return {
        "frame_metrics_final_frame": fnum(last_row["frame"]),
        "request_rate_mean": req_sent / req_candidates if req_candidates else math.nan,
        "slot_util_mean": slot_util_mean,
        "ctrl_collision_mean": ctrl_collision_mean,
        "control_pressure": control_pressure,
        "req_pressure_mean": req_pressure_mean,
        "Wt_p95": percentile(wt_values, 95),
        "num_data_slots": num_slots,
        "frame_metrics_rows": float(count),
    }


def read_run_perf(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key.strip()] = value.strip()
    return out


def scenario_meta(scenario: str, run_dir: Path) -> dict[str, object]:
    perf = read_run_perf(run_dir / "run_perf.txt")
    meta: dict[str, object] = {
        "scenario": scenario,
        "num_nodes": perf.get("num_nodes", ""),
        "topology_mode": perf.get("topology_mode", ""),
    }
    m = SCENARIO_RE.match(scenario)
    if m:
        meta["num_nodes"] = meta["num_nodes"] or m.group("num_nodes")
        meta["topology_mode"] = meta["topology_mode"] or m.group("topology")
    return meta


def seed_key(path: Path) -> int:
    text = path.name.replace("seed", "")
    return int(text) if text.isdigit() else 0


def group_key(path: Path | str) -> int:
    name = path.name if isinstance(path, Path) else path
    return GROUP_ORDER.index(name) if name in GROUP_ORDER else 99


def discover_runs(root: Path) -> Iterable[tuple[str, str, int, Path]]:
    for group_dir in sorted(root.iterdir(), key=lambda p: GROUP_ORDER.index(p.name) if p.name in GROUP_ORDER else 99):
        if not group_dir.is_dir():
            continue
        if group_dir.name in GROUP_ORDER:
            for seed_dir in sorted(group_dir.glob("seed*"), key=seed_key):
                seed_text = seed_dir.name.replace("seed", "")
                if not seed_text.isdigit():
                    continue
                yield "", group_dir.name, int(seed_text), seed_dir
            continue

        scenario = group_dir.name
        for nested_group in sorted(group_dir.iterdir(), key=group_key):
            if not nested_group.is_dir() or nested_group.name not in GROUP_ORDER:
                continue
            for seed_dir in sorted(nested_group.glob("seed*"), key=seed_key):
                seed_text = seed_dir.name.replace("seed", "")
                if not seed_text.isdigit():
                    continue
                yield scenario, nested_group.name, int(seed_text), seed_dir


def summarize_run(scenario: str, group: str, seed: int, run_dir: Path) -> dict[str, object]:
    metrics_dir = run_dir / "metrics"
    row: dict[str, object] = {
        "scenario": scenario,
        "group": group,
        "seed": seed,
        "complete": False,
        "metrics_dir": str(metrics_dir),
    }
    row.update(scenario_meta(scenario, run_dir))
    row.update(last_ppo(run_dir / "python.log"))
    row.update(summarize_frame_metrics(metrics_dir / "frame_metrics.csv"))
    row.update(summarize_fairness(metrics_dir / "fairness.csv"))
    row.update(
        summarize_slot_stats(
            metrics_dir / "slot_stats.csv",
            num_slots=float(row.get("num_data_slots", math.nan)),
            total_arrivals=float(row.get("total_arrivals", math.nan)),
        )
    )

    goodput = float(row.get("goodput_per_slot", math.nan))
    jain_tail = float(row.get("jain_tx_tail100", math.nan))
    final_queue = float(row.get("final_queue", math.nan))
    total_arrivals = float(row.get("total_arrivals", math.nan))
    slot_util = float(row.get("slot_util_mean", math.nan))
    control_pressure = float(row.get("control_pressure", math.nan))
    req_pressure = float(row.get("req_pressure_mean", math.nan))
    if not math.isnan(goodput) and not math.isnan(jain_tail):
        row["fair_goodput"] = goodput * jain_tail
    if total_arrivals:
        row["drop_or_backlog_rate"] = final_queue / total_arrivals if not math.isnan(final_queue) else math.nan
    if not math.isnan(goodput) and not math.isnan(slot_util) and slot_util > 0:
        row["concurrent_success_degree"] = goodput / slot_util
    if not math.isnan(req_pressure) or not math.isnan(control_pressure) or not math.isnan(slot_util):
        row["energy_proxy"] = sum(
            v for v in [req_pressure, control_pressure, slot_util] if not math.isnan(v)
        )

    final_frame = row.get("final_frame")
    complete = (
        sim_completed(run_dir / "sim.log")
        and float(row.get("ppo_frame", 0.0)) > 0.0
        and float(row.get("ppo_update", 0.0)) > 0.0
        and float(row.get("slot_final_nodes", 0.0)) > 0.0
        and final_frame == row.get("fairness_final_frame")
        and final_frame == row.get("frame_metrics_final_frame")
    )
    row["complete"] = complete
    return row


def mean_std(values: list[float]) -> tuple[float, float]:
    vals = [v for v in values if not math.isnan(v)]
    if not vals:
        return math.nan, math.nan
    if len(vals) == 1:
        return vals[0], math.nan
    return statistics.fmean(vals), statistics.stdev(vals)


def group_summary(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    groups = sorted({str(r["group"]) for r in rows}, key=lambda g: GROUP_ORDER.index(g) if g in GROUP_ORDER else 99)
    for group in groups:
        group_rows = [r for r in rows if r["group"] == group]
        out: dict[str, object] = {"group": group, "runs": len(group_rows), "complete_runs": sum(1 for r in group_rows if r["complete"])}
        for metric in SUMMARY_METRICS:
            mean, std = mean_std([float(r.get(metric, math.nan)) for r in group_rows])
            out[f"{metric}_mean"] = mean
            out[f"{metric}_std"] = std
        output.append(out)
    attach_spatial_reuse_gain(output)
    return output


def scenario_summary(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    keys = sorted({(str(r.get("scenario", "")), str(r["group"])) for r in rows})
    for scenario, group in keys:
        scenario_rows = [r for r in rows if str(r.get("scenario", "")) == scenario and str(r["group"]) == group]
        first = scenario_rows[0]
        out: dict[str, object] = {
            "scenario": scenario,
            "num_nodes": first.get("num_nodes", ""),
            "topology_mode": first.get("topology_mode", ""),
            "group": group,
            "runs": len(scenario_rows),
            "complete_runs": sum(1 for r in scenario_rows if r["complete"]),
        }
        for metric in SUMMARY_METRICS:
            mean, std = mean_std([float(r.get(metric, math.nan)) for r in scenario_rows])
            out[f"{metric}_mean"] = mean
            out[f"{metric}_std"] = std
        output.append(out)
    attach_spatial_reuse_gain(output)
    return output


def attach_spatial_reuse_gain(summary_rows: list[dict[str, object]]) -> None:
    baselines: dict[str, float] = {}
    for row in summary_rows:
        scenario = str(row.get("scenario", ""))
        if row.get("group") == "baseline":
            baselines[scenario] = float(row.get("goodput_per_slot_mean", math.nan))
    if "" not in baselines:
        baseline_rows = [row for row in summary_rows if row.get("group") == "baseline"]
        if baseline_rows:
            baselines[""] = float(baseline_rows[0].get("goodput_per_slot_mean", math.nan))
    for row in summary_rows:
        scenario = str(row.get("scenario", ""))
        base = baselines.get(scenario, baselines.get("", math.nan))
        goodput = float(row.get("goodput_per_slot_mean", math.nan))
        gain = goodput / base if base and not math.isnan(base) else math.nan
        row["spatial_reuse_gain_mean"] = gain
        row["spatial_reuse_gain_std"] = math.nan


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def print_markdown(summary_rows: list[dict[str, object]]) -> None:
    headers = [
        "场景",
        "组",
        "完整run",
        "avg_r",
        "packets/frame",
        "goodput/slot",
        "PDR",
        "queue_slope",
        "Wt_P95",
        "starvation",
        "tx_success",
        "Jain尾100",
        "queue尾100",
        "slot_util",
    ]
    print("| " + " | ".join(headers) + " |")
    print("|" + "|".join([":---"] + ["---:" for _ in headers[1:]]) + "|")
    for row in summary_rows:
        print(
            "| "
            + " | ".join(
                [
                    str(row.get("scenario", "")),
                    str(row["group"]),
                    f"{row['complete_runs']}/{row['runs']}",
                    f"{fmt(float(row['avg_r_mean']), 3)} ± {fmt(float(row['avg_r_std']), 3)}",
                    f"{fmt(float(row['packets_per_frame_mean']), 3)} ± {fmt(float(row['packets_per_frame_std']), 3)}",
                    f"{fmt(float(row.get('goodput_per_slot_mean', math.nan)), 4)}",
                    f"{fmt(float(row.get('packet_delivery_ratio_mean', math.nan)), 4)}",
                    f"{fmt(float(row.get('queue_stability_slope_mean', math.nan)), 4)}",
                    f"{fmt(float(row.get('Wt_p95_mean', math.nan)), 4)}",
                    f"{fmt(float(row.get('starvation_ratio_mean', math.nan)), 4)}",
                    f"{fmt(float(row['tx_success_rate_mean']), 4)}",
                    f"{fmt(float(row['jain_tx_tail100_mean']), 4)}",
                    f"{fmt(float(row['queue_tail100_mean']), 2)}",
                    f"{fmt(float(row['slot_util_mean_mean']), 4)}",
                ]
            )
            + " |"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="Ablation root, e.g. logs/ablation_YYYYMMDD_HHMMSS")
    parser.add_argument("--no-markdown", action="store_true", help="Do not print Markdown summary")
    args = parser.parse_args()

    root = args.root.resolve()
    rows = [summarize_run(scenario, group, seed, run_dir) for scenario, group, seed, run_dir in discover_runs(root)]
    if not rows:
        raise SystemExit(f"No runs found under {root}")

    write_csv(root / "metrics_summary.csv", rows, RUN_FIELDS)
    summary_rows = group_summary(rows)
    summary_fields = ["group", "runs", "complete_runs"]
    for metric in SUMMARY_METRICS:
        summary_fields.extend([f"{metric}_mean", f"{metric}_std"])
    write_csv(root / "metrics_group_summary.csv", summary_rows, summary_fields)

    scenario_rows = scenario_summary(rows)
    scenario_fields = ["scenario", "num_nodes", "topology_mode", "group", "runs", "complete_runs"]
    for metric in SUMMARY_METRICS:
        scenario_fields.extend([f"{metric}_mean", f"{metric}_std"])
    write_csv(root / "metrics_scenario_summary.csv", scenario_rows, scenario_fields)

    if not args.no_markdown:
        print_markdown(scenario_rows if any(r.get("scenario") for r in rows) else summary_rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
