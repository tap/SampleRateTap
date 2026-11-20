// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (examples/eight_check.cpp); see ../README.md.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
template <typename T>
void show(const char* name, const T& t, double out_khz) {
    const auto l = t.latency_output_frames();
    std::printf("%-26s %6.1f MAC/out   latency %7.2f out frames = %5.2f ms at %g kHz out\n", name, t.macs_per_output(),
                l.value(), l.value() / out_khz, out_khz);
}
int main() {
    show("down_8 (one stage)", converter<2, down_8>{}, 6.0);
    show("down_3 . 3/4 . down_2", down_3_ratio_3_4_down_2<float, 2>{}, 6.0);
    show("down_2 . down_2 . down_2", chain<2, down_2, down_2, down_2>{}, 6.0);
    show("up_8 (one stage)", converter<2, up_8>{}, 48.0);
    show("up_2 . 4/3 . up_3", up_2_ratio_4_3_up_3<float, 2>{}, 48.0);
    show("up_2 . up_2 . up_2", chain<2, up_2, up_2, up_2>{}, 48.0);
}
