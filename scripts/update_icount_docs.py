#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Regenerate an engine README's instruction-count table from its baselines.

Usage: scripts/update_icount_docs.py [--engine async|bridge|rational]

Each engine's table lives between the ICOUNT markers in <engine>/README.md
and is generated from <engine>/bench/baselines.json. Run from the
repository root.

The table derives 1:1 from the committed baselines, so CI regenerates it and
fails on any diff — the published numbers cannot drift from the gated ones
(docs/PERFORMANCE.md, "Docs freshness").
"""
import argparse
import json
import pathlib
import re
import sys

BEGIN, END = "<!-- ICOUNT:BEGIN -->", "<!-- ICOUNT:END -->"
TARGET_NAMES = {"m33": "Cortex-M33", "m55": "Cortex-M55", "hexagon": "Hexagon"}


def table(baselines: dict, engine: str) -> str:
    targets = [t for t in ("m33", "m55", "hexagon") if baselines.get(t)]
    scenarios = sorted({s for t in targets for s in baselines[t]})
    lines = [
        f"Executed instructions per fixed workload (`{engine}/bench/icount/`), "
        "measured under QEMU with a counting plugin — deterministic, and gated "
        f"in CI at ±3% against `{engine}/bench/baselines.json`. A `callmax_` row "
        "is the worst single call instead of a total: the most instructions one "
        "call executes over that stream (`bench/icount/callmax_main.cpp`):",
        "",
        "| Workload | " + " | ".join(TARGET_NAMES[t] for t in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
    for s in scenarios:
        cells = " | ".join(f"{baselines[t][s]:,}" if s in baselines[t] else "—"
                           for t in targets)
        lines.append(f"| `{s}` | {cells} |")
    host = baselines.get("host")
    if host:
        lines += [
            "",
            "On the host, the float converter in 128-frame blocks at 2 and 12 channels "
            f"(`{engine}/bench/icount/host_main.cpp`): instructions executed by the "
            "streaming loop alone, counted under callgrind on x86-64 with GCC 13 "
            "(`scripts/icount.py --target host`), gated the same way:",
            "",
            "| Workload | x86-64 |",
            "|---|---:|",
        ]
        lines += [f"| `{s}` | {host[s]:,} |" for s in sorted(host)]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", choices=["async", "bridge", "rational"], default="async")
    args = ap.parse_args()
    readme = pathlib.Path(args.engine) / "README.md"
    baselines = json.loads((pathlib.Path(args.engine) / "bench" / "baselines.json").read_text())
    text = readme.read_text()
    if BEGIN not in text or END not in text:
        print(f"markers not found in {readme}", file=sys.stderr)
        return 1
    block = f"{BEGIN}\n{table(baselines, args.engine)}\n{END}"
    readme.write_text(re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), block,
                             text, flags=re.S))
    print(f"updated {readme}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
