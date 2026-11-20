#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Spike X-0 of the x4 / x8 / x16 plan: the coverage-matrix generator's own
answers with the 4th-band stage admitted (X3), with a chain chosen per tier
(X4), with the `by` search (section 5), and with the designer's Kaiser beta
as a variable (finding dsp-1).

The shipping generator, rational/tools/coverage/matrix.py, is imported
read-only and driven from here: band 4 is admitted by emptying its
NO_SINGLE_STAGE (decision 6), and a beta rule other than today's replaces its
search_m with the pins pins_beta.cpp computes with DspTap's own designer.
Nothing in matrix.py, the tables or the rows header is written.

Usage (from rational/tools/spikes/x4x8, after ../build.sh; numpy needed):
    generator/x0_matrix.py diff       # X3 and X4 against today's tables
    generator/x0_matrix.py pertier    # X4's prototype: md and diff with a second chain column
    generator/x0_matrix.py rows       # X4's prototype ROW format (generator/rows_pertier.txt, never tests/)
    generator/x0_matrix.py by         # the `by` mode: the minimum per (ratio, profile)
    generator/x0_matrix.py beta       # the designer's beta as a variable
    generator/x0_matrix.py keys       # every (band, p, A) the generator designs, for pins_beta
Outputs are committed beside this file as <mode>.txt.
"""
import collections
import importlib.util
import itertools
import pathlib
import subprocess
import sys
from fractions import Fraction as F

HERE = pathlib.Path(__file__).resolve().parent
SPIKE = HERE.parent
ENGINE = HERE.parents[3]  # rational/
BUILD = SPIKE / "build"

_spec = importlib.util.spec_from_file_location("matrix", ENGINE / "tools" / "coverage" / "matrix.py")
mx = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mx)
NUMPY_SEARCH = mx.search_m  # matrix.py's own search (numpy), today's beta

ECO, TR = "economy", "transparent"


# --- driving the generator ---------------------------------------------------

def set_band4(admit):
    """Admit the 4th-band single stage (X3) or keep decision 6."""
    mx.NO_SINGLE_STAGE = set() if admit else {4}
    mx.BRIDGE_COST.clear()
    mx.economy_chains.cache_clear()


def set_rule(rule):
    """None: matrix.py's own numpy search (today's designer). Otherwise a
    pins_beta rule ("dA=0", "dA=2", "search", ...), looked up per key."""
    mx.BRIDGE_COST.clear()
    mx.economy_chains.cache_clear()
    if rule is None:
        mx.search_m = NUMPY_SEARCH
        return
    table = pins_table(rule)

    def lookup(band, p, atten):
        return table[(band, F(p), float(atten))][0]

    mx.search_m = lookup


def run(prof):
    """{(src, dst): (chosen, minimal)} for all 182 ordered pairs at prof."""
    mx.BRIDGE_COST.clear()
    return {(s, d): mx.best_chain(s, d, prof) for s in mx.RATES for d in mx.RATES if s != d}


def within(k):
    return (k[0] in mx.A_RATES) == (k[1] in mx.A_RATES)


def sh(chain):
    return mx.shape(chain[3])


def pair(k):
    return f"{mx.fmt(k[0])} → {mx.fmt(k[1])}"


# --- the keys and the beta rules ---------------------------------------------

def collect_keys():
    """Every (band, p / d, A) any edge of the generator's search designs, at
    every profile, band 4 admitted, plus every lattice divisor up to 48 and
    147/160 at every band (the `by` search and the pins tables)."""
    keys = set()

    def rec(band, p, atten):
        keys.add((band, F(p), float(atten)))
        return 4  # any m: the search's edges do not depend on it

    saved = mx.search_m, set(mx.NO_SINGLE_STAGE)
    mx.search_m = rec
    set_band4(True)
    for prof in mx.PROFILES:
        for s in mx.RATES:
            for d in mx.RATES:
                if s != d:
                    mx.chains_by_stage_count(s, d, prof)
    mx.search_m = saved[0]
    set_band4(not saved[1])
    divs = [F(147, 160)] + [F(x) for x in mx.LATTICE if x <= 48]
    for prof, (A, p) in mx.PROFILES.items():
        for d in divs:
            for B in mx.BANDS:
                keys.add((B, p / d, float(A)))
    return sorted(keys)


def keys_text(keys):
    return "".join(f"{B} {p.numerator} {p.denominator} {A:g}\n" for B, p, A in keys)


_TABLES = {}
_KEYS = []


def all_keys():
    if not _KEYS:
        _KEYS.extend(collect_keys())
    return _KEYS


def pins_table(rule):
    """{(band, p, A): (m, x, worst)} under a rule, from pins_beta (cached in
    build/, recomputed when a key is missing)."""
    if rule in _TABLES:
        return _TABLES[rule]
    cache = BUILD / f"pins_{rule.replace('=', '')}.txt"
    keys = all_keys()

    def parse():
        table = {}
        for line in cache.read_text().splitlines():
            B, pn, pd, A, m, x, w = line.split()
            table[(int(B), F(int(pn), int(pd)), float(A))] = (int(m), float(x), float(w))
        return table

    table = parse() if cache.exists() else {}
    if any(k not in table for k in keys):
        out = subprocess.run([str(BUILD / "pins_beta"), rule], input=keys_text(keys), capture_output=True,
                             text=True, check=True).stdout
        cache.write_text(out)
        table = parse()
    _TABLES[rule] = table
    return table


# --- today's tables and the C ABI's constants -----------------------------------

def plan_tables():
    """The chain per pair in rational/PLAN.md's tables (as matrix.py's diff parses them)."""
    import re
    plan = (ENGINE / "PLAN.md").read_text()
    table = {}
    for line in plan.splitlines():
        m = re.match(r"\| (\d+(?:\.\d+)?) → (\d+(?:\.\d+)?) \| (.+?) \|", line)
        if m:
            table[(int(float(m.group(1)) * 1000), int(float(m.group(2)) * 1000))] = m.group(3).strip()
        m = re.match(r"\| (\d+)/(\d+) \| (.+?) \| (.+?) \|", line)
        if m:
            for pr in m.group(3).split(","):
                if "→" not in pr:
                    continue
                a, b = pr.strip().split("→")
                table[(int(float(a) * 1000), int(float(b) * 1000))] = m.group(4).strip()
    return table


def capi_constants():
    """{chain shape: (C constant, value)} from the C ABI header."""
    import re
    hdr = (ENGINE / "capi" / "tap_sr_rational_capi.h").read_text()
    out = {}
    for name, val in re.findall(r"#define TAP_SR_RATIONAL_((?:UP|DOWN|RATIO)_[A-Z0-9_]+) (\d+)", hdr):
        out[c_shape(name)] = (f"TAP_SR_RATIONAL_{name}", int(val))
    return out


def c_shape(name):
    """TAP constant suffix (UP_2_RATIO_4_3_UP_3) -> chain shape (↑2 · 4/3 · ↑3)."""
    toks = name.split("_")
    stages, i = [], 0
    while i < len(toks):
        if toks[i] == "UP":
            stages.append(f"↑{toks[i + 1]}")
            i += 2
        elif toks[i] == "DOWN":
            stages.append(f"↓{toks[i + 1]}")
            i += 2
        else:  # RATIO_L_M
            stages.append(f"{toks[i + 1]}/{toks[i + 2]}")
            i += 3
    return " · ".join(stages)


def cxx_name(shape):
    """Chain shape -> today's C++ spelling (chain.h) and X0's spelling."""
    today, x0 = [], []
    for st in shape.split(" · "):
        if st[0] == "↑":
            today.append(f"up_{st[1:]}")
            x0.append(f"up{st[1:]}")
        elif st[0] == "↓":
            today.append(f"down_{st[1:]}")
            x0.append(f"down{st[1:]}")
        else:
            L, M = st.split("/")
            today.append(f"ratio_{L}_{M}")
            x0.append(f"ratio{L}over{M}")
    return "_".join(today), "_".join(x0)


# --- modes ----------------------------------------------------------------------

def mode_diff():
    """X3 (band 4 admitted, economy) and X4 (a chain per tier) against today's tables."""
    plan = plan_tables()
    print(f"rational/PLAN.md tables parsed: {len(plan)} rows")
    set_rule(None)

    # The generator's own diff, unchanged, today and with band 4 admitted.
    for admit in (False, True):
        set_band4(admit)
        print(f"\n## matrix.py diff (economy), band 4 {'admitted (X3)' if admit else 'not admitted (today)'}")
        mx.diff()

    set_band4(False)
    today_e, today_t = run(ECO), run(TR)
    set_band4(True)
    x3_e, x3_t = run(ECO), run(TR)

    def report(title, rows):
        w = sum(within(k) for k, _, _ in rows)
        print(f"\n## {title}: {len(rows)} rows ({w} within the families, {len(rows) - w} across)")
        for k, a, b in rows:
            print(f"  {'W' if within(k) else 'X'} {pair(k)}: {a}  →  {b}")

    keys = list(today_e)
    report("X3 at economy: the economy chain moves (today → band 4 admitted)",
           [(k, sh(today_e[k][0]), sh(x3_e[k][0])) for k in keys if sh(today_e[k][0]) != sh(x3_e[k][0])])
    report("X3 + X4: the transparent chain differs from the X3 economy chain (the per-tier split)",
           [(k, sh(x3_e[k][0]), sh(x3_t[k][0])) for k in keys if sh(x3_e[k][0]) != sh(x3_t[k][0])])
    report("X3 + X4 at transparent against today's tables (today's transparent column runs the economy chain)",
           [(k, plan[k], sh(x3_t[k][0])) for k in keys if plan[k] != sh(x3_t[k][0])])
    def band4_rows(tbl):
        return [k for k in keys if any(lb in ("↑4", "↓4") for lb in sh(tbl[k][0]).split(" · "))]

    for title, tbl in (("X3, economy", x3_e), ("X3 + X4, transparent", x3_t)):
        rows = band4_rows(tbl)
        single = [k for k in rows if not within(k)]
        print(f"\n## rows that run a 4th-band stage, {title}: {len(rows)} ({len(single)} across the families, "
              f"which the C ABI builds stage by stage through create_stage(4, 1) or (1, 4))")
    report("X4 alone (decision 6 kept): the transparent chain differs from today's economy chain",
           [(k, sh(today_e[k][0]), sh(today_t[k][0])) for k in keys if sh(today_e[k][0]) != sh(today_t[k][0])])
    print("\n## rows whose X3 + X4 economy chain differs from the X3 economy chain (sanity: the 70 dB tier is economy's)")
    print("  0 by construction (the economy chain is selected at economy)")

    # The named chains (within the families) and the C ABI's constants.
    def chains(tbl):
        return {sh(tbl[k][0]) for k in keys if within(k)}

    today_set, x3_set = chains(today_e), chains(x3_e)
    x3_tr_set = chains(x3_t)
    union = x3_set | x3_tr_set
    plan_set = {plan[k] for k in keys if within(k)}
    assert plan_set == today_set, (plan_set ^ today_set)
    multi = lambda s: {c for c in s if "·" in c}  # noqa: E731
    print("\n## within-family chains (the C ABI's constants are exactly these)")
    print(f"  today: {len(today_set)} ({len(multi(today_set))} named multi-stage chains)")
    print(f"  X3, economy: {len(x3_set)} ({len(multi(x3_set))} multi-stage)")
    print(f"  X3 + X4, economy ∪ transparent: {len(union)} ({len(multi(union))} multi-stage)")
    retired = sorted(today_set - union)
    added_x3 = sorted(x3_set - today_set)
    tr_only = sorted(union - x3_set)
    print(f"  retired by X3 (no row uses them at either tier): {len(retired)}")
    for c in retired:
        print(f"    {c}  ({cxx_name(c)[0]})")
    retired_x3_only = sorted(today_set - x3_set)
    print(f"  retired at economy by X3 alone: {len(retired_x3_only)}")
    for c in retired_x3_only:
        back = " — back at transparent under X4" if c in x3_tr_set else ""
        print(f"    {c}  ({cxx_name(c)[0]}){back}")
    print(f"  added by X3 (economy): {len(added_x3)}")
    for c in added_x3:
        print(f"    {c}  ({cxx_name(c)[1]})")
    print(f"  transparent-only under X4 (no economy row uses them): {len(tr_only)}")
    for c in tr_only:
        print(f"    {c}  ({cxx_name(c)[1]})")

    capi = capi_constants()
    assert set(capi) == today_set, set(capi) ^ today_set
    new = sorted(union - today_set, key=lambda c: (c.count("·"), c))
    print("\n## C ABI (X7): today's constants", len(capi), "values 0 ..", max(v for _, v in capi.values()))
    print(f"  retired values ({len(retired)}): " + ", ".join(f"{capi[c][1]} {capi[c][0]}" for c in
                                                              sorted(retired, key=lambda c: capi[c][1])))
    print(f"  new constants: {len(new)} (X3: {len(added_x3)}, X4: {len(tr_only)}), values "
          f"{len(capi)} .. {len(capi) + len(new) - 1} in this (illustrative) order:")
    for i, c in enumerate(new):
        tag = "X3" if c in x3_set else "X4"
        print(f"    {len(capi) + i} TAP_SR_RATIONAL_{cxx_name(c)[1].upper()}  ({c}, {tag})")
    print(f"  live chains: {len(union)}; CHAIN_END (one past the largest value ever assigned): "
          f"{len(capi) + len(new)}")

    # Rows per ratio: what each moved ratio becomes (within the families).
    print("\n## the within-family ratios that move, per tier")
    for title, a, b in (("X3, economy", today_e, x3_e), ("X3 + X4, transparent vs X3 economy", x3_e, x3_t)):
        by_ratio = collections.OrderedDict()
        for k in keys:
            if within(k) and sh(a[k][0]) != sh(b[k][0]):
                fr = F(k[1], k[0])
                by_ratio.setdefault((fr.numerator, fr.denominator), []).append(k)
        print(f"  {title}: {len(by_ratio)} ratios")
        for (L, M), ks in by_ratio.items():
            k = ks[0]
            ma, mb = a[k][0][0], b[k][0][0]
            print(f"    {L}/{M} ({', '.join(pair(x) for x in ks)}): {sh(a[k][0])} {float(ma):.2f} → "
                  f"{sh(b[k][0])} {float(mb):.2f} MAC/out at its tier")


def mode_pertier():
    """X4's prototype md and diff: a second chain column (the transparent tier's)."""
    set_rule(None)
    set_band4(True)
    e, t = run(ECO), run(TR)
    plan = plan_tables()
    for rates, title in ((mx.A_RATES, "within 48"), (mx.B_RATES, "within 44.1")):
        print(f"### {title} (X3 + X4)")
        print("| Ratio L/M | Pairs (from → to, kHz) | Chain eco | MAC/out eco | Latency eco (out) "
              "| Chain tr | MAC/out tr | Latency tr (out) |")
        print("|---|---|---|---|---|---|---|---|")
        for (L, M), pairs in mx.within_groups(rates):
            cells = set()
            for s, d in pairs:
                ce, ct = e[(s, d)][0], t[(s, d)][0]
                cells.add((sh(ce), ce[0], ce[1], sh(ct), ct[0], ct[1]))
            assert len(cells) == 1, (L, M, cells)
            a, me, le, b, mt, lt = cells.pop()
            pl = ", ".join(f"{mx.fmt(s)}→{mx.fmt(d)}" for s, d in pairs)
            print(f"| {L}/{M} | {pl} | {a} | {float(me):.1f} | {float(le):.1f} | {b} | {float(mt):.1f} | {float(lt):.1f} |")
        print()
    for name, srcs, dsts in (("48 → 44.1", mx.A_RATES, mx.B_RATES), ("44.1 → 48", mx.B_RATES, mx.A_RATES)):
        print(f"### {name} (X3 + X4)")
        print("| From → To | Chain eco | MAC/out eco | Latency eco (ms) | Chain tr | MAC/out tr | Latency tr (ms) | ⚑ |")
        print("|---|---|---|---|---|---|---|---|")
        for s in srcs:
            for d in dsts:
                ce, ct = e[(s, d)][0], t[(s, d)][0]
                fl = mx.flagged(ce[3], d) or mx.flagged(ct[3], d)
                print(f"| {mx.fmt(s)} → {mx.fmt(d)} | {sh(ce)} | {float(ce[0]):.1f} | {float(ce[1]) / d * 1e3:.2f} | "
                      f"{sh(ct)} | {float(ct[0]):.1f} | {float(ct[1]) / d * 1e3:.2f} | {'⚑' if fl else ''} |")
        print()
    print("### diff, per tier, against rational/PLAN.md's tables (one chain, the economy one, at every tier)")
    n = {ECO: 0, TR: 0}
    for k in e:
        old = plan[k]
        for tier, tbl in ((ECO, e), (TR, t)):
            new = sh(tbl[k][0])
            if old != new:
                n[tier] += 1
                print(f"{tier:>11}: {pair(k)}: {old}  →  {new}")
    print(f"changed rows: economy {n[ECO]}, transparent {n[TR]}, of {len(plan)} parsed")


def mode_rows():
    """X4's prototype rows header: a second chain and divisor tuple per row
    (written to build/, never to tests/coverage/)."""
    set_rule(None)
    set_band4(True)
    e, t = run(ECO), run(TR)

    def stage_tuple(c):
        types, divs = [], []
        for u, v, label, kind, N, dv, m in c[3]:
            if kind == "bridge":
                types.append(f"bridge_{'down' if '↓' in label else 'up'}<{int(label[-1])}>")
                divs.append("d(0, 1)")
            else:
                fr = F(v, u)
                L, M = fr.numerator, fr.denominator
                types.append(f"up{L}" if M == 1 else f"down{M}" if L == 1 else f"ratio{L}over{M}")
                divs.append(f"d({dv.numerator}, {dv.denominator})")
        return f"({', '.join(types)}), ({', '.join(divs)})"

    out = ["// Prototype (spike X-0): X4's ROW format, a chain per tier, in X0's spelling. Not a test input.",
           "// ROW(src_hz, dst_hz, flagged, (economy stage types...), (divisors...),",
           "//     (transparent stage types...), (divisors...),",
           "//     (macs_num, macs_den, lat_num, lat_den) x 4 profiles: the economy chain at",
           "//     super_economy, economy, balanced; the transparent chain at transparent)",
           "#define TAP_SR_RATIONAL_MATRIX_ROWS(ROW) \\"]
    for s in mx.RATES:
        for d in mx.RATES:
            if s == d:
                continue
            ce, ct = e[(s, d)][0], t[(s, d)][0]
            cells = []
            for prof in mx.PROFILES:
                c = ct if prof == TR else ce
                mo, lat, _ = mx.cost_chain(c[3], s, d, prof)
                cells.append(f"{mo.numerator}, {mo.denominator}, {lat.numerator}, {lat.denominator}")
            fl = mx.flagged(ce[3], d) or mx.flagged(ct[3], d)
            out.append(f"    ROW({s}, {d}, {'true' if fl else 'false'}, {stage_tuple(ce)}, {stage_tuple(ct)}, "
                       f"{', '.join(cells)}) \\")
    out[-1] = out[-1].rstrip(" \\")
    path = HERE / "rows_pertier.txt"  # a .txt, so the tree's C/C++ format gate leaves the prototype alone
    path.write_text("\n".join(out) + "\n")
    print(f"wrote {path.relative_to(SPIKE)} ({len(out) - 6} rows)")


# --- the `by` mode ---------------------------------------------------------------

STAGES = [F(x) for x in (2, 3, 4, 6, 8)] + [F(1, x) for x in (2, 3, 4, 6, 8)] + \
         [F(3, 2), F(2, 3), F(4, 3), F(3, 4), F(8, 3), F(3, 8)]


def chain_cost(ratios, prof):
    """MACs per output and latency in output frames of a chain of stage
    ratios, relative rates (input 1), costed as chain.h builds it (each
    stage at its design divisor over the chain's lowest rate)."""
    rates = [F(1)]
    for r in ratios:
        rates.append(rates[-1] * r)
    rmin, dst = min(rates), rates[-1]
    macs, lat = F(0), F(0)
    for u, v in zip(rates, rates[1:]):
        s = mx.stage(u, v, prof, rmin)
        if s is None:
            return None
        mo, l = s[0], s[1]
        macs += mo * v
        lat += l * dst / v
    return macs / dst, lat


def label(ratios):
    return " · ".join(f"↑{r.numerator}" if r.denominator == 1 else f"↓{r.denominator}" if r.numerator == 1
                      else f"{r.numerator}/{r.denominator}" for r in ratios)


def by_space(target, nmax):
    """Section 3.1's space: every chain of 1..nmax stages from the vocabulary
    plus the 4th-band stage whose product is target and whose every
    intermediate rate lies between the input and output rates."""
    lo, hi = min(F(1), target), max(F(1), target)
    out = []
    for n in range(1, nmax + 1):
        for combo in itertools.product(STAGES, repeat=n):
            r, ok = F(1), True
            for i, s in enumerate(combo):
                r *= s
                if i < n - 1 and not (lo <= r <= hi):
                    ok = False
                    break
            if ok and r == target:
                out.append(combo)
    return out


BY_TARGETS = [(F(1, 4), 3), (F(4), 3), (F(1, 8), 3), (F(8), 3), (F(1, 16), 4), (F(16), 4)]


def by_table(profiles=tuple(mx.PROFILES)):
    """{(target, prof): [(macs, lat, n, label)] sorted by macs then stages then latency}."""
    set_band4(True)
    res = {}
    for target, nmax in BY_TARGETS:
        space = by_space(target, nmax)
        for prof in profiles:
            rows = []
            for combo in space:
                c = chain_cost(combo, prof)
                if c:
                    rows.append((c[0], c[1], len(combo), label(combo)))
            rows.sort(key=lambda r: (r[0], r[2], r[1]))
            res[(target, prof)] = rows
    return res


def r3_choice(rows):
    """R3 (decision 2): the fewest stages within 10 % of the minimum, then latency."""
    best = rows[0][0]
    ok = [r for r in rows if r[0] <= best * (1 + mx.FEWER_STAGES_TOL)]
    return min(ok, key=lambda r: (r[2], r[0], r[1]))


def tname(target):
    return f"{target.numerator}/{target.denominator}"


def mode_by():
    set_rule(None)
    res = by_table()
    named = {
        F(1, 4): ["↓2 · ↓2", "↓4"], F(4): ["↑2 · ↑2", "↑4"],
        F(1, 8): ["↓4 · ↓2", "↓2 · ↓4", "↓2 · ↓2 · ↓2", "↓3 · 3/4 · ↓2", "3/8 · ↓3", "↓8"],
        F(8): ["↑2 · ↑4", "↑4 · ↑2", "↑2 · ↑2 · ↑2", "↑2 · 4/3 · ↑3", "↑3 · 8/3", "↑8"],
        F(1, 16): ["↓8 · ↓2", "↓2 · ↓4 · ↓2", "↓4 · ↓2 · ↓2", "↓2 · ↓2 · ↓2 · ↓2", "↓4 · ↓4"],
        F(16): ["↑2 · ↑8", "↑2 · ↑4 · ↑2", "↑2 · ↑2 · ↑4", "↑2 · ↑2 · ↑2 · ↑2", "↑4 · ↑4"],
    }
    print("The `by` mode: section 3.1's search space from the pins (band 4 admitted), the minimum per")
    print("(ratio, profile), R3's choice (the fewest stages within 10 % of it), and the named competitors.")
    print("MAC/out per channel; latency in output frames. Today's designer (matrix.py's search).")
    for target, nmax in BY_TARGETS:
        print(f"\n## {tname(target)} (chains of 1..{nmax} stages: {len(by_space(target, nmax))})")
        for prof in mx.PROFILES:
            rows = res[(target, prof)]
            mn, ch = rows[0], r3_choice(rows)
            idx = {r[3]: r for r in rows}
            comp = "; ".join(f"{nm} {float(idx[nm][0]):.4g} ({float(idx[nm][1]):.4g})" for nm in named[target]
                             if nm in idx)
            print(f"  {prof:>13}: min {mn[3]} {float(mn[0]):.4g} ({float(mn[1]):.4g}); R3 {ch[3]} "
                  f"{float(ch[0]):.4g} ({float(ch[1]):.4g}) | {comp}")


# --- the designer's beta as a variable -----------------------------------------------

RULES = ["dA=0", "dA=1", "dA=2", "dA=3", "dA=4", "search"]
RULE_NAME = {"dA=0": "β for A (today)", "dA=1": "β for A + 1", "dA=2": "β for A + 2", "dA=3": "β for A + 3",
             "dA=4": "β for A + 4", "search": "β searched per entry"}
PIN_DIVS = [F(147, 160), F(1), F(2), F(3), F(4), F(6), F(8), F(9), F(12), F(16)]


def mode_beta():
    keys = all_keys()
    print(f"keys (band, p / d, A) the generator designs, plus the pins tables' and the by search's: {len(keys)}")
    # Rule dA=0 through DspTap's designer against matrix.py's numpy search.
    t0 = pins_table("dA=0")
    bad = [(k, t0[k][0], NUMPY_SEARCH(k[0], k[1], k[2])) for k in keys if t0[k][0] != NUMPY_SEARCH(k[0], k[1], k[2])]
    print(f"rule dA=0 (DspTap's designer) against matrix.py's numpy search: {len(keys) - len(bad)} of {len(keys)} "
          f"keys agree" + "".join(f"\n  differs: {k} cxx {a} numpy {b}" for k, a, b in bad))

    # The pins tables (design.h's form) per rule, and how many entries move.
    today = {(prof, d, B): t0[(B, p / d, float(A))][0]
             for prof, (A, p) in mx.PROFILES.items() for d in PIN_DIVS for B in mx.BANDS}
    print("\n## the pins (design.h's tables: m for bands 2, 3, 4, 6, 8 per divisor); entries moved vs today")
    for rule in RULES:
        t = pins_table(rule)
        moved = 0
        per_prof = {}
        print(f"\n### {RULE_NAME[rule]}")
        for prof, (A, p) in mx.PROFILES.items():
            line = []
            before = moved
            for d in PIN_DIVS:
                ms = [t[(B, p / d, float(A))][0] for B in mx.BANDS]
                moved += sum(ms[i] != today[(prof, d, B)] for i, B in enumerate(mx.BANDS))
                xs = [t[(B, p / d, float(A))][1] for B in mx.BANDS]
                xt = f" x={'/'.join(f'{x:g}' for x in xs)}" if rule == "search" else ""
                line.append(f"{d}: {','.join(map(str, ms))}{xt}")
            print(f"  {prof:>13}: " + " | ".join(line))
            per_prof[prof] = moved - before
        print(f"  entries that differ from today's 200 (4 profiles x 10 divisors x 5 bands): {moved} ("
              + ", ".join(f"{k} {v} of 50" for k, v in per_prof.items()) + ")")

    # X3's gain and the x4 / x16 chains per rule, from the by search.
    print("\n## x4, x8, x16 per rule (MAC/out per channel, latency in output frames), from the `by` search")
    focus = {F(1, 8): ["↓4 · ↓2", "↓2 · ↓2 · ↓2", "↓3 · 3/4 · ↓2", "3/8 · ↓3"],
             F(8): ["↑2 · ↑4", "↑2 · ↑2 · ↑2", "↑2 · 4/3 · ↑3", "↑3 · 8/3"],
             F(1, 4): ["↓2 · ↓2", "↓4"], F(4): ["↑2 · ↑2", "↑4"],
             F(1, 16): ["↓8 · ↓2", "↓2 · ↓4 · ↓2", "↓4 · ↓2 · ↓2", "↓2 · ↓2 · ↓2 · ↓2"],
             F(16): ["↑2 · ↑8", "↑2 · ↑4 · ↑2", "↑2 · ↑2 · ↑4", "↑2 · ↑2 · ↑2 · ↑2"]}
    summary = {}
    for rule in RULES:
        set_rule(rule)
        res = by_table()
        print(f"\n### {RULE_NAME[rule]}")
        for prof in mx.PROFILES:
            for target, _ in BY_TARGETS:
                rows = res[(target, prof)]
                idx = {r[3]: r for r in rows}
                mn, ch = rows[0], r3_choice(rows)
                cells = " ".join(f"[{nm} {float(idx[nm][0]):.4g}]" for nm in focus[target])
                print(f"  {prof:>13} {tname(target):>5}: min {mn[3]} {float(mn[0]):.4g}; R3 {ch[3]} | {cells}")
                summary[(rule, prof, target)] = (idx, mn, ch)

    # The matrix under each rule: economy with and without band 4, transparent per tier.
    print("\n## the matrix under each rule (182 rows): chains that move, against today's tables")
    plan = plan_tables()
    results = {}
    for rule in RULES:
        set_rule(rule)
        set_band4(False)
        ne = run(ECO)
        set_band4(True)
        be, bt = run(ECO), run(TR)
        results[rule] = (ne, be, bt)
    base_ne, base_be, base_bt = results["dA=0"]
    for rule in RULES:
        ne, be, bt = results[rule]
        k8 = (96000, 12000)
        k8u = (12000, 96000)
        k8b, k8bu = (88200, 11025), (11025, 88200)
        mv_ne = [k for k in plan if plan[k] != sh(ne[k][0])]
        mv_be = [k for k in plan if plan[k] != sh(be[k][0])]
        mv_be_x3 = [k for k in plan if sh(base_be[k][0]) != sh(be[k][0])]
        mv_bt = [k for k in plan if sh(be[k][0]) != sh(bt[k][0])]
        num_ne = sum(ne[k][0][0] != base_ne[k][0][0] for k in plan)
        print(f"\n### {RULE_NAME[rule]}")
        print(f"  decision 6 kept, economy: {len(mv_ne)} rows change chain against today's tables "
              f"({sum(within(k) for k in mv_ne)} within); {num_ne} rows' economy MAC/out change")
        print(f"  band 4 admitted (X3), economy: {len(mv_be)} rows change chain against today's tables "
              f"({sum(within(k) for k in mv_be)} within), {len(mv_be_x3)} against X3 with today's β")
        print(f"  X3 + X4: {len(mv_bt)} rows' transparent chain differs from their economy chain "
              f"({sum(within(k) for k in mv_bt)} within)")
        eco_set = {sh(be[k][0]) for k in plan if within(k)}
        tr_set = {sh(bt[k][0]) for k in plan if within(k)}
        print(f"  within-family chains: X3 economy {len(eco_set)}, X3 + X4 union {len(eco_set | tr_set)}, "
              f"transparent-only {len(tr_set - eco_set)}")
        for k in (k8, k8u, k8b, k8bu, (192000, 12000), (12000, 192000)):
            print(f"    {pair(k)}: decision 6 kept {sh(ne[k][0])} {float(ne[k][0][0]):.4g} | X3 {sh(be[k][0])} "
                  f"{float(be[k][0][0]):.4g} | X4 transparent {sh(bt[k][0])} {float(bt[k][0][0]):.4g}")
        if rule != "dA=0":
            for k in mv_ne:
                if within(k):
                    print(f"    decision 6 kept, moved: {pair(k)}: {plan[k]} → {sh(ne[k][0])}")

    # The go/no-go: three half-bands against x4 · x2 at x8, per rule and tier, as R3 reads it:
    # the two-stage chain stays unless it costs more than 10 % above the three-stage one.
    print("\n## go/no-go 2: at x8, do three half-bands beat ↓4 · ↓2 (↑2 · ↑4) by more than R3's 10 %?")
    print("  (the 4th-band chain's MACs over three half-bands' − 1; R3 keeps the two-stage chain at <= +10 %)")
    for rule in RULES:
        for prof in mx.PROFILES:
            line = []
            for target, a, b in ((F(1, 8), "↓4 · ↓2", "↓2 · ↓2 · ↓2"), (F(8), "↑2 · ↑4", "↑2 · ↑2 · ↑2")):
                idx = summary[(rule, prof, target)][0]
                ma, mb = idx[a][0], idx[b][0]
                verdict = "three half-bands win" if ma > mb * (1 + mx.FEWER_STAGES_TOL) else "R3 keeps the 4th-band chain"
                line.append(f"{a} {float(ma):.4g} ({float(idx[a][1]):.4g}) vs {b} {float(mb):.4g} "
                            f"({float(idx[b][1]):.4g}): {float(ma / mb - 1) * 100:+.1f} %, {verdict}")
            print(f"  {RULE_NAME[rule]:>22} {prof:>13}: " + " | ".join(line))

    # The whole matrix's cost per rule: the mean over the 182 rows of the chosen chain's
    # MAC/out (X3 at economy; X3 + X4 at transparent), against today's beta.
    print("\n## the matrix's mean MAC/out over its 182 rows per rule (rational stages and bridge alike)")
    base = None
    for rule in RULES:
        ne, be, bt = results[rule]
        m_ne = sum(ne[k][0][0] for k in plan) / len(plan)
        m_be = sum(be[k][0][0] for k in plan) / len(plan)
        m_bt = sum(bt[k][0][0] for k in plan) / len(plan)
        if base is None:
            base = (m_ne, m_be, m_bt)
        print(f"  {RULE_NAME[rule]:>22}: decision 6 kept, economy {float(m_ne):.2f} ({float(m_ne / base[0] - 1) * 100:+.1f} %)"
              f" | X3 economy {float(m_be):.2f} ({float(m_be / base[0] - 1) * 100:+.1f} % vs today's matrix)"
              f" | X3 + X4 transparent {float(m_bt):.2f} ({float(m_bt / base[2] - 1) * 100:+.1f} % vs X3 + X4 today)")


def mode_keys():
    sys.stdout.write(keys_text(all_keys()))


if __name__ == "__main__":
    BUILD.mkdir(exist_ok=True)
    cmd = sys.argv[1] if len(sys.argv) > 1 else "diff"
    {"diff": mode_diff, "pertier": mode_pertier, "rows": mode_rows, "by": mode_by, "beta": mode_beta,
     "keys": mode_keys}[cmd]()
