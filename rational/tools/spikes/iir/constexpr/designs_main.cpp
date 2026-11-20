// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// IIR-0, go/no-go 6 (IIR_PLAN I4): the named designs computed at compile
// time, printed as the bits of each double (so two compilers' tables can be
// compared byte for byte, expected_bits.txt), with each one's largest
// difference from the same steps through libm.
//
// IIR_CX_WHICH selects what is evaluated as a constant expression, so a
// compiler's step or operation limit can be probed per design
// (probe_steps.cmake):
//   0  every named design (the default): the nonlinear half-band of every
//      profile at divisors 1, 2, 4, 8, and the Chebyshev II of every
//      profile at orders 3 to 11, with the L-inf gain over 256 frequencies
//   1  transparent's N17 half-band alone (the largest named order)
//   2  economy's N9 half-band alone
//   3  the order-11 Chebyshev II (transparent) with the L-inf gain (256)
//   4  the order-5 Chebyshev II (economy) with the L-inf gain (256)
//   5  the order-11 Chebyshev II (transparent), unity DC gain per section
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "cx_designs.h"

#ifndef IIR_CX_WHICH
#define IIR_CX_WHICH 0
#endif

namespace {

    [[maybe_unused]] constexpr int k_grid = 256;

    template <std::size_t N>
    void print_hb(const char* name, const std::array<double, N>& cx, const std::array<double, N>& rt) {
        double worst = 0;
        std::printf("halfband %s:", name);
        for (std::size_t i = 0; i < N; ++i) {
            std::printf(" %016llx", static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(cx[i])));
            worst = std::fmax(worst, std::fabs(cx[i] - rt[i]));
        }
        std::printf(" | libm %.2g\n", worst);
    }
    template <int O>
    void print_c2(const char* name, const iir_cx::cheby2_design<O>& cx, const iir_cx::cheby2_design<O>& rt) {
        double worst = 0;
        std::printf("cheby2 %s:", name);
        for (std::size_t i = 0; i < cx.s.size(); ++i) {
            const auto& a = cx.s[i];
            const auto& b = rt.s[i];
            for (double v : {a.b0, a.b1, a.b2, a.a1, a.a2})
                std::printf(" %016llx", static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(v)));
            for (auto [x, y] : {std::pair{a.b0, b.b0}, {a.b1, b.b1}, {a.b2, b.b2}, {a.a1, b.a1}, {a.a2, b.a2}})
                worst = std::fmax(worst, std::fabs(x - y));
        }
        std::printf(" | edge %.6f | libm %.2g\n", cx.stop_edge, worst);
    }

#define HB(tag, nc, p, d)                                                                                              \
    do {                                                                                                               \
        static constexpr auto cx = iir_cx::halfband<nc>(0.5 - (p) / (d));                                              \
        print_hb(tag, cx, iir_cx::halfband<nc, iir_cx::libm_policy>(0.5 - (p) / (d)));                                 \
    } while (0)
#define C2(tag, o, p, A, g)                                                                                            \
    do {                                                                                                               \
        static constexpr auto cx = iir_cx::cheby2<o>((p), (A), 0.01, (g));                                             \
        print_c2<o>(tag, cx, iir_cx::cheby2<o, iir_cx::libm_policy>((p), (A), 0.01, (g)));                             \
    } while (0)

} // namespace

int main() {
    constexpr double se = 1.0 / 3.0, ec = 3.0 / 8.0, ba = 19.0 / 48.0, tr = 5.0 / 12.0;
#if IIR_CX_WHICH == 0
    // the nonlinear tables (the fewest coefficients per divisor, gen_designs.py)
    HB("super_economy/1", 4, se, 1);
    HB("super_economy/2", 2, se, 2);
    HB("super_economy/4", 2, se, 4);
    HB("super_economy/8", 1, se, 8);
    HB("economy/1", 4, ec, 1);
    HB("economy/2", 2, ec, 2);
    HB("economy/4", 2, ec, 4);
    HB("economy/8", 1, ec, 8);
    HB("balanced/1", 4, ba, 1);
    HB("balanced/2", 2, ba, 2);
    HB("balanced/4", 2, ba, 4);
    HB("balanced/8", 1, ba, 8);
    HB("transparent/1", 8, tr, 1);
    HB("transparent/2", 4, tr, 2);
    HB("transparent/4", 3, tr, 4);
    HB("transparent/8", 2, tr, 8);
    C2("super_economy/3", 3, se, 70, k_grid);
    C2("super_economy/5", 5, se, 70, k_grid);
    C2("super_economy/7", 7, se, 70, k_grid);
    C2("super_economy/9", 9, se, 70, k_grid);
    C2("super_economy/11", 11, se, 70, k_grid);
    C2("economy/3", 3, ec, 70, k_grid);
    C2("economy/5", 5, ec, 70, k_grid);
    C2("economy/7", 7, ec, 70, k_grid);
    C2("economy/9", 9, ec, 70, k_grid);
    C2("economy/11", 11, ec, 70, k_grid);
    C2("balanced/3", 3, ba, 70, k_grid);
    C2("balanced/5", 5, ba, 70, k_grid);
    C2("balanced/7", 7, ba, 70, k_grid);
    C2("balanced/9", 9, ba, 70, k_grid);
    C2("balanced/11", 11, ba, 70, k_grid);
    C2("transparent/3", 3, tr, 120, k_grid);
    C2("transparent/5", 5, tr, 120, k_grid);
    C2("transparent/7", 7, tr, 120, k_grid);
    C2("transparent/9", 9, tr, 120, k_grid);
    C2("transparent/11", 11, tr, 120, k_grid);
#elif IIR_CX_WHICH == 1
    HB("transparent/1", 8, tr, 1);
#elif IIR_CX_WHICH == 2
    HB("economy/1", 4, ec, 1);
#elif IIR_CX_WHICH == 3
    C2("transparent/11", 11, tr, 120, k_grid);
#elif IIR_CX_WHICH == 4
    C2("economy/5", 5, ec, 70, k_grid);
#elif IIR_CX_WHICH == 5
    C2("transparent/11-unity", 11, tr, 120, 0);
#endif
    (void)se;
    (void)ec;
    (void)ba;
    (void)tr;
    return 0;
}
