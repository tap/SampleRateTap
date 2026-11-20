#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""IIR-0, go/no-go 7 (IIR_PLAN I12): one four-path stage against the pair of
half-bands it would replace, inside x4, x8 and x16 cascades.

    python3 fourpath.py           # after gen_designs.py; minutes

A four-path stage whose output is at divisor D (input rate 4 D r_min)
replaces the nonlinear half-bands at divisors 2D and D. Its spec is what the
pair has to reject (2.4): the bands that fold onto the passband after the
decimation by 4, [1/4 - f, 1/4 + f] and [1/2 - f, 1/2] of its input rate,
f = p / (4D), at -(A + 1 dB) like every design here. The structure is
H(z) = 1/4 sum_k z^-k A_k(z^4), A_k cascades of one-multiplier sections in
z^4 (harris 2004; Renfors and Saramaki 1987); its passband ripple is tied to
those bands by the M-path power complementarity, so only the stopband is
optimized (iir_design.fourpath_design: SLSQP minimax from random starts over
every layout with the paths at most two sections apart up to 6 sections,
and the balanced layouts, at most one apart, above). The search stops at
the first section count that meets the spec, or at the tie.

MACs per chain output frame per channel, decimating: the pair spends
n_hi * 2D + n_lo * D, the four-path stage S * D for S sections. The bar
(I12): more than 10 % cheaper, so S * D < 0.9 (pair); a tie is S * D = pair.
Placements: D = 1 (x4, and the bottom of x8 and x16), D = 2 (the top of x8,
the middle of x16), D = 4 (the top of x16).

Writes ../outputs/fourpath.json and ../outputs/fourpath.md.
"""
import json
import math
import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import iir_design as d  # noqa: E402

OUT = HERE.parent / "outputs"


def main():
    D_all = json.loads((OUT / "designs.json").read_text())
    rows, js = [], {}
    for name in d.PROFILE_ORDER:
        A, p = d.PROFILES[name]
        P = D_all["profiles"][name]
        for D in (1, 2, 4):
            hi = P["nonlinear"][str(2 * D)]
            lo = P["nonlinear"][str(D)]
            hb_hi = d.TwoPath("nonlinear", a0=np.array(hi["a0"]), a1=np.array(hi["a1"]))
            hb_lo = d.TwoPath("nonlinear", a0=np.array(lo["a0"]), a1=np.array(lo["a1"]))
            n_hi, n_lo = (hi["order"] - 1) // 2, (lo["order"] - 1) // 2
            pair = n_hi * 2 * D + n_lo * D
            pair_sb = d.halfband_pair_on_bands(hb_hi, hb_lo, p, D)
            s_tie = pair // D
            target = -(A + d.MARGIN_DB)
            per_s = {}
            found = None
            for S in range(1, min(s_tie, 16) + 1):
                # every layout with the paths at most two sections apart up to 6
                # sections, then the balanced ones (at most one apart)
                best = d.fourpath_design(A, p, D, S, starts=16 if S <= 5 else 8, spread=2 if S <= 6 else 1)
                if best is None:
                    continue
                per_s[S] = best
                print(f"{name} D{D}: S{S} {best[0]:.1f} dB layout {best[1]}", flush=True)
                if best[0] <= target:
                    found = S
                    break
            # group delays (in chain output frames), passband DC and edge
            fp = p / (4.0 * D)  # over the four-path stage's input rate
            f = np.linspace(1e-6, fp, 2001)
            pair_gd = (hb_hi.group_delay(f) + 2 * hb_lo.group_delay(2 * f)) / (4.0 * D)
            entry = {"pair": [n_hi, n_lo], "pair_macs": pair, "pair_stopband_db": pair_sb, "tie_sections": s_tie,
                     "bar_sections": math.ceil(0.9 * pair / D) - 1,  # the most S with S D < 0.9 pair
                     "found_sections": found, "per_sections": {str(k): v for k, v in per_s.items()},
                     "pair_gd_dc": float(pair_gd[0]), "pair_gd_edge": float(pair_gd[-1])}
            if found:
                paths = per_s[found][2]
                g = d.fourpath_group_delay(paths, f) / (4.0 * D)
                entry["fourpath_gd_dc"] = float(g[0])
                entry["fourpath_gd_edge"] = float(g.max())
            js[f"{name}/D{D}"] = entry
            fs_txt = f"{found} ({found * D} MACs)" if found else f"none up to {max(per_s) if per_s else 0}"
            gd_fp = f"{entry['fourpath_gd_dc']:.2f} .. {entry['fourpath_gd_edge']:.2f}" if found else "-"
            verdict = ("passes" if found and found * D < 0.9 * pair else
                       "tie" if found and found * D == pair else "fails")
            rows.append([f"`{name}`", D, f"N{hi['order']} . N{lo['order']}", pair, f"{pair_sb:.1f}",
                         s_tie, fs_txt, f"{pair_gd[0]:.2f} .. {pair_gd.max():.2f}", gd_fp, verdict])
            print(rows[-1], flush=True)
    OUT.mkdir(exist_ok=True)
    (OUT / "fourpath.json").write_text(json.dumps(js, indent=1) + "\n")
    hdr = ["profile", "D", "pair it replaces", "pair MACs", "pair on the bands dB", "sections at a tie",
           "fewest sections meeting -(A+1)", "pair group delay", "four-path group delay", "bar (> 10 %)"]
    lines = ["# IIR-0: the four-path stage against the half-band pair (generated by python/fourpath.py)", "",
             "Group delays in chain output frames over the chain's passband (DC .. edge).", "",
             "| " + " | ".join(hdr) + " |", "|" + "|".join(["---"] + ["---:"] * (len(hdr) - 1)) + "|"]
    lines += ["| " + " | ".join(str(x) for x in r) + " |" for r in rows]
    lines += ["", "Best stopband per section count (dB on the folding bands, layout):", ""]
    for k, e in js.items():
        lines.append(f"- {k}: " + "; ".join(f"S{s}: {v[0]:.1f} {v[1]}" for s, v in e["per_sections"].items()))
    (OUT / "fourpath.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
