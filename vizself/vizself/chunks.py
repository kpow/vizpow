"""Cut source files and docs into pieces small enough to search and read: whole
functions where possible, never more than MAX_LINES."""

from __future__ import annotations

import re
from dataclasses import dataclass

MIN_LINES = 12     # top-level pieces smaller than this merge with their neighbours
MAX_LINES = 70     # bigger pieces are split, preferring a blank line before a definition
OVERLAP = 6

_LINE_COMMENT = re.compile(r"//.*$")
_STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
_PY_DEF = re.compile(r"^\s*(?:async\s+def|def|class)\s")
_CPP_DEF = re.compile(r"^[A-Za-z_][\w:<>,\s\*&]*\([^;]*$|^(?:struct|class|enum|namespace)\b|^#define\s+\w+\(")


@dataclass
class Chunk:
    path: str
    start: int       # 1-based, inclusive
    end: int
    text: str
    kind: str        # "code", "map" (self-map / hardware section) or "doc"
    title: str = ""  # heading for map/doc pieces


def chunk_file(path: str, text: str) -> list[Chunk]:
    if path.endswith(".md"):
        return [Chunk(path, s, e, t, "doc", title) for title, s, e, t in md_sections(text)]
    lines = text.splitlines()
    cuts = _py_cuts(lines) if path.endswith(".py") else _cpp_cuts(lines)
    pieces = _merge(_segments(cuts, len(lines)))
    out = []
    for s, e in pieces:
        for ws, we in _windows(lines, s, e):
            body = "\n".join(lines[ws:we])
            if body.strip():
                out.append(Chunk(path, ws + 1, we, body, "code"))
    return out


def md_sections(text: str, level: str = "## ") -> list[tuple[str, int, int, str]]:
    """(heading, start, end, text) per `## ` section; text before the first one is "(top)"."""
    lines = text.splitlines()
    heads = [i for i, l in enumerate(lines) if l.startswith(level)]
    bounds = ([0] if not heads or heads[0] > 0 else []) + heads + [len(lines)]
    out = []
    for s, e in zip(bounds, bounds[1:]):
        body = "\n".join(lines[s:e]).strip()
        if body:
            title = lines[s][len(level):].strip() if lines[s].startswith(level) else "(top)"
            out.append((title, s + 1, e, body))
    return out


def _cpp_cuts(lines: list[str]) -> list[int]:
    """Line indexes where a top-level C++ piece starts (brace depth 0, after a blank line or a close)."""
    cuts, depth, in_block = [0], 0, False
    for i, raw in enumerate(lines):
        line = raw
        if in_block:
            if "*/" not in line:
                continue
            line, in_block = line.split("*/", 1)[1], False
        line = _LINE_COMMENT.sub("", _STRING.sub('""', line))
        while "/*" in line:
            head, _, rest = line.partition("/*")
            if "*/" in rest:
                line = head + rest.split("*/", 1)[1]
            else:
                line, in_block = head, True
        if depth == 0 and i > 0 and raw.strip() and not lines[i - 1].strip():
            cuts.append(i)
        depth = max(0, depth + line.count("{") - line.count("}"))
    return cuts


def _py_cuts(lines: list[str]) -> list[int]:
    cuts = [0]
    for i, line in enumerate(lines):
        if i and line and not line[0].isspace() and not lines[i - 1].strip():
            cuts.append(i)
    return cuts


def _segments(cuts: list[int], n: int) -> list[tuple[int, int]]:
    edges = sorted(set(cuts)) + [n]
    return [(s, e) for s, e in zip(edges, edges[1:]) if e > s]


def _merge(segs: list[tuple[int, int]]) -> list[tuple[int, int]]:
    out: list[list[int]] = []
    for s, e in segs:
        if out and (out[-1][1] - out[-1][0] < MIN_LINES or e - s < MIN_LINES) \
                and e - out[-1][0] <= MAX_LINES:
            out[-1][1] = e
        else:
            out.append([s, e])
    return [(s, e) for s, e in out]


def _windows(lines: list[str], s: int, e: int) -> list[tuple[int, int]]:
    """Split [s, e) into pieces of at most MAX_LINES, cutting before a definition when one is near."""
    out = []
    while e - s > MAX_LINES:
        cut = s + MAX_LINES
        for j in range(s + MAX_LINES, s + MAX_LINES // 2, -1):
            if not lines[j - 1].strip() and (_PY_DEF.match(lines[j]) or _CPP_DEF.match(lines[j].strip())):
                cut = j
                break
        out.append((s, cut))
        s = max(s + 1, cut - OVERLAP)
    out.append((s, e))
    return out
