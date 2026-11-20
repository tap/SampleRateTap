#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Spike X-0 (X3, go/no-go 3): the x8 instruction counts side by side.

Reads icount/<target>[_noexc].txt (run_icount.py's tables) and writes
icount/summary.txt: per target and format, the instructions per output frame
of each chain, their ratio to X3's chain, and the instructions per MAC (two
channels; MACs per output from the matrix, economy, today's designer)."""
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
TARGETS = ["m33", "m55", "hexagon"]
# MAC per output frame per channel at economy (generator/by.txt, programs/costs.txt).
MACS = {"↓4 · ↓2": 85, "↓3 · 3/4 · ↓2": 105, "↓2 · ↓2 · ↓2": 113, "3/8 · ↓3": 124,
        "↑2 · ↑4": 10.625, "↑2 · 4/3 · ↑3": 12.4583, "↑2 · ↑2 · ↑2": 14.125, "↑3 · 8/3": 14.5}
X3 = {"down": "↓4 · ↓2", "up": "↑2 · ↑4"}


def load(name):
    rows = {}
    path = HERE / f"{name}.txt"
    if not path.exists():
        return None
    for line in path.read_text().splitlines():
        m = re.match(r"\| (.+?)(?: \(.*\))? \| (float|q15) \| ([\d.]+) \|", line)
        if m:
            rows[(m.group(1), m.group(2))] = float(m.group(3))
    return rows


def main():
    out = ["# x8 chains at economy: instructions per output frame (two channels), from icount/<target>.txt",
           "# ratio: against X3's chain (↓4 · ↓2 down, ↑2 · ↑4 up); per MAC: per output frame / (MACs per output x 2)",
           ""]
    for suffix, title in (("", "exceptions on (the ratchet's first build)"),
                          ("_noexc", "TAP_SR_NO_EXCEPTIONS_RTTI=ON (the ratchet's second build)")):
        data = {t: load(t + suffix) for t in TARGETS}
        if not all(data.values()):
            continue
        out.append(f"## {title}")
        out.append("| chain | format | " + " | ".join(f"{t} insns (ratio, per MAC)" for t in TARGETS) + " |")
        out.append("|---|---|" + "---:|" * len(TARGETS))
        for fmt in ("float", "q15"):
            for direction in ("down", "up"):
                for chain in MACS:
                    if (chain[0] == "↓" or chain.startswith("3/8")) != (direction == "down"):
                        continue
                    cells = []
                    for t in TARGETS:
                        v = data[t][(chain, fmt)]
                        ref = data[t][(X3[direction], fmt)]
                        cells.append(f"{v:.1f} ({v / ref:.3f}, {v / (MACS[chain] * 2):.2f})")
                    out.append(f"| {chain} | {fmt} | " + " | ".join(cells) + " |")
        out.append("")
        # go/no-go 3: X3's chain against the 48 family's matrix chain, per target and format.
        out.append("go/no-go 3 (X3's chain against the 48 family's matrix chain; ratio > 1: X3's chain counts fewer):")
        for direction, today in (("down", "↓3 · 3/4 · ↓2"), ("up", "↑2 · 4/3 · ↑3")):
            for fmt in ("float", "q15"):
                parts = []
                for t in TARGETS:
                    v, ref = data[t][(today, fmt)], data[t][(X3[direction], fmt)]
                    parts.append(f"{t} {v / ref:.3f} ({'X3 fewer' if ref < v else 'X3 NOT fewer'})")
                out.append(f"  {direction} {fmt}: " + ", ".join(parts))
        out.append("The 44.1 family's matrix chain against X3's (X6: the rows X3 changes in that family):")
        for direction, today in (("down", "3/8 · ↓3"), ("up", "↑3 · 8/3")):
            for fmt in ("float", "q15"):
                parts = []
                for t in TARGETS:
                    v, ref = data[t][(today, fmt)], data[t][(X3[direction], fmt)]
                    parts.append(f"{t} {v / ref:.3f} ({'X3 fewer' if ref < v else 'X3 NOT fewer'})")
                out.append(f"  {direction} {fmt}: " + ", ".join(parts))
        out.append("")
    (HERE / "summary.txt").write_text("\n".join(out) + "\n")
    print("\n".join(out))


if __name__ == "__main__":
    main()
