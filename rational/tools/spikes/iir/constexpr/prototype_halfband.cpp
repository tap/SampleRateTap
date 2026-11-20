// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0: moved from the plan's scratch programs, the code unchanged. IIR_PLAN I4's
// v5 feasibility prototype; cx_designs.h carries it forward with one step
// rewritten (README, the constexpr designs).
// Feasibility prototype (scratch): the elliptic (bireciprocal) half-band's
// allpass coefficients computed at compile time, from the published closed form
// (Valenzuela & Constantinides 1983; the nome-series evaluation in harris,
// Multirate Signal Processing, ch. on recursive polyphase filters), with
// constexpr math of our own (no libm). Then checked at run time: the two-path
// response's stopband, against the 70 dB spec, and the same design with libm.
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>

namespace cx {
    constexpr double k_pi = 3.14159265358979323846264338327950288;
    constexpr double abs(double x) {
        return x < 0 ? -x : x;
    }
    constexpr double sqrt(double x) { // Newton, from a good start
        if (x <= 0)
            return 0;
        double r = x > 1 ? x : 1;
        for (int i = 0; i < 200; ++i) {
            const double n = 0.5 * (r + x / r);
            if (n == r)
                break;
            r = n;
        }
        return r;
    }
    constexpr double powi(double x, int n) {
        double r = 1;
        for (int i = 0; i < n; ++i)
            r *= x;
        return r;
    }
    constexpr double sin_small(double x) { // |x| <= pi/4
        double term = x, sum = x;
        for (int n = 1; n < 30; ++n) {
            term *= -x * x / ((2 * n) * (2 * n + 1));
            sum += term;
        }
        return sum;
    }
    constexpr double cos_small(double x) {
        double term = 1, sum = 1;
        for (int n = 1; n < 30; ++n) {
            term *= -x * x / ((2 * n - 1) * (2 * n));
            sum += term;
        }
        return sum;
    }
    constexpr double sin(double x) { // reduce to [-pi/4, pi/4] by quadrant
        const double t = x / (k_pi / 2);
        long         q = static_cast<long>(t >= 0 ? t + 0.5 : t - 0.5);
        const double r = x - static_cast<double>(q) * (k_pi / 2);
        switch (((q % 4) + 4) % 4) {
        case 0:
            return sin_small(r);
        case 1:
            return cos_small(r);
        case 2:
            return -sin_small(r);
        default:
            return -cos_small(r);
        }
    }
    constexpr double cos(double x) {
        return sin(x + k_pi / 2);
    }
    constexpr double tan(double x) {
        return sin(x) / cos(x);
    }
} // namespace cx

template <int NCoef, class M>
constexpr std::array<double, NCoef> halfband_coefficients(double transition, M m) {
    const int order = 2 * NCoef + 1;
    double    k     = m.tan((1 - 2 * transition) * cx::k_pi / 4);
    k *= k;
    const double              kk = m.sqrt(m.sqrt(1 - k * k));
    const double              e = 0.5 * (1 - kk) / (1 + kk), e4 = e * e * e * e;
    const double              q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4))); // the nome
    std::array<double, NCoef> a{};
    for (int idx = 0; idx < NCoef; ++idx) {
        const int c   = idx + 1;
        double    num = 0, den = 0;
        for (int i = 0, s = 1; i < 12; ++i, s = -s)
            num += s * cx::powi(q, i * (i + 1)) * m.sin((2 * i + 1) * c * cx::k_pi / order);
        for (int i = 1, s = -1; i < 12; ++i, s = -s)
            den += s * cx::powi(q, i * i) * m.cos(2 * i * c * cx::k_pi / order);
        const double ww = num * m.sqrt(m.sqrt(q)) / (den + 0.5), w2 = ww * ww;
        const double x = m.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2);
        a[idx]         = (1 - x) / (1 + x);
    }
    return a;
}
struct cx_math {
    constexpr double sqrt(double x) const { return cx::sqrt(x); }
    constexpr double sin(double x) const { return cx::sin(x); }
    constexpr double cos(double x) const { return cx::cos(x); }
    constexpr double tan(double x) const { return cx::tan(x); }
};
struct libm {
    double sqrt(double x) const { return std::sqrt(x); }
    double sin(double x) const { return std::sin(x); }
    double cos(double x) const { return std::cos(x); }
    double tan(double x) const { return std::tan(x); }
};

// economy's last half-band: f_pass = 3/8 of the low rate = 3/16 of the high rate;
// the transition is 1/2 - 2 * 3/16 = 1/8 of the high rate, N9 (4 coefficients).
constexpr auto k_economy = halfband_coefficients<4>(0.125, cx_math{});
static_assert(k_economy[0] > 0 && k_economy[3] < 1, "the coefficients lie in (0, 1)");

int main() {
    const auto rt      = halfband_coefficients<4>(0.125, libm{});
    double     maxdiff = 0;
    for (int i = 0; i < 4; ++i)
        maxdiff = std::fmax(maxdiff, std::fabs(rt[i] - k_economy[i]));
    std::printf("constexpr coefficients: %.17g %.17g %.17g %.17g\n", k_economy[0], k_economy[1], k_economy[2],
                k_economy[3]);
    std::printf("max |constexpr - libm| = %.3g\n", maxdiff);
    // H(z) = 1/2 [A0(z^2) + z^-1 A1(z^2)]: the sorted coefficients alternate paths.
    auto resp = [&](double f) { // f in cycles per high-rate sample
        const std::complex<double> z = std::polar(1.0, 2 * cx::k_pi * f), z2 = z * z;
        std::complex<double>       a0 = 1, a1 = 1;
        for (int i = 0; i < 4; ++i) {
            const auto sec = (k_economy[i] + 1.0 / z2) / (1.0 + k_economy[i] / z2);
            (i % 2 == 0 ? a0 : a1) *= sec;
        }
        return std::abs(0.5 * (a0 + a1 / z));
    };
    double stop = 0, pass_dev = 0;
    for (int i = 0; i <= 16384; ++i) {
        const double f = 0.5 * i / 16384;
        const double h = resp(f);
        if (f >= 0.5 - 3.0 / 16)
            stop = std::fmax(stop, h); // stopband from 5/16
        if (f <= 3.0 / 16)
            pass_dev = std::fmax(pass_dev, std::fabs(1 - h)); // passband to 3/16
    }
    std::printf("stopband (from 5/16 of the high rate): %.1f dB; passband deviation %.2g dB\n", 20 * std::log10(stop),
                -20 * std::log10(1 - pass_dev));
}
