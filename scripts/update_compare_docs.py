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
# Display order: (binary stem, row label, the SampleRateTap row it is measured
# against, or None for our own rows). Floating-point competitors are ratioed
# against balanced float; SpeexDSP's fixed-point build, the one competitor
# with a fixed-point path, against balanced Q15. A row whose binaries the
# record lacks is left out, so an older record still derives.
ENGINES = [
    ("srt_float", "**SampleRateTap** balanced, float", None),
    ("srt_q15", "**SampleRateTap** balanced, Q15", None),
    ("r8b_120", "r8brain 120 dB, default 2 % band³", "srt_float"),
    ("r8b_120_tb8", "r8brain 120 dB, 8 % band (flat to 20 kHz)³", "srt_float"),
    ("lsr_medium", "libsamplerate `MEDIUM`", "srt_float"),
    ("lsr_best", "libsamplerate `BEST`", "srt_float"),
    ("speex_float_q10", "SpeexDSP float, quality 10 (~100 dB)", "srt_float"),
    ("speex_float_q4", "SpeexDSP float, quality 4 (its passband knee)", "srt_float"),
    ("speex_fixed_q10", "SpeexDSP fixed-point, quality 10 (× vs. Q15)", "srt_q15"),
    ("speex_fixed_q4", "SpeexDSP fixed-point, quality 4 (× vs. Q15)", "srt_q15"),
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
        "balanced float, or vs. balanced Q15 where the row says so; the cheaper",
        "SampleRateTap row per target in bold):",
        "",
        "| Engine | " + " | ".join(n for _, n in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
    present = [(e, n, ref) for e, n, ref in ENGINES
               if all(f"cmp_icount_{e}" in counts[t] for t, _ in targets)]
    best = {t: min(steady(counts[t], e) for e, _, ref in present if ref is None) for t, _ in targets}
    for e, name, ref in present:
        cells = []
        for t, _ in targets:
            v = steady(counts[t], e)
            if ref is None:
                cells.append(f"**{v:,.0f}**" if v == best[t] else f"{v:,.0f}")
            else:
                cells.append(f"{v:,.0f} ({v / steady(counts[t], ref):.1f}×)")
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines += [
        "",
        "One-time construction, millions of instructions:",
        "",
        "| Engine | " + " | ".join(n.split(" (")[0] for _, n in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
    for e, name, _ in present:
        cells = [f"{construction(counts[t], e):,.1f}" for t, _ in targets]
        lines.append(f"| {name.replace('³', '').replace(' (× vs. Q15)', '')} | " + " | ".join(cells) + " |")
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
            f"and r8brain at 8 % **~{steady(m33, 'r8b_120_tb8') / steady(m33, 'srt_q15'):.0f}×**"
            + (f", SpeexDSP's fixed-point build at quality 4 "
               f"**{steady(m33, 'speex_fixed_q4') / steady(m33, 'srt_q15'):.1f}×**"
               if 'cmp_icount_speex_fixed_q4' in m33 else "") + "; "
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
