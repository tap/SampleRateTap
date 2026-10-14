// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Non-finite input (the family roadmap's A7, row P9). A stage is a pure
// FIR: every output is one dot product over its window (window_frames()
// input frames), the accumulator cleared per output, and nothing in a stage
// or a chain is computed from a sample's value. A NaN/Inf burst therefore
// corrupts only the outputs whose windows, stage by stage, cover it: the
// stage or chain that took the burst and a twin fed finite samples in its
// place agree bit for bit outside that span, and the other channel never
// sees it. Q15 and Q31 have no non-finite values, so this is the floating
// formats' contract.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    template <typename S>
    class non_finite_test : public ::testing::Test {};
    using floating_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(non_finite_test, floating_types, );

    /// The burst: NaN, +Inf and -Inf in turn.
    template <typename S>
    S burst_value(std::size_t i) {
        switch (i % 3) {
        case 0:
            return std::numeric_limits<S>::quiet_NaN();
        case 1:
            return std::numeric_limits<S>::infinity();
        default:
            return -std::numeric_limits<S>::infinity();
        }
    }

    /// Outputs a stage of ratio L/M can let a span of `in_span` corrupted
    /// input frames reach: every output whose window of `window` frames
    /// overlaps the span, rounded up, plus one for the schedule's phase.
    std::size_t reach(std::size_t in_span, std::size_t window, std::size_t up, std::size_t down) {
        return ((in_span + window) * up + down - 1) / down + 1;
    }

    template <typename S>
    bool same_bits(S a, S b) {
        return std::memcmp(&a, &b, sizeof(S)) == 0;
    }

    /// Streams two copies of `Engine` side by side, the first with a burst of
    /// `burst_len` non-finite samples on channel 0, and checks the contract;
    /// `bound` is the most outputs the burst may reach.
    template <typename S, typename Engine>
    void check_burst(Engine& hit, Engine& twin, std::size_t burst_len, std::size_t bound) {
        constexpr std::size_t k_ch       = 2;
        constexpr std::size_t k_block    = 32;
        constexpr std::size_t k_blocks   = 300; // 9,600 input frames
        constexpr std::size_t k_burst_at = 3000;

        std::vector<S> in_hit(k_block * k_ch);
        std::vector<S> in_twin(k_block * k_ch);
        std::vector<S> out_hit(hit.outputs_for(k_block) * k_ch + 64 * k_ch);
        std::vector<S> out_twin(out_hit.size());
        std::vector<S> y_hit[k_ch];
        std::vector<S> y_twin[k_ch];

        for (std::size_t b = 0; b < k_blocks; ++b) {
            for (std::size_t f = 0; f < k_block; ++f) {
                const std::size_t i     = b * k_block + f;
                const auto        t     = static_cast<double>(i);
                const auto        left  = static_cast<S>(0.5 * std::sin(2.0 * std::numbers::pi * 0.0207 * t));
                const auto        right = static_cast<S>(0.5 * std::sin(2.0 * std::numbers::pi * 0.0091 * t));
                const bool        burst = i >= k_burst_at && i < k_burst_at + burst_len;
                in_hit[f * k_ch]        = burst ? burst_value<S>(i) : left;
                in_twin[f * k_ch]       = left;
                in_hit[f * k_ch + 1]    = right;
                in_twin[f * k_ch + 1]   = right;
            }
            ASSERT_LE(hit.outputs_for(k_block) * k_ch, out_hit.size());
            const std::size_t made = hit.process(in_hit.data(), k_block, out_hit.data());
            ASSERT_EQ(twin.process(in_twin.data(), k_block, out_twin.data()), made);
            for (std::size_t n = 0; n < made; ++n) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    y_hit[c].push_back(out_hit[n * k_ch + c]);
                    y_twin[c].push_back(out_twin[n * k_ch + c]);
                }
            }
        }

        for (std::size_t n = 0; n < y_hit[1].size(); ++n) {
            ASSERT_TRUE(same_bits(y_hit[1][n], y_twin[1][n])) << "the burst reached the other channel at output " << n;
        }

        std::size_t first = y_hit[0].size();
        std::size_t last  = 0;
        std::size_t hits  = 0;
        for (std::size_t n = 0; n < y_hit[0].size(); ++n) {
            if (!same_bits(y_hit[0][n], y_twin[0][n])) {
                EXPECT_FALSE(std::isfinite(y_hit[0][n])) << "a finite output moved at " << n;
                first = std::min(first, n);
                last  = n;
                ++hits;
            }
            else {
                EXPECT_TRUE(std::isfinite(y_hit[0][n])) << "output " << n;
            }
        }
        ASSERT_GT(hits, 0U) << "the burst never reached the output";
        EXPECT_LE(last - first + 1, bound);
        EXPECT_GT(y_hit[0].size() - last, 4 * bound) << "the stream goes on, clean, long after the burst";
    }

    // Measured (clang 19, x86-64, Release, economy; float and double alike):
    // up 3, 114 outputs over a span of 134 against a bound of 139 (inside
    // the span the copy phase's outputs of finite inputs stay clean); down
    // 2, 33 over 33 against 34; the 8/1 chain, 401 over 419 against 448.
    TYPED_TEST(non_finite_test, BurstClearsAfterOneWindowInterpolator) {
        constexpr std::size_t        k_burst = 24;
        basic_stage<TypeParam, up_3> hit(2);
        basic_stage<TypeParam, up_3> twin(2);
        check_burst<TypeParam>(hit, twin, k_burst, reach(k_burst, hit.window_frames(), 3, 1));
    }

    TYPED_TEST(non_finite_test, BurstClearsAfterOneWindowDecimator) {
        constexpr std::size_t          k_burst = 24;
        basic_stage<TypeParam, down_2> hit(2);
        basic_stage<TypeParam, down_2> twin(2);
        check_burst<TypeParam>(hit, twin, k_burst, reach(k_burst, hit.window_frames(), 1, 2));
    }

    // A chain: the span widens stage by stage, each stage's reach applied to
    // the last one's.
    TYPED_TEST(non_finite_test, BurstClearsAfterOneWindowPerStageThroughAChain) {
        using engine                  = up_2_ratio_4_3_up_3<TypeParam>; // 8/1: up 2, 4/3, up 3
        constexpr std::size_t k_burst = 24;
        engine                hit(2);
        engine                twin(2);
        std::size_t           span = k_burst;
        span                       = reach(span, hit.template stage<0>().window_frames(), 2, 1);
        span                       = reach(span, hit.template stage<1>().window_frames(), 4, 3);
        span                       = reach(span, hit.template stage<2>().window_frames(), 3, 1);
        check_burst<TypeParam>(hit, twin, k_burst, span);
    }

} // namespace
