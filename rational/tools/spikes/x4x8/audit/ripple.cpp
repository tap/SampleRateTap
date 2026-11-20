// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/ripple.cpp); see ../README.md.
#include <cmath>
#include <cstdio>
#include <vector>

#include "tap/dsp/nyquist.h"
// in-band round-trip residual |1 - prod H_i(f)^2| for the FIR chains, f in fractions of r_min, dense sweep
struct St {
    int    B;
    int    m;
    double A;
    int    div;
}; // stage: band, m, A, design divisor (its lower rate / r_min)
double mag(const std::vector<double>& h, int B, double fnorm) {
    return std::pow(10.0, tap::dsp::nyquist_response_db(h, B, fnorm) / 20);
}
void sweep(const char* nm, std::vector<St> st, double pmax) {
    std::vector<std::vector<double>> hs;
    for (auto s : st) {
        hs.emplace_back(tap::dsp::nyquist_length(s.B, s.m));
        tap::dsp::design_nyquist(hs.back(), s.B, tap::dsp::kaiser_beta(s.A));
    }
    double worst = 0, wf = 0, best = 1;
    for (int i = 1; i <= 20000; i++) {
        double f = pmax * i / 20000.0;
        double g = 1;
        for (size_t k = 0; k < st.size(); k++) {
            double Fhi = st[k].B * st[k].div;
            g *= mag(hs[k], st[k].B, f / Fhi);
        } // f / F_hi in r_min units
        double r = std::fabs(1 - g * g);
        if (r > worst) {
            worst = r;
            wf    = f;
        }
        if (r < best)
            best = r;
    }
    printf("%-40s worst in-band removal residual %.1f dB at %.4f r_min (best %.1f)\n", nm, 20 * std::log10(worst), wf,
           20 * std::log10(best));
}
int main() {
    sweep("x4 economy down2.down2 (m7 div2, m11)", {{2, 7, 70, 2}, {2, 11, 70, 1}}, 0.375);
    sweep("x8 economy X3 down4.down2 (m5 div2, m11)", {{4, 5, 70, 2}, {2, 11, 70, 1}}, 0.375);
    sweep("x16 economy down8.down2 (m5 div2, m11)", {{8, 5, 70, 2}, {2, 11, 70, 1}}, 0.375);
    sweep("x8 transparent 2.2.2 (m9 div4, m10 div2, m31)", {{2, 9, 120, 4}, {2, 10, 120, 2}, {2, 31, 120, 1}},
          5.0 / 12);
}
