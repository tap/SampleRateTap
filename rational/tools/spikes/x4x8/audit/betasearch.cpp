// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/betasearch.cpp); see ../README.md.
#include <cstdio>
#include <vector>

#include "tap/dsp/nyquist.h"
// smallest m such that SOME beta in [beta(A-5), beta(A+20)] meets -(A+1) dB on 16384 grid
std::pair<int, double> best(int B, double p, double A) {
    for (int m = 1; m <= 40; m++) {
        for (double Ad = A - 5; Ad <= A + 25; Ad += 0.25) {
            double              beta = tap::dsp::kaiser_beta(Ad);
            std::vector<double> h(tap::dsp::nyquist_length(B, m));
            tap::dsp::design_nyquist(h, B, beta);
            if (tap::dsp::nyquist_worst_stopband_db(h, B, p, 16384) <= -(A + 1))
                return {m, Ad};
        }
    }
    return {0, 0};
}
int main() {
    double      ps[4] = {1.0 / 3, 3.0 / 8, 19.0 / 48, 5.0 / 12};
    double      As[4] = {70, 70, 70, 120};
    const char* nm[4] = {"super_economy", "economy", "balanced", "transparent"};
    for (int k = 0; k < 4; k++) {
        printf("%s\n", nm[k]);
        for (int d : {1, 2, 4, 8}) {
            printf("  div %d:", d);
            for (int B : {2, 4, 8}) {
                auto [m, Ad] = best(B, ps[k] / d, As[k]);
                printf("  B%d m=%d (beta for %.2f dB)", B, m, Ad);
            }
            printf("\n");
        }
    }
}
