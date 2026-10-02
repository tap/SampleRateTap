#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""The coverage matrix of the rational engine (PLAN.md section 3): one chain
for every ordered pair of the family's fourteen rates, chosen by the plan's
rules over MEASURED stage lengths.

For every pair the chain declares f_pass = p * r_min (the profile's passband
fraction of the lower of the two rates). Every rational stage of a chain
whose lower rate is r is designed with its passband at f_pass, i.e. at the
fraction p / d of its own lower rate, where the DIVISOR d is the largest
2^a * 3^b at or below r / r_min: exact inside a
family, where r / r_min is such a number, and conservative across (a
passband at or above f_pass, so the stage still meets 2.1(a)); the one
stage that runs BELOW r_min, a 48-family r_min's stage at bridge's rate
(44.1 * 2^k / 48 * 2^k = 147/160), takes that fraction exactly. The stage's
length is the smallest m whose B-th-band Kaiser design meets the stopband
with >= 1 dB margin on the 16384-point grid (design.h's criterion, M2's
search, here in numpy from the same published math), so the d = 1 column
reproduces design.h's pins and the other columns are the relaxed pins
design.h carries per named profile (`relaxations`), which test_design.cpp
verifies against the shipping designer.

MACs per output are what stage.h executes: each row of the table (an L-phase
row, or a decimating branch) trimmed to its nonzero span, summed over a
superblock, divided by the outputs per superblock; bridge's stage costs its
taps per phase. Latency is the exact rational sum of (N - 1) / 2 at each
stage's composite rate, in the chain's output frames.

The chain per pair is chosen by the plan's rules (3.1): the MAC-minimal
chain through the 2^a * 3^b lattice over the single-stage vocabulary (no
single 4th-band stage, decision 6; one bridge stage at the lowest k whose
passband admits f_pass, decision 1), then the fewest stages whose rational
stages' MACs are within 10 % of the minimum's around the same bridge stage
(decision 2), then latency; a row whose bridge output rate is >= 2x the
chain's output rate carries the flag (decision 3).

Usage (from the engine root):
    tools/coverage/matrix.py md      # section 3's tables (3.3-3.6), markdown
    tools/coverage/matrix.py pins    # design.h's relaxation tables
    tools/coverage/matrix.py rows    # writes tests/coverage/matrix_rows.h
    tools/coverage/matrix.py diff    # chains that differ from PLAN.md's tables
    tools/coverage/matrix.py rule    # rows where the fewest-stages rule overrode the minimum
"""
import collections
import functools
import heapq
import pathlib
import re
import sys
from fractions import Fraction as F

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]

A_RATES = [8000, 12000, 16000, 24000, 32000, 48000, 96000, 192000, 384000]
B_RATES = [11025, 22050, 44100, 88200, 176400]
RATES = A_RATES + B_RATES
PROFILES = collections.OrderedDict([  # name: (A dB, p = f_pass / r_min)
    ("super_economy", (70.0, F(1, 3))),
    ("economy", (70.0, F(3, 8))),
    ("balanced", (70.0, F(19, 48))),
    ("transparent", (120.0, F(5, 12))),
])
# bridge's pinned taps per phase (bridge/design.h): (down 48 -> 44.1, up, passband Hz at k = 0).
BRIDGE = {"super_economy": (40, 28, 16000), "economy": (58, 38, 18000),
          "balanced": (78, 44, 19000), "transparent": (184, 96, 20000)}
BRIDGE_L = {"down": 147, "up": 160}  # output phases: 48 -> 44.1 is L = 147, M = 160
BRIDGE_M = {"down": 160, "up": 147}
ALLOWED = {1, 2, 3, 4, 6, 8}
NO_SINGLE_STAGE = {4}  # decision 6: by 4 is two half-bands; the 4th-band design serves 4/3 and 3/4
BANDS = [2, 3, 4, 6, 8]
FEWER_STAGES_TOL = F(1, 10)
FLAG_RATIO = 2
NMAX = 8
GRID = 16384
MARGIN_DB = 1.0
MAX_M = 128


# --- the design (mirrors tap::dsp::design_nyquist and design.h's search) ----

def kaiser_beta(atten_db):
    if atten_db > 50.0:
        return 0.1102 * (atten_db - 8.7)
    if atten_db > 21.0:
        return 0.5842 * (atten_db - 21.0) ** 0.4 + 0.07886 * (atten_db - 21.0)
    return 0.0


def design_nyquist(band, m, beta):
    n = 2 * m * band - 1
    c = (n - 1) // 2
    i = np.arange(n)
    u = (i - c) / c
    w = np.i0(beta * np.sqrt(np.maximum(0.0, 1.0 - u * u))) / np.i0(beta)
    h = np.sinc((i - c) / band) / band * w
    h[c] = 1.0
    h[(i != c) & ((i - c) % band == 0)] = 0.0
    for j in range(band):
        if j == c % band:
            continue
        sel = (i % band) == j
        h[sel] /= h[sel].sum()
    return h


def worst_stopband_db(h, band, p, grid=GRID):
    f_stop = (1.0 - p) / band
    f = f_stop + (0.5 - f_stop) * np.arange(grid) / (grid - 1)
    m = np.arange(len(h))
    worst = -1e9
    for k in range(0, grid, 512):
        e = np.exp(-2j * np.pi * f[k:k + 512, None] * m[None, :])
        worst = max(worst, 20 * np.log10(np.maximum(np.abs(e @ h) / band, 1e-300)).max())
    return worst


@functools.lru_cache(maxsize=None)
def search_m(band, p, atten):
    """The smallest m meeting the stopband with the margin on the grid (design.h's search)."""
    beta = kaiser_beta(atten)
    for m in range(1, MAX_M + 1):
        if worst_stopband_db(design_nyquist(band, m, beta), band, float(p)) <= -(atten + MARGIN_DB):
            return m
    raise RuntimeError(f"no length meets band {band} p {p} A {atten}")


LATTICE = sorted({2 ** a * 3 ** b for a in range(0, 12) for b in range(0, 4)})


def divisor(lower, rmin):
    """The design divisor of a stage whose lower rate is `lower` in a chain
    whose lowest rate is rmin: the largest lattice number at or below
    lower / rmin (a relaxation: exact within a family, conservative across),
    or lower / rmin itself when that is below 1 — the stage runs at bridge's
    rate under a 48-family r_min (44.1 * 2^k over 48 * 2^k, always 147/160),
    a tightening the pins carry as that one fraction."""
    q = F(lower, rmin)
    if q < 1:
        return q
    return F(max(d for d in LATTICE if d <= q))


# --- one stage, costed as stage.h builds it -------------------------------

def span(flags):
    idx = [i for i, f in enumerate(flags) if f]
    return idx[-1] - idx[0] + 1 if idx else 0


@functools.lru_cache(maxsize=None)
def stage_macs(L, M, m):
    """MACs per output of a stage at L/M with taps per branch m: the trimmed
    rows of stage.h's table, summed over a superblock, per output."""
    B = max(L, M)
    N = 2 * m * B - 1
    c = m * B - 1
    nz = [(i == c) or ((i - c) % B != 0) for i in range(N)]
    if L == 1:  # the M-branch commutator: branch j holds taps j + s M
        return F(sum(span(nz[j::M]) for j in range(M)))
    return F(sum(span(nz[p::L]) for p in range(L)), L)  # L-phase rows: taps p + t L


def stage(x, y, prof, rmin):
    """A rational stage from rate x to rate y under the profile, in the chain
    whose lowest rate is rmin: (MACs per output, latency in output frames,
    label, N, divisor, m) or None when the ratio is not one stage of the
    vocabulary."""
    fr = F(y, x)
    L, M = fr.numerator, fr.denominator
    if L not in ALLOWED or M not in ALLOWED:
        return None
    if (L == 1 or M == 1) and max(L, M) in NO_SINGLE_STAGE:
        return None
    A, p = PROFILES[prof]
    d = divisor(min(x, y), rmin)
    m = search_m(max(L, M), p / d, A)
    N = 2 * m * max(L, M) - 1
    lat = F(N - 1, 2 * M)  # (N - 1) / 2 at the composite rate, in output frames
    label = f"↑{L}" if M == 1 else f"↓{M}" if L == 1 else f"{L}/{M}"
    return stage_macs(L, M, m), lat, label, N, d, m


def bridge(x, y, prof, rmin):
    td, tu, pb = BRIDGE[prof]
    A, p = PROFILES[prof]
    fpass = p * rmin
    for k in range(3):
        lo44, hi48 = 44100 * 2 ** k, 48000 * 2 ** k
        if (x, y) == (hi48, lo44) and fpass <= pb * 2 ** k:
            return F(td), F(td * BRIDGE_L["down"] - 1, 2 * BRIDGE_M["down"]), f"bridge↓k{k}", k
        if (x, y) == (lo44, hi48) and fpass <= pb * 2 ** k:
            return F(tu), F(tu * BRIDGE_L["up"] - 1, 2 * BRIDGE_M["up"]), f"bridge↑k{k}", k
    return None


# --- the search -------------------------------------------------------------

def lattice_rates(lo, hi):
    out = set()
    # Intermediate rates: the family's base times 2^a 3^b (b <= 3), the same
    # relative lattice in both families, so a ratio's chain is the same chain
    # in either (36 kHz and 33.075 kHz are both r_min * 3 for a 12 / 11.025
    # kHz r_min; neither is a supported rate, both are stage boundaries).
    for base in (1000, 11025):
        for a in range(0, 12):
            for b in range(0, 4):
                r = base * 2 ** a * 3 ** b
                if lo <= r <= hi:
                    out.add(r)
    return out


def chains_by_stage_count(src, dst, prof, only_bridge=None):
    """Dijkstra over (node, stage count): the MAC-minimal chain to dst for
    every stage count n <= NMAX. {n: (macs/out, latency, n, chain)}; a chain
    is a tuple of (from, to, label, kind, N, divisor, m)."""
    rmin = min(src, dst)
    lo, hi = rmin, max(src, dst)
    nodes = lattice_rates(lo, max(hi, 48000)) | {src, dst}
    for k in range(3):
        for r in (44100 * 2 ** k, 48000 * 2 ** k):
            if lo * 0.9 <= r <= hi * 1.1:
                nodes.add(r)
    edges = {}
    for u in nodes:
        for v in nodes:
            if v == u:
                continue
            b = bridge(u, v, prof, rmin)
            if b:
                macs, lat, label, k = b
                if only_bridge is not None and (u, v, label) != only_bridge:
                    continue
                edges.setdefault(u, []).append((v, macs * v, lat, (u, v, label, "bridge", 0, 0, 0)))
            else:
                s = stage(u, v, prof, rmin)
                if not s:
                    continue
                macs, lat, label, N, d, m = s
                edges.setdefault(u, []).append((v, macs * v, lat, (u, v, label, "stage", N, d, m)))
    pq = [(F(0), 0, F(0), src, ())]
    seen = {}
    while pq:
        macs, n, lat, u, chain = heapq.heappop(pq)
        if (u, n) in seen:
            continue
        seen[(u, n)] = (macs, n, lat, chain)
        if n >= NMAX:
            continue
        for v, cost, l, e in edges.get(u, ()):
            if (v, n + 1) in seen:
                continue
            # latency of the edge in the CHAIN's output frames: scale by dst / v
            heapq.heappush(pq, (macs + cost, n + 1, lat + l * F(dst, v), v, chain + (e,)))
    out = {}
    for n in range(1, NMAX + 1):
        r = seen.get((dst, n))
        if r:
            out[n] = (r[0] / dst, r[2], n, r[3])
    return out


def rational_macs(r, dst):
    macs = r[0]
    for u, v, label, kind, N, d, m in r[3]:
        if kind == "bridge":
            macs -= F(BRIDGE_COST[(label, u, v)]) * F(v, dst)
    return macs


BRIDGE_COST = {}


def best_chain(src, dst, prof):
    """The MAC-minimal chain, then the fewest-stages rule (decision 2).
    Returns (chosen, minimal)."""
    per_n = chains_by_stage_count(src, dst, prof)
    if not per_n:
        return None, None
    minimal = min(per_n.values(), key=lambda r: (r[0], r[2], r[1]))
    br = [e for e in minimal[3] if e[3] == "bridge"]
    for e in minimal[3]:
        if e[3] == "bridge":
            BRIDGE_COST[(e[2], e[0], e[1])] = bridge(e[0], e[1], prof, min(src, dst))[0]
    if br:
        per_n = chains_by_stage_count(src, dst, prof, only_bridge=(br[0][0], br[0][1], br[0][2]))
        for r in per_n.values():
            for e in r[3]:
                if e[3] == "bridge":
                    BRIDGE_COST[(e[2], e[0], e[1])] = bridge(e[0], e[1], prof, min(src, dst))[0]
    chosen = minimal
    for n in sorted(per_n):
        if n >= minimal[2]:
            break
        if rational_macs(per_n[n], dst) <= rational_macs(minimal, dst) * (1 + FEWER_STAGES_TOL):
            chosen = per_n[n]
            break
    return chosen, minimal


def cost_chain(chain, src, dst, prof):
    """The economy chain's cost under another profile: (macs/out, latency)."""
    rmin = min(src, dst)
    macs = F(0)
    lat = F(0)
    stages = []
    for u, v, label, kind, N, d, m in chain:
        if kind == "bridge":
            b = bridge(u, v, prof, rmin)
            assert b, (chain, prof, "bridge passband excludes f_pass")
            mo, l = b[0], b[1]
            stages.append((u, v, label, kind, 0, 0, 0))
        else:
            s = stage(u, v, prof, rmin)
            assert s, (chain, prof)
            mo, l, _, N, d, m = s
            stages.append((u, v, label, kind, N, d, m))
        macs += mo * v
        lat += l * F(dst, v)
    return macs / dst, lat, tuple(stages)


def flagged(chain, dst):
    return any(kind == "bridge" and v >= FLAG_RATIO * dst for u, v, label, kind, N, d, m in chain)


def shape(chain):
    return " · ".join(e[2] for e in chain)


def fmt(x):
    return f"{x / 1000:g}"


@functools.lru_cache(maxsize=None)
def economy_chains():
    """The chain per ordered pair at economy (the table's chains): {(src, dst): (chosen, minimal)}."""
    return {(s, d): best_chain(s, d, "economy") for s in RATES for d in RATES if s != d}


def ratio_key(kv):
    (L, M), _ = kv
    q = L / M
    return (q < 1, q if q >= 1 else 1 / q)


def within_groups(rates):
    groups = collections.OrderedDict()
    for s in rates:
        for d in rates:
            if s == d:
                continue
            fr = F(d, s)
            groups.setdefault((fr.numerator, fr.denominator), []).append((s, d))
    return sorted(groups.items(), key=ratio_key)


def g(x):
    return f"{float(x):g}"


def md():
    allc = economy_chains()
    for rates, title in ((A_RATES, "within 48"), (B_RATES, "within 44.1")):
        print(f"### {title}")
        print("| Ratio L/M | Pairs (from → to, kHz) | Chain | MAC/out eco | Latency eco (out) | MAC/out tr | Latency tr (out) |")
        print("|---|---|---|---|---|---|---|")
        for (L, M), pairs in within_groups(rates):
            cells = set()
            for s, d in pairs:
                r, _ = allc[(s, d)]
                mt, lt, _ = cost_chain(r[3], s, d, "transparent")
                cells.add((shape(r[3]), r[0], r[1], mt, lt))
            assert len(cells) == 1, (L, M, cells)
            sh, me, le, mt, lt = cells.pop()
            pl = ", ".join(f"{fmt(s)}→{fmt(d)}" for s, d in pairs)
            print(f"| {L}/{M} | {pl} | {sh} | {float(me):.1f} | {float(le):.1f} | {float(mt):.1f} | {float(lt):.1f} |")
        print()
    nflag = 0
    for name, srcs, dsts in (("48 → 44.1", A_RATES, B_RATES), ("44.1 → 48", B_RATES, A_RATES)):
        print(f"### {name}")
        print("| From → To | Chain | f_pass eco (kHz) | MAC/out eco | Latency eco (ms) | MAC/out tr | Latency tr (ms) | ⚑ |")
        print("|---|---|---|---|---|---|---|---|")
        for s in srcs:
            for d in dsts:
                r, _ = allc[(s, d)]
                mt, lt, _ = cost_chain(r[3], s, d, "transparent")
                fl = flagged(r[3], d)
                nflag += fl
                fp = PROFILES["economy"][1] * min(s, d)
                print(f"| {fmt(s)} → {fmt(d)} | {shape(r[3])} | {float(fp) / 1000:.3f} | {float(r[0]):.1f} | "
                      f"{float(r[1]) / d * 1e3:.2f} | {float(mt):.1f} | {float(lt) / d * 1e3:.2f} | {'⚑' if fl else ''} |")
        print()
    print(f"flagged rows: {nflag}")


def rule():
    """Rows (economy) where the fewest-stages rule overrode the MAC minimum."""
    n = 0
    for (s, d), (c, m) in economy_chains().items():
        if c is not m:
            n += 1
            rm, rc = rational_macs(m, d), rational_macs(c, d)
            print(f"{fmt(s)} → {fmt(d)}: {shape(m[3])} ({float(m[0]):.1f}, {m[2]} stages) → {shape(c[3])} "
                  f"({float(c[0]):.1f}, {c[2]} stages)  rational {float(rm):.1f} → {float(rc):.1f}, +{100 * float(rc / rm - 1):.1f} %")
    print("rows where the rule bit:", n)


def used_divisors():
    """{(profile, band): sorted divisors} every chain of the matrix uses."""
    used = collections.defaultdict(set)
    for (s, d), (c, _) in economy_chains().items():
        for prof in PROFILES:
            _, _, stages = cost_chain(c[3], s, d, prof)
            for u, v, label, kind, N, dv, m in stages:
                if kind == "stage":
                    fr = F(v, u)
                    used[prof].add(dv)
    return used


def pins():
    """design.h's relaxation tables: m per band at every divisor the matrix uses."""
    used = used_divisors()
    for prof, (A, p) in PROFILES.items():
        divs = sorted(set().union(*used.values()))
        print(f"// {prof}: A = {A:g} dB, p = {p}")
        for dv in divs:
            ms = [search_m(B, p / dv, A) for B in BANDS]
            print(f"    {{{{{dv.numerator}, {dv.denominator}}}, {{{', '.join(str(m) for m in ms)}}}}},")


def rows_header():
    """tests/coverage/matrix_rows.h: every row as an X-macro over the chain's
    stage types, its divisors and the exact (MACs/out, latency) per profile."""
    allc = economy_chains()
    out = []
    out.append("// Generated by tools/coverage/matrix.py — DO NOT EDIT.")
    out.append("// The coverage matrix (PLAN.md section 3): one chain per ordered pair of")
    out.append("// the fourteen rates, with the relaxation divisor of every rational stage")
    out.append("// and the exact MACs per output (num, den) and latency in output frames")
    out.append("// (num, den) per profile, in the order super_economy, economy, balanced,")
    out.append("// transparent. See the generator for the rules and the provenance.")
    out.append("// SPDX-License-Identifier: MIT")
    out.append("// Copyright 2026 Timothy Place and the SampleRateTap contributors")
    out.append("#pragma once")
    out.append("")
    out.append("// clang-format off")
    out.append("// ROW(src_hz, dst_hz, flagged, (stage types...), (divisors...),")
    out.append("//     (macs_num, macs_den, lat_num, lat_den) x 4 profiles)")
    out.append("#define TAP_SR_RATIONAL_MATRIX_ROWS(ROW) \\")
    n = 0
    for s in RATES:
        for d in RATES:
            if s == d:
                continue
            c, _ = allc[(s, d)]
            types = []
            divs = []
            for u, v, label, kind, N, dv, m in c[3]:
                if kind == "bridge":
                    k = int(label[-1])
                    types.append(f"bridge_{'down' if '↓' in label else 'up'}<{k}>")
                    divs.append("d(0, 1)")
                else:
                    fr = F(v, u)
                    L, M = fr.numerator, fr.denominator
                    types.append(f"up_{L}" if M == 1 else f"down_{M}" if L == 1 else f"ratio_{L}_{M}")
                    divs.append(f"d({dv.numerator}, {dv.denominator})")
            cells = []
            for prof in PROFILES:
                mo, lat, _ = cost_chain(c[3], s, d, prof)
                cells.append(f"{mo.numerator}, {mo.denominator}, {lat.numerator}, {lat.denominator}")
            out.append(f"    ROW({s}, {d}, {'true' if flagged(c[3], d) else 'false'}, ({', '.join(types)}), "
                       f"({', '.join(divs)}), {', '.join(cells)}) \\")
            n += 1
    out[-1] = out[-1].rstrip(" \\")
    out.append("// clang-format on")
    out.append("")
    out.append(f"inline constexpr unsigned k_matrix_rows = {n};")
    out.append("")
    path = ROOT / "tests" / "coverage" / "matrix_rows.h"
    path.write_text("\n".join(out))
    print(f"wrote {path} ({n} rows)")


def diff():
    """Chains that differ from the tables PLAN.md carries (section 3)."""
    plan = (ROOT / "PLAN.md").read_text()
    table = {}
    for line in plan.splitlines():
        m = re.match(r"\| (\d+(?:\.\d+)?) → (\d+(?:\.\d+)?) \| (.+?) \|", line)
        if m:
            table[(int(float(m.group(1)) * 1000), int(float(m.group(2)) * 1000))] = m.group(3).strip()
        m = re.match(r"\| (\d+)/(\d+) \| (.+?) \| (.+?) \|", line)
        if m:
            for pair in m.group(3).split(","):
                if "→" not in pair:  # another table keyed by ratio (section 6's)
                    continue
                a, b = pair.strip().split("→")
                table[(int(float(a) * 1000), int(float(b) * 1000))] = m.group(4).strip()
    n = 0
    for (s, d), (c, _) in economy_chains().items():
        old = table.get((s, d))
        new = shape(c[3])
        if old != new:
            n += 1
            print(f"{fmt(s)} → {fmt(d)}: {old}  →  {new}")
    print("changed rows:", n, "of", len(table), "parsed")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "md"
    {"md": md, "pins": pins, "rows": rows_header, "diff": diff, "rule": rule}[cmd]()
