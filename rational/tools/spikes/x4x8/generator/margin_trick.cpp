// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0 (finding dsp-1): can a fixed beta offset ride DspTap 0.2.0
// unchanged? search_nyquist_m(band, p, A', margin) designs with
// kaiser_beta(A') and accepts at -(A' + margin); called with A' = A + x and
// margin = 1 - x it designs with kaiser_beta(A + x) and accepts at
// -(A + 1), today's criterion. This checks, on every key the generator
// designs (stdin, as pins_beta reads them), that the call returns the m
// pins_beta's rule dA=x finds (its output on argv[2]), for x = argv[1].
// Prints the count that agree and every key that does not.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "tap/dsp/nyquist.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: margin_trick <x> <pins_beta output for dA=x> < keys\n");
        return 2;
    }
    const double  x = std::atof(argv[1]);
    std::ifstream ref(argv[2]);
    std::size_t   band = 0, n = 0, agree = 0;
    long long     pn = 0, pd = 0;
    double        atten = 0.0;
    while (std::cin >> band >> pn >> pd >> atten) {
        std::size_t rb = 0, rm = 0;
        long long   rpn = 0, rpd = 0;
        double      ra = 0.0, rx = 0.0, rw = 0.0;
        ref >> rb >> rpn >> rpd >> ra >> rm >> rx >> rw;
        const double      p = static_cast<double>(pn) / static_cast<double>(pd);
        const std::size_t m = tap::dsp::search_nyquist_m(band, p, atten + x, 1.0 - x, 128, 16384);
        ++n;
        if (m == rm && rb == band && rpn == pn && rpd == pd) {
            ++agree;
        }
        else {
            std::printf("differs: band %zu p %lld/%lld A %g: search_nyquist_m %zu, pins_beta %zu\n", band, pn, pd,
                        atten, m, rm);
        }
    }
    std::printf("x = %g: %zu of %zu keys agree\n", x, agree, n);
    return agree == n ? 0 : 1;
}
