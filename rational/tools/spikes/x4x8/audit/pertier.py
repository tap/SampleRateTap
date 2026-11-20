# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: committed from the session scratchpad (audit/redteam/pertier.py); see ../README.md.
# Red-team scratch: what X3 (band 4 admitted) and X4 (a chain per tier) move in the
# generator, measured with rational/tools/coverage/matrix.py itself (imported read-only).
import importlib.util, sys, collections
from fractions import Fraction as F
import pathlib  # spike X-0: matrix.py is found from this file
spec = importlib.util.spec_from_file_location("matrix", pathlib.Path(__file__).resolve().parents[3] / "coverage" / "matrix.py")
mx = importlib.util.module_from_spec(spec); spec.loader.exec_module(mx)

def run(admit4):
    mx.NO_SINGLE_STAGE = set() if admit4 else {4}
    mx.BRIDGE_COST.clear()
    out = {}
    for s in mx.RATES:
        for d in mx.RATES:
            if s == d: continue
            ce, _ = mx.best_chain(s, d, "economy")
            ct, _ = mx.best_chain(s, d, "transparent")
            out[(s, d)] = (ce, ct)
    return out

today = run(False)
x3 = run(True)
def sh(c): return mx.shape(c[3])
moved_eco = [(k, sh(today[k][0]), sh(x3[k][0])) for k in today if sh(today[k][0]) != sh(x3[k][0])]
print("X3 alone: economy chains that move:", len(moved_eco))
for k, a, b in moved_eco: print("  ", mx.fmt(k[0]), "->", mx.fmt(k[1]), ":", a, "=>", b)
moved_tr = [(k, sh(x3[k][0]), sh(x3[k][1])) for k in x3 if sh(x3[k][0]) != sh(x3[k][1])]
print("X3+X4: rows whose transparent chain differs from their economy chain:", len(moved_tr))
for k, a, b in moved_tr: print("  ", mx.fmt(k[0]), "->", mx.fmt(k[1]), ":", a, "=>", b)
def within(k): return (k[0] in mx.A_RATES) == (k[1] in mx.A_RATES)
eco_set = {sh(x3[k][0]) for k in x3 if within(k)}
tr_set = {sh(x3[k][1]) for k in x3 if within(k)}
today_set = {sh(today[k][0]) for k in today if within(k)}
print("distinct within-family chains today:", len(today_set))
print("distinct within-family chains X3 economy:", len(eco_set), " X3+X4 union:", len(eco_set | tr_set))
print("new transparent-only chains:", sorted(tr_set - eco_set))
print("retired (today, not in X3+X4 union):", sorted(today_set - (eco_set | tr_set)))
print("new (X3+X4 union, not today):", sorted((eco_set | tr_set) - today_set))
