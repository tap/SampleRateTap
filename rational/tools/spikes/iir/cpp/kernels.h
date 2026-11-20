// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 spike kernels (scratch, not shipped): the two-path half-band's paths
// in floating point, generic over the arithmetic type T, so the float
// profile's noise floor (IIR_PLAN I14) and the flush window (I6) are measured
// on one code path for float, double and long double.
//
// The structures (Valenzuela and Constantinides 1983; harris 2004, the
// recursive polyphase chapter):
// - a one-multiplier first-order allpass section in the path variable w,
//   (a + w^-1) / (1 + a w^-1): y = x1 + a (x - y1); a cascade shares states,
//   section i's previous output being section i+1's previous input, so n
//   sections hold n + 1 states;
// - a second-order allpass section (c2 + c1 w^-1 + w^-2) / (1 + c1 w^-1 +
//   c2 w^-2): y = c2 (x - y2) + c1 (x1 - y1) + x2, two multiplies, with the
//   same sharing (two states per joint);
// - the decimator: even inputs feed path 0, odd inputs path 1 (one input
//   late), the output is the half-sum; the interpolator: both paths run on
//   each input, path 0 gives the even output and path 1 the odd one.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "designs.h"

namespace iir_spike {

    /// The flush policy of a floating-point state (IIR_PLAN I9): none, or the
    /// per-section flush (a compare and select per state per sample, its
    /// threshold the type's smallest normal value).
    enum class flush { none, per_section };

    template <class T, flush F>
    inline T flushed(T v) noexcept {
        if constexpr (F == flush::per_section) {
            return std::fabs(v) < std::numeric_limits<T>::min() ? T(0) : v;
        }
        else {
            return v;
        }
    }

    /// A cascade of one-multiplier sections, n + 1 shared states.
    template <class T, flush F = flush::none>
    struct first_order_path {
        std::vector<T> a;
        std::vector<T> s;
        explicit first_order_path(std::span<const double> coefs)
            : a(coefs.begin(), coefs.end())
            , s(coefs.size() + 1, T(0)) {}
        T operator()(T x) noexcept {
            const std::size_t n = a.size();
            T                 v = x;
            for (std::size_t i = 0; i < n; ++i) {
                const T y = s[i] + a[i] * (v - s[i + 1]);
                s[i]      = flushed<T, F>(v);
                v         = y;
            }
            s[n] = flushed<T, F>(v);
            return v;
        }
        bool zero() const noexcept {
            for (T x : s)
                if (x != T(0))
                    return false;
            return true;
        }
    };

    /// A cascade of second-order sections (c1, c2), then one first-order
    /// section c1 when the order is odd: `sos` as designs.h stores it.
    template <class T, flush F = flush::none>
    struct sos_path {
        struct sec {
            T    c1, c2;
            bool second;
        };
        std::vector<sec> secs;
        std::vector<T>   s1, s2; // per joint: the previous and the one before
        sos_path(std::span<const double> flat, int order) {
            std::size_t i = 0;
            for (int left = order; left > 0;) {
                if (left >= 2) {
                    secs.push_back({T(flat[i]), T(flat[i + 1]), true});
                    i += 2;
                    left -= 2;
                }
                else {
                    secs.push_back({T(flat[i]), T(0), false});
                    i += 1;
                    left -= 1;
                }
            }
            s1.assign(secs.size() + 1, T(0));
            s2.assign(secs.size() + 1, T(0));
        }
        T operator()(T x) noexcept {
            const std::size_t n = secs.size();
            T                 v = x;
            for (std::size_t i = 0; i < n; ++i) {
                const sec& c = secs[i];
                T          y;
                if (c.second) {
                    y = c.c2 * (v - s2[i + 1]) + c.c1 * (s1[i] - s1[i + 1]) + s2[i];
                }
                else {
                    y = s1[i] + c.c1 * (v - s1[i + 1]);
                }
                s2[i] = flushed<T, F>(s1[i]);
                s1[i] = flushed<T, F>(v);
                v     = y;
            }
            s2[n] = flushed<T, F>(s1[n]);
            s1[n] = flushed<T, F>(v);
            return v;
        }
        bool zero() const noexcept {
            for (std::size_t i = 0; i < s1.size(); ++i)
                if (s1[i] != T(0) || s2[i] != T(0))
                    return false;
            return true;
        }
    };

    /// A pure delay of K path samples.
    template <class T>
    struct delay_path {
        std::vector<T> d;
        std::size_t    at = 0;
        explicit delay_path(int K)
            : d(static_cast<std::size_t>(K), T(0)) {}
        T operator()(T x) noexcept {
            const T y = d[at];
            d[at]     = x;
            at        = (at + 1 == d.size()) ? 0 : at + 1;
            return y;
        }
        bool zero() const noexcept {
            for (T x : d)
                if (x != T(0))
                    return false;
            return true;
        }
    };

    /// The two-path half-band, both variants, both directions.
    template <class T, flush F = flush::none>
    struct nonlinear_halfband {
        first_order_path<T, F> p0, p1;
        T                      odd_prev = T(0); // the decimator's path-1 input runs one sample late
        explicit nonlinear_halfband(const nonlinear_design& dsg)
            : p0(dsg.a0)
            , p1(dsg.a1) {}
        T decimate(T even, T odd) noexcept {
            const T y = T(0.5) * (p0(even) + p1(odd_prev));
            odd_prev  = odd;
            return y;
        }
        void interpolate(T x, T& even, T& odd) noexcept {
            even = p0(x);
            odd  = p1(x);
        }
        bool zero() const noexcept { return p0.zero() && p1.zero() && odd_prev == T(0); }
    };

    template <class T, flush F = flush::none>
    struct near_linear_halfband {
        delay_path<T>  p0;
        sos_path<T, F> p1;
        T              odd_prev = T(0);
        explicit near_linear_halfband(const near_linear_design& dsg)
            : p0(dsg.K)
            , p1(dsg.sos, dsg.N) {}
        T decimate(T even, T odd) noexcept {
            const T y = T(0.5) * (p0(even) + p1(odd_prev));
            odd_prev  = odd;
            return y;
        }
        void interpolate(T x, T& even, T& odd) noexcept {
            even = p0(x);
            odd  = p1(x);
        }
        bool zero() const noexcept { return p0.zero() && p1.zero() && odd_prev == T(0); }
    };

    inline const nonlinear_design& find_nonlinear(const char* profile, int divisor) {
        for (const auto& d : k_nonlinear)
            if (std::string_view(d.profile) == profile && d.divisor == divisor)
                return d;
        return k_nonlinear[0];
    }
    inline const near_linear_design& find_near_linear(const char* profile, int divisor) {
        for (const auto& d : k_near_linear)
            if (std::string_view(d.profile) == profile && d.divisor == divisor)
                return d;
        return k_near_linear[0];
    }

} // namespace iir_spike
