// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/sixteen.cpp); see ../README.md.
// Scratch: would admitting the 4th-band stage move rows beyond 1/8 and 8/1?
#include <cstdio>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
using d4 = ratio<1, 4>;
using u4 = ratio<4, 1>;
template <class T>
void row(const char* n) {
    T s(profile::economy());
    std::printf("  %-22s %7.2f MAC/out  latency %.3f out\n", n, s.macs_per_output(), s.latency_output_frames().value());
}
int main() {
    std::printf("1/16 (matrix: down_8 . down_2)\n");
    row<chain<1, down_8, down_2>>("down_8 . down_2");
    row<chain<1, d4, d4>>("down_4 . down_4");
    row<chain<1, d4, down_2, down_2>>("down_4 . down_2 . down_2");
    row<chain<1, down_2, d4, down_2>>("down_2 . down_4 . down_2");
    row<chain<1, d4, down_3, ratio_3_4>>("down_4 . down_3 . 3/4");
    std::printf("16/1 (matrix: up_2 . up_8)\n");
    row<chain<1, up_2, up_8>>("up_2 . up_8");
    row<chain<1, u4, u4>>("up_4 . up_4");
    row<chain<1, up_2, up_2, u4>>("up_2 . up_2 . up_4");
    row<chain<1, up_2, u4, up_2>>("up_2 . up_4 . up_2");
    std::printf("1/32 (matrix: down_2 . down_8 . down_2)\n");
    row<chain<1, down_2, down_8, down_2>>("down_2 . down_8 . down_2");
    row<chain<1, down_8, d4>>("down_8 . down_4");
    row<chain<1, d4, d4, down_2>>("down_4 . down_4 . down_2");
    row<chain<1, down_2, d4, d4>>("down_2 . down_4 . down_4");
    std::printf("1/24 (matrix: down_2 . down_6 . down_2)\n");
    row<chain<1, down_2, down_6, down_2>>("down_2 . down_6 . down_2");
    row<chain<1, down_6, d4>>("down_6 . down_4");
    row<chain<1, d4, down_3, down_2>>("down_4 . down_3 . down_2");
    row<chain<1, down_3, d4, down_2>>("down_3 . down_4 . down_2");
}
