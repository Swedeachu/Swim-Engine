"""Ranks the zones of an engine `profile` CSV, and compares two runs.

The engine's `profile <frames> <warmup> <csv> [label]` command writes one row per zone:
label,category,name,frames,per_frame_ms,mean_ms,min_ms,p50_ms,p95_ms,p99_ms,max_ms
(categories: frame, cpu, render, record, feature, gpu). This prints the frame summary and the
costliest zones of each category by per-frame cost; with --compare, the other run's value and
the difference too.

usage: python profile_csv.py <frames.csv> [--compare <other.csv>] [--top 15] [--category gpu ...]
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load(path: Path) -> dict[tuple[str, str], dict[str, float]]:
    rows: dict[tuple[str, str], dict[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            values = {}
            for key, value in row.items():
                if key and (key.endswith("_ms") or key == "frames"):
                    try:
                        values[key] = float(value)
                    except (TypeError, ValueError):
                        pass
            rows[(row.get("category", ""), row.get("name", ""))] = values
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--category", action="append", help="Only these categories (repeatable).")
    args = parser.parse_args()

    run = load(args.csv)
    other = load(args.compare) if args.compare else {}
    categories = sorted({category for category, _ in run})
    order = ["frame", "cpu", "render", "record", "feature", "gpu"]
    categories.sort(key=lambda c: order.index(c) if c in order else len(order))

    for category in categories:
        if args.category and category not in args.category:
            continue
        zones = [(name, values) for (c, name), values in run.items() if c == category]
        zones.sort(key=lambda z: -z[1].get("per_frame_ms", 0.0))
        limit = len(zones) if category == "frame" else args.top
        print(f"\n== {category} ({len(zones)} zones) ==")
        header = f"{'per frame':>10} {'p50':>8} {'p99':>8}"
        if other:
            header += f" {'other':>10} {'delta':>9}"
        print(header + "  zone")
        for name, values in zones[:limit]:
            line = f"{values.get('per_frame_ms', 0.0):10.3f} {values.get('p50_ms', 0.0):8.3f} {values.get('p99_ms', 0.0):8.3f}"
            if other:
                theirs = other.get((category, name), {}).get("per_frame_ms")
                if theirs is None:
                    line += f" {'-':>10} {'':>9}"
                else:
                    line += f" {theirs:10.3f} {values.get('per_frame_ms', 0.0) - theirs:+9.3f}"
            print(f"{line}  {name}")


if __name__ == "__main__":
    main()
