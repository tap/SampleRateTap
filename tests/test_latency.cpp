#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.h"
#include "support/two_clock_sim.h"

namespace {

    constexpr double k_fs = 48000.0;

    TEST(Latency, ImpulseDelayMatchesDesignedLatency) {
        srt::config cfg;
        cfg.channels = 1;
        srt::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim          sim{
                     .asrc = asrc, .fs_in = k_fs, .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const std::uint64_t impulse_index = 24000; // 0.5 s in, well past acquisition
        sim.gen                           = [&](std::uint64_t i) { return i == impulse_index ? 1.0f : 0.0f; };
        std::vector<float> out;
        out.reserve(60000);
        sim.run(1.2, [&](const float* x, std::size_t frames, double) { out.insert(out.end(), x, x + frames); });

        // Locate the impulse response peak with parabolic refinement.
        std::size_t peak = 0;
        for (std::size_t n = 0; n < out.size(); ++n) {
            if (std::abs(out[n]) > std::abs(out[peak])) {
                peak = n;
            }
        }
        ASSERT_GT(std::abs(out[peak]), 0.5f);
        const double y0             = out[peak - 1];
        const double y1             = out[peak];
        const double y2             = out[peak + 1];
        const double refine         = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
        const double measured_delay = static_cast<double>(peak) + refine - static_cast<double>(impulse_index);

        const double designed_delay = asrc.designed_latency_seconds() * k_fs;
        // The sim's event interleaving contributes up to ~1 sample of offset on
        // top of the designed figure.
        EXPECT_NEAR(measured_delay, designed_delay, 2.5);
    }

    TEST(Latency, DesignedLatencyConsistency) {
        srt::config cfg;
        cfg.channels = 1;
        srt::async_sample_rate_converter asrc(cfg);
        const double                     group_delay = asrc.filter_bank().group_delay_samples();
        EXPECT_NEAR(group_delay, 24.0, 0.1); // ~T/2 for balanced (T = 48)
        EXPECT_NEAR(asrc.designed_latency_seconds(),
                    (static_cast<double>(cfg.target_latency_frames) + group_delay) / k_fs, 1e-12);
        // The whitepaper budget: ~1 ms core latency for the default config.
        EXPECT_LT(asrc.designed_latency_seconds(), 2e-3);
    }

} // namespace
