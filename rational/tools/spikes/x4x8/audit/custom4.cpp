// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/custom4.cpp); see ../README.md.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
profile custom(double p, double a) {
    profile c;
    c.passband_frac     = p;
    c.stopband_atten_db = a;
    c.taps_per_branch   = {0, 0, 0, 0, 0};
    c.relaxations       = {};
    return c;
}
template <class T>
double per_full(const profile& p, unsigned D, bool down) {
    T s(p);
    return down ? s.macs_per_output() / D : s.macs_per_output();
}
int main() {
    for (auto [p, a] : {std::pair{0.375, 70.0}, std::pair{0.40, 70.0}, std::pair{0.45, 70.0}}) {
        profile c   = custom(p, a);
        double  d42 = per_full<chain<1, ratio<1, 4>, down_2>>(c, 8, true),
               u24  = per_full<chain<1, up_2, ratio<4, 1>>>(c, 8, false);
        double d222 = per_full<chain<1, down_2, down_2, down_2>>(c, 8, true),
               u222 = per_full<chain<1, up_2, up_2, up_2>>(c, 8, false);
        std::printf("p=%.3f A=%.0f: x8 down4.down2 + up2.up4 = %.2f ; three half-bands each way = %.2f\n", p, a,
                    d42 + u24, d222 + u222);
    }
}
