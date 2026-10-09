#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""The vocabulary gate: no retired word in the tree (scripts/retired_words.txt).

Scans every git-tracked text file. Notebooks are scanned by source cell
(their executed outputs are a record, not prose). Lines that carry a
retired word in an allowed sense are listed in ALLOWED below, as regexes
matched against the whole line; the files in HISTORY are records and are
skipped entirely. Exit 1 on any hit, with file:line.

    python3 scripts/check_retired_words.py            # the whole tree
    python3 scripts/check_retired_words.py path ...   # a subset
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORDS = ROOT / "scripts" / "retired_words.txt"

HISTORY = (
    "bridge/docs/HISTORY.md",
    "bridge/HANDOFF.md",
    "docs/MIGRATION_RUNS.md",
    "docs/AUDIT_2026-10.md",
    "docs/AUDIT_2026-10_PLAN.md",
    "PLAN.md",
    "async/PLAN.md",
    "bridge/PLAN.md",
    "rational/PLAN.md",
    "scripts/retired_words.txt",
    "scripts/check_retired_words.py",
    ".git-blame-ignore-revs",
)
ALLOWED = [
    r"formerly RatioTap",
    r"RatioTap v0\.3\.0",                 # the merge sentence in the root README and CLAUDE.md
    r"maps RatioTap's commits",           # the HISTORY.md pointer
    r"r8brain|CDSPResampler|136 dB preset|24-bit preset|16-bit preset",  # another library's own presets
    r"VISIBILITY_PRESET|visibility preset",
    r"`preset` retired|preset retires|retired name|preset\b.*retired",  # the retirement itself, stated
    r"set_title\(|annotate\(",            # rendered figure text in scripts/book_figures.py until the figures are regenerated
]
TEXT_SUFFIXES = {".md", ".h", ".hpp", ".c", ".cpp", ".py", ".yml", ".yaml", ".txt", ".cmake", ".sh", ".ipynb", ".in", ".json"}


def tracked_files(paths):
    out = subprocess.run(["git", "ls-files", "-z", *paths], cwd=ROOT, capture_output=True, check=True).stdout
    return [ROOT / p for p in out.decode().split("\0") if p]


def load_words():
    words = []
    for raw in WORDS.read_text().splitlines():
        line = raw.split("  #", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        words.append(re.compile(line))
    return words


def lines_of(path):
    if path.suffix == ".ipynb":
        try:
            nb = json.loads(path.read_text())
        except (ValueError, UnicodeDecodeError):
            return []
        out = []
        for ci, cell in enumerate(nb.get("cells", [])):
            for li, line in enumerate("".join(cell.get("source", [])).splitlines()):
                out.append((f"cell {ci} line {li + 1}", line))
        return out
    try:
        text = path.read_text()
    except UnicodeDecodeError:
        return []
    return [(str(i + 1), line) for i, line in enumerate(text.splitlines())]


def main(argv):
    words = load_words()
    allowed = [re.compile(a) for a in ALLOWED]
    hits = 0
    for path in tracked_files(argv or ["."]):
        rel = path.relative_to(ROOT).as_posix()
        if rel in HISTORY or rel.startswith("submodules/") or path.suffix not in TEXT_SUFFIXES:
            continue
        if path.name == "CMakeLists.txt" or path.suffix in TEXT_SUFFIXES:
            for where, line in lines_of(path):
                for w in words:
                    if w.search(line) and not any(a.search(line) for a in allowed):
                        print(f"{rel}:{where}: retired word {w.pattern!r}: {line.strip()[:120]}")
                        hits += 1
    if hits:
        print(f"{hits} hit(s); the family's vocabulary is in scripts/retired_words.txt", file=sys.stderr)
        return 1
    print("no retired word in the tree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
