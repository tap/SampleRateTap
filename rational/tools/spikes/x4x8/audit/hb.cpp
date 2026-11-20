// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/hb.cpp); see ../README.md.
#include <cstdio>
#include <vector>

#include "tap/dsp/nyquist.h"
int main() {
    for (double p : {3.0 / 16, 3.0 / 32}) {
        printf("p=%g (economy relaxed)\n", p);
        for (double A : {70.0, 71.0, 72.0, 74.0, 76.0}) {
            double beta = tap::dsp::kaiser_beta(A);
            printf(" beta(%g)=%.3f:", A, beta);
            for (int m = 3; m <= 8; m++) {
                std::vector<double> h(tap::dsp::nyquist_length(2, m));
                tap::dsp::design_nyquist(h, 2, beta);
                printf(" m%d:%.1f", m, tap::dsp::nyquist_worst_stopband_db(h, 2, p, 16384));
            }
            printf("\n");
        }
        // band 4 too
        printf(" band4 beta(70):");
        for (int m = 3; m <= 7; m++) {
            std::vector<double> h(tap::dsp::nyquist_length(4, m));
            tap::dsp::design_nyquist(h, 4, tap::dsp::kaiser_beta(70));
            printf(" m%d:%.1f", m, tap::dsp::nyquist_worst_stopband_db(h, 4, p, 16384));
        }
        printf("\n");
        printf(" band4 beta(72):");
        for (int m = 3; m <= 7; m++) {
            std::vector<double> h(tap::dsp::nyquist_length(4, m));
            tap::dsp::design_nyquist(h, 4, tap::dsp::kaiser_beta(72));
            printf(" m%d:%.1f", m, tap::dsp::nyquist_worst_stopband_db(h, 4, p, 16384));
        }
        printf("\n");
    }
}
