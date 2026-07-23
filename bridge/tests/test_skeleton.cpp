// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// M1 skeleton battery: pins the library's identity constants and proves the
// DspTap substrate is wired end to end — everything the M2 table builder
// needs (design a prototype at a rational phase count, quantize a branch
// row-sum-exactly, run the shared dot kernel on it) works through this
// repo's build.

#include <cstdint>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/kaiser.h"
#include "tap/dsp/quantize.h"
#include "tap/dsp/sample_traits.h"
#include "tap/ratio/ratio.h"

namespace {

    TEST(Skeleton, IdentityConstants) {
        // 44.1/48 = 147/160 in lowest terms; the two directions' phase counts
        // are coprime and fixed forever. If either constant changes, this is
        // not RatioTap anymore.
        EXPECT_EQ(tap::ratio::k_phases_up, 160u);
        EXPECT_EQ(tap::ratio::k_phases_down, 147u);
        EXPECT_EQ(std::gcd(tap::ratio::k_phases_up, tap::ratio::k_phases_down), 1u);
        EXPECT_EQ(TAP_RATIO_VERSION_MAJOR, 0);
    }

    // The substrate chain the M2 table builder will use, end to end at this
    // library's own geometry: design at L = 147 (non-power-of-two), quantize
    // one branch with the row-sum guarantee, dot it against DC through the
    // shared kernel. Catches submodule/link/include-path breakage with real
    // arithmetic rather than a version string.
    TEST(Skeleton, SubstrateIsWiredEndToEnd) {
        constexpr std::size_t k_phases = tap::ratio::k_phases_down;
        constexpr std::size_t k_taps   = 24;
        std::vector<double>   proto(k_phases * k_taps);
        tap::dsp::design_prototype(proto, k_phases, (19000.0 + 22050.0) / 48000.0, tap::dsp::kaiser_beta(70.0));

        // Branch 0 in storage order, quantized to Q15 coefficients.
        std::vector<double> row_d(k_taps);
        for (std::size_t t = 0; t < k_taps; ++t) {
            row_d[k_taps - 1 - t] = proto[t * k_phases];
        }
        std::vector<std::int16_t> row_q(k_taps);
        tap::dsp::quantize_row_preserving_sum<std::int16_t>(row_d, row_q);

        // Row-sum preservation: the branch's DC sum survives quantization
        // exactly (design normalizes every branch's sum to ~1.0).
        std::int64_t sum = 0;
        for (const auto c : row_q) {
            sum += c;
        }
        const double exact = std::accumulate(row_d.begin(), row_d.end(), 0.0);
        EXPECT_EQ(sum, std::llround(exact * tap::dsp::sample_traits<std::int16_t>::k_coeff_scale));

        // And the shared kernel computes a DC output within one LSB of unity.
        std::vector<std::int16_t> dc(k_taps, 32767);
        const std::int16_t        y = tap::dsp::dot_row<std::int16_t>(row_q.data(), dc.data(), k_taps);
        EXPECT_NEAR(y, 32767, 2);
    }

} // namespace
