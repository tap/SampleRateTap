// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/codebase-fidelity/relax.cpp); see ../README.md.
// The FIR half-band's taps per branch at each design divisor (IIR plan 2.4's "stops getting cheaper at m = 7").
#include <cstdio>

#include "tap/sr/rational/rational.h"
namespace r = tap::sr::rational;
int main() {
    for (std::uint64_t d : {1u, 2u, 3u, 4u, 6u, 8u, 9u, 12u, 16u}) {
        auto                                p = r::profile::economy().relaxed(tap::dsp::exact_ratio{d, 1});
        r::basic_chain<float, 1, r::down_2> c(p);
        std::printf("economy down_2 at divisor %2llu: m=%zu  macs/out %.1f\n", (unsigned long long)d,
                    p.taps_per_branch[0], c.macs_per_output());
    }
}
