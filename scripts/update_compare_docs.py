#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Regenerate COMPARISON.md's embedded instruction-count tables.

Usage: scripts/update_compare_docs.py   (from the repository root)

The block between the COMPARE markers in async/docs/COMPARISON.md derives
from async/bench/compare_counts.json, the record scripts/harvest_compare.py
writes from a compare.yml run. Every engine is measured at 2 s and 4 s of
audio: the difference is the steady-state cost per output frame, and
2 * (2 s) - (4 s) is the one-time construction. CI regenerates the block and
fails on any diff (ci.yml, compare-docs), so the published numbers cannot
drift from the measured ones when a pin moves.
"""
import json
import pathlib
import re
import sys
import textwrap

BEGIN, END = "<!-- COMPARE:BEGIN -->", "<!-- COMPARE:END -->"
DOC = pathlib.Path("async/docs/COMPARISON.md")
COUNTS = pathlib.Path("async/bench/compare_counts.json")
FRAMES = 2 * 48000  # the 2 s workload's output frames at 48 kHz
TARGETS = [("m55", "Cortex-M55"), ("m33", "Cortex-M33 (Pico 2 class)"), ("hexagon", "Hexagon")]
# Display order; the SampleRateTap rows carry no ratio, the competitors'
# ratio is against SampleRateTap balanced float.
ENGINES = [
    ("srt_float", "**SampleRateTap** balanced, float", True),
    ("srt_q15", "**SampleRateTap** balanced, Q15", True),
    ("r8b_120", "r8brain 120 dB, default 2 % band³", False),
    ("r8b_120_tb8", "r8brain 120 dB, 8 % band (flat to 20 kHz)³", False),
    ("lsr_medium", "libsamplerate `MEDIUM`", False),
    ("lsr_best", "libsamplerate `BEST`", False),
]


def steady(c: dict, e: str) -> float:
    return (c[f"cmp_icount_{e}_4s"] - c[f"cmp_icount_{e}"]) / FRAMES


def construction(c: dict, e: str) -> float:
    return (2 * c[f"cmp_icount_{e}"] - c[f"cmp_icount_{e}_4s"]) / 1e6


def block(doc: dict) -> str:
    p, counts = doc["provenance"], doc["counts"]
    targets = [(t, n) for t, n in TARGETS if t in counts]
    wrap = lambda t: textwrap.fill(t, 78, break_long_words=False, break_on_hyphens=False)
    lines = [
        wrap(f"Measured in `compare.yml` run [{p['run_id']}]({p['run_url']}) on "
             f"`{p['sha'][:7]}` (DspTap `{p['dsptap'][:7]}`), {p['date']}"
             + (f"; {p['toolchains']}" if p["toolchains"] else "") + "."),
        "",
        "Steady state, instructions per stereo output frame (× = vs. SampleRateTap",
        "balanced float; the cheaper SampleRateTap row per target in bold):",
        "",
        "| Engine | " + " | ".join(n for _, n in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
    best = {t: min(steady(counts[t], e) for e, _, ours in ENGINES if ours) for t, _ in targets}
    for e, name, ours in ENGINES:
        cells = []
        for t, _ in targets:
            v = steady(counts[t], e)
            if ours:
                cells.append(f"**{v:,.0f}**" if v == best[t] else f"{v:,.0f}")
            else:
                cells.append(f"{v:,.0f} ({v / steady(counts[t], 'srt_float'):.1f}×)")
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines += [
        "",
        "One-time construction, millions of instructions:",
        "",
        "| Engine | " + " | ".join(n.split(" (")[0] for _, n in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
    for e, name, _ in ENGINES:
        cells = [f"{construction(counts[t], e):,.1f}" for t, _ in targets]
        lines.append(f"| {name.replace('³', '')} | " + " | ".join(cells) + " |")
    # The scalars the prose below the block cites, so they cannot go stale
    # separately from the tables.
    if "m33" in counts and "m55" in counts:
        m33, m55 = counts["m33"], counts["m55"]
        q15, fl = steady(m55, "srt_q15"), steady(m55, "srt_float")
        lines += [
            "",
            wrap(f"Key figures: on the M33 the Q15 datapath costs "
            f"**{steady(m33, 'srt_q15'):,.0f} instructions/frame** in steady state, "
            f"libsamplerate `MEDIUM` **~{steady(m33, 'lsr_medium') / steady(m33, 'srt_q15'):.0f}×** that "
            f"and r8brain at 8 % **~{steady(m33, 'r8b_120_tb8') / steady(m33, 'srt_q15'):.0f}×**; "
            f"on the M55 the float datapath ({fl:,.0f}) is "
            f"{'cheaper' if fl < q15 else 'dearer'} than Q15 ({q15:,.0f}) by "
            f"{abs(q15 - fl) / min(q15, fl):.0%}."),
        ]
    return "\n".join(lines)


def main() -> int:
    text = DOC.read_text()
    if BEGIN not in text or END not in text:
        print(f"markers not found in {DOC}", file=sys.stderr)
        return 1
    new = f"{BEGIN}\n{block(json.loads(COUNTS.read_text()))}\n{END}"
    DOC.write_text(re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), new, text, flags=re.S))
    print(f"updated {DOC}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
