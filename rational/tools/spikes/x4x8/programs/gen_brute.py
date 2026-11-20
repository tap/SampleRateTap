#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: generates brute.cpp (the plan's x4x8/brute, section 3.1): every
# chain of 1..3 stages from the vocabulary plus the 4th-band stage whose
# product is 1/4, 4, 1/8 or 8, at the four profiles, each printed as
# "profile|ratio|chain|MAC/out|latency in output frames" from the shipping
# basic_chain. Every chain is printed, the same-spec filter (every
# intermediate rate between the input and output rates) is the reader's:
# generator/x0_matrix.py's `by` mode applies it. The scratch brute.cpp was
# written by hand from this recipe; this generator reproduces it byte for
# byte (checked when it was committed).
import itertools
import sys
from fractions import Fraction as F

stages = {
    "up_2": F(2), "down_2": F(1, 2), "up_3": F(3), "down_3": F(1, 3), "up_6": F(6), "down_6": F(1, 6),
    "up_8": F(8), "down_8": F(1, 8),
    "ratio_3_2": F(3, 2), "ratio_2_3": F(2, 3), "ratio_4_3": F(4, 3), "ratio_3_4": F(3, 4),
    "ratio_8_3": F(8, 3), "ratio_3_8": F(3, 8),
    "ratio<4, 1>": F(4), "ratio<1, 4>": F(1, 4)}
targets = {F(8): "8", F(1, 8): "1/8", F(4): "4", F(1, 4): "1/4"}
rows = []
for n in range(1, 4):
    for combo in itertools.product(stages, repeat=n):
        r = F(1)
        for s in combo:
            r *= stages[s]
        if r in targets:
            rows.append((targets[r], combo))
print(len(rows), "chains", file=sys.stderr)
out = ["#include <cstdio>", "#include <tap/sr/rational/rational.h>", "using namespace tap::sr::rational;",
       "template <class T> void row(const char* t, const char* n, const profile& p, const char* pn) {",
       "  const char* why = T::validate(p); if (why) { std::printf(\"%s|%s|%s|reject\\n\", pn, t, n); return; }",
       "  T s(p); auto l = s.latency_output_frames();",
       "  std::printf(\"%s|%s|%s|%.4f|%.4f\\n\", pn, t, n, s.macs_per_output(), l.value()); }",
       "int main() {"]
for p in ("economy", "super_economy", "balanced", "transparent"):
    out.append(f"  {{ const profile p = profile::{p}();")
    for t, combo in rows:
        out.append(f'    row<chain<1, {", ".join(combo)}>>("{t}", "{" . ".join(combo)}", p, "{p}");')
    out.append("  }")
out.append("}")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
