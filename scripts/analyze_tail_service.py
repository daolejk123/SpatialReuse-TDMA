#!/usr/bin/env python3
"""Report per-node tail-service statistics for one or more metrics directories."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def _metrics_dir(path: Path) -> Path:
    return path if path.name == "metrics" else path / "metrics"


def _load_frame_rows(metrics_dir: Path) -> dict[tuple[int, int], dict[str, str]]:
    rows: dict[tuple[int, int], dict[str, str]] = {}
    with (metrics_dir / "frame_metrics.csv").open(newline="") as handle:
        for row in csv.DictReader(handle):
            rows[(int(row["frame"]), int(row["nodeId"]))] = row
    return rows


def _summarize(metrics_dir: Path, long_gap_threshold: int) -> list[dict[str, object]]:
    frame_rows = _load_frame_rows(metrics_dir)
    prev_success: dict[int, int] = {}
    gaps: defaultdict[int, int] = defaultdict(int)
    summary: defaultdict[int, dict[str, float]] = defaultdict(
        lambda: {
            "queued_frames": 0.0,
            "long_gap_frames": 0.0,
            "max_gap": 0.0,
            "req_candidates": 0.0,
            "req_sent": 0.0,
            "successes": 0.0,
        }
    )
    with (metrics_dir / "slot_stats.csv").open(newline="") as handle:
        for row in csv.DictReader(handle):
            frame = int(row["frame"])
            node_id = int(row["nodeId"])
            total_success = int(row["totalSuccessfulTx"])
            previous_total = prev_success.get(node_id, total_success)
            served = total_success > previous_total
            prev_success[node_id] = total_success

            frame_row = frame_rows[(frame, node_id)]
            qt = float(frame_row["Qt"])
            if served or qt <= 0.0:
                gaps[node_id] = 0
            else:
                gaps[node_id] += 1

            stats = summary[node_id]
            stats["req_candidates"] += float(frame_row["req_candidates"])
            stats["req_sent"] += float(frame_row["req_sent"])
            if served:
                stats["successes"] += 1.0
            if qt > 0.0:
                stats["queued_frames"] += 1.0
                stats["max_gap"] = max(stats["max_gap"], float(gaps[node_id]))
                if gaps[node_id] >= long_gap_threshold:
                    stats["long_gap_frames"] += 1.0

    rows: list[dict[str, object]] = []
    for node_id, stats in sorted(summary.items()):
        req_candidates = stats["req_candidates"]
        rows.append(
            {
                "node_id": node_id,
                "queued_frames": int(stats["queued_frames"]),
                "long_gap_frames": int(stats["long_gap_frames"]),
                "max_gap": int(stats["max_gap"]),
                "successes": int(stats["successes"]),
                "req_candidates": int(req_candidates),
                "req_sent": int(stats["req_sent"]),
                "sent_per_candidate": (
                    stats["req_sent"] / req_candidates if req_candidates > 0.0 else 0.0
                ),
            }
        )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Summarize node-level service tails from frame_metrics/slot_stats."
    )
    parser.add_argument("paths", nargs="+", type=Path, help="run dir(s) or metrics dir(s)")
    parser.add_argument("--long_gap_threshold", type=int, default=1200)
    args = parser.parse_args()

    writer = csv.DictWriter(
        __import__("sys").stdout,
        fieldnames=[
            "run",
            "node_id",
            "queued_frames",
            "long_gap_frames",
            "max_gap",
            "successes",
            "req_candidates",
            "req_sent",
            "sent_per_candidate",
        ],
        lineterminator="\n",
    )
    writer.writeheader()
    for path in args.paths:
        metrics_dir = _metrics_dir(path)
        for row in _summarize(metrics_dir, args.long_gap_threshold):
            writer.writerow(
                {
                    "run": str(path),
                    **{
                        key: f"{value:.6f}" if isinstance(value, float) else value
                        for key, value in row.items()
                    },
                }
            )


if __name__ == "__main__":
    main()
