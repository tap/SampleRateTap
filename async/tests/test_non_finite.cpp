// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Non-finite input (the family roadmap's A7, row P9). The servo reads
// counts only (the FIFO's occupancy and the interpolator's phase), never a
// sample, and the interpolator is a pure FIR over the last taps() frames,
// its accumulator cleared per output. A NaN/Inf burst therefore corrupts
// exactly the outputs whose window covers it and cannot reach the clock
// recovery: the converter that took the burst and a twin fed finite samples
// in its place, driven by the same two clocks, report the same status() bit
// for bit at every pull and produce the same output outside that span, and
// the other channel never sees it. Q15 and Q31 have no non-finite values,
// so this is float's contract.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "tap/sr/async/converter.h"

namespace {

    using tap::sr::async::converter_state;

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

    bool same_status(const tap::sr::async::converter_status& a, const tap::sr::async::converter_status& b) {
        return a.state == b.state && std::bit_cast<std::uint64_t>(a.ppm) == std::bit_cast<std::uint64_t>(b.ppm)
               && std::bit_cast<std::uint64_t>(a.fifo_fill_frames) == std::bit_cast<std::uint64_t>(b.fifo_fill_frames)
               && a.underruns == b.underruns && a.overruns == b.overruns && a.resyncs == b.resyncs
               && a.effective_target_latency_frames == b.effective_target_latency_frames;
    }

    // Two clocks 500 ppm apart, paced as two_clock_sim paces them, in
    // 16-frame blocks (where the servo locks within 0.5 s, the Threads
    // test's grid). The burst arrives at 1.5 s, locked, and the run goes on
    // to 3 s.
    TEST(NonFinite, BurstClearsAfterOneWindowAndLeavesTheServoAlone) {
        constexpr std::size_t k_ch        = 2;
        constexpr std::size_t k_block     = 16;
        constexpr double      k_fs        = 48000.0;
        constexpr double      k_fs_in     = k_fs * (1.0 + 500e-6);
        constexpr double      k_seconds   = 3.0;
        constexpr std::size_t k_burst_at  = 72000; // input frame, 1.5 s
        constexpr std::size_t k_burst_len = 24;

        tap::sr::async::config cfg;
        cfg.channels = k_ch;
        tap::sr::async::converter hit(cfg);
        tap::sr::async::converter twin(cfg);
        const std::size_t         taps = hit.filter_bank().taps();

        std::vector<float> in_hit(k_block * k_ch);
        std::vector<float> in_twin(k_block * k_ch);
        std::vector<float> out_hit(k_block * k_ch);
        std::vector<float> out_twin(k_block * k_ch);
        std::vector<float> y_hit[k_ch];
        std::vector<float> y_twin[k_ch];

        double      t_in            = 0.0;
        double      t_out           = 0.0;
        std::size_t idx             = 0;
        bool        locked_at_burst = false;
        std::size_t status_checks   = 0;
        double      ppm_sum         = 0.0;
        std::size_t ppm_n           = 0;
        while (t_out < k_seconds) {
            if (t_in <= t_out) {
                if (idx <= k_burst_at && idx + k_block > k_burst_at) {
                    locked_at_burst = hit.status().state == converter_state::locked;
                }
                for (std::size_t f = 0; f < k_block; ++f, ++idx) {
                    const auto t      = static_cast<double>(idx);
                    const auto left   = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 997.0 * t / k_fs));
                    const auto right  = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 441.0 * t / k_fs));
                    const bool burst  = idx >= k_burst_at && idx < k_burst_at + k_burst_len;
                    in_hit[f * k_ch]  = burst ? burst_value(idx) : left;
                    in_twin[f * k_ch] = left;
                    in_hit[f * k_ch + 1]  = right;
                    in_twin[f * k_ch + 1] = right;
                }
                ASSERT_EQ(hit.push(in_hit.data(), k_block), twin.push(in_twin.data(), k_block));
                t_in += static_cast<double>(k_block) / k_fs_in;
            }
            else {
                const std::size_t made = hit.pull(out_hit.data(), k_block);
                ASSERT_EQ(twin.pull(out_twin.data(), k_block), made);
                ASSERT_TRUE(same_status(hit.status(), twin.status())) << "the servo saw the burst at " << t_out << " s";
                ++status_checks;
                if (t_out >= 1.0) {
                    ppm_sum += hit.status().ppm;
                    ++ppm_n;
                }
                for (std::size_t n = 0; n < k_block; ++n) {
                    for (std::size_t c = 0; c < k_ch; ++c) {
                        y_hit[c].push_back(out_hit[n * k_ch + c]);
                        y_twin[c].push_back(out_twin[n * k_ch + c]);
                    }
                }
                t_out += static_cast<double>(k_block) / k_fs;
            }
        }

        EXPECT_TRUE(locked_at_burst) << "the burst should arrive at a locked converter";
        const auto end = hit.status();
        EXPECT_EQ(end.state, converter_state::locked);
        EXPECT_EQ(end.underruns, 0U);
        EXPECT_EQ(end.resyncs, 0U);
        // The estimate wobbles with the block beat, so the clock recovery is
        // judged as the lock tests judge it: the mean over the last two seconds.
        ASSERT_GT(ppm_n, 0U);
        EXPECT_NEAR(ppm_sum / static_cast<double>(ppm_n), 500.0, 50.0);
        EXPECT_GT(status_checks, 8000U);

        for (std::size_t n = 0; n < y_hit[1].size(); ++n) {
            ASSERT_EQ(std::bit_cast<std::uint32_t>(y_hit[1][n]), std::bit_cast<std::uint32_t>(y_twin[1][n]))
                << "the burst reached the other channel at output " << n;
        }

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
        // Near unity, one output per input frame, give or take the one slip
        // the 500 ppm drift may land inside the window. Measured (clang 19,
        // x86-64, Release): 71 outputs, all non-finite, over a span of 71 =
        // 24 + 48 - 1, the burst plus the default bank's 48 taps.
        EXPECT_GE(hits, k_burst_len);
        EXPECT_LE(last - first + 1, k_burst_len + taps + 1) << "taps " << taps;
        EXPECT_GT(y_hit[0].size() - last, 48000U) << "the stream goes on, clean, long after the burst";
    }

} // namespace
