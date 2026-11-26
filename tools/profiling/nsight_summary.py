"""Ranks GPU work from Nsight Graphics exports: passes/ranges, draws/dispatches and shader lines.

Nsight Graphics exports tables as CSV (GPU Trace: the range and action tables; Shader
Profiler: the source or function view). Columns differ between versions and views, so this
finds them by name:
  name     : the first column whose header contains name, range, marker, label, event,
             function or source;
  duration : the first column whose header contains duration, time, gpu ms, elapsed or us/ns
             (converted to milliseconds by the unit in the header: ns, us, ms; ms when none);
  samples  : for shader views, a column containing samples, hits or % (used instead of a
             duration when there is none);
  file/line: for shader views, columns named like file and line (combined as file:line).
Rows with the same name are summed (a pass repeated per frame or per view adds up), so a
whole-capture export ranks what costs most overall.

usage: python nsight_summary.py <export.csv> [<export.csv> ...] [--top 25] [--frames N]
  --frames N divides totals by the captured frame count (per-frame milliseconds).
"""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path


def find(headers: list[str], patterns: list[str], exclude: list[str] | None = None) -> str | None:
    for header in headers:
        low = header.lower()
        if exclude and any(word in low for word in exclude):
            continue
        if any(re.search(pattern, low) for pattern in patterns):
            return header
    return None


def unit_scale(header: str) -> float:
    low = header.lower()
    if "ns" in re.findall(r"[a-zμ]+", low) or "(ns)" in low or "_ns" in low:
        return 1.0e-6
    if "us" in re.findall(r"[a-zμ]+", low) or "μs" in low or "(us)" in low or "_us" in low:
        return 1.0e-3
    return 1.0


def number(text: str) -> float | None:
    text = (text or "").strip().replace(",", "").rstrip("%")
    try:
        return float(text)
    except ValueError:
        return None


def summarize(path: Path, top: int, frames: int) -> None:
    with path.open(newline="", encoding="utf-8-sig", errors="replace") as handle:
        sample = handle.read(4096)
        handle.seek(0)
        dialect = csv.Sniffer().sniff(sample, delimiters=",;\t") if sample else csv.excel
        reader = csv.DictReader(handle, dialect=dialect)
        headers = reader.fieldnames or []
        rows = list(reader)

    file_column = find(headers, [r"\bfile\b", r"path"])
    line_column = find(headers, [r"\bline\b"])
    name_column = find(headers, [r"name", r"range", r"marker", r"label", r"event", r"function", r"source"], exclude=["file"])
    duration_column = find(headers, [r"duration", r"\btime\b", r"gpu ms", r"elapsed", r"\(ms\)", r"\(us\)", r"\(ns\)"])
    samples_column = find(headers, [r"samples", r"hits", r"%"])
    value_column = duration_column or samples_column

    if not value_column or not (name_column or file_column):
        print(f"{path}: no name/duration columns found among {headers}")
        return

    scale = unit_scale(value_column) if value_column == duration_column else 1.0
    totals: dict[str, float] = defaultdict(float)
    counts: dict[str, int] = defaultdict(int)

    for row in rows:
        value = number(row.get(value_column, ""))
        if value is None:
            continue
        if file_column and line_column and row.get(file_column):
            key = f"{row[file_column]}:{row.get(line_column, '').strip()}"
            if name_column and row.get(name_column):
                key += f"  {row[name_column]}"
        else:
            key = (row.get(name_column) or "").strip() or "(unnamed)"
        totals[key] += value * scale
        counts[key] += 1

    ranked = sorted(totals.items(), key=lambda item: -item[1])
    total = sum(totals.values()) or 1.0
    unit = "ms" if value_column == duration_column else value_column
    divisor = max(frames, 1)
    print(f"\n== {path.name}: {len(rows)} rows, ranked by {value_column} ==")
    print(f"{'total':>10} {'share':>6} {'count':>6}  name   ({unit}{' per frame' if frames > 1 else ''})")
    for key, value in ranked[:top]:
        print(f"{value / divisor:10.3f} {100.0 * value / total:5.1f}% {counts[key]:6d}  {key}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exports", type=Path, nargs="+")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--frames", type=int, default=1)
    args = parser.parse_args()

    for export in args.exports:
        summarize(export, args.top, args.frames)


if __name__ == "__main__":
    main()
