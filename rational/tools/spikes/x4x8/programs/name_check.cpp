// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (examples/name_check.cpp); see ../README.md.
#include <cstdio>
#include <type_traits>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
static_assert(std::is_same_v<down_3_ratio_3_4_down_2<float, 2>, chain<2, down_3, ratio_3_4, down_2>>);
int main() {
    chain<2, down_3, ratio_3_4, down_2> a;
    chain<2, down_2, down_2, down_2>    b;
    converter<2, down_8>                c;
    std::printf("down_3 . 3/4 . down_2: %.2f MAC/out\ndown_2 . down_2 . down_2: %.2f\ndown_8 alone: %.2f\n",
                a.macs_per_output(), b.macs_per_output(), c.macs_per_output());
}
