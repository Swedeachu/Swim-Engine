"""Ranks source files by line count, optionally the longest functions too, and compares runs.

usage:
  python tools/line_counts.py [roots...] [--top 30] [--over 1000] [--ext .cpp .h]
                              [--functions 100] [--save counts.json] [--compare counts.json]

Roots default to Source/. --functions lists C/C++ function bodies longer than the given number
of lines (a brace-matching heuristic: good enough to rank, not a parser). --save writes the
counts as JSON; --compare prints each file's previous count and the difference.
"""

import argparse
import json
import os
import re
import sys

DEFAULT_EXTENSIONS = [".cpp", ".h", ".hpp", ".c", ".cc", ".inl", ".slang", ".hlsl"]
SKIP_DIRS = {".git", "build", "out", ".cache", "node_modules", "third_party", "external", "vendor"}
SIGNATURE = re.compile(r"\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(final\s*)?(->\s*[\w:<>,\s*&]+)?\s*$")
CONTROL = re.compile(r"^\s*(if|for|while|switch|catch|else)\b")


def source_files(roots, extensions):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for directory, subdirs, files in os.walk(root):
            subdirs[:] = [d for d in subdirs if d not in SKIP_DIRS]
            for name in files:
                if os.path.splitext(name)[1].lower() in extensions:
                    yield os.path.join(directory, name)


def read_lines(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read().splitlines()


def long_functions(lines, limit):
    """Yields (first line, length, signature) for brace blocks that look like function bodies."""
    depth = 0
    start = None
    signature = ""
    previous = ""
    for number, line in enumerate(lines, 1):
        code = line.split("//", 1)[0]
        for char in code:
            if char == "{":
                if start is None:
                    head = (previous + " " + code.split("{", 1)[0]).strip()
                    if SIGNATURE.search(head) and not CONTROL.match(head) and "=" not in head.split("(", 1)[0]:
                        start, signature, base = number, head[-120:], depth
                depth += 1
            elif char == "}":
                depth -= 1
                if start is not None and depth == base:
                    length = number - start + 1
                    if length > limit:
                        yield start, length, signature
                    start = None
        if code.strip():
            previous = code.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("roots", nargs="*", default=["Source"])
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--over", type=int, default=0, help="only files longer than this")
    parser.add_argument("--ext", nargs="*", default=DEFAULT_EXTENSIONS)
    parser.add_argument("--functions", type=int, default=0, help="list functions longer than this")
    parser.add_argument("--save")
    parser.add_argument("--compare")
    args = parser.parse_args()

    extensions = {e.lower() if e.startswith(".") else "." + e.lower() for e in args.ext}
    counts = {}
    functions = []
    for path in source_files(args.roots, extensions):
        lines = read_lines(path)
        key = os.path.relpath(path).replace("\\", "/")
        counts[key] = len(lines)
        if args.functions:
            functions += [(length, key, first, signature) for first, length, signature in long_functions(lines, args.functions)]

    previous = {}
    if args.compare:
        with open(args.compare, encoding="utf-8") as handle:
            previous = json.load(handle)

    ranked = sorted(counts.items(), key=lambda item: -item[1])
    shown = [item for item in ranked if item[1] > args.over][: args.top]
    print(f"{len(counts)} files, {sum(counts.values())} lines")
    for path, lines in shown:
        if previous:
            before = previous.get(path)
            delta = "" if before is None else f"  (was {before}, {lines - before:+d})"
            print(f"{lines:7d}  {path}{delta}")
        else:
            print(f"{lines:7d}  {path}")

    if previous:
        gone = [path for path in previous if path not in counts and previous[path] > args.over]
        for path in sorted(gone, key=lambda p: -previous[p])[: args.top]:
            print(f"      -  {path}  (was {previous[path]}, removed)")

    if args.functions:
        print(f"\nfunctions over {args.functions} lines: {len(functions)}")
        for length, path, first, signature in sorted(functions, reverse=True)[: args.top]:
            print(f"{length:7d}  {path}:{first}  {signature}")

    if args.save:
        with open(args.save, "w", encoding="utf-8") as handle:
            json.dump(counts, handle, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
