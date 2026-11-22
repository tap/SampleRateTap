#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Generates tests/reference/iir_reference_vectors.h — the golden leg of the
recursive stages (docs/IIR_PLAN.md section 4), independent of their
structure.

For every recursive stage the engine offers (basic_iir_stage: the nonlinear
and the near-linear half-band, down and up, at every named profile and the
design divisors 1, 2, 4 and 8 a power-of-2 cascade uses), the same transfer
function

    H(z) = 1/2 [A0(z^2) + z^-1 A1(z^2)]

is written out as ONE direct-form rational function b(z) / a(z) (the paths'
allpass sections multiplied out at 60 digits with mpmath, then rounded to
float64), and run through scipy.signal.lfilter at the stage's higher rate,
with down-sampling after it (y[k] = (H x)[2k], decimate.h's alignment) or
zero-stuffing before it (y = 2 H (x stuffed)). Nothing of the polyphase
structure is used: no commutator, no paths at the lower rate, no section
cascade. The coefficients are the designs the stages run:
  - nonlinear: Gazsi's explicit formulas at 60 digits (DspTap's mpmath leg,
    submodules/dsptap/tools/iir/iir_design.py, the same published closed
    form the C++ evaluates in constexpr), at the order the C++ selects;
  - near-linear: the committed tables (DspTap's
    include/tap/dsp/detail/iir_tables_data.h), read from the header.

The input is deterministic xorshift noise drawn as integers in Q15 at 0.9 of
full scale, so the same values are exact in every format the tests run:
k / 32768 in float and double, k in Q15, k * 2^16 in Q31. The decimators
read 192 frames (96 outputs), the interpolators the first 96 (192 outputs).
The expected outputs are stored as float64 (17 significant digits), so the
double stage is compared at its own floor and the other formats at theirs
(tests/test_iir_stage.cpp states each tolerance and its measured margin).

Run in a venv with the versions DspTap's tools/iir/requirements.txt pins
(numpy 2.4.6, scipy 1.17.1, mpmath 1.4.1; system Python lacks scipy and
mpmath):

    python3.11 -m venv build-iir-venv
    build-iir-venv/bin/pip install -r submodules/dsptap/tools/iir/requirements.txt
    build-iir-venv/bin/python rational/tools/reference/make_iir_reference_vectors.py

Re-run only when a design or the stage's alignment changes; commit the header.
"""
import pathlib
import re
import sys

import mpmath
import numpy as np
from scipy import signal

ROOT = pathlib.Path(__file__).resolve().parents[2]
DSPTAP = ROOT.parent / "submodules" / "dsptap"
sys.path.insert(0, str(DSPTAP / "tools" / "iir"))
import iir_design  # noqa: E402  (DspTap's mpmath leg of the nonlinear design)

# The named profiles' spec points (A, p num, p den), in profile order, and the
# divisors (rational's design.h; DspTap's iir_design.h keys them by number).
PROFILES = [("super_economy", 70, 1, 3), ("economy", 70, 3, 8), ("balanced", 70, 19, 48),
            ("transparent", 120, 5, 12)]
DIVISORS = (1, 2, 4, 8)
N_DOWN = 192
N_UP = 96
DPS = 60


def xorshift_q15(count, seed):
    s = seed & 0xFFFFFFFF
    out = []
    for _ in range(count):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        out.append(int(round((s % 65536 - 32768) * 0.9)))
    return out


def committed(prefix):
    """The committed tables' coefficient arrays, by name, from DspTap's data header."""
    text = (DSPTAP / "include" / "tap" / "dsp" / "detail" / "iir_tables_data.h").read_text()
    arrays = {}
    for m in re.finditer(r"inline constexpr double (k_" + prefix + r"_[0-9_p]+)\[\] = \{([^}]*)\};", text):
        arrays[m.group(1)] = [mpmath.mpf(v) for v in m.group(2).replace("\n", " ").split(",") if v.strip()]
    rows = {}
    for m in re.finditer(r"\{(\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (k_near_linear_[0-9_]+),", text):
        a, pn, pd, d, order, delay, name = m.groups()
        rows[(int(a), int(pn), int(pd), int(d))] = (int(order), int(delay), arrays[name])
    return rows


def poly_mul(p, q):
    out = [mpmath.mpf(0)] * (len(p) + len(q) - 1)
    for i, a in enumerate(p):
        if a == 0:
            continue
        for j, b in enumerate(q):
            out[i + j] += a * b
    return out


def poly_add(p, q):
    n = max(len(p), len(q))
    return [(p[i] if i < len(p) else 0) + (q[i] if i < len(q) else 0) for i in range(n)]


def stretch(p, m):
    """p(w) with w = z^m, as coefficients of z^-1."""
    out = [mpmath.mpf(0)] * ((len(p) - 1) * m + 1)
    for i, a in enumerate(p):
        out[i * m] = a
    return out


def allpass1(a):
    """(a + w^-1) / (1 + a w^-1): (numerator, denominator) in w^-1."""
    return [a, mpmath.mpf(1)], [mpmath.mpf(1), a]


def allpass2(c1, c2):
    """(c2 + c1 w^-1 + w^-2) / (1 + c1 w^-1 + c2 w^-2)."""
    return [c2, c1, mpmath.mpf(1)], [mpmath.mpf(1), c1, c2]


def cascade(sections):
    num, den = [mpmath.mpf(1)], [mpmath.mpf(1)]
    for n, d in sections:
        num, den = poly_mul(num, n), poly_mul(den, d)
    return num, den


def two_path(path0, path1):
    """H(z) = 1/2 [A0(z^2) + z^-1 A1(z^2)] from the paths' (num, den) in w."""
    n0, d0 = stretch(path0[0], 2), stretch(path0[1], 2)
    n1, d1 = stretch(path1[0], 2), stretch(path1[1], 2)
    b = poly_add(poly_mul(n0, d1), [mpmath.mpf(0)] + poly_mul(n1, d0))
    a = poly_mul(d0, d1)
    return [x / 2 for x in b], a


def nonlinear_transfer(stop_db, p, d):
    order, _ = iir_design.nonlinear_design(stop_db, p, d)
    f_pass = p / (2.0 * d)
    coefs = iir_design.halfband_mp(order, f_pass, DPS)  # ascending, mpf
    path0 = cascade([allpass1(c) for c in coefs[0::2]])
    path1 = cascade([allpass1(c) for c in coefs[1::2]])
    return two_path(path0, path1), order


def near_linear_transfer(row):
    order, delay, secs = row
    sections = []
    i = 0
    left = order
    while left > 0:
        if left >= 2:
            sections.append(allpass2(secs[i], secs[i + 1]))
            i, left = i + 2, left - 2
        else:
            sections.append(allpass1(secs[i]))
            i, left = i + 1, left - 1
    path0 = ([mpmath.mpf(0)] * delay + [mpmath.mpf(1)], [mpmath.mpf(1)])  # w^-K
    path1 = cascade(sections)
    return two_path(path0, path1)


def run(ba, x, down):
    b = np.array([float(v) for v in ba[0]])
    a = np.array([float(v) for v in ba[1]])
    if down:
        return signal.lfilter(b, a, x)[0::2]
    u = np.zeros(2 * len(x))
    u[0::2] = x
    return 2.0 * signal.lfilter(b, a, u)


def fmt(arr, per_line=4):
    return "\n".join("    " + ", ".join(f"{v:.17g}" for v in arr[i:i + per_line]) + ","
                     for i in range(0, len(arr), per_line))


def main():
    mpmath.mp.dps = DPS
    k_in = xorshift_q15(N_DOWN, 0x1D2C3B4A)
    x = np.array(k_in, dtype=np.float64) / 32768.0
    near = committed("near_linear")

    out = []
    out.append("// Generated by tools/reference/make_iir_reference_vectors.py — DO NOT EDIT.")
    out.append("// The recursive stages' golden leg: each stage's transfer function in ONE")
    out.append("// direct form through scipy.signal.lfilter (float64) around down-sampling or")
    out.append("// zero-stuffing, independent of the polyphase structure. See that script.")
    out.append("// SPDX-License-Identifier: MIT")
    out.append("// Copyright 2026 Timothy Place and the SampleRateTap contributors")
    out.append("// NOLINTBEGIN(readability-identifier-naming)")
    out.append("// clang-format off")
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace rational_iir_ref {")
    out.append("")
    out.append("// The input in Q15 (0.9 of full scale): k / 32768 in float and double, k in")
    out.append("// Q15, k * 2^16 in Q31, exact in every format. The interpolators read the")
    out.append("// first 96 frames.")
    out.append(f"inline constexpr std::array<std::int16_t, {N_DOWN}> k_input = {{")
    out.append("\n".join("    " + ", ".join(str(v) for v in k_in[i:i + 12]) + ","
                         for i in range(0, len(k_in), 12)))
    out.append("};")
    out.append("")
    out.append("// One case per stage: phase (0 nonlinear, 1 near-linear), down (1) or up (0),")
    out.append("// profile index (super_economy, economy, balanced, transparent), divisor,")
    out.append("// the nonlinear order or the near-linear N, and the expected outputs.")
    out.append("struct iir_case {")
    out.append("    int phase;")
    out.append("    int down;")
    out.append("    int profile;")
    out.append("    int divisor;")
    out.append("    int order;")
    out.append("    const double* expected;")
    out.append("    int frames;")
    out.append("};")
    out.append("")
    cases = []
    for pi, (name, stop_db, pn, pd) in enumerate(PROFILES):
        p = pn / pd
        for d in DIVISORS:
            nl_ba, nl_order = nonlinear_transfer(stop_db, p, d)
            row = near[(stop_db, pn, pd, d)]
            nr_ba = near_linear_transfer(row)
            for phase, ba, order in ((0, nl_ba, nl_order), (1, nr_ba, row[0])):
                for down in (1, 0):
                    xs = x if down else x[:N_UP]
                    y = run(ba, xs, down)
                    tag = (f"k_{'nonlinear' if phase == 0 else 'near_linear'}_{'down' if down else 'up'}"
                           f"_{name}_d{d}")
                    out.append(f"inline constexpr double {tag}[] = {{")
                    out.append(fmt(y))
                    out.append("};")
                    cases.append((phase, down, pi, d, order, tag, len(y)))
    out.append("")
    out.append(f"inline constexpr std::array<iir_case, {len(cases)}> k_cases = {{{{")
    for phase, down, pi, d, order, tag, n in cases:
        out.append(f"    {{{phase}, {down}, {pi}, {d}, {order}, {tag}, {n}}},")
    out.append("}};")
    out.append("")
    out.append("} // namespace rational_iir_ref")
    out.append("// NOLINTEND(readability-identifier-naming)")
    out.append("")
    dest = ROOT / "tests" / "reference" / "iir_reference_vectors.h"
    dest.write_text("\n".join(out))
    print(f"wrote {dest} ({len(cases)} cases)")


if __name__ == "__main__":
    main()
