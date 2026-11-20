// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/dbl.cpp); see ../README.md.
#include <cstdio>
#include <vector>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
int main() {
    basic_chain<double, 1, down_3, ratio_3_4, down_2> d;
    basic_chain<double, 1, up_2, ratio_4_3, up_3>     u;
    std::vector<double>                               x(8192, 0.0), lo(d.outputs_for(8192) + 2), y(8192 + 16);
    const std::size_t                                 nl   = d.process(x.data(), 8192, lo.data());
    const std::size_t                                 ny   = u.process(lo.data(), nl, y.data());
    bool                                              zero = true;
    for (std::size_t i = 0; i < ny; ++i)
        zero = zero && y[i] == 0.0;
    std::printf("double chains: %zu low, %zu out, zeros in -> exact zeros out: %s\n", nl, ny, zero ? "yes" : "no");
}
