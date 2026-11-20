// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/betarule.cpp); see ../README.md.
#include <cstdio>
#include <vector>

#include "tap/dsp/nyquist.h"
int m_for(int B, double p, double A, double dA) {
    double beta = tap::dsp::kaiser_beta(A + dA);
    for (int m = 1; m <= 60; m++) {
        std::vector<double> h(tap::dsp::nyquist_length(B, m));
        tap::dsp::design_nyquist(h, B, beta);
        if (tap::dsp::nyquist_worst_stopband_db(h, B, p, 16384) <= -(A + 1))
            return m;
    }
    return 0;
}
int main() {
    double p = 3.0 / 8, A = 70;
    for (double dA : {0.0, 2.0, 3.0, 4.0}) {
        int b2_1 = m_for(2, p, A, dA), b2_2 = m_for(2, p / 2, A, dA), b2_4 = m_for(2, p / 4, A, dA),
            b4_2 = m_for(4, p / 2, A, dA), b4_1 = m_for(4, p, A, dA), b8_2 = m_for(8, p / 2, A, dA);
        // decimator MACs per output: band B -> nonzero taps 2m(B-1)+1
        auto d  = [](int B, int m) { return 2 * m * (B - 1) + 1; };
        int  x4 = d(2, b2_2) * 2 + d(2, b2_1), x4one = d(4, b4_1);
        int  x8_42 = d(4, b4_2) * 2 + d(2, b2_1), x8_222 = d(2, b2_4) * 4 + d(2, b2_2) * 2 + d(2, b2_1);
        int  x16_82 = d(8, b8_2) * 2 + d(2, b2_1);
        printf(
            "beta=kaiser_beta(A+%.0f): m band2 div1/2/4 = %d/%d/%d, band4 div1/2 = %d/%d, band8 div2 = %d | x4: 2.2=%d one4=%d | x8: 4.2=%d 2.2.2=%d (gap %.0f%%) | x16 8.2=%d\n",
            dA, b2_1, b2_2, b2_4, b4_1, b4_2, b8_2, x4, x4one, x8_42, x8_222, 100.0 * (x8_222 - x8_42) / x8_42, x16_82);
    }
}
