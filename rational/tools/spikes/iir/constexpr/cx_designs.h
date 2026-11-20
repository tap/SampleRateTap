// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 spike (scratch): the two closed-form designs of IIR_PLAN I4, written
// once over a math policy M so the same source runs in constexpr (iir_cx's
// series, no libm) and at run time through libm, for the comparison.
//
// - halfband<NCoef, M>(transition): the elliptic (bireciprocal) half-band's
//   allpass coefficients, from the prototype (prototype_halfband.cpp) with
//   one step rewritten without its cancellation (below). The
//   formulas: the selectivity k = tan^2(omega_p / 2) and the half-band's
//   coefficient a = (1 - x) / (1 + x) (Gazsi 1985; Valenzuela and
//   Constantinides 1983), the modulus, the nome series and the critical
//   frequencies' theta-function series (the elliptic approximation as
//   Antoniou 2006 gives it).
// - cheby2<Order, M>(p, A, rp, grid): the odd-order Chebyshev II low-pass
//   flat within rp dB to p (a fraction of the rate) and A dB down from the
//   stopband edge that follows: the analogue prototype (its poles the
//   reciprocals of Chebyshev I poles at the stopband ripple, its zeros at
//   Omega_s / cos(theta_k)), the bilinear transform with prewarping (Antoniou
//   2006), each conjugate pole pair with the zero pair of the same index
//   (the nearest), the sections ordered by increasing pole radius, and the
//   gain distributed by the L-inf norm over a grid of `grid` frequencies
//   (Jackson 1970), or every section at unity DC gain when grid is 0. The
//   odd order puts one first-order section with its zero at z = -1.
#pragma once

#include <array>
#include <cmath>

#include "cx_math.h"

namespace iir_cx {

    struct cx_policy {
        static constexpr double sqrt(double x) { return iir_cx::sqrt(x); }
        static constexpr double sin(double x) { return iir_cx::sin(x); }
        static constexpr double cos(double x) { return iir_cx::cos(x); }
        static constexpr double tan(double x) { return iir_cx::tan(x); }
        static constexpr double exp(double x) { return iir_cx::exp(x); }
        static constexpr double log(double x) { return iir_cx::log(x); }
    };
    struct libm_policy {
        static double sqrt(double x) { return std::sqrt(x); }
        static double sin(double x) { return std::sin(x); }
        static double cos(double x) { return std::cos(x); }
        static double tan(double x) { return std::tan(x); }
        static double exp(double x) { return std::exp(x); }
        static double log(double x) { return std::log(x); }
    };

    template <int NCoef, class M = cx_policy>
    constexpr std::array<double, NCoef> halfband(double transition) {
        const int order = 2 * NCoef + 1;
        double    k     = M::tan((1 - 2 * transition) * k_pi / 4);
        k *= k;
        const double kk = M::sqrt(M::sqrt(1 - k * k));
        // e = (1 - kk) / (2 (1 + kk)) without the cancellation in 1 - kk, which
        // costs the relaxed (wide-transition) designs up to 1e5 ulps:
        // 1 - kk = (1 - kk^4) / ((1 + kk)(1 + kk^2)) = k^2 / ((1 + kk)(1 + kk^2)).
        const double              e = 0.5 * (k * k / ((1 + kk) * (1 + kk * kk))) / (1 + kk), e4 = e * e * e * e;
        const double              q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4))); // the nome
        std::array<double, NCoef> a{};
        for (int idx = 0; idx < NCoef; ++idx) {
            const int c   = idx + 1;
            double    num = 0, den = 0;
            for (int i = 0, s = 1; i < 12; ++i, s = -s)
                num += s * powi(q, i * (i + 1)) * M::sin((2 * i + 1) * c * k_pi / order);
            for (int i = 1, s = -1; i < 12; ++i, s = -s)
                den += s * powi(q, i * i) * M::cos(2 * i * c * k_pi / order);
            const double ww = num * M::sqrt(M::sqrt(q)) / (den + 0.5), w2 = ww * ww;
            const double x                = M::sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2);
            a[static_cast<unsigned>(idx)] = (1 - x) / (1 + x);
        }
        // sorted ascending (they come out so; the paths alternate)
        return a;
    }

    struct cplx {
        double re = 0, im = 0;
    };
    constexpr cplx operator+(cplx a, cplx b) {
        return {a.re + b.re, a.im + b.im};
    }
    constexpr cplx operator-(cplx a, cplx b) {
        return {a.re - b.re, a.im - b.im};
    }
    constexpr cplx operator*(cplx a, cplx b) {
        return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    }
    constexpr cplx operator/(cplx a, cplx b) {
        const double d = b.re * b.re + b.im * b.im;
        return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
    }
    constexpr double norm(cplx a) {
        return a.re * a.re + a.im * a.im;
    }

    struct section {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // a0 = 1; first order: b2 = a2 = 0
        double radius = 0;
    };

    template <int Order>
    struct cheby2_design {
        std::array<section, (Order + 1) / 2> s{};
        double                               stop_edge = 0; // fraction of the rate
    };

    template <class M>
    constexpr double atan_pos(double x) { // for the reported edge only: atan by bisection on tan
        double lo = 0, hi = k_pi / 2;
        for (int i = 0; i < 80; ++i) {
            const double mid            = 0.5 * (lo + hi);
            (M::tan(mid) < x ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    template <int Order, class M = cx_policy>
    constexpr cheby2_design<Order> cheby2(double p, double A, double rp, int grid) {
        static_assert(Order % 2 == 1, "odd orders only: the zero at z = -1");
        constexpr int        n   = Order;
        constexpr int        m   = (n - 1) / 2;
        const double         a10 = M::exp(A / 10 * 2.302585092994045684017991454684364208);
        const double         r10 = M::exp(rp / 10 * 2.302585092994045684017991454684364208);
        const double         Op  = M::tan(k_pi * p);
        const double         D   = M::sqrt((a10 - 1) / (r10 - 1));
        const double         ac  = M::log(D + M::sqrt(D * D - 1)) / n;    // acosh(D) / n
        const double         Os  = Op * 0.5 * (M::exp(ac) + M::exp(-ac)); // cosh
        const double         eps = 1 / M::sqrt(a10 - 1);
        const double         x   = 1 / eps;
        const double         mu  = M::log(x + M::sqrt(x * x + 1)) / n; // asinh(1 / eps) / n
        const double         sh = 0.5 * (M::exp(mu) - M::exp(-mu)), ch = 0.5 * (M::exp(mu) + M::exp(-mu));
        cheby2_design<Order> out{};
        out.stop_edge = atan_pos<M>(Os) / k_pi;
        const cplx one{1, 0};
        for (int k = 1; k <= m; ++k) {
            const double th = (2 * k - 1) * k_pi / (2 * n);
            const cplx   q{-sh * M::sin(th), ch * M::cos(th)};
            const cplx   pz = cplx{Os, 0} / q;               // the analogue pole
            const cplx   zp = (one + pz) / (one - pz);       // bilinear
            const double Oz = Os / M::cos(th);               // the analogue zero j Oz
            const double cz = (1 - Oz * Oz) / (1 + Oz * Oz); // Re of its z-plane image
            section&     s  = out.s[static_cast<unsigned>(k - 1)];
            s.b0            = 1;
            s.b1            = -2 * cz;
            s.b2            = 1;
            s.a1            = -2 * zp.re;
            s.a2            = norm(zp);
            s.radius        = M::sqrt(s.a2);
        }
        {
            const double pr = -Os / sh; // the real pole, theta = pi / 2
            const double zr = (1 + pr) / (1 - pr);
            section&     s  = out.s[m];
            s.b0            = 1;
            s.b1            = 1;
            s.b2            = 0;
            s.a1            = -zr;
            s.a2            = 0;
            s.radius        = zr < 0 ? -zr : zr;
        }
        // order by increasing pole radius (insertion sort)
        for (int i = 1; i < m + 1; ++i)
            for (int j = i;
                 j > 0 && out.s[static_cast<unsigned>(j)].radius < out.s[static_cast<unsigned>(j - 1)].radius; --j) {
                const section t                     = out.s[static_cast<unsigned>(j)];
                out.s[static_cast<unsigned>(j)]     = out.s[static_cast<unsigned>(j - 1)];
                out.s[static_cast<unsigned>(j - 1)] = t;
            }
        // every section to unity DC gain
        for (auto& s : out.s) {
            const double g = (1 + s.a1 + s.a2) / (s.b0 + s.b1 + s.b2);
            s.b0 *= g;
            s.b1 *= g;
            s.b2 *= g;
        }
        if (grid > 0) {
            // L-inf: each partial cascade peaks at 1 over the grid; the last section restores unity DC gain
            double carried = 1; // the gain removed so far (DC of the partial cascade)
            for (int i = 0; i < m; ++i) {
                double peak = 0;
                for (int g = 0; g <= grid; ++g) {
                    const double w = k_pi * g / grid;
                    const cplx   z1{M::cos(w), -M::sin(w)}; // z^-1
                    const cplx   z2   = z1 * z1;
                    double       mag2 = 1;
                    for (int j = 0; j <= i; ++j) {
                        const section& s  = out.s[static_cast<unsigned>(j)];
                        const cplx     nn = cplx{s.b0, 0} + cplx{s.b1, 0} * z1 + cplx{s.b2, 0} * z2;
                        const cplx     dd = one + cplx{s.a1, 0} * z1 + cplx{s.a2, 0} * z2;
                        mag2 *= norm(nn) / norm(dd);
                    }
                    if (mag2 > peak)
                        peak = mag2;
                }
                const double sc = 1 / M::sqrt(peak); // the partial cascade (earlier sections already scaled) to peak 1
                section&     s  = out.s[static_cast<unsigned>(i)];
                s.b0 *= sc;
                s.b1 *= sc;
                s.b2 *= sc;
                carried *= sc;
            }
            section& last = out.s[m];
            last.b0 /= carried;
            last.b1 /= carried;
            last.b2 /= carried;
        }
        return out;
    }

} // namespace iir_cx
