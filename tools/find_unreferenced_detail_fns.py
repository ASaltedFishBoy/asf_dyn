#!/usr/bin/env python3
"""Find internal (include/detail) functions with no references in include/ or tests/."""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DETAIL = ROOT / "include" / "detail"
SCAN_DIRS = [ROOT / "include", ROOT / "tests"]

# Line starts a plausible function definition (free or static member).
DEF_LINE = re.compile(
    r"^\s*(?:template\s*<[^;{]*>\s*)?"
    r"(?:static\s+|inline\s+|constexpr\s+|consteval\s+|friend\s+|virtual\s+)*"
    r"(?:explicit\s+)?"
    r"(?:[A-Za-z_][\w:<>,\s*&]*\s+)"  # return type must start with id char
    r"(?P<name>\w+)\s*"
    r"(?:<[^>]*>)?\s*"
    r"(?:\([^)]*\)\s*(?:const|noexcept|\s)*[;{]|=\s*default|=\s*delete)",
)

# Forward declaration only (no body, ends with ;)
FWD_LINE = re.compile(
    r"^\s*(?:template\s*<[^;{]*>\s*)?"
    r"(?:static\s+|inline\s+|constexpr\s+|consteval\s+)*"
    r"(?:[A-Za-z_][\w:<>,\s*&]*\s+)"
    r"(?P<name>\w+)\s*"
    r"(?:<[^>]*>)?\s*\([^)]*\)\s*;\s*$",
)

SKIP_NAMES = frozenset(
    {
        "if",
        "for",
        "while",
        "switch",
        "catch",
        "return",
        "operator",
        "concept",
        "requires",
        "sizeof",
        "decltype",
        "type",
        "value",
        "get",
        "set",
        "at",
        "end",
        "begin",
    }
)


@dataclass(frozen=True)
class DefSite:
    name: str
    path: Path
    line: int
    text: str


def iter_source_files() -> list[Path]:
    out: list[Path] = []
    for base in SCAN_DIRS:
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix in (".hpp", ".h", ".cpp", ".inc"):
                out.append(path)
    return out


def is_def_line(line: str, name: str) -> bool:
    stripped = line.strip()
    # Template call / invocation, not a definition.
    if re.search(rf"(?:^|\W){re.escape(name)}\s*<", stripped):
        if re.search(rf"\b{re.escape(name)}\s*<[^>]*>\s*\(", stripped):
            if "{" not in stripped and not re.search(r"=\s*default\b|=\s*delete\b", stripped):
                return False
    if re.search(rf"\breturn\b.*\b{re.escape(name)}\b", stripped):
        return False
    m = DEF_LINE.match(line) or FWD_LINE.match(line)
    return m is not None and m.group("name") == name


def collect_detail_definitions() -> list[DefSite]:
    defs: list[DefSite] = []
    for path in sorted(DETAIL.glob("*.hpp")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines, 1):
            m = DEF_LINE.match(line)
            if not m:
                m = FWD_LINE.match(line)
            if not m:
                continue
            name = m.group("name")
            if name in SKIP_NAMES:
                continue
            if name.endswith("_t") or name.endswith("_v"):
                continue
            if not is_def_line(line, name):
                continue
            defs.append(DefSite(name, path, i, line.strip()))
    return defs


def find_mentions(name: str) -> list[tuple[Path, int, str]]:
    word = re.compile(rf"\b{re.escape(name)}\b")
    meta = re.compile(rf"\^\^{re.escape(name)}\b")
    splice = re.compile(rf"\[:{re.escape(name)}:\]")
    mentions: list[tuple[Path, int, str]] = []
    for path in iter_source_files():
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines, 1):
            if word.search(line) or meta.search(line) or splice.search(line):
                mentions.append((path, i, line))
    return mentions


def main() -> int:
    defs = collect_detail_definitions()
    by_name: dict[str, list[DefSite]] = {}
    for d in defs:
        by_name.setdefault(d.name, []).append(d)

    unreferenced: list[DefSite] = []
    used_only_internally: list[tuple[str, int, int]] = []

    for name in sorted(by_name):
        sites = by_name[name]
        mentions = find_mentions(name)
        def_keys = {(d.path.resolve(), d.line) for d in sites}
        use_lines = [
            (p, ln, txt)
            for p, ln, txt in mentions
            if (p.resolve(), ln) not in def_keys and not is_def_line(txt, name)
        ]
        # Also count other def lines of overloads as "internal" not use
        non_def_mentions = len(use_lines)
        if non_def_mentions == 0:
            for d in sites:
                unreferenced.append(d)
        elif non_def_mentions <= 2:
            # flag weak: only 1-2 uses (often tests-only or single callsite)
            used_only_internally.append((name, non_def_mentions, len(sites)))

    print(f"Detail definitions scanned: {len(defs)} sites, {len(by_name)} names\n")

    print("=== No references outside definition lines ===")
    if not unreferenced:
        print("(none)")
    else:
        # group by name
        shown: set[str] = set()
        for d in unreferenced:
            if d.name in shown:
                continue
            shown.add(d.name)
            sites = by_name[d.name]
            print(f"\n  {d.name} ({len(sites)} definition(s)):")
            for s in sites:
                rel = s.path.relative_to(ROOT)
                print(f"    {rel}:{s.line}  {s.text[:100]}")

    print(f"\n=== Summary ===")
    print(f"Unreferenced function names: {len({d.name for d in unreferenced})}")
    print(f"Unreferenced definition sites: {len(unreferenced)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
