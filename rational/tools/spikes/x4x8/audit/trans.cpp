// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/trans.cpp); see ../README.md.
#include <cstdio>
#include <vector>

#include "tap/dsp/nyquist.h"
int main() {
    struct P {
        const char* n;
        int         m;
        double      A;
    } ps[] = {{"economy", 11, 70}, {"transparent", 31, 120}};
    for (auto p : ps) {
        std::vector<double> h(tap::dsp::nyquist_length(2, p.m));
        tap::dsp::design_nyquist(h, 2, tap::dsp::kaiser_beta(p.A));
        for (double f : {0.45, 0.5, 0.55, 0.6})
            printf("%s last half-band at %.2f r_low: %.1f dB\n", p.n, f, tap::dsp::nyquist_response_db(h, 2, f / 2));
    }
}
