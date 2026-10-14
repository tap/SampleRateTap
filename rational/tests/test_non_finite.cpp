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

    /// The comparison, made block by block as the outputs arrive (the
    /// embedded legs' heap holds no whole stream): channel 0 carries the
    /// burst, every other channel must match its twin throughout.
    template <typename S>
    struct burst_tally {
        static constexpr std::size_t k_none = ~std::size_t{0};

        std::size_t outputs      = 0;
        std::size_t hits         = 0;      ///< channel-0 outputs that differ from the twin's
        std::size_t first        = k_none; ///< the first of them
        std::size_t last         = 0;      ///< the last of them
        std::size_t other_moved  = k_none; ///< first output where another channel differs
        std::size_t finite_moved = k_none; ///< first differing output that is finite
        std::size_t nonfinite_ok = k_none; ///< first matching output that is non-finite

        void observe(const S* hit, const S* twin, std::size_t frames, std::size_t channels) {
            for (std::size_t n = 0; n < frames; ++n, ++outputs) {
                for (std::size_t c = 1; c < channels; ++c) {
                    if (!same_bits(hit[n * channels + c], twin[n * channels + c]) && other_moved == k_none) {
                        other_moved = outputs;
                    }
                }
                const S y = hit[n * channels];
                if (!same_bits(y, twin[n * channels])) {
                    if (std::isfinite(y) && finite_moved == k_none) {
                        finite_moved = outputs;
                    }
                    first = first == k_none ? outputs : first;
                    last  = outputs;
                    ++hits;
                }
                else if (!std::isfinite(y) && nonfinite_ok == k_none) {
                    nonfinite_ok = outputs;
                }
            }
        }

        /// The contract: the burst reached channel 0 only, as non-finite
        /// outputs within `bound` of each other, and the stream went on clean
        /// for `clean_after` outputs after them.
        void expect(std::size_t bound, std::size_t clean_after) const {
            EXPECT_EQ(other_moved, k_none) << "the burst reached another channel";
            EXPECT_EQ(finite_moved, k_none) << "a finite output moved";
            EXPECT_EQ(nonfinite_ok, k_none) << "a non-finite output outside the burst's reach";
            ASSERT_GT(hits, 0U) << "the burst never reached the output";
            EXPECT_LE(last - first + 1, bound);
            EXPECT_GT(outputs - last, clean_after) << "the stream goes on, clean, long after the burst";
        }
    };

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
        burst_tally<S> tally;

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
            tally.observe(out_hit.data(), out_twin.data(), made, k_ch);
        }
        tally.expect(bound, 4 * bound);
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
