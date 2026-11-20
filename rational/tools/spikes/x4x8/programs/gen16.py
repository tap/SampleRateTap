# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: committed from the session scratchpad (x4x8/gen16.py); see ../README.md.
# Generates x16.cpp: every chain of 1..4 stages (vocabulary + 4th-band) whose
# product is 1/16 or 16/1 and whose every intermediate rate lies between the
# input and output rates (same spec as the endpoints), at the four profiles.
from fractions import Fraction as F
import itertools, sys
stages = {
    "up_2": F(2), "down_2": F(1, 2), "up_3": F(3), "down_3": F(1, 3), "up_6": F(6), "down_6": F(1, 6),
    "up_8": F(8), "down_8": F(1, 8), "ratio<4, 1>": F(4), "ratio<1, 4>": F(1, 4),
    "ratio_3_2": F(3, 2), "ratio_2_3": F(2, 3), "ratio_4_3": F(4, 3), "ratio_3_4": F(3, 4),
    "ratio_8_3": F(8, 3), "ratio_3_8": F(3, 8)}
rows = []
for target, tname in ((F(1, 16), "1/16"), (F(16), "16")):
    lo, hi = min(F(1), target), max(F(1), target)
    for n in range(1, 5):
        for combo in itertools.product(stages, repeat=n):
            r, ok = F(1), True
            for i, s in enumerate(combo):
                r *= stages[s]
                if i < n - 1 and not (lo <= r <= hi):
                    ok = False; break
            if ok and r == target:
                rows.append((tname, combo))
print(len(rows), "chains", file=sys.stderr)
profiles = ["super_economy", "economy", "balanced", "transparent"]
out = ["#include <cstdio>", "#include <tap/sr/rational/rational.h>", "using namespace tap::sr::rational;",
       "template <class T> void row(const char* t, const char* n, const profile& p, const char* pn) {",
       "  if (const char* why = T::validate(p)) { std::printf(\"%s|%s|%s|reject\\n\", pn, t, n); return; }",
       "  T s(p); std::printf(\"%s|%s|%s|%.4f|%.4f\\n\", pn, t, n, s.macs_per_output(), s.latency_output_frames().value()); }",
       "int main() {"]
for p in profiles:
    out.append(f"  {{ const profile p = profile::{p}();")
    for t, combo in rows:
        out.append(f'    row<chain<1, {", ".join(combo)}>>("{t}", "{" . ".join(combo)}", p, "{p}");')
    out.append("  }")
out.append("}")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
