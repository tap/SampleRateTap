// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/pins.cpp); see ../README.md.
#include <cstdio>

#include "tap/dsp/nyquist.h"
int main() {
    double      ps[4]  = {1.0 / 3, 3.0 / 8, 19.0 / 48, 5.0 / 12};
    double      As[4]  = {70, 70, 70, 120};
    const char* nm[4]  = {"super_economy", "economy", "balanced", "transparent"};
    int         divs[] = {1, 2, 3, 4, 6, 8, 9, 12, 16};
    for (int k = 0; k < 4; k++) {
        printf("%s\n", nm[k]);
        for (int d : divs) {
            printf("  div %2d:", d);
            for (int B : {2, 3, 4, 6, 8}) {
                auto m = tap::dsp::search_nyquist_m(B, ps[k] / d, As[k], 1.0, 128, 16384);
                printf(" %3zu", m);
            }
            printf("\n");
        }
    }
}
