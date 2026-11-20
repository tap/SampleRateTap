# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: committed from the session scratchpad (audit/redteam-verify/recheck.py); see ../README.md.
# Independent re-run: band 4 admitted vs not, economy and transparent, all 182 pairs.
import importlib.util, sys, time
from fractions import Fraction as F
import pathlib  # spike X-0: matrix.py is found from this file
spec = importlib.util.spec_from_file_location("matrix", pathlib.Path(__file__).resolve().parents[3] / "coverage" / "matrix.py")
mx = importlib.util.module_from_spec(spec); spec.loader.exec_module(mx)
def run(admit4, prof):
    mx.NO_SINGLE_STAGE = set() if admit4 else {4}
    mx.BRIDGE_COST.clear()
    mx.chains_by_stage_count.__dict__  # no cache on this one
    out = {}
    for s in mx.RATES:
        for d in mx.RATES:
            if s == d: continue
            c, m = mx.best_chain(s, d, prof)
            out[(s, d)] = (mx.shape(c[3]), c[0], c[1], mx.shape(m[3]), m[0])
    return out
t=time.time()
te = run(False, "economy"); xe = run(True, "economy"); xt = run(True, "transparent"); tt = run(False, "transparent")
print("time", time.time()-t)
within = lambda k: (k[0] in mx.A_RATES) == (k[1] in mx.A_RATES)
mv = [k for k in te if te[k][0] != xe[k][0]]
print("economy moved:", len(mv), "within:", sum(within(k) for k in mv), "cross:", sum(not within(k) for k in mv))
for k in mv:
    if within(k): print("  W", mx.fmt(k[0]), "->", mx.fmt(k[1]), te[k][0], float(te[k][1]), "=>", xe[k][0], float(xe[k][1]))
# today's matrix: transparent column runs economy chain; check that today's per-tier choice would also differ
d_tr = [k for k in xe if xe[k][0] != xt[k][0]]
print("X3+X4 transparent != economy:", len(d_tr), "within:", sum(within(k) for k in d_tr))
d_tr0 = [k for k in te if te[k][0] != tt[k][0]]
print("X4 alone (no band4) transparent != economy:", len(d_tr0))
# spike X-0: the scratch run pickled its results to recheck.pkl here; nothing reads it, so it is dropped
