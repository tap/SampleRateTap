// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (x4x8/split.cpp); see ../README.md.
// Scratch measurement: the decimate / interpolate round trip as a band
// split, R(x) = up(down(x)), high = x delayed by tau - R(x). Float, mono.
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tap/sr/rational/rational.h>
using namespace tap::sr::rational;

// amplitude of frequency f (cycles/sample) in x[from, from+n), Hann window
double amp(const std::vector<double>& x, std::size_t from, std::size_t n, double f) {
    std::complex<double> acc{};
    double               wsum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (n - 1));
        acc += w * x[from + i] * std::polar(1.0, -2 * std::numbers::pi * f * double(from + i));
        wsum += w;
    }
    return 2 * std::abs(acc) / wsum;
}
double db(double a) {
    return 20 * std::log10(std::max(a, 1e-12));
}

template <class Down, class Up>
void run(const char* name, unsigned D, const profile& p) {
    Down              dn(p);
    Up                up(p);
    const auto        tau_r = dn.latency_output_frames() * tap::dsp::exact_ratio{D, 1} + up.latency_output_frames();
    const std::size_t tau   = tau_r.num / tau_r.den;
    std::printf("%s: round trip tau = %llu/%llu full-rate samples\n", name, (unsigned long long)tau_r.num,
                (unsigned long long)tau_r.den);
    const std::size_t n = 1 << 17;
    // fractions of the LOW rate r = 1/D cycles per full-rate sample
    const double fr[] = {0.10, 0.30, 0.375, 0.40, 0.45, 0.50, 0.55, 0.60, 0.625, 0.65, 0.70};
    std::printf("  f/r    R gain   alias in R (at r-f)   high path at f   down: alias in low band\n");
    for (double k : fr) {
        const double f = k / D; // cycles per full-rate sample
        dn.reset();
        up.reset();
        std::vector<float> x(n), lo(dn.outputs_for(n) + 4), y(n * 2);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = float(0.5 * std::sin(2 * std::numbers::pi * f * double(i)));
        const std::size_t   nl = dn.process(x.data(), n, lo.data());
        const std::size_t   ny = up.process(lo.data(), nl, y.data());
        std::vector<double> r(ny), hi(ny), lod(nl);
        for (std::size_t i = 0; i < ny; ++i) {
            r[i]  = y[i];
            hi[i] = (i >= tau ? x[i - tau] : 0.0) - y[i];
        }
        for (std::size_t i = 0; i < nl; ++i)
            lod[i] = lo[i];
        const std::size_t from = 4096, len = ny - 8192;
        const double      fa  = (k > 0.5 ? (1.0 - k) : -1.0) / D; // alias frequency, full-rate units
        const double      a_r = fa > 0 ? db(amp(r, from, len, fa) / 0.5) : NAN;
        // in the decimated signal: the alias of f lands at r - f, i.e. (1-k) of the low rate
        const double a_lo = k > 0.5 ? db(amp(lod, 512, nl - 1024, 1.0 - k) / 0.5) : NAN;
        std::printf("  %.3f  %7.2f   %8.1f dB           %7.1f dB       %8.1f dB\n", k, db(amp(r, from, len, f) / 0.5),
                    a_r, db(amp(hi, from, len, f) / 0.5), a_lo);
    }
}

int main() {
    run<chain<1, down_2, down_2>, chain<1, up_2, up_2>>("x4 economy (down2.down2 / up2.up2)", 4, profile::economy());
    run<chain<1, down_3, ratio_3_4, down_2>, chain<1, up_2, ratio_4_3, up_3>>("x8 economy (matrix chains)", 8,
                                                                              profile::economy());
    run<chain<1, down_2, down_2, down_2>, chain<1, up_2, up_2, up_2>>("x8 transparent (half-bands)", 8,
                                                                      profile::transparent());
}
