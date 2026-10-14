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

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
        std::vector<float> y_hit[k_ch];
        std::vector<float> y_twin[k_ch];

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
            for (std::size_t n = 0; n < made; ++n) {
                for (std::size_t c = 0; c < k_ch; ++c) {
                    y_hit[c].push_back(out_hit[n * k_ch + c]);
                    y_twin[c].push_back(out_twin[n * k_ch + c]);
                }
            }
        }

        // The other channel: bit-identical throughout.
        for (std::size_t n = 0; n < y_hit[1].size(); ++n) {
            ASSERT_EQ(std::bit_cast<std::uint32_t>(y_hit[1][n]), std::bit_cast<std::uint32_t>(y_twin[1][n]))
                << "the burst reached the other channel at output " << n;
        }

        // The burst's channel: every output that differs from the twin's is
        // non-finite (it multiplied a burst sample), and they all lie within
        // one window of the burst, mapped through the ratio.
        std::size_t first = y_hit[0].size();
        std::size_t last  = 0;
        std::size_t hits  = 0;
        for (std::size_t n = 0; n < y_hit[0].size(); ++n) {
            if (std::bit_cast<std::uint32_t>(y_hit[0][n]) != std::bit_cast<std::uint32_t>(y_twin[0][n])) {
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
        // Measured (clang 19, x86-64, Release, economy): up 67 outputs over a
        // span of 67 against a bound of 69 (38 taps), down 75 over 75 against
        // 77 (58 taps).
        const std::size_t span  = last - first + 1;
        const std::size_t bound = ((k_burst_len + taps) * k_up + k_down - 1) / k_down + 1;
        EXPECT_GE(hits, k_burst_len * k_up / k_down);
        EXPECT_LE(span, bound) << "taps " << taps;
        // And the stream goes on long after it, clean.
        EXPECT_GT(y_hit[0].size() - last, 8 * taps);
    }

    TEST(NonFinite, BurstClearsAfterOneWindowUp) {
        burst_clears_after_one_window<direction::up_to_48k>();
    }

    TEST(NonFinite, BurstClearsAfterOneWindowDown) {
        burst_clears_after_one_window<direction::down_to_44k1>();
    }

} // namespace
