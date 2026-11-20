// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/codebase-fidelity/tie.cpp); see ../README.md.
// X4/X8 plan 3.1: at super_economy, is down_2.down_2 vs one 4th-band down a MAC tie? (matrix.py breaks ties by fewer
// stages)
#include <cstdio>

#include "tap/sr/rational/rational.h"
namespace r = tap::sr::rational;
int main() {
    auto                                           p = r::profile::super_economy();
    r::basic_chain<float, 1, r::down_2, r::down_2> a(p);
    r::basic_chain<float, 1, r::ratio<1, 4>>       b(p);
    r::basic_chain<float, 1, r::up_2, r::up_2>     c(p);
    r::basic_chain<float, 1, r::ratio<4, 1>>       d(p);
    auto                                           pr = [](const char* n, auto& ch) {
        auto m = ch.macs_per_output_exact();
        std::printf("%-12s macs %llu/%llu latency %.4f\n", n, (unsigned long long)m.num, (unsigned long long)m.den,
                                                              ch.latency_output_frames().value());
    };
    pr("down2.down2", a);
    pr("down4", b);
    pr("up2.up2", c);
    pr("up4", d);
}
