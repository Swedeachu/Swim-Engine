"""Blank-line spacing around C++ control flow and inside declaration scopes."""

from __future__ import annotations

import bisect
import re


LITERALS = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/'
    r'|(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"'
    r"|'(?:\\.|[^'\\\n])*'"
)
HEADERS = re.compile(
    r"(?m)^[ \t]*(?P<kind>inline[ \t]+namespace|namespace|class|struct|union|"
    r"if|for|while|switch|do|try|catch|else)\b"
)
COMMENT = re.compile(r"^\s*(?://|/\*|\*(?:/|\s))")
CONDITIONS = {"if", "for", "while", "switch", "catch"}
SCOPES = {"namespace", "class", "struct", "union"}
MEMBER_SCOPES = {"class", "struct", "union"}
ACCESS = re.compile(r"(?:public|private|protected)[ \t]*:")
MEMBER_NAME = re.compile(r"(~?[A-Za-z_]\w*)[ \t]*$")
OPERATOR_NAME = re.compile(r"\boperator[ \t]*(?:\(\)|\[\]|[^\s(]+)[ \t]*$")
NON_METHOD_NAMES = {"noexcept", "decltype", "sizeof", "alignof", "requires", "static_assert"}
BUILTIN_TYPES = {"bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void"}


def _mask(source: str) -> str:
    masked = LITERALS.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), source)
    return re.sub(
        r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*",
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        masked,
        flags=re.MULTILINE,
    )


def _skip_space(source: str, position: int) -> int:
    while position < len(source) and source[position].isspace():
        position += 1
    return position


def _opening(source: str, match: re.Match[str]) -> tuple[int | None, str]:
    kind = match.group("kind")
    if kind.startswith("inline"):
        kind = "namespace"
    position = _skip_space(source, match.end())

    if kind == "else" and re.match(r"if\b", source[position:]):
        kind = "else if"
        position = _skip_space(source, position + 2)

    if kind in CONDITIONS or kind == "else if":
        if kind in {"if", "else if"}:
            qualifier = re.match(r"(?:constexpr|consteval)\b", source[position:])
            if qualifier:
                position = _skip_space(source, position + qualifier.end())
        if source[position:position + 1] == "{":  # if consteval
            return position, kind
        if source[position:position + 1] != "(":
            return None, kind
        depth = 0
        for index in range(position, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    position = _skip_space(source, index + 1)
                    break
        else:
            return None, kind
        return (position if source[position:position + 1] == "{" else None), kind

    if kind in {"do", "try", "else"}:
        return (position if source[position:position + 1] == "{" else None), kind

    boundary = re.search(r"[{;]", source[position:])
    if boundary is None or boundary.group() == ";":
        return None, kind
    return position + boundary.start(), kind


def _method_member(header: str, original: str, *, definition: bool = False) -> bool:
    """Recognize member functions conservatively; direct-initialized fields stay grouped."""
    header = header.strip()
    if not header or re.match(r"(?:using|typedef|static_assert|class|struct|union|enum)\b", header):
        return False

    pairs: list[tuple[int, int]] = []
    stack: list[int] = []
    for index, token in enumerate(header):
        if token == "(":
            stack.append(index)
        elif token == ")" and stack:
            start = stack.pop()
            if not stack:
                pairs.append((start, index))

    for start, end in reversed(pairs):
        prefix = header[:start].rstrip()
        suffix = header[end + 1:].strip()
        name = MEMBER_NAME.search(prefix)
        if not name and not OPERATOR_NAME.search(prefix):
            continue
        if name and name.group(1) in NON_METHOD_NAMES:
            continue
        if "=" in prefix and not re.search(r"\boperator\b", prefix):
            continue
        if suffix and not re.match(
            r"(?:const|volatile|noexcept|override|final|requires|throw|->|&|\[\[|=\s*(?:0|default|delete)\b|:)",
            suffix,
        ):
            continue

        arguments = header[start + 1:end].strip()
        if not arguments:
            if original[start + 1:end].strip():
                continue
            return True
        if re.match(r"(?:\d|[\"']|nullptr\b|true\b|false\b|this\b|\{|\[)", arguments):
            continue
        if re.search(r"\bstd::(?:move|forward)\s*\(", arguments):
            continue
        if re.fullmatch(r"[a-z_]\w*", arguments) and arguments not in BUILTIN_TYPES and not arguments.endswith("_t"):
            continue
        if all(re.fullmatch(r"[a-z_]\w*", argument.strip()) for argument in arguments.split(",")):
            continue
        if definition and re.search(r"\)\s*:\s*", header):
            return True
        return True
    return False


def _member_spacing(
    source: str,
    masked: str,
    opening: int,
    closing: int,
    paired: dict[int, int],
    line_at,
    insert_before: set[int],
) -> None:
    cursor = opening + 1
    while cursor < closing:
        cursor = _skip_space(masked, cursor)
        if cursor >= closing:
            break
        if masked[cursor] == ";":
            cursor += 1
            continue

        access = ACCESS.match(masked, cursor)
        if access:
            insert_before.add(line_at(access.end() - 1) + 1)
            cursor = access.end()
            continue

        start = cursor
        parentheses = 0
        brackets = 0
        while cursor < closing:
            token = masked[cursor]
            if token == "(":
                parentheses += 1
            elif token == ")":
                parentheses -= 1
            elif token == "[":
                brackets += 1
            elif token == "]":
                brackets -= 1
            elif parentheses == 0 and brackets == 0 and token in ";{":
                header = masked[start:cursor]
                original = source[start:cursor]
                if token == ";":
                    if _method_member(header, original):
                        insert_before.add(line_at(cursor) + 1)
                    cursor += 1
                else:
                    end = paired.get(cursor)
                    if end is None or end > closing:
                        return
                    if _method_member(header, original, definition=True):
                        insert_before.add(line_at(end) + 1)
                    cursor = end + 1
                break
            cursor += 1


def cpp_spacing(source: str) -> str:
    """Apply one blank line at structural boundaries without changing tokens."""
    if not source:
        return source

    masked = _mask(source)
    lines = source.splitlines(keepends=True)
    masked_lines = masked.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))

    def line_at(position: int) -> int:
        return bisect.bisect_right(offsets, position) - 1

    paired: dict[int, int] = {}
    stack: list[int] = []
    for brace in re.finditer(r"[{}]", masked):
        if brace.group() == "{":
            stack.append(brace.start())
        elif stack:
            paired[stack.pop()] = brace.start()

    blocks: list[tuple[int, int, int, str]] = []
    member_scopes: list[tuple[int, int]] = []
    for header in HEADERS.finditer(masked):
        opening, kind = _opening(masked, header)
        if opening is None or opening not in paired:
            continue
        start, first, last = line_at(header.start()), line_at(opening), line_at(paired[opening])
        if first >= last or masked_lines[first].strip() != "{":
            continue
        if masked_lines[last].strip() not in {"}", "};"}:
            continue
        blocks.append((start, first, last, kind))
        if kind in MEMBER_SCOPES:
            member_scopes.append((opening, paired[opening]))

    insert_before: set[int] = set()
    do_closings = {last for _, _, last, kind in blocks if kind == "do"}
    for start, first, last, kind in blocks:
        if kind in SCOPES:
            if kind != "struct" and first + 1 < last:
                insert_before.add(first + 1)
                insert_before.add(last)
            continue

        previous = lines[start - 1].strip() if start else ""
        following = lines[last + 1].strip() if last + 1 < len(lines) else ""
        continuation = kind in {"else", "else if", "catch"} or (
            kind == "while" and start - 1 in do_closings
        )
        if previous and previous != "{" and not COMMENT.match(previous) and not continuation:
            insert_before.add(start)
        if following and following not in {"}", "};"} and not re.match(r"(?:else|catch)\b", following):
            if not (kind == "do" and re.match(r"while\b", following)):
                insert_before.add(last + 1)

    for opening, closing in member_scopes:
        _member_spacing(source, masked, opening, closing, paired, line_at, insert_before)

    remove_blank_indices: set[int] = set()
    for _, first, last, kind in blocks:
        if kind != "struct":
            continue
        insert_before.discard(last)
        index = first + 1
        while index < last and not lines[index].strip():
            remove_blank_indices.add(index)
            index += 1
        index = last - 1
        while index > first and not lines[index].strip():
            remove_blank_indices.add(index)
            index -= 1

    output: list[str] = []
    for index, line in enumerate(lines):
        if index in remove_blank_indices:
            continue
        if index in insert_before and line.strip() and output and output[-1].strip():
            output.append("\n")
        output.append(line)
    return "".join(output)
