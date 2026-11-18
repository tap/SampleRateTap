#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Applies gate 0's rule (docs/CHANNELS_PLAN.md section 3, item 4) to the
reference and variant measurements: checksums equal, no streaming row up more
than 0.5 % (rises above 0.1 % listed), each engine's summed streaming count not
rising, the gap between the two configurations no wider than the
reference's (within 0.1 points), and the single-count probes' sizes
and M33 stacks, and the multi-count probe. Prints markdown tables and the
verdict, and writes every measurement to <work dir>/gate0_measurements.json.

Usage: compare.py <work dir> [target ...]"""
import json, os, sys

G = sys.argv[1]
TARGETS = sys.argv[2:] or ["m55", "m33", "hexagon", "host"]
ENGINES = ["async", "bridge", "rational"]

def load(t, tree, opt, e):
    p = f"{G}/b-{t}-{tree}-{opt}-{e}.json"
    if not os.path.exists(p):
        return None
    j = json.load(open(p))
    return j.get(t, j)

fails, traces, rows_out = [], [], []
for t in TARGETS:
    for e in ENGINES:
        r, v = load(t, "ref", "OFF", e), load(t, "var", "OFF", e)
        rn, vn = load(t, "ref", "ON", e), load(t, "var", "ON", e)
        if r is None or v is None:
            fails.append(f"{t} {e}: measurements missing"); continue
        sr = sv = 0
        for row in sorted(r):
            a, b = r[row]["insns"], v[row]["insns"]
            d = 100.0 * (b - a) / a
            construct = row.startswith("construct")
            if not construct:
                sr += a; sv += b
            ok = r[row].get("checksum") == v[row].get("checksum")
            if not ok:
                fails.append(f"{t} {e} {row}: checksum ref {r[row].get('checksum')} var {v[row].get('checksum')}")
            # The two configurations: the reference itself is not identical on
            # the QEMU targets (program start-up differs, and a few rows'
            # codegen), so the rule is that the variant's gap between them is
            # no wider than the reference's by more than 0.1 percentage points.
            if rn is None or vn is None or row not in rn or row not in vn:
                fails.append(f"{t} {e} {row}: a no-exceptions measurement is missing")
            else:
                if rn[row].get("checksum") != r[row].get("checksum") or vn[row].get("checksum") != v[row].get("checksum"):
                    fails.append(f"{t} {e} {row}: checksum differs between configurations")
                gr = 100.0 * (rn[row]["insns"] - a) / a
                gv = 100.0 * (vn[row]["insns"] - b) / b
                if abs(gv) - abs(gr) > 0.1:
                    fails.append(f"{t} {e} {row}: configuration gap {gv:+.3f} % against the reference's {gr:+.3f} %")
            if not construct and d > 0.5:
                fails.append(f"{t} {e} {row}: +{d:.2f} %")
            elif not construct and d > 0.1:
                traces.append(f"{t} {e} {row}: +{d:.2f} %")
            rows_out.append((t, e, row, a, b, d, ok, construct))
        ds = 100.0 * (sv - sr) / sr if sr else 0.0
        if sv > sr:
            fails.append(f"{t} {e}: summed streaming count up {ds:+.2f} %")
        rows_out.append((t, e, "(streaming sum)", sr, sv, ds, True, False))

print("| Target | Engine | Row | Reference | Variant | Change | Checksum |")
print("|---|---|---|---|---|---|---|")
for t, e, row, a, b, d, ok, c in rows_out:
    print(f"| {t} | {e} | {row}{' (construction)' if c else ''} | {a:,} | {b:,} | {d:+.2f} % | {'same' if ok else 'DIFFERS'} |")

# Sizes and stacks (exceptions builds, as CI measures ceilings).
print("\n| Target | Engine | Format | Engine .text | Engine .rodata | Image |")
print("|---|---|---|---|---|---|")
for t in TARGETS:
    if t == "host":
        continue
    for e in ENGINES:
        pr, pv = f"{G}/b-{t}-ref-OFF-{e}.ceil.json", f"{G}/b-{t}-var-OFF-{e}.ceil.json"
        if not (os.path.exists(pr) and os.path.exists(pv)):
            fails.append(f"{t} {e}: ceilings missing"); continue
        cr, cv = json.load(open(pr))[t], json.load(open(pv))[t]
        for fmt in cr["size"]:
            cells = []
            for k in ("engine_text", "engine_rodata", "image"):
                a, b = cr["size"][fmt][k]["recorded"], cv["size"][fmt][k]["recorded"]
                cells.append(f"{a:,} → {b:,} ({100.0*(b-a)/a:+.1f} %)" if a else f"{a:,} → {b:,}")
                if k in ("engine_text", "image") and b >= a:
                    fails.append(f"{t} {e} {fmt}: {k} did not fall ({a} -> {b})")
            print(f"| {t} | {e} | {fmt} | " + " | ".join(cells) + " |")
        if "stack" in cr:
            for fmt in cr["stack"]:
                for call in cr["stack"][fmt]:
                    for k in ("static", "measured"):
                        a, b = cr["stack"][fmt][call][k]["recorded"], cv["stack"][fmt][call][k]["recorded"]
                        print(f"STACK {t} {e} {fmt} {call} {k}: {a} -> {b}")
                        if b > a:
                            fails.append(f"{t} {e} {fmt} {call}: {k} stack rose ({a} -> {b})")

print("\nTRACE (rises above 0.1 %):", *traces or ["none"], sep="\n  ")
print("\nFAILS:", *fails or ["none"], sep="\n  ")

# The multi-count probe (object .text; not part of the pass rule: the
# maintainer's to accept, plan section 3 item 4).
mc = {}
p = f"{G}/multicount.tsv"
if os.path.exists(p):
    for line in open(p):
        t, bt, name, text = line.rstrip("\n").split("\t")
        mc[(t, bt, name)] = int(text)
    print("\n| Target | Build | Format | Counts | Today | With K12 | Change |")
    print("|---|---|---|---|---|---|---|")
    for t in ("m33", "m55", "hexagon"):
        for bt in ("Release", "MinSizeRel"):
            for fmt in ("float", "int16"):
                for s_, lab in (("c1", "1"), ("c12", "1, 2"), ("c126", "1, 2, 6")):
                    a, b = mc.get((t, bt, f"p_today_{fmt}_{s_}")), mc.get((t, bt, f"p_k12_{fmt}_{s_}"))
                    if a and b:
                        fl = "Q15" if fmt == "int16" else "float"
                        print(f"| {t} | {bt} | {fl} | {lab} | {a:,} | {b:,} | {100.0 * (b - a) / a:+.1f} % |")

# The placements (bridge and rational, exceptions on): each row against the
# reference, under the same rule.
placements = {}
for n in ("out", "decimators"):
    for t in ("m55", "m33", "hexagon"):
        for e in ("bridge", "rational"):
            p = f"{G}/x-{t}-{n}-{e}.json"
            r = load(t, "ref", "OFF", e)
            if not os.path.exists(p) or r is None:
                continue
            x = json.load(open(p))[t]
            placements.setdefault(n, {}).setdefault(t, {})[e] = x
            rows = sorted(((100.0 * (x[k]["insns"] - r[k]["insns"]) / r[k]["insns"], k, x[k]["checksum"] == r[k]["checksum"])
                           for k in r if k in x and not k.startswith("construct")), reverse=True)
            over = [f"{k} {d:+.2f} %" for d, k, ok in rows if d > 0.5 or not ok]
            print(f"PLACEMENT {n} {t} {e}: worst {rows[0][1]} {rows[0][0]:+.2f} %; "
                  f"{'passes' if not over else 'fails: ' + ', '.join(over)}")

# Every measurement, in one file for the record.
out = {"counts": {}, "ceilings": {}, "placements": placements,
       "multicount": [list(k) + [v] for k, v in sorted(mc.items())]}
for t in TARGETS:
    for tree in ("ref", "var"):
        for opt in ("OFF", "ON"):
            for e in ENGINES:
                j = load(t, tree, opt, e)
                if j is not None:
                    out["counts"].setdefault(tree, {}).setdefault(f"noexc_{opt}", {}).setdefault(t, {})[e] = j
        for e in ENGINES:
            p = f"{G}/b-{t}-{tree}-OFF-{e}.ceil.json"
            if os.path.exists(p):
                out["ceilings"].setdefault(tree, {}).setdefault(t, {})[e] = json.load(open(p))[t]
json.dump(out, open(f"{G}/gate0_measurements.json", "w"), indent=1, sort_keys=True)
