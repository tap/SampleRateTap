#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Regenerate the engines' COMPARISON.md embedded instruction-count tables.

Usage: scripts/update_compare_docs.py   (from the repository root)

Per engine, the block between the COMPARE markers in <engine>/docs/COMPARISON.md
derives from <engine>/bench/compare_counts.json, the record
scripts/harvest_compare.py writes from a compare.yml run. Every engine is
measured at 2 s and 4 s of audio: the difference is the steady-state cost
per output frame, and 2 * (2 s) - (4 s) is the one-time construction. CI
regenerates every block and fails on any diff (ci.yml, compare-docs), so the
published numbers cannot drift from the measured ones when a pin moves.
"""
import json
import pathlib
import re
import sys
import textwrap

BEGIN, END = "<!-- COMPARE:BEGIN -->", "<!-- COMPARE:END -->"
TARGETS = [("m55", "Cortex-M55"), ("m33", "Cortex-M33 (Pico 2 class)"), ("hexagon", "Hexagon")]


def wrap(t: str) -> str:
    return textwrap.fill(t, 78, break_long_words=False, break_on_hyphens=False)


def provenance(p: dict) -> str:
    return wrap(f"Measured in `compare.yml` run [{p['run_id']}]({p['run_url']}) on "
                f"`{p['sha'][:7]}` (DspTap `{p['dsptap'][:7]}`), {p['date']}"
                + (f"; {p['toolchains']}" if p["toolchains"] else "") + ".")


def tables(counts: dict, targets: list, engines: list, frames: int, prefix: str, steady_intro: list) -> list:
    """The steady-state and construction tables for one record (or one
    direction of one): `engines` rows of (binary stem, label, reference stem
    or None); a row whose binaries the record lacks is left out, so an older
    record still derives."""
    def steady(c, e):
        return (c[f"{prefix}{e}_4s"] - c[f"{prefix}{e}"]) / frames

    def construction(c, e):
        return (2 * c[f"{prefix}{e}"] - c[f"{prefix}{e}_4s"]) / 1e6

    present = [(e, n, ref) for e, n, ref in engines
               if all(f"{prefix}{e}" in counts[t] and f"{prefix}{e}_4s" in counts[t] for t, _ in targets)]
    best = {t: min(steady(counts[t], e) for e, _, ref in present if ref is None) for t, _ in targets}
    lines = steady_intro + [
        "",
        "| Engine | " + " | ".join(n for _, n in targets) + " |",
        "|---|" + "---:|" * len(targets),
    ]
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
        # The ratio clauses belong to the steady-state table only.
        label = re.sub(r" \(× vs\. Q15\)|; × vs\. \w+", "", name).replace("³", "")
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    return lines


# ---------------------------------------------------------------------------
# async: one near-unity task (docs: async/docs/COMPARISON.md). Display order:
# (binary stem, row label, the SampleRateTap row it is measured against, or
# None for our own rows). Floating-point competitors are ratioed against
# balanced float; SpeexDSP's fixed-point build, the one competitor with a
# fixed-point path, against balanced Q15.
ASYNC_ENGINES = [
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
ASYNC_FRAMES = 2 * 48000  # the 2 s workload's output frames at 48 kHz


def async_block(doc: dict) -> str:
    p, counts = doc["provenance"], doc["counts"]
    targets = [(t, n) for t, n in TARGETS if t in counts]
    prefix = "cmp_icount_"

    def steady(c, e):
        return (c[f"{prefix}{e}_4s"] - c[f"{prefix}{e}"]) / ASYNC_FRAMES

    lines = [provenance(p), ""] + tables(counts, targets, ASYNC_ENGINES, ASYNC_FRAMES, prefix, [
        "Steady state, instructions per stereo output frame (× = vs. SampleRateTap",
        "balanced float, or vs. balanced Q15 where the row says so; the cheaper",
        "SampleRateTap row per target in bold):",
    ])
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


# ---------------------------------------------------------------------------
# bridge: the 44.1 <-> 48 pair, one table pair per direction (docs:
# bridge/docs/COMPARISON.md). The matched settings are the ones
# bridge/bench/compare/bench_compare.cpp names; floating-point competitors
# are ratioed against economy float, SpeexDSP's fixed-point build against
# economy Q15, and the transparent-matched rows against transparent float.
BRIDGE_ENGINES = [
    ("srt_eco_float", "**bridge** economy, float", None),
    ("srt_eco_q15", "**bridge** economy, Q15", None),
    ("srt_tr_float", "**bridge** transparent, float", None),
    ("lsr_medium", "libsamplerate `MEDIUM` (economy-matched)", "srt_eco_float"),
    ("r8b_eco", "r8brain 70 dB, 12 % band (economy-matched)³", "srt_eco_float"),
    ("speex_float_q3", "SpeexDSP float, quality 3 (economy-matched)", "srt_eco_float"),
    ("speex_fixed_q3", "SpeexDSP fixed-point, quality 3 (× vs. Q15)", "srt_eco_q15"),
    ("lsr_best", "libsamplerate `BEST` (transparent-matched; × vs. transparent)", "srt_tr_float"),
    ("r8b_tr", "r8brain 120 dB, 6 % band (transparent-matched; × vs. transparent)³", "srt_tr_float"),
    ("speex_float_q9", "SpeexDSP float, quality 9 (transparent-matched; × vs. transparent)", "srt_tr_float"),
]
BRIDGE_DIRECTIONS = [("down", "48 → 44.1 kHz", 2 * 44100), ("up", "44.1 → 48 kHz", 2 * 48000)]


def bridge_block(doc: dict) -> str:
    p, counts = doc["provenance"], doc["counts"]
    targets = [(t, n) for t, n in TARGETS if t in counts]
    lines = [provenance(p)]
    for d, title, frames in BRIDGE_DIRECTIONS:
        prefix = f"cmp_bridge_icount_{d}_"
        lines += ["", f"**{title}.** Steady state, instructions per stereo output frame (× = vs. the",
                  "bridge row the label names; the cheapest bridge row per target in bold):"]
        lines += tables(counts, targets, BRIDGE_ENGINES, frames, prefix, [])
    if "m33" in counts and "m55" in counts:
        m33, m55 = counts["m33"], counts["m55"]
        pre = "cmp_bridge_icount_down_"

        def steady(c, e, frames=2 * 44100):
            return (c[f"{pre}{e}_4s"] - c[f"{pre}{e}"]) / frames

        q15, fl = steady(m55, "srt_eco_q15"), steady(m55, "srt_eco_float")
        lines += [
            "",
            wrap(f"Key figures (48 → 44.1): on the M33 the economy Q15 converter costs "
                 f"**{steady(m33, 'srt_eco_q15'):,.0f} instructions/frame** in steady state, "
                 f"SpeexDSP's fixed-point build at quality 3 "
                 f"**{steady(m33, 'speex_fixed_q3') / steady(m33, 'srt_eco_q15'):.1f}×** that, "
                 f"libsamplerate `MEDIUM` **~{steady(m33, 'lsr_medium') / steady(m33, 'srt_eco_q15'):.0f}×** "
                 f"and r8brain at 70 dB **~{steady(m33, 'r8b_eco') / steady(m33, 'srt_eco_q15'):.0f}×**; "
                 f"on the M55 the economy float converter ({fl:,.0f}) is "
                 f"{'cheaper' if fl < q15 else 'dearer'} than Q15 ({q15:,.0f}) by "
                 f"{abs(q15 - fl) / min(q15, fl):.0%}."),
        ]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# rational: four ratios of the vocabulary, one table pair per ratio (docs:
# rational/docs/COMPARISON.md). The matched settings are the ones
# rational/bench/compare/bench_compare.cpp names; floating-point competitors
# are ratioed against economy float, SpeexDSP's fixed-point build against
# economy Q15, and the transparent-matched rows against transparent float.
RATIONAL_ENGINES = [
    ("srt_eco_float", "**rational** economy, float", None),
    ("srt_eco_q15", "**rational** economy, Q15", None),
    ("srt_tr_float", "**rational** transparent, float", None),
    ("lsr_medium", "libsamplerate `MEDIUM` (economy-matched)", "srt_eco_float"),
    ("r8b_eco", "r8brain 70 dB, 16 % band (economy-matched)³", "srt_eco_float"),
    ("speex_float_q2", "SpeexDSP float, quality 2 (economy-matched)", "srt_eco_float"),
    ("speex_fixed_q3", "SpeexDSP fixed-point, quality 3 (× vs. Q15)", "srt_eco_q15"),
    ("lsr_best", "libsamplerate `BEST` (transparent-matched; × vs. transparent)", "srt_tr_float"),
    ("r8b_tr", "r8brain 120 dB, 10 % band (transparent-matched; × vs. transparent)³", "srt_tr_float"),
    ("speex_float_q9", "SpeexDSP float, quality 9 (transparent-matched; × vs. transparent)", "srt_tr_float"),
]
# (binary tag, title, the 2 s workload's output frames)
RATIONAL_RATIOS = [("up2", "↑2 (48 → 96 kHz)", 2 * 96000), ("down2", "↓2 (96 → 48 kHz)", 2 * 48000),
                   ("r32", "3/2 (32 → 48 kHz)", 2 * 48000), ("r23", "2/3 (48 → 32 kHz)", 2 * 32000)]


def rational_block(doc: dict) -> str:
    p, counts = doc["provenance"], doc["counts"]
    targets = [(t, n) for t, n in TARGETS if t in counts]
    lines = [provenance(p)]
    for tag, title, frames in RATIONAL_RATIOS:
        prefix = f"cmp_rational_icount_{tag}_"
        lines += ["", f"**{title}.** Steady state, instructions per stereo output frame (× = vs. the",
                  "rational row the label names; the cheapest rational row per target in bold):"]
        lines += tables(counts, targets, RATIONAL_ENGINES, frames, prefix, [])
    if "m33" in counts and "m55" in counts:
        m33, m55 = counts["m33"], counts["m55"]
        pre = "cmp_rational_icount_down2_"

        def steady(c, e, frames=2 * 48000):
            return (c[f"{pre}{e}_4s"] - c[f"{pre}{e}"]) / frames

        q15, fl = steady(m55, "srt_eco_q15"), steady(m55, "srt_eco_float")
        lines += [
            "",
            wrap(f"Key figures (↓2, 96 → 48): on the M33 the economy Q15 half-band costs "
                 f"**{steady(m33, 'srt_eco_q15'):,.0f} instructions/frame** in steady state, "
                 f"SpeexDSP's fixed-point build at quality 3 "
                 f"**{steady(m33, 'speex_fixed_q3') / steady(m33, 'srt_eco_q15'):.1f}×** that, "
                 f"libsamplerate `MEDIUM` **~{steady(m33, 'lsr_medium') / steady(m33, 'srt_eco_q15'):.0f}×** "
                 f"and r8brain at 70 dB **~{steady(m33, 'r8b_eco') / steady(m33, 'srt_eco_q15'):.0f}×**; "
                 f"on the M55 the economy float half-band ({fl:,.0f}) is "
                 f"{'cheaper' if fl < q15 else 'dearer'} than Q15 ({q15:,.0f}) by "
                 f"{abs(q15 - fl) / min(q15, fl):.0%}."),
        ]
    return "\n".join(lines)


DOCS = [
    (pathlib.Path("async/docs/COMPARISON.md"), pathlib.Path("async/bench/compare_counts.json"), async_block),
    (pathlib.Path("bridge/docs/COMPARISON.md"), pathlib.Path("bridge/bench/compare_counts.json"), bridge_block),
    (pathlib.Path("rational/docs/COMPARISON.md"), pathlib.Path("rational/bench/compare_counts.json"), rational_block),
]


def main() -> int:
    rc = 0
    for doc, record, block in DOCS:
        if not record.exists():
            print(f"no record {record}; {doc} left alone")
            continue
        text = doc.read_text()
        if BEGIN not in text or END not in text:
            print(f"markers not found in {doc}", file=sys.stderr)
            rc = 1
            continue
        new = f"{BEGIN}\n{block(json.loads(record.read_text()))}\n{END}"
        doc.write_text(re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), new, text, flags=re.S))
        print(f"updated {doc}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
