// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/more32.cpp); see ../README.md.
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
using d4 = ratio<1, 4>;
template <class T>
void row(const char* n) {
    T s(profile::economy());
    std::printf("  %-28s %7.2f MAC/out  latency %.3f out\n", n, s.macs_per_output(), s.latency_output_frames().value());
}
int main() {
    std::printf("1/32 economy (matrix: down_2 . down_8 . down_2 = 277; plan's best 4th-band: 285)\n");
    row<chain<1, down_2, down_2, d4, down_2>>("down_2 . down_2 . down_4 . down_2");
    row<chain<1, down_2, d4, down_2, down_2>>("down_2 . down_4 . down_2 . down_2");
    row<chain<1, d4, down_2, d4>>("down_4 . down_2 . down_4");
    row<chain<1, down_2, down_2, down_8>>("down_2 . down_2 . down_8");
    row<chain<1, d4, down_8>>("down_4 . down_8");
    std::printf("1/24 economy (matrix: down_2 . down_6 . down_2 = 209; plan's best 4th-band: 215)\n");
    row<chain<1, down_2, d4, down_3>>("down_2 . down_4 . down_3");
    row<chain<1, down_2, down_3, d4>>("down_2 . down_3 . down_4");
    row<chain<1, down_2, down_2, down_3, down_2>>("down_2 . down_2 . down_3 . down_2");
    row<chain<1, down_2, down_3, down_2, down_2>>("down_2 . down_3 . down_2 . down_2");
}
