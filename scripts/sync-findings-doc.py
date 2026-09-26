#!/usr/bin/env python3
"""Copies the sandbox findings (Source/Game/Findings.cpp) into docs/EngineRuntime.md.

The Findings tab in the sandbox and the documentation list the same items; run this
after editing Findings.cpp (the build-layout verifier checks that they match).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Source" / "Game" / "Findings.cpp"
DOC = ROOT / "docs" / "EngineRuntime.md"
BEGIN = "<!-- findings:begin -->"
END = "<!-- findings:end -->"


def parse_findings(text: str) -> list[tuple[str, str, str]]:
    body = text.split("Items{ {", 1)[1].split("} };", 1)[0]
    findings = []
    for entry in re.findall(r"\{\s*((?:\"(?:[^\"\\]|\\.)*\"\s*,?\s*)+)\}", body):
        strings = re.findall(r"\"((?:[^\"\\]|\\.)*)\"\s*(,?)", entry)
        fields, current = [], ""
        for value, comma in strings:
            current += value
            if comma:
                fields.append(current)
                current = ""
        fields.append(current)
        if len(fields) == 3:
            findings.append(tuple(field.replace('\\"', '"') for field in fields))
    return findings


def render(findings: list[tuple[str, str, str]]) -> str:
    lines = [BEGIN, "", "| Status | Finding | Detail |", "| --- | --- | --- |"]
    for title, detail, status in findings:
        escaped = detail.replace("|", "\\|")
        lines.append(f"| {status} | {title} | {escaped} |")
    lines += ["", END]
    return "\n".join(lines)


def main() -> int:
    findings = parse_findings(SOURCE.read_text(encoding="utf-8"))
    doc = DOC.read_text(encoding="utf-8")
    start, end = doc.index(BEGIN), doc.index(END) + len(END)
    updated = doc[:start] + render(findings) + doc[end:]
    if "--check" in sys.argv:
        if updated != doc:
            print("docs/EngineRuntime.md findings are out of date; run scripts/sync-findings-doc.py")
            return 1
        return 0
    DOC.write_text(updated, encoding="utf-8")
    print(f"Wrote {len(findings)} findings to {DOC.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
