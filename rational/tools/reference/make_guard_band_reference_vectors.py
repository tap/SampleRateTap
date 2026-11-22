#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""Generates tests/reference/guard_band_reference_vectors.h — the golden leg
of the guard band filter (docs/IIR_PLAN.md I7 and section 4), independent of
its structure and, for the Chebyshev II, of its designer.

For each of the six distinct named settings (rational's guard_band_settings:
the profiles' A and passband edge p at I7's order behind the half-band the
filter serves, docs/IIR_PLAN.md questions 14 and 20: 5 / 7 / 7 / 7 behind the
nonlinear half-band, 7 / 7 / 7 / 11 behind the FIR and near-linear ones), both
forms:
  - nonlinear: the odd-order Chebyshev II designed by scipy.signal.cheby2 at
    the stop edge the order sets (the closed form Os = Op cosh(acosh(D) / n)
    with Op = tan(pi p) and D^2 = (10^(A/10) - 1) / (10^(0.01/10) - 1),
    evaluated with mpmath at 60 digits, as Antoniou 2006 states it), as
    scipy's own second-order sections run through scipy.signal.sosfilt. None
    of DspTap's design is used for the output: not its poles, its pairing,
    its section order or its gains. The script checks that the two designs
    are one transfer function (DspTap's mpmath leg, submodules/dsptap/tools/
    iir/iir_design.py, against scipy's on 4097 frequencies), that the
    direct form b(z) / a(z) through scipy.signal.lfilter agrees with
    sosfilt (within 1e-8: the order-11 polynomials lose digits), and that
    the stop edge is the one DspTap's committed rows carry;
  - linear: DspTap's committed type II row (include/tap/dsp/detail/
    iir_tables_data.h, read from the header) through scipy.signal.lfilter:
    independent of the stage's history, compaction and channel machine;
  - linear in Q15: at the four 70 dB settings, where DspTap (0.4.0) commits
    a Q15 row (k_guard_band_fir_q15, which rational's Q15 linear form runs),
    that row's double design taps through scipy.signal.lfilter, a case of its
    own (k_q15_linear_cases); the script checks the row is symmetric and its
    stop edge the double row's. At 120 dB Q15 runs the double row, whose case
    serves every format.

The input is deterministic xorshift noise drawn as integers in Q15 at 0.9 of
full scale, so the same values are exact in every format the tests run:
k / 32768 in float and double, k in Q15, k * 2^16 in Q31; 256 frames in,
256 out (the filter is 1:1). Each Chebyshev case also carries scipy's
group delay (scipy.signal.group_delay of each of its sections, summed) at 0,
half and all of the passband edge, in frames, for the latency members (I5).
The expected
outputs are stored as float64 (17 significant digits), so the double filter
is compared at its own floor and the other formats at theirs
(tests/test_guard_band.cpp states each tolerance and its measured margin).

Run in a venv with the versions DspTap's tools/iir/requirements.txt pins
(numpy 2.4.6, scipy 1.17.1, mpmath 1.4.1), as
make_iir_reference_vectors.py is:

    python3.11 -m venv build-iir-venv
    build-iir-venv/bin/pip install -r submodules/dsptap/tools/iir/requirements.txt
    build-iir-venv/bin/python rational/tools/reference/make_guard_band_reference_vectors.py

Re-run only when a named setting or a committed design changes; commit the
header.
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
import iir_design  # noqa: E402  (DspTap's mpmath leg of the Chebyshev II)

# The six distinct named settings (A, p num, p den, order), in the order of
# rational's guard_band.h detail::k_guard_band_named; DspTap's tables key them by
# number.
SETTINGS = [("super_economy_nonlinear", 70, 1, 3, 5), ("super_economy_fir", 70, 1, 3, 7),
            ("economy", 70, 3, 8, 7), ("balanced", 70, 19, 48, 7),
            ("transparent_nonlinear", 120, 5, 12, 7), ("transparent_fir", 120, 5, 12, 11)]
RIPPLE_DB = 0.01
N_FRAMES = 256
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


def stop_edge(order, p, stop_db):
    """The order-n Chebyshev II's stop edge, a fraction of its rate."""
    with mpmath.workdps(DPS):
        a10 = mpmath.mpf(10) ** (mpmath.mpf(stop_db) / 10) - 1
        r10 = mpmath.mpf(10) ** (mpmath.mpf(RIPPLE_DB) / 10) - 1
        op = mpmath.tan(mpmath.pi * p)
        os_ = op * mpmath.cosh(mpmath.acosh(mpmath.sqrt(a10 / r10)) / order)
        return mpmath.atan(os_) / mpmath.pi


def committed_fir():
    """DspTap's committed type II rows: {(A, num, den, order): (taps, stop_edge)}."""
    text = (DSPTAP / "include" / "tap" / "dsp" / "detail" / "iir_tables_data.h").read_text()
    arrays = {}
    for m in re.finditer(r"inline constexpr double (k_guard_fir_[0-9_]+)\[\] = \{([^}]*)\};", text):
        arrays[m.group(1)] = np.array([float(v) for v in m.group(2).replace("\n", " ").split(",") if v.strip()])
    rows = {}
    for m in re.finditer(r"\{(\d+), (\d+), (\d+), (\d+), ([-0-9.e]+), (k_guard_fir_[0-9_]+),", text):
        a, pn, pd, order, edge, name = m.groups()
        rows[(int(a), int(pn), int(pd), int(order))] = (arrays[name], float(edge))
    return rows


def committed_fir_q15():
    """DspTap's committed Q15 type II rows: {(A, num, den, order): (taps, stop_edge)}."""
    text = (DSPTAP / "include" / "tap" / "dsp" / "detail" / "iir_tables_data.h").read_text()
    arrays = {}
    for m in re.finditer(r"inline constexpr double (k_guard_fir_q15_[0-9_]+)\[\] = \{([^}]*)\};", text):
        arrays[m.group(1)] = np.array([float(v) for v in m.group(2).replace("\n", " ").split(",") if v.strip()])
    rows = {}
    for m in re.finditer(r"\{(\d+), (\d+), (\d+), (\d+), ([-0-9.e]+), (k_guard_fir_q15_[0-9_]+),", text):
        a, pn, pd, order, edge, name = m.groups()
        rows[(int(a), int(pn), int(pd), int(order))] = (arrays[name], float(edge))
    return rows


def check_designs_agree(sos, order, p, stop_db):
    """scipy's design and DspTap's mpmath leg are one transfer function."""
    secs, _ = iir_design.cheby2_mp(order, p, stop_db, rp=RIPPLE_DB, dps=40)
    f = np.linspace(0.0, 0.5, 4097)
    _, h_scipy = signal.sosfreqz(sos, worN=2.0 * np.pi * f)
    worst = 0.0
    for fi, hs in zip(f, h_scipy):
        hd = float(iir_design.sections_mag_mp(secs, mpmath.mpf(float(fi))))
        worst = max(worst, abs(abs(hs) - hd))
    assert worst < 1e-12, f"scipy's Chebyshev II and DspTap's differ by {worst}"
    return worst


def fmt(arr, per_line=4):
    return "\n".join("    " + ", ".join(f"{v:.17g}" for v in arr[i:i + per_line]) + ","
                     for i in range(0, len(arr), per_line))


def main():
    mpmath.mp.dps = DPS
    k_in = xorshift_q15(N_FRAMES, 0x2E3D4C5B)
    x = np.array(k_in, dtype=np.float64) / 32768.0
    fir = committed_fir()
    fir_q15 = committed_fir_q15()

    out = []
    out.append("// Generated by tools/reference/make_guard_band_reference_vectors.py — DO NOT EDIT.")
    out.append("// The guard band filter's golden leg: the Chebyshev II as scipy.signal.cheby2's")
    out.append("// own sections through scipy.signal.sosfilt, the type II FIR's committed taps")
    out.append("// through scipy.signal.lfilter (float64), independent of the stage's structure.")
    out.append("// SPDX-License-Identifier: MIT")
    out.append("// Copyright 2026 Timothy Place and the SampleRateTap contributors")
    out.append("// NOLINTBEGIN(readability-identifier-naming)")
    out.append("// clang-format off")
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace rational_guard_ref {")
    out.append("")
    out.append("// The input in Q15 (0.9 of full scale): k / 32768 in float and double, k in")
    out.append("// Q15, k * 2^16 in Q31, exact in every format.")
    out.append(f"inline constexpr std::array<std::int16_t, {N_FRAMES}> k_input = {{")
    out.append("\n".join("    " + ", ".join(str(v) for v in k_in[i:i + 12]) + ","
                         for i in range(0, len(k_in), 12)))
    out.append("};")
    out.append("")
    out.append("// One case per named setting and form: form (0 nonlinear, 1 linear), setting")
    out.append("// index (super_economy behind the nonlinear half-band, then behind the FIR and")
    out.append("// near-linear ones, economy, balanced, transparent likewise), the Chebyshev II")
    out.append("// order, the FIR's taps (0 for the Chebyshev), the stop edge (a fraction of")
    out.append("// the rate), the expected outputs, and for the Chebyshev the direct form's")
    out.append("// group delay at 0, half and all of the passband edge.")
    out.append("struct guard_case {")
    out.append("    int form;")
    out.append("    int setting;")
    out.append("    int order;")
    out.append("    int taps;")
    out.append("    double stop_edge;")
    out.append("    const double* expected;")
    out.append("    int frames;")
    out.append("    std::array<double, 3> gd_f;   // cycles per frame")
    out.append("    std::array<double, 3> gd;     // group delay, frames")
    out.append("};")
    out.append("")
    cases = []
    q15_cases = []
    for si, (name, stop_db, pn, pd, order) in enumerate(SETTINGS):
        p = pn / pd
        edge = stop_edge(order, mpmath.mpf(pn) / pd, stop_db)
        taps, committed_edge = fir[(stop_db, pn, pd, order)]
        assert abs(float(edge) - committed_edge) < 1e-12, (name, float(edge), committed_edge)
        # nonlinear: scipy's own design at the same stop edge, its own sections.
        sos = signal.cheby2(order, stop_db, 2.0 * float(edge), btype="low", output="sos")
        worst = check_designs_agree(sos, order, mpmath.mpf(pn) / pd, stop_db)
        y_nl = signal.sosfilt(sos, x)
        b, a = signal.sos2tf(sos)
        y_df = signal.lfilter(b, a, x)
        # The direct form's polynomials, their zeros on the unit circle, lose
        # digits as the order grows: 4.6e-12 at order 7, 1.7e-9 at order 11.
        assert np.max(np.abs(y_nl - y_df)) < 1e-8, (name, np.max(np.abs(y_nl - y_df)))
        # The group delay of scipy's sections, summed (the direct form's
        # polynomial, its zeros on the unit circle, loses digits there).
        f_gd = np.array([0.0, 0.5 * p, p])
        gd = sum(signal.group_delay((sec[:3], sec[3:]), w=2.0 * np.pi * f_gd)[1] for sec in sos)
        tag = f"k_nonlinear_{name}"
        out.append(f"inline constexpr double {tag}[] = {{")
        out.append(fmt(y_nl))
        out.append("};")
        cases.append((0, si, order, 0, float(edge), tag, len(y_nl), list(f_gd), list(gd)))
        print(f"{name}: order {order}, stop edge {float(edge):.6f}, scipy and DspTap within {worst:.2g}, "
              f"sosfilt and the direct form within {np.max(np.abs(y_nl - y_df)):.2g}")
        # linear: the committed row through lfilter.
        y_li = signal.lfilter(taps, [1.0], x)
        tag = f"k_linear_{name}"
        out.append(f"inline constexpr double {tag}[] = {{")
        out.append(fmt(y_li))
        out.append("};")
        cases.append((1, si, order, len(taps), float(edge), tag, len(y_li), [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]))
        # linear in Q15: the Q15 row's double taps through lfilter, where there is one.
        if (stop_db, pn, pd, order) in fir_q15:
            taps_q15, edge_q15 = fir_q15[(stop_db, pn, pd, order)]
            assert len(taps_q15) % 2 == 0 and np.array_equal(taps_q15, taps_q15[::-1]), name
            assert abs(edge_q15 - committed_edge) < 1e-12, (name, edge_q15, committed_edge)
            y_q15 = signal.lfilter(taps_q15, [1.0], x)
            tag = f"k_linear_q15_{name}"
            out.append(f"inline constexpr double {tag}[] = {{")
            out.append(fmt(y_q15))
            out.append("};")
            q15_cases.append((1, si, order, len(taps_q15), float(edge), tag, len(y_q15), [0.0, 0.0, 0.0],
                              [0.0, 0.0, 0.0]))
            print(f"{name}: the Q15 row, {len(taps_q15)} taps (the double row {len(taps)})")
    out.append("")
    out.append(f"inline constexpr std::array<guard_case, {len(cases)}> k_cases = {{{{")
    for form, si, order, ntaps, edge, tag, n, gf, gd in cases:
        gfs = ", ".join(f"{v:.17g}" for v in gf)
        gds = ", ".join(f"{v:.17g}" for v in gd)
        out.append(f"    {{{form}, {si}, {order}, {ntaps}, {edge:.17g}, {tag}, {n}, {{{gfs}}}, {{{gds}}}}},")
    out.append("}};")
    out.append("")
    out.append("// The linear form in Q15 at the settings where DspTap commits a Q15 row (the")
    out.append("// 70 dB ones): that row's design taps through lfilter. Q15 runs these in place")
    out.append("// of k_cases' linear cases at the same settings.")
    out.append(f"inline constexpr std::array<guard_case, {len(q15_cases)}> k_q15_linear_cases = {{{{")
    for form, si, order, ntaps, edge, tag, n, gf, gd in q15_cases:
        out.append(f"    {{{form}, {si}, {order}, {ntaps}, {edge:.17g}, {tag}, {n}, {{0, 0, 0}}, {{0, 0, 0}}}},")
    out.append("}};")
    out.append("")
    out.append("} // namespace rational_guard_ref")
    out.append("// NOLINTEND(readability-identifier-naming)")
    out.append("")
    dest = ROOT / "tests" / "reference" / "guard_band_reference_vectors.h"
    dest.write_text("\n".join(out))
    print(f"wrote {dest} ({len(cases)} cases, {len(q15_cases)} Q15 linear cases)")


if __name__ == "__main__":
    main()
