// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/removal.cpp); see ../README.md.
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
template <class Down, class Up>
void test(const char* nm, const profile& p, unsigned D, double fs, double f) {
    const std::size_t   len = 1 << 20;
    std::vector<double> x(len);
    for (std::size_t i = 0; i < len; ++i)
        x[i] = 0.5 * std::sin(2 * std::numbers::pi * f * double(i) / fs);
    Down                d(p);
    Up                  u(p);
    std::vector<double> lo(d.outputs_for(len));
    lo.resize(d.process(x.data(), len, lo.data()));
    std::vector<double> back(u.outputs_for(lo.size()));
    back.resize(u.process(lo.data(), lo.size(), back.data()));
    const double      T  = d.latency_output_frames().value() * D + u.latency_output_frames().value();
    const std::size_t Ti = std::size_t(std::lround(T));
    double            e = 0, s = 0;
    for (std::size_t n = len / 2; n < back.size(); ++n) {
        double r = back[n] - x[n - Ti];
        e += r * r;
        s += x[n - Ti] * x[n - Ti];
    }
    std::printf("%-34s T=%.3f f=%.1f Hz: removal residual %.1f dB\n", nm, T, f, 10 * std::log10(e / s));
}
int main() {
    for (double f : {204.0, 604.2, 900.0, 1100.0, 1124.0})
        test<basic_chain<double, 1, down_8, down_2>, basic_chain<double, 1, up_2, up_8>>(
            "x16 economy (48k -> 3k)", profile::economy(), 16, 48000, f);
    for (double f : {1800.0, 2400.0, 2450.4, 2490.0})
        test<basic_chain<double, 1, down_2, down_2, down_2>, basic_chain<double, 1, up_2, up_2, up_2>>(
            "x8 transparent 2.2.2 (48k -> 6k)", profile::transparent(), 8, 48000, f);
}
