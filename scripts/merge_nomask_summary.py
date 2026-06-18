#!/usr/bin/env python3
"""Merge the new B_debt_adaptive (DASD-PPO w/o Mask) summary into the existing
static main summary for a 11-row comparison table.

Usage:
    python scripts/merge_nomask_summary.py \
        --old_root logs/benchmark_b_prime_main_formal_20260523_20260523_095144 \
        --new_root logs/benchmark_b_prime_main_formal_nomask_<TS> \
        --output_dir summaries/static_with_nomask_<TS>
"""
import argparse
import csv
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SUMMARIZE = REPO / "scripts" / "summarize_benchmark.py"


def run_summarize(root: Path) -> Path:
    """Run summarize_benchmark.py on a root and return the produced metrics_summary.csv path."""
    summary_csv = root / "summaries" / "metrics_summary.csv"
    if not summary_csv.exists():
        print(f"[merge] running summarize on {root}")
        subprocess.run(["python", str(SUMMARIZE), str(root)], check=True)
    else:
        print(f"[merge] reusing existing summary: {summary_csv}")
    return summary_csv


def concat_csvs(csv_a: Path, csv_b: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    rows_a = list(csv.DictReader(csv_a.open()))
    rows_b = list(csv.DictReader(csv_b.open()))
    fields = list(rows_a[0].keys()) if rows_a else list(rows_b[0].keys())
    # Ensure new rows have all fields (fill empty if missing)
    for r in rows_b:
        for f in fields:
            r.setdefault(f, "")
    with output.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows_a)
        writer.writerows(rows_b)
    print(f"[merge] wrote combined summary: {output} ({len(rows_a)} old + {len(rows_b)} new = {len(rows_a)+len(rows_b)} rows)")


def aggregate_by_method(combined_csv: Path, output_csv: Path) -> None:
    """Group by (scenario, method); compute mean across seeds for each key metric."""
    rows = list(csv.DictReader(combined_csv.open()))
    metrics = [
        "goodput_per_slot",
        "packet_delivery_ratio",
        "mac_efficiency",
        "queue_stability_slope",
        "Wt_p95",
        "jain_tx_tail100",
        "starvation_ratio",
    ]
    groups: dict[tuple[str, str], dict[str, list[float]]] = {}
    for r in rows:
        key = (r["scenario"], r["method"])
        if key not in groups:
            groups[key] = {m: [] for m in metrics}
        for m in metrics:
            try:
                groups[key][m].append(float(r[m]))
            except (KeyError, TypeError, ValueError):
                pass

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["scenario", "method", "n_seeds", *metrics])
        for (sc, mth), agg in sorted(groups.items()):
            n = max(len(v) for v in agg.values())
            row = [sc, mth, n]
            for m in metrics:
                vals = agg[m]
                row.append(f"{sum(vals)/len(vals):.4f}" if vals else "")
            writer.writerow(row)
    print(f"[merge] wrote per-method aggregate: {output_csv}")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--old_root", required=True, type=Path)
    p.add_argument("--new_root", required=True, type=Path)
    p.add_argument("--output_dir", required=True, type=Path)
    args = p.parse_args()

    old_csv = run_summarize(args.old_root)
    new_csv = run_summarize(args.new_root)
    combined = args.output_dir / "metrics_summary_combined.csv"
    aggregate = args.output_dir / "metrics_method_summary_combined.csv"
    concat_csvs(old_csv, new_csv, combined)
    aggregate_by_method(combined, aggregate)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
