// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Spike X-0: committed from the session scratchpad (audit/completeness/q15band4.cpp); see ../README.md.
// Completeness critic: does the 4th-band stage that X-a admits attain the 70 dB tier in Q15,
// as a decimator and an interpolator, at divisor 1 and at the relaxed divisor 2 it runs at in down4_down2?
// The same measurement as rational/tests/test_fixed_point.cpp's quantized_stopband_db.
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

#include "tap/sr/rational/rational.h"
namespace r = tap::sr::rational;
template <class R>
constexpr std::size_t k_rows = R::k_up == 1 ? R::k_down : R::k_up;
template <class R>
std::size_t design_index(std::size_t row, std::size_t t, std::size_t len) {
    const std::size_t step = R::k_up == 1 ? R::k_down : R::k_up;
    return row + (len - 1 - t) * step;
}
template <typename S, class R>
double qstop(const r::profile& p, double* dcout = nullptr) {
    r::basic_stage<S, 1, R> c(p);
    constexpr std::size_t   l = R::k_up, m = R::k_down, band = l > m ? l : m;
    const std::size_t       t_len = c.row_length();
    const double            scale = tap::dsp::sample_traits<S>::k_coeff_scale;
    std::vector<double>     h(t_len * k_rows<R>, 0.0);
    for (std::size_t row = 0; row < k_rows<R>; ++row)
        for (std::size_t t = 0; t < t_len; ++t)
            h[design_index<R>(row, t, t_len)] =
                static_cast<double>(c.coefficient(row, t)) / scale * (l == 1 ? double(m) : 1.0);
    double dc = 0;
    for (double v : h)
        dc += v;
    if (dcout)
        *dcout = dc;
    const double      f0    = (1.0 - p.passband_frac) / double(band);
    double            worst = -400;
    const std::size_t G     = 4096;
    for (std::size_t g = 0; g < G; ++g) {
        const double         f = f0 + (0.5 - f0) * double(g) / double(G - 1);
        std::complex<double> acc{0, 0};
        for (std::size_t i = 0; i < h.size(); ++i)
            acc += h[i] * std::polar(1.0, -2.0 * std::numbers::pi * f * double(i));
        worst = std::max(worst, 20.0 * std::log10(std::abs(acc) / dc + 1e-300));
    }
    return worst;
}
int main() {
    const char* names[] = {"super_economy", "economy", "balanced", "transparent"};
    r::profile  ps[]    = {r::profile::super_economy(), r::profile::economy(), r::profile::balanced(),
                           r::profile::transparent()};
    for (int i = 0; i < 4; ++i) {
        for (auto div : {tap::dsp::exact_ratio{1, 1}, tap::dsp::exact_ratio{2, 1}}) {
            r::profile   p   = (div.num == 1) ? ps[i] : ps[i].relaxed(div);
            double       dcd = 0, dcu = 0;
            const double d15 = qstop<std::int16_t, r::ratio<1, 4>>(p, &dcd);
            const double u15 = qstop<std::int16_t, r::ratio<4, 1>>(p, &dcu);
            const double d31 = qstop<std::int32_t, r::ratio<1, 4>>(p);
            std::printf("%-13s divisor %llu: Q15 down4 %.1f dB (dc %.6f)  Q15 up4 %.1f dB  Q31 down4 %.1f dB\n",
                        names[i], (unsigned long long)div.num, d15, dcd, u15, d31);
        }
    }
}
