#!/usr/bin/env python3
"""Monitor ablation/generalization run progress from local log files."""

from __future__ import annotations

import argparse
import csv
import os
import re
import time
from pathlib import Path
from typing import Iterable


PPO_RE = re.compile(
    r"^\[PPO\]\s+frame=\s*(?P<frame>\d+)\s+update=\s*(?P<update>\d+)\s+"
    r"avg_r=(?P<avg_r>[+-]?\d+(?:\.\d+)?)\s+.*?entropy=(?P<entropy>[+-]?\d+(?:\.\d+)?)"
)
TARGET_RE = re.compile(r"\[PPO\] target_(?:updates|frames) reached:\s*(\d+)")
SPEED_RE = re.compile(r"Speed:\s+(.*)")


def read_kv_tsv(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    data: dict[str, str] = {}
    with path.open(encoding="utf-8", errors="ignore") as f:
        reader = csv.reader(f, delimiter="\t")
        header = next(reader, None)
        if header != ["field", "value"]:
            return data
        for row in reader:
            if len(row) >= 2:
                data[row[0]] = row[1]
    return data


def read_manifest(root: Path) -> list[dict[str, str]]:
    path = root / "manifest.tsv"
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8", errors="ignore") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def age(path: Path, now: float) -> int | str:
    if not path.exists():
        return ""
    return int(now - path.stat().st_mtime)


def last_matching(path: Path, pattern: re.Pattern[str]) -> re.Match[str] | None:
    if not path.exists():
        return None
    last: re.Match[str] | None = None
    with path.open(encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                last = m
    return last


def text_contains(path: Path, needle: str) -> bool:
    if not path.exists():
        return False
    with path.open(encoding="utf-8", errors="ignore") as f:
        return any(needle in line for line in f)


def discover_log_dirs(root: Path) -> list[Path]:
    manifest = read_manifest(root)
    dirs = [Path(row["log_dir"]) for row in manifest if row.get("log_dir")]
    if dirs:
        return sorted(dict.fromkeys(dirs))
    return sorted({p.parent for p in root.rglob("python.log")} | {p.parent for p in root.rglob("sim.log")})


def scenario_from_path(root: Path, log_dir: Path) -> str:
    try:
        rel = log_dir.relative_to(root)
    except ValueError:
        return ""
    parts = rel.parts
    if len(parts) >= 3:
        return parts[0]
    return ""


def infer_group_seed(log_dir: Path) -> tuple[str, str]:
    group = log_dir.parent.name if log_dir.name.startswith("seed") else ""
    seed = log_dir.name.replace("seed", "") if log_dir.name.startswith("seed") else ""
    return group, seed


def estimate_eta(row: dict[str, str], now: float) -> str:
    target = row.get("target_updates") or ""
    update = row.get("update") or ""
    started = row.get("started_at") or ""
    if not (target.isdigit() and update.isdigit() and started.isdigit()):
        return ""
    target_i = int(target)
    update_i = int(update)
    if target_i <= 0 or update_i <= 0 or update_i >= target_i:
        return ""
    elapsed = max(1.0, now - int(started))
    rate = update_i / elapsed
    if rate <= 0:
        return ""
    remain = int((target_i - update_i) / rate)
    return f"{remain}s"


def summarize_run(root: Path, log_dir: Path, stale_timeout: int, now: float) -> dict[str, str]:
    status_file = log_dir / "run_status.tsv"
    python_log = log_dir / "python.log"
    sim_log = log_dir / "sim.log"
    status = read_kv_tsv(status_file)
    group, seed = infer_group_seed(log_dir)
    ppo = last_matching(python_log, PPO_RE)
    speed = last_matching(sim_log, SPEED_RE)

    row = {
        "scenario": status.get("scenario") or scenario_from_path(root, log_dir),
        "group": status.get("group") or group,
        "seed": status.get("seed") or seed,
        "status": status.get("status", ""),
        "update": status.get("last_update", ""),
        "frame": status.get("last_frame", ""),
        "avg_r": status.get("last_avg_r", ""),
        "entropy": status.get("last_entropy", ""),
        "age_py": str(age(python_log, now)),
        "age_sim": str(age(sim_log, now)),
        "speed": status.get("last_sim_speed", ""),
        "target_updates": status.get("target_updates", ""),
        "target_frames": status.get("target_frames", ""),
        "started_at": status.get("started_at", ""),
        "message": status.get("message", ""),
        "log_dir": str(log_dir),
    }
    if ppo:
        row.update(
            {
                "update": ppo.group("update"),
                "frame": ppo.group("frame"),
                "avg_r": ppo.group("avg_r"),
                "entropy": ppo.group("entropy"),
            }
        )
    if speed and not row["speed"]:
        row["speed"] = speed.group(1)

    target_done = text_contains(python_log, "[PPO] target_")
    sim_done = text_contains(sim_log, "Simulation time limit reached") and text_contains(sim_log, "End.")
    py_age = age(python_log, now)
    sim_age = age(sim_log, now)
    stale = isinstance(py_age, int) and isinstance(sim_age, int) and py_age >= stale_timeout and sim_age >= stale_timeout
    if not row["status"]:
        if target_done or sim_done:
            row["status"] = "done"
        elif stale:
            row["status"] = "stale"
        else:
            row["status"] = "running"
    if row["status"] in {"target_reached", "sim_completed"}:
        row["status"] = "done"
    elif row["status"] in {"running", "starting"} and stale:
        row["status"] = "stale"
    row["eta"] = estimate_eta(row, now)
    return row


def collect(root: Path, stale_timeout: int) -> list[dict[str, str]]:
    now = time.time()
    return [summarize_run(root, log_dir, stale_timeout, now) for log_dir in discover_log_dirs(root)]


def shorten(value: str, width: int) -> str:
    value = value or ""
    if len(value) <= width:
        return value
    return value[: max(0, width - 1)] + "…"


def print_table(rows: Iterable[dict[str, str]]) -> None:
    fields = [
        ("scenario", 16),
        ("group", 8),
        ("seed", 4),
        ("status", 8),
        ("update", 6),
        ("frame", 7),
        ("avg_r", 8),
        ("entropy", 7),
        ("age_py", 6),
        ("age_sim", 7),
        ("eta", 8),
        ("speed", 26),
    ]
    rows = list(rows)
    print(" ".join(name.ljust(width) for name, width in fields))
    print(" ".join("-" * width for _, width in fields))
    for row in rows:
        print(" ".join(shorten(row.get(name, ""), width).ljust(width) for name, width in fields))
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    print()
    print("summary:", " ".join(f"{k}={v}" for k, v in sorted(counts.items())) or "no runs")


def main() -> int:
    parser = argparse.ArgumentParser(description="Monitor DynamicTDMA experiment runs")
    parser.add_argument("root", type=Path)
    parser.add_argument("--watch", action="store_true", help="Refresh continuously")
    parser.add_argument("--once", action="store_true", help="Print one snapshot and exit")
    parser.add_argument("--interval", type=int, default=30)
    parser.add_argument("--stale-timeout", type=int, default=300)
    args = parser.parse_args()

    root = args.root.resolve()
    while True:
        rows = collect(root, args.stale_timeout)
        if args.watch:
            os.system("clear")
        print(f"root: {root}")
        print_table(rows)
        if not args.watch:
            return 0
        time.sleep(args.interval)


if __name__ == "__main__":
    raise SystemExit(main())
