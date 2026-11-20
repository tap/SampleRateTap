#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""IIR-0: the spike's tables, measured from ../outputs/designs.json.

    python3 tables.py             # after gen_designs.py, from this folder

writes ../outputs/tables.md (the README cites it) and ../outputs/tables.json:
1. sections, multiplies and attained stopbands per profile, variant and
   divisor (IIR_PLAN I3, I13);
2. cascade MACs per output frame, relaxed and unrelaxed (2.4), both
   variants, decimating and interpolating, with the FIR chains for scale;
3. group delays (2.2) and the near-linear phase identity (go/no-go 2);
4. the round trips' phase and residual at x2, x4, x8 and x16 (question 5);
5. the guard band filter: the Chebyshev II reach per order, the type II
   FIR at the same edges and the same passband spec (0.01 dB peak to
   peak), what each order leaves of the half-band's aliases, and the
   filter before the decimator against after it (2.3);
6. L1 norms per path node and the full-scale square-wave and clipped-input
   peaks (I8);
7. window_frames() for float and double under I6's restated rule.
"""
import json
import math
import pathlib
import sys

import numpy as np
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import iir_design as d  # noqa: E402

OUT = HERE.parent / "outputs"

# rational's pinned FIR half-band taps per branch m per divisor (design.h's
# relaxation tables; band 2 column), for scale: a decimator spends 2m + 1
# MACs per output (2m nonzero taps off centre and the centre tap), an
# interpolator (2m + 1) / 2 per output frame (IIR_PLAN 2.2).
FIR_M = {
    "super_economy": {1: 9, 2: 7, 4: 7, 8: 4},
    "economy": {1: 11, 2: 7, 4: 7, 8: 4},
    "balanced": {1: 13, 2: 7, 4: 7, 8: 4},
    "transparent": {1: 31, 2: 10, 4: 9, 8: 9},
}


def load():
    return json.loads((OUT / "designs.json").read_text())


def two_path(entry, kind):
    if kind == "nonlinear":
        return d.TwoPath("nonlinear", a0=np.array(entry["a0"]), a1=np.array(entry["a1"]))
    return d.TwoPath("near_linear", K=entry["K"], den=np.array(entry["den"]))


def md_table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" if i == 0 else "---:" for i in range(len(header))) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


def main():
    D = load()
    md, js = [], {}

    # 1. sections per profile, variant, divisor
    rows = []
    js["sections"] = {}
    for name in d.PROFILE_ORDER:
        P = D["profiles"][name]
        for div in d.DIVISORS:
            nl = P["nonlinear"][str(div)]
            nr = P["near_linear"][str(div)]
            nsec = [len(s) for s in nr["sos"]]
            rows.append([f"`{name}`", div, f"N{nl['order']}", (nl["order"] - 1) // 2, f"{nl['stopband_db']:.1f}",
                         nr["N"], f"{nsec.count(2)} + {nsec.count(1)}", nr["K"], f"{nr['stopband_db']:.1f}",
                         f"{nr['pole_radius']:.3f}"])
            js["sections"][f"{name}/{div}"] = {"nonlinear_coefs": (nl["order"] - 1) // 2, "near_linear_order": nr["N"],
                                               "near_linear_sections": len(nsec), "K": nr["K"]}
    md.append("## 1. Designs per profile, variant and divisor\n\nNonlinear: the elliptic order and its "
              "coefficients (one-multiplier sections, one multiply each). Near-linear: the allpass path's order N "
              "(= multiplies per path sample), its sections (second-order + first-order), the delay path w^-K, and "
              "the largest pole radius. Stopbands attained on the 16384-point grid; every design meets "
              "-(A + 1 dB).\n")
    md.append(md_table(["profile", "divisor", "nonlinear", "coefs", "stop dB", "near-linear N", "sections",
                        "K", "stop dB", "pole r"], rows))

    # 2. cascade MACs
    md.append("\n## 2. Cascade MACs per output frame per channel\n\nDecimating (one multiply per allpass "
              "coefficient per path sample, at each stage's lower rate, summed per chain output). Interpolating "
              "is half of each stage's figure per its own output (both paths run once per input frame and "
              "interleave two outputs). The FIR column is rational's pinned half-band at the same divisors "
              "(design.h), 2m + 1 per decimated output; the cheapest FIR chains the x4/x8/x16 plan measured are "
              "in IIR_PLAN 2.4.\n")
    rows = []
    js["cascade_macs"] = {}
    for name in d.PROFILE_ORDER:
        P = D["profiles"][name]
        for kind in ("nonlinear", "near_linear", "fir"):
            cells = []
            for fac, divs in ((2, [1]), (4, [2, 1]), (8, [4, 2, 1]), (16, [8, 4, 2, 1])):
                def cost(div, relaxed):
                    use = div if relaxed else 1
                    if kind == "nonlinear":
                        return (P["nonlinear"][str(use)]["order"] - 1) // 2
                    if kind == "near_linear":
                        return P["near_linear"][str(use)]["N"]
                    return 2 * FIR_M[name][use] + 1
                rel = sum(cost(v, True) * v for v in divs)
                unr = sum(cost(v, False) * v for v in divs)
                cells.append(f"{rel} / {unr}")
                js["cascade_macs"][f"{name}/{kind}/x{fac}"] = {"relaxed": rel, "unrelaxed": unr}
            rows.append([f"`{name}`", kind.replace("_", "-"), *cells])
    md.append(md_table(["profile", "variant", "x2", "x4 relaxed / unrelaxed", "x8", "x16"], rows))

    # 3. group delays and the phase identity
    md.append("\n## 3. Group delay and passband phase\n\nHigh-rate samples of each stage, from DC to its "
              "passband edge p / (2d) of its high rate. Nonlinear: the group delay at DC and its maximum (at "
              "the edge). Near-linear: the nominal delay 2K (exact) and the largest deviation from it, and the "
              "passband phase error against linear phase; `bound` is asin of the attained stopband, the "
              "near-linear identity (README).\n")
    rows = []
    js["group_delay"] = {}
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D["profiles"][name]
        for div in d.DIVISORS:
            fp_hi, thp = d.stage_edges(p, div)
            f = np.linspace(0, fp_hi, 8001)
            nl = two_path(P["nonlinear"][str(div)], "nonlinear")
            g = nl.group_delay(f)
            nr = two_path(P["near_linear"][str(div)], "near_linear")
            gr = nr.group_delay(f)
            pe = d.nearlinear_phase_error(nr.den, nr.K, 4 * np.pi * f) / 2
            sb = 10 ** (P["near_linear"][str(div)]["stopband_db"] / 20)
            rows.append([f"`{name}`", div, f"{g[0]:.2f}", f"{g.max():.2f}", 2 * nr.K, f"{np.abs(gr - 2 * nr.K).max():.4f}",
                         f"{np.degrees(np.abs(pe).max()):.5f}", f"{np.degrees(math.asin(sb)):.5f}"])
            js["group_delay"][f"{name}/{div}"] = {"nl_dc": float(g[0]), "nl_max": float(g.max()), "nr_nominal": 2 * nr.K,
                                                  "nr_dev": float(np.abs(gr - 2 * nr.K).max()),
                                                  "nr_phase_deg": float(np.degrees(np.abs(pe).max()))}
    md.append(md_table(["profile", "divisor", "nonlinear DC", "nonlinear max", "near-linear 2K", "deviation",
                        "phase error deg", "bound deg"], rows))

    # 4. round trips
    md.append("\n## 4. Round trips (near-linear)\n\nDown then up through the same relaxed stages (a half-band's "
              "decimator and interpolator share one H(z), so their phase errors add). `phase` is the largest "
              "|sum of phase errors| over the chain's passband [0, p r_min]; `residual` is max |1 - G|, G the round "
              "trip's in-band response with its delay removed (magnitude included); `bound` sums the per-stage "
              "maxima. The plan's target (question 5) is 0.57 deg (-40 dB); the FIR round trip pins -60 dB.\n")
    rows = []
    js["round_trip"] = {}
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D["profiles"][name]
        for fac, divs in ((2, [1]), (4, [2, 1]), (8, [4, 2, 1]), (16, [8, 4, 2, 1])):
            st = [(two_path(P["near_linear"][str(v)], "near_linear"), v) for v in divs]
            r = d.round_trip(st, p)
            # the plan's bound: every stage exactly at -(A) (no margin), 2 per stage
            plan_bound = 2 * len(divs) * math.asin(10 ** (-A / 20))
            rows.append([f"`{name}`", f"x{fac}", f"{r['max_phase_deg']:.4f}", f"{r['residual_db']:.1f}",
                         f"{r['bound_phase_deg']:.4f}", f"{r['bound_residual_db']:.1f}",
                         f"{math.degrees(plan_bound):.4f}", f"{20 * math.log10(2 * math.sin(plan_bound / 2)):.1f}"])
            js["round_trip"][f"{name}/x{fac}"] = r
    md.append(md_table(["profile", "round trip", "phase deg", "residual dB", "bound deg", "bound dB",
                        "every stage at -A: deg", "dB"], rows))

    # 5. guard band filter
    md.append("\n## 5. The guard band filter\n\nAt the low rate r, flat within 0.01 dB peak to peak to the "
              "profile's passband edge p r (Chebyshev II: monotone, rp = 0.01 dB; the type II FIR: equiripple, "
              "0.01 dB peak to peak), A dB from the stopband edge. Edges as fractions of the low Nyquist. "
              "Chebyshev MACs count biquads with b0 = b2 (4) and the first-order section (2). FIR MACs folded "
              "(N / 2), delay (N - 1) / 2 low-rate samples. `aliases < -A from` is where the combined alias "
              "(the nonlinear half-band at its own design, then the filter) falls below -A, as a fraction of "
              "the low Nyquist; `worst alias` is the combined alias at its largest in the guard band.\n")
    rows = []
    js["guard"] = {}
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D["profiles"][name]
        nl = two_path(P["nonlinear"]["1"], "nonlinear")
        for order in (3, 5, 7, 9, 11):
            fs = d.cheby2_stopband_edge(p, order, A)
            z, pz, k = signal.cheby2(order, A, 2 * fs, output="zpk")
            # combined alias: a tone at high-rate g in (1/4, 1/2 - p/2) of the high rate aliases to
            # 1/2 - g of the high rate = (1 - 2 g) of the low rate's... in low-rate cycles: f = 1/2 - g * 2 / 2
            f_low = np.linspace(p, 0.5, 4001)  # low-rate cycles/sample, guard band
            g_hi = 0.5 - f_low / 2  # the high-rate frequency that aliases onto f_low (cycles/high-rate sample)
            Hh = np.abs(nl.response(g_hi))
            _, G = signal.freqz_zpk(z, pz, k, worN=2 * np.pi * f_low)
            comb = d.db(Hh * np.abs(G))
            above = f_low[comb > -A]
            free_from = (above.max() if above.size else p) / 0.5
            fir = d.typeii_min_length(p, fs, A)
            nfir = fir[0] if fir else None
            rows.append([f"`{name}`", order, f"{fs / 0.5:.4f}", d.cheby2_macs(order), nfir,
                         nfir // 2 if nfir else "-", f"{(nfir - 1) / 2:.1f}" if nfir else "-",
                         f"{free_from:.4f}", f"{comb.max():.1f}"])
            js["guard"][f"{name}/{order}"] = {"stop_edge_nyq": fs / 0.5, "cheb_macs": d.cheby2_macs(order),
                                              "fir_taps": nfir, "aliases_below_A_from_nyq": float(free_from),
                                              "worst_alias_db": float(comb.max())}
            print(name, order, nfir, flush=True)
    md.append(md_table(["profile", "order", "stop edge", "Cheb MACs", "type II taps", "FIR MACs", "FIR delay",
                        "aliases < -A from", "worst alias dB"], rows))

    # before the decimator vs after (economy, 16 kHz out)
    A, p = d.PROFILES["economy"]
    fs_after = d.cheby2_stopband_edge(p, 5, A)  # of the low rate
    # the same order and passband designed at twice the rate
    fs_before = d.cheby2_stopband_edge(p / 2, 5, A)  # of the high rate
    z, pz, k = signal.cheby2(5, A, 2 * fs_before, output="zpk")
    pts = {}
    for khz in (7.6, 8.0, 8.4, 10.0):
        _, h = signal.freqz_zpk(z, pz, k, worN=[2 * np.pi * khz / 32.0])
        pts[khz] = float(d.db(h)[0])
    n_match, _ = signal.cheb2ord(p / 2 * 2, fs_after / 2 * 2, 0.01, A)  # edges over the high Nyquist
    n_match_odd = n_match + (1 - n_match % 2)
    md.append("\n### Before the decimator against after it (`economy`, 16 kHz out, 0.01 dB to 6 kHz, 70 dB)\n")
    md.append(f"- After the decimator, order 5 at 16 kHz: 70 dB from {fs_after * 16:.3f} kHz, its zero at 8 kHz, "
              f"{d.cheby2_macs(5)} MACs per output.")
    md.append(f"- The same order and passband at 32 kHz reaches 70 dB only at {fs_before * 32:.2f} kHz; it is "
              + ", ".join(f"{v:.2f} dB at {k_} kHz" for k_, v in pts.items()) + ".")
    md.append(f"- Matching the after-filter (70 dB from {fs_after * 16:.3f} kHz) at 32 kHz needs order {n_match} "
              f"({n_match_odd} odd): {d.cheby2_macs(n_match_odd)} MACs per high-rate sample, "
              f"{2 * d.cheby2_macs(n_match_odd)} per output, {2 * d.cheby2_macs(n_match_odd) / d.cheby2_macs(5):.1f} "
              f"times the after-filter's {d.cheby2_macs(5)}.")
    js["before_after"] = {"after_edge_khz": fs_after * 16, "before_edge_khz": fs_before * 32, "before_points": pts,
                          "match_order": int(n_match), "match_order_odd": int(n_match_odd)}

    # the order rule the spike proposes, and the guard filter's own headroom
    md.append("\n### An order per profile, and the guard filter's fixed-point headroom\n\nA rule that sets one "
              "order per profile from the table above: the lowest odd order whose worst combined alias in the "
              "guard band is at or below -A / 2 (the half-band's aliases at Nyquist sit at -3 dB). Headroom: the "
              "L1 norm from the filter's input to each partial cascade's output (the direct form I states), with "
              "the sections ordered by increasing pole radius and the gain distributed by the L-inf norm "
              "(gen_designs.py), at that order.\n")
    rows = []
    js["guard_rule"] = {}
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D["profiles"][name]
        pick = next((o for o in (3, 5, 7, 9, 11) if js["guard"][f"{name}/{o}"]["worst_alias_db"] <= -A / 2), 11)
        sos = np.array(P["guard"][str(pick)]["sos"])
        x = d.impulse(20000)
        l1s = []
        for s in sos:
            x = signal.lfilter(s[:3], s[3:], x)
            l1s.append(float(np.abs(x).sum()))
        g = js["guard"][f"{name}/{pick}"]
        rows.append([f"`{name}`", pick, f"{g['stop_edge_nyq']:.4f}", f"{g['worst_alias_db']:.1f}", g["cheb_macs"],
                     g["fir_taps"], g["fir_taps"] // 2 if g["fir_taps"] else "-", " / ".join(f"{v:.2f}" for v in l1s)])
        js["guard_rule"][name] = {"order": pick, "l1_partials": l1s}
    md.append(md_table(["profile", "order", "stop edge", "worst alias dB", "Cheb MACs", "type II taps", "FIR MACs",
                        "L1 per partial cascade"], rows))

    # 6. L1 norms and the square wave
    md.append("\n## 6. Fixed-point headroom: L1 norms and full-scale inputs\n\nThe worst-case peak gain (L1 "
              "norm of the impulse response) from a path's input to each node, the largest per path over every "
              "node (section outputs and the difference nodes x - y1 that feed the multiply), at divisor 1 "
              "and over every divisor; and the steady-state peak of any node for full-scale square waves "
              "(periods 2 to 400 high-rate samples) and for a sine at +12 dB clipped to full scale (eight "
              "frequencies). Two integer guard bits hold |node| < 4.\n")
    rows = []
    js["l1"] = {}
    imp = d.impulse(6000)
    for name in d.PROFILE_ORDER:
        P = D["profiles"][name]
        for kind in ("nonlinear", "near_linear"):
            worst_all = 0.0
            per_path_d1 = []
            out_only = []
            for div in d.DIVISORS:
                e = P[kind][str(div)]
                if kind == "nonlinear":
                    paths = [(e["a0"], d.nonlinear_path_nodes), (e["a1"], d.nonlinear_path_nodes)]
                else:
                    paths = [(e["sos"], d.sos_allpass_nodes)]
                for coefs, fn in paths:
                    nodes = fn(coefs, imp)
                    l1 = max(np.abs(v).sum() for _, _, v in nodes) if nodes else 1.0
                    l1o = max(np.abs(v).sum() for t, _, v in nodes if t == "out") if nodes else 1.0
                    worst_all = max(worst_all, l1)
                    if div == 1:
                        per_path_d1.append(l1)
                        out_only.append(l1o)
            sq, clip = square_and_clip(P, kind)
            rows.append([f"`{name}`", kind.replace("_", "-"), " / ".join(f"{x:.2f}" for x in out_only),
                         " / ".join(f"{x:.2f}" for x in per_path_d1), f"{worst_all:.2f}", f"{sq:.3f}", f"{clip:.3f}"])
            js["l1"][f"{name}/{kind}"] = {"d1_outputs": out_only, "d1_all_nodes": per_path_d1, "all_divisors": worst_all,
                                          "square": sq, "clipped": clip}
    md.append(md_table(["profile", "variant", "L1, section outputs (per path, d=1)", "L1, every node (d=1)",
                        "every node, every divisor", "square-wave peak", "clipped-sine peak"], rows))

    # 7. window frames (float, double) by I6's rule
    md.append("\n## 7. window_frames() for float and double (I6, restated)\n\nThe count from the format's "
              "largest finite value down to its smallest normal value through the stage's largest pole radius "
              "(the one-multiplier section's |a|, a second-order section's radius), in low-rate (path) frames: "
              "ceil(ln(max / min_normal) / ln(1 / r)). The cascade's composite decay is checked in C++ "
              "(fixed_point/decay.cpp, outputs/decay.txt). Divisor 1.\n")
    rows = []
    js["window_float"] = {}
    for name in d.PROFILE_ORDER:
        P = D["profiles"][name]
        for kind in ("nonlinear", "near_linear"):
            e = P[kind]["1"]
            r = max(e["a0"] + e["a1"]) if kind == "nonlinear" else e["pole_radius"]
            f32 = math.ceil((math.log(float(np.finfo(np.float32).max)) - math.log(float(np.finfo(np.float32).tiny))) / math.log(1 / r))
            f64 = math.ceil((math.log(float(np.finfo(np.float64).max)) - math.log(float(np.finfo(np.float64).tiny))) / math.log(1 / r))
            rows.append([f"`{name}`", kind.replace("_", "-"), f"{r:.4f}", f32, f64])
            js["window_float"][f"{name}/{kind}"] = {"r": r, "float": f32, "double": f64}
    md.append(md_table(["profile", "variant", "largest pole radius", "float frames", "double frames"], rows))

    # 8. coefficient quantization
    md.append("\n## 8. The stopband after coefficient quantization (I8's formats)\n\nOne-multiplier coefficients "
              "rounded to Q31, second-order section coefficients to Q2.30 (the near-linear allpass path rebuilt "
              "from its rounded sections), divisor 1; the attained stopband on the 16384-point grid.\n")
    rows = []
    js["quantized"] = {}
    q31 = lambda v: np.round(np.asarray(v) * 2.0 ** 31) / 2.0 ** 31
    q230 = lambda v: np.round(np.asarray(v) * 2.0 ** 30) / 2.0 ** 30
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D["profiles"][name]
        fs = d.stopband_grid(p / 2)
        e = P["nonlinear"]["1"]
        nl_q = d.TwoPath("nonlinear", a0=q31(e["a0"]), a1=q31(e["a1"]))
        r = P["near_linear"]["1"]
        den = np.array([1.0])
        for s in r["sos"]:
            den = np.convolve(den, [1.0, *q230(s)] if len(s) == 2 else [1.0, *q31(s)])
        nr_q = d.TwoPath("near_linear", K=r["K"], den=den)
        sq_nl = float(d.db(nl_q.response(fs)).max())
        sq_nr = float(d.db(nr_q.response(fs)).max())
        rows.append([f"`{name}`", f"{e['stopband_db']:.2f}", f"{sq_nl:.2f}", f"{r['stopband_db']:.2f}", f"{sq_nr:.2f}"])
        js["quantized"][name] = {"nonlinear": sq_nl, "near_linear": sq_nr}
    md.append(md_table(["profile", "nonlinear, double", "nonlinear, Q31", "near-linear, double", "near-linear, Q2.30 / Q31"],
                       rows))

    (OUT / "tables.md").write_text("# IIR-0 tables (generated by python/tables.py)\n\n" + "\n".join(md) + "\n")
    (OUT / "tables.json").write_text(json.dumps(js, indent=1) + "\n")


def square_and_clip(P, kind):
    """Largest steady-state |node| over every divisor for full-scale square
    waves and clipped sines, through the decimator's paths (even samples to
    path 0, odd to path 1)."""
    worst_sq = worst_clip = 0.0
    n = np.arange(8000)
    inputs_sq = [np.where((n % per) < per / 2, 1.0, -1.0) for per in range(2, 401)]
    inputs_clip = [np.clip(4.0 * np.sin(2 * np.pi * fr * n), -1, 1) for fr in (0.001, 0.01, 0.03, 0.07, 0.11, 0.17, 0.23, 0.29)]
    for div in d.DIVISORS:
        e = P[kind][str(div)]
        for inputs, which in ((inputs_sq, "sq"), (inputs_clip, "clip")):
            for x in inputs:
                if kind == "nonlinear":
                    pk = 0.0
                    for coefs, start in ((e["a0"], 0), (e["a1"], 1)):
                        for _, _, v in d.nonlinear_path_nodes(coefs, x[start::2]):
                            pk = max(pk, np.abs(v[500:]).max())
                else:
                    pk = max(np.abs(v[500:]).max() for _, _, v in d.sos_allpass_nodes(e["sos"], x[1::2]))
                if which == "sq":
                    worst_sq = max(worst_sq, pk)
                else:
                    worst_clip = max(worst_clip, pk)
    return float(worst_sq), float(worst_clip)


if __name__ == "__main__":
    main()
