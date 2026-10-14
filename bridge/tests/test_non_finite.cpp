// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Non-finite input (the family roadmap's A7, row P9). The converter is a
// pure FIR: every output is one dot product over the last taps() input
// frames, its accumulator cleared per output, and nothing in the converter
// is computed from a sample's value. A NaN/Inf burst therefore corrupts
// exactly the outputs whose window covers it and nothing after: the
// converter that took the burst and a twin fed finite samples in its place
// agree bit for bit outside that span, and the other channel never sees it.
// Q15 and Q31 have no non-finite values, so this is float's contract.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/bridge/converter.h"

namespace {

    using tap::sr::bridge::direction;

    /// The burst: NaN, +Inf and -Inf in turn.
    float burst_value(std::size_t i) {
        switch (i % 3) {
        case 0:
            return std::numeric_limits<float>::quiet_NaN();
        case 1:
            return std::numeric_limits<float>::infinity();
        default:
            return -std::numeric_limits<float>::infinity();
        }
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

    template <direction D>
    void burst_clears_after_one_window() {
        using converter                   = tap::sr::bridge::basic_converter<float, D, 0>;
        constexpr std::size_t k_ch        = 2;
        constexpr std::size_t k_block     = 32;
        constexpr std::size_t k_blocks    = 400; // 12,800 input frames
        constexpr std::size_t k_burst_at  = 4000;
        constexpr std::size_t k_burst_len = 24;
        constexpr std::size_t k_up        = D == direction::up_to_48k ? 160 : 147;
        constexpr std::size_t k_down      = D == direction::up_to_48k ? 147 : 160;

        converter          hit(k_ch);
        converter          twin(k_ch);
        const std::size_t  taps = hit.table().taps();
        std::vector<float> in_hit(k_block * k_ch);
        std::vector<float> in_twin(k_block * k_ch);
        std::vector<float> out_hit(2 * k_block * k_ch);
        std::vector<float> out_twin(2 * k_block * k_ch);
        burst_tally<float> tally;

        for (std::size_t b = 0; b < k_blocks; ++b) {
            for (std::size_t f = 0; f < k_block; ++f) {
                const std::size_t i  = b * k_block + f;
                const auto        t  = static_cast<double>(i);
                const auto left      = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 997.0 * t / 44100.0));
                const auto right     = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 441.0 * t / 44100.0));
                const bool burst     = i >= k_burst_at && i < k_burst_at + k_burst_len;
                in_hit[f * k_ch]     = burst ? burst_value(i) : left;
                in_twin[f * k_ch]    = left;
                in_hit[f * k_ch + 1] = right;
                in_twin[f * k_ch + 1] = right;
            }
            const std::size_t made = hit.process(in_hit.data(), k_block, out_hit.data());
            ASSERT_EQ(twin.process(in_twin.data(), k_block, out_twin.data()), made);
            tally.observe(out_hit.data(), out_twin.data(), made, k_ch);
        }

        // Every output that differs from the twin's is non-finite (it
        // multiplied a burst sample), and they all lie within one window of
        // the burst, mapped through the ratio. Measured (clang 19, x86-64,
        // Release, economy): up 67 outputs over a span of 67 against a bound
        // of 69 (38 taps), down 75 over 75 against 77 (58 taps).
        const std::size_t bound = ((k_burst_len + taps) * k_up + k_down - 1) / k_down + 1;
        tally.expect(bound, 8 * taps);
        EXPECT_GE(tally.hits, k_burst_len * k_up / k_down);
    }

    TEST(NonFinite, BurstClearsAfterOneWindowUp) {
        burst_clears_after_one_window<direction::up_to_48k>();
    }

    TEST(NonFinite, BurstClearsAfterOneWindowDown) {
        burst_clears_after_one_window<direction::down_to_44k1>();
    }

} // namespace
