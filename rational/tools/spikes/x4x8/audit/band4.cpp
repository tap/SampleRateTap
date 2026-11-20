// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/codebase-fidelity/band4.cpp); see ../README.md.
// Checks the X4/X8 plan's claims about band 4 today: basic_stage<ratio<1,4>> builds,
// macs, and a chain down4·down2's macs at economy; plus up_2's MACs per output (IIR plan 2.2).
#include <cstdio>

#include "tap/sr/rational/rational.h"
namespace r = tap::sr::rational;
int main() {
    r::basic_stage<float, 1, r::ratio<1, 4>>                       d4(r::profile::economy());
    r::basic_chain<float, 1, r::ratio<1, 4>, r::down_2>            c(r::profile::economy());
    r::basic_chain<float, 1, r::down_2, r::ratio<1, 4>, r::down_2> c16(r::profile::economy());
    std::printf("down4 alone macs/out %.4f  window %zu\n",
                r::basic_chain<float, 1, r::ratio<1, 4>>(r::profile::economy()).macs_per_output(), d4.window_frames());
    std::printf("down4.down2 economy macs %.4f latency %.4f\n", c.macs_per_output(), c.latency_output_frames().value());
    std::printf("down2.down4.down2 economy macs %.4f latency %.4f\n", c16.macs_per_output(),
                c16.latency_output_frames().value());
    for (auto p :
         {r::profile::super_economy(), r::profile::economy(), r::profile::balanced(), r::profile::transparent()}) {
        r::basic_chain<float, 1, r::up_2>   u(p);
        r::basic_chain<float, 1, r::down_2> d(p);
        std::printf("A=%.0f p=%.4f  up_2 macs/out %.2f  down_2 macs/out %.2f\n", p.stopband_atten_db, p.passband_frac,
                    u.macs_per_output(), d.macs_per_output());
    }
}
