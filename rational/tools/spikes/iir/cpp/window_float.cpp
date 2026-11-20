// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0 (IIR_PLAN I6, restated): does a whole stage, not one section, reach
// an exactly zero state within the formula count ceil(ln(max / min_normal) /
// ln(1 / r))? Every state of the decimator starts at +-max / 8 in every sign
// pattern of its first states (max the format's largest finite value, 8 the
// state headroom of three integer bits), then zero input runs with the
// per-section flush (the same count as the guard: both zero what falls below
// the smallest normal), and the frames to an exactly zero state are counted.
// Build: g++ -std=c++20 -O2 window_float.cpp -o window_float
#include <cmath>
#include <cstdio>
#include <limits>

#include "kernels.h"

using namespace iir_spike;

template <class T, class Stage>
long worst(Stage proto) {
    long w = 0;
    for (unsigned pattern = 0; pattern < 64; ++pattern) {
        Stage    st   = proto;
        unsigned b    = 0;
        auto     seed = [&](auto& v) {
            for (auto& x : v)
                x = ((pattern >> (b++ % 6)) & 1 ? T(1) : T(-1)) * std::numeric_limits<T>::max() / T(8);
        };
        if constexpr (requires { st.p0.s; })
            seed(st.p0.s);
        if constexpr (requires { st.p0.d; })
            seed(st.p0.d);
        if constexpr (requires { st.p1.s; })
            seed(st.p1.s);
        if constexpr (requires { st.p1.s1; }) {
            seed(st.p1.s1);
            seed(st.p1.s2);
        }
        long n = 0;
        for (; n < 100000 && !st.zero(); ++n)
            st.decimate(T(0), T(0));
        w = std::max(w, n);
    }
    return w;
}

int main() {
    std::printf(
        "| profile | variant | float frames (formula) | float observed | double frames (formula) | double observed |\n"
        "|---|---|---:|---:|---:|---:|\n");
    for (const char* p : {"super_economy", "economy", "balanced", "transparent"}) {
        const auto& nl   = find_nonlinear(p, 1);
        const auto& nr   = find_near_linear(p, 1);
        double      r_nl = 0;
        for (double a : nl.a0)
            r_nl = std::max(r_nl, a);
        for (double a : nl.a1)
            r_nl = std::max(r_nl, a);
        double r_nr = 0; // the largest pole radius of the sections (a second-order one may hold two real poles)
        for (int left = nr.N, i = 0; left > 0;) {
            if (left >= 2) {
                const double c1 = nr.sos[static_cast<std::size_t>(i)], c2 = nr.sos[static_cast<std::size_t>(i + 1)];
                const double disc = c1 * c1 - 4 * c2;
                const double r    = disc < 0 ? std::sqrt(c2) : (std::fabs(c1) + std::sqrt(disc)) / 2;
                r_nr              = std::max(r_nr, r);
                i += 2;
                left -= 2;
            }
            else {
                r_nr = std::max(r_nr, std::fabs(nr.sos[static_cast<std::size_t>(i)]));
                i += 1;
                left -= 1;
            }
        }
        auto formula = [](double r, double lmax, double lmin) {
            return static_cast<long>(std::ceil((lmax - lmin) / std::log(1 / r)));
        };
        const double lf  = std::log(std::numeric_limits<float>::max()),
                     lfm = std::log(std::numeric_limits<float>::min());
        const double ld  = std::log(std::numeric_limits<double>::max()),
                     ldm = std::log(std::numeric_limits<double>::min());
        std::printf("| `%s` | nonlinear | %ld | %ld | %ld | %ld |\n", p, formula(r_nl, lf, lfm),
                    worst<float>(nonlinear_halfband<float, flush::per_section>(nl)), formula(r_nl, ld, ldm),
                    worst<double>(nonlinear_halfband<double, flush::per_section>(nl)));
        std::printf("| `%s` | near-linear | %ld | %ld | %ld | %ld |\n", p, formula(r_nr, lf, lfm),
                    worst<float>(near_linear_halfband<float, flush::per_section>(nr)), formula(r_nr, ld, ldm),
                    worst<double>(near_linear_halfband<double, flush::per_section>(nr)));
    }
}
