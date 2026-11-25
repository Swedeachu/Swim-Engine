"""Summarizes one Invoke-EngineProfile.ps1 run (and compares it with another).

Reads <run>/frames.csv (the engine's `profile` CSV) and <run>/cpu.txt (SwimStackSampler)
and writes <run>/summary.md: frame time, the costliest GPU passes and CPU zones, and the
functions with the most CPU samples. With --compare <other run> every row also shows the
other run's value and the difference.

usage: python Summarize-EngineProfile.py <run dir> [--compare <run dir>] [--top 15]
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


def load_frames(run: Path) -> dict[tuple[str, str], dict[str, float]]:
    path = run / "frames.csv"
    rows: dict[tuple[str, str], dict[str, float]] = {}
    if not path.exists():
        return rows
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            values = {key: float(value) for key, value in row.items() if key.endswith("_ms") or key == "frames"}
            rows[(row["category"], row["name"])] = values
    return rows


def load_sampler(run: Path) -> dict[str, dict[str, float]]:
    path = run / "cpu.txt"
    tables: dict[str, dict[str, float]] = {"thread": {}, "exclusive": {}, "inclusive": {}}
    if not path.exists():
        return tables
    current = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CPU share per thread"):
            current = "thread"
        elif line.startswith("Top functions by exclusive"):
            current = "exclusive"
        elif line.startswith("Top functions by inclusive"):
            current = "inclusive"
        elif current and (match := re.match(r"\s+([\d.]+) %\s+(.*)", line)):
            tables[current][match.group(2)] = float(match.group(1))
    return tables


def short(name: str, width: int = 110) -> str:
    return name if len(name) <= width else name[: width - 3] + "..."


def table(title: str, rows: list[tuple[str, float]], other: dict[str, float] | None, unit: str) -> list[str]:
    lines = [f"### {title}", ""]
    if other is None:
        lines += [f"| {unit} | Name |", "| ---: | --- |"]
        lines += [f"| {value:.3f} | `{short(name)}` |" for name, value in rows]
    else:
        lines += [f"| {unit} | Before | Change | Name |", "| ---: | ---: | ---: | --- |"]
        for name, value in rows:
            before = other.get(name)
            change = f"{value - before:+.3f}" if before is not None else "new"
            lines.append(f"| {value:.3f} | {before if before is not None else '-'} | {change} | `{short(name)}` |")
    return lines + [""]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--top", type=int, default=15)
    args = parser.parse_args()

    frames = load_frames(args.run)
    sampler = load_sampler(args.run)
    other_frames = load_frames(args.compare) if args.compare else None
    other_sampler = load_sampler(args.compare) if args.compare else None

    def by_category(rows, category):
        return {name: values["per_frame_ms"] for (cat, name), values in rows.items() if cat == category}

    out = [f"# Profile: {args.run.name}", ""]
    check = args.run / "process-check.txt"
    if check.exists():
        out += ["```text", check.read_text(encoding="utf-8").strip(), "```", ""]

    frame_rows = [(key[1], values) for key, values in frames.items() if key[0] == "frame"]
    if frame_rows:
        out += ["### Frame", "", "| Zone | Mean ms | p50 | p95 | p99 | Before (mean) |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
        for name, values in sorted(frame_rows):
            before = other_frames.get(("frame", name), {}).get("mean_ms") if other_frames else None
            out.append(
                f"| {name} | {values['mean_ms']:.3f} | {values['p50_ms']:.3f} | {values['p95_ms']:.3f} | {values['p99_ms']:.3f} | "
                f"{'-' if before is None else f'{before:.3f}'} |"
            )
        out.append("")

    for category, title in (("gpu", "GPU passes (ms per frame)"), ("cpu", "CPU zones (ms per frame)"), ("render", "Renderer CPU phases (ms per frame)")):
        rows = sorted(by_category(frames, category).items(), key=lambda item: -item[1])[: args.top]
        if rows:
            out += table(title, rows, by_category(other_frames, category) if other_frames is not None else None, "ms")

    for key, title in (("thread", "CPU share per thread (%)"), ("exclusive", "Functions by exclusive CPU share (%)"), ("inclusive", "Functions by inclusive CPU share (%)")):
        rows = sorted(sampler[key].items(), key=lambda item: -item[1])[: args.top]
        if rows:
            out += table(title, rows, other_sampler[key] if other_sampler is not None else None, "%")

    summary = args.run / "summary.md"
    summary.write_text("\n".join(out), encoding="utf-8")
    print("\n".join(out))
    print(f"\nWrote {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
