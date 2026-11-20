// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 spike (scratch): constexpr double math with no libm, for the
// compile-time designs of IIR_PLAN I4. Plain C++20 (no builtins, no
// __int128), so GCC, Clang, MSVC and AppleClang evaluate the same source.
// Every function is a textbook series or iteration in double:
// - sqrt: Newton's iteration from max(x, 1), stopped at a fixed point;
// - sin / cos: Taylor series on [-pi/4, pi/4] after a quadrant reduction;
// - exp: Taylor series on |r| <= ln2 / 2 after x = k ln2 + r, then 2^k by
//   repeated doubling or halving;
// - log: x = m 2^e with m in [1, 2) by repeated halving or doubling, then
//   2 atanh((m - 1) / (m + 1)) by its series, plus e ln2;
// - sinh, cosh, asinh, acosh from exp, log and sqrt.
// Their bit stability rests on each compiler's constant evaluator, which the
// spike compares between compilers (expected_bits.txt).
#pragma once

namespace iir_cx {

    inline constexpr double k_pi  = 3.14159265358979323846264338327950288;
    inline constexpr double k_ln2 = 0.693147180559945309417232121458176568;

    constexpr double abs(double x) {
        return x < 0 ? -x : x;
    }

    constexpr double sqrt(double x) {
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

    constexpr double sin_small(double x) {
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
    constexpr double sin(double x) {
        const double t = x / (k_pi / 2);
        const long   q = static_cast<long>(t >= 0 ? t + 0.5 : t - 0.5);
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

    constexpr double exp(double x) {
        const double t    = x / k_ln2;
        const long   k    = static_cast<long>(t >= 0 ? t + 0.5 : t - 0.5);
        const double r    = x - static_cast<double>(k) * k_ln2;
        double       term = 1, sum = 1;
        for (int n = 1; n < 30; ++n) {
            term *= r / n;
            sum += term;
        }
        if (k > 0)
            for (long i = 0; i < k; ++i)
                sum *= 2;
        else
            for (long i = 0; i < -k; ++i)
                sum *= 0.5;
        return sum;
    }

    constexpr double log(double x) {
        if (x <= 0)
            return -1e308;
        int e = 0;
        while (x >= 2) {
            x *= 0.5;
            ++e;
        }
        while (x < 1) {
            x *= 2;
            --e;
        }
        const double u = (x - 1) / (x + 1), u2 = u * u;
        double       term = u, sum = 0;
        for (int n = 1; n < 80; n += 2) {
            sum += term / n;
            term *= u2;
        }
        return 2 * sum + e * k_ln2;
    }

    constexpr double sinh(double x) {
        return 0.5 * (exp(x) - exp(-x));
    }
    constexpr double cosh(double x) {
        return 0.5 * (exp(x) + exp(-x));
    }
    constexpr double asinh(double x) {
        return log(x + sqrt(x * x + 1));
    }
    constexpr double acosh(double x) {
        return log(x + sqrt(x * x - 1));
    }
    constexpr double pow10(double x) {
        return exp(x * 2.302585092994045684017991454684364208);
    }

} // namespace iir_cx
