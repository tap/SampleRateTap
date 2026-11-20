// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/verify-dsp/rt_v.cpp); see ../README.md.
// Independent check of the x8 transparent round-trip residual at in-band
// frequencies: residual r = up(down(x)) - x[n-T], projected onto sin/cos at f
// (least squares), so images and other tones are excluded.
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;
template <class Down, class Up>
double resid(const profile& p, unsigned D, double frac /* of r_low */) {
    const std::size_t   len = 1 << 18;
    std::vector<double> x(len);
    const double        f = frac / D; // cycles per full-rate sample
    for (std::size_t i = 0; i < len; ++i)
        x[i] = 0.5 * std::sin(2 * std::numbers::pi * f * double(i));
    Down                d(p);
    Up                  u(p);
    std::vector<double> lo(d.outputs_for(len));
    lo.resize(d.process(x.data(), len, lo.data()));
    std::vector<double> back(u.outputs_for(lo.size()));
    back.resize(u.process(lo.data(), lo.size(), back.data()));
    const double      T  = d.latency_output_frames().value() * D + u.latency_output_frames().value();
    const std::size_t Ti = std::size_t(std::lround(T));
    double            sc = 0, ss = 0;
    std::size_t       n0 = len / 4, n1 = back.size() - 16;
    double            cnt = 0;
    for (std::size_t n = n0; n < n1; ++n) {
        double r  = back[n] - x[n - Ti];
        double ph = 2 * std::numbers::pi * f * double(n);
        double w  = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * double(n - n0) / double(n1 - n0)); // Hann over the span
        sc += w * r * std::cos(ph);
        ss += w * r * std::sin(ph);
        cnt += w;
    }
    const double amp = 2 * std::sqrt(sc * sc + ss * ss) / cnt;
    return 20 * std::log10(amp / 0.5);
}
int main() {
    double worst = -999, wf = 0;
    for (double k = 0.30; k <= 0.4167; k += 0.0004) {
        double r = resid<basic_chain<double, 1, down_2, down_2, down_2>, basic_chain<double, 1, up_2, up_2, up_2>>(
            profile::transparent(), 8, k);
        if (r > worst) {
            worst = r;
            wf    = k;
        }
        if (std::fmod(k * 10000 + 0.5, 100) < 4.1 || r > -112)
            std::printf("k=%.4f r_low: %.1f dB\n", k, r);
    }
    std::printf("worst on [0.30, 5/12]: %.1f dB at %.4f r_low\n", worst, wf);
    double w2 = -999, f2 = 0;
    for (double k = 0.01; k <= 0.375; k += 0.002) {
        double r = resid<basic_chain<double, 1, down_2, down_2, down_2>, basic_chain<double, 1, up_2, up_2, up_2>>(
            profile::transparent(), 8, k);
        if (r > w2) {
            w2 = r;
            f2 = k;
        }
    }
    std::printf("worst on [0.01, 0.375]: %.1f dB at %.4f r_low\n", w2, f2);
}
