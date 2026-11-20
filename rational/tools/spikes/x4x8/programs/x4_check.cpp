// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (examples/x4_check.cpp); see ../README.md.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
using down_4 = ratio<1, 4>;
using up_4   = ratio<4, 1>;
template <typename T>
void show(const char* n, const T& t) {
    std::printf("%-24s %7.2f MAC/out  latency %8.3f out frames\n", n, t.macs_per_output(),
                t.latency_output_frames().value());
}
int main() {
    for (auto p : {profile::economy(), profile::transparent()}) {
        std::printf("-- profile passband %.4f\n", p.passband_frac);
        show("down_4 . down_2", chain<2, down_4, down_2>{p});
        show("down_3 . 3/4 . down_2", chain<2, down_3, ratio_3_4, down_2>{p});
        show("down_2 x3", chain<2, down_2, down_2, down_2>{p});
        show("up_2 . up_4", chain<2, up_2, up_4>{p});
        show("up_2 . 4/3 . up_3", chain<2, up_2, ratio_4_3, up_3>{p});
    }
}
