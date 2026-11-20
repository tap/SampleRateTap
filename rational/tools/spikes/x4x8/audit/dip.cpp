// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/dsp/dip.cpp); see ../README.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/dsp/fft.h>
#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
int main() {
    const double        fs = 48000, pi = std::numbers::pi;
    const std::size_t   len = 12 * 48000, n = 4096;
    std::vector<double> x(len);
    for (std::size_t i = 0; i < len; i++) {
        double t = i / fs;
        x[i] =
            0.25 * std::sin(2 * pi * 200 * t) + 0.25 * std::sin(2 * pi * 205 * t) + 0.1 * std::sin(2 * pi * 5000 * t);
    }
    down_8_down_2<double, 1> d;
    std::vector<double>      lo(d.outputs_for(len));
    lo.resize(d.process(x.data(), len, lo.data()));
    tap::dsp::real_fft  fft(n);
    std::vector<double> w(n), buf(n);
    for (std::size_t i = 0; i < n; i++)
        w[i] = 0.5 - 0.5 * std::cos(2 * pi * i / n);
    double worst = 0, bestd = 1e9;
    for (std::size_t at = 6000; at < 6000 + 600; at += 30) { // 600 low-rate samples = 0.2 s = one beat period
        for (std::size_t i = 0; i < n; i++)
            buf[i] = lo[at + i] * w[i];
        fft.forward_inplace(buf.data());
        auto   mag = [&](std::size_t k) { return std::hypot(buf[2 * k], buf[2 * k + 1]); };
        double p1 = 0, p2 = 0, mn = 1e9;
        for (std::size_t k = 270; k <= 276; k++)
            p1 = std::max(p1, mag(k));
        for (std::size_t k = 277; k <= 283; k++)
            p2 = std::max(p2, mag(k));
        // the dip: the deepest bin strictly between the two peak bins
        std::size_t k1 = 270, k2 = 283;
        for (std::size_t k = 270; k <= 276; k++)
            if (mag(k) == p1)
                k1 = k;
        for (std::size_t k = 277; k <= 283; k++)
            if (mag(k) == p2)
                k2 = k;
        double mx = 0;
        for (std::size_t k = k1 + 1; k < k2; k++)
            mn = std::min(mn, mag(k));
        double dip = 20 * std::log10(std::min(p1, p2) / mn);
        std::printf("frame at %5zu: dip %.1f dB\n", at, dip);
        bestd = std::min(bestd, dip);
    }
    std::printf("shallowest dip over one beat period: %.1f dB\n", bestd);
}
