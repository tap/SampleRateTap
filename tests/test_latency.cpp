#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/two_clock_sim.hpp"

namespace {

    constexpr double kFs = 48000.0;

    TEST(Latency, ImpulseDelayMatchesDesignedLatency) {
        srt::Config cfg;
        cfg.channels = 1;
        srt::AsyncSampleRateConverter asrc(cfg);
        srt_test::TwoClockSim sim{.asrc = asrc, .fsIn = kFs, .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const std::uint64_t   impulseIndex = 24000; // 0.5 s in, well past acquisition
        sim.gen                            = [&](std::uint64_t i) { return i == impulseIndex ? 1.0f : 0.0f; };
        std::vector<float> out;
        out.reserve(60000);
        sim.run(1.2, [&](const float* x, std::size_t frames, double) { out.insert(out.end(), x, x + frames); });

        // Locate the impulse response peak with parabolic refinement.
        std::size_t peak = 0;
        for (std::size_t n = 0; n < out.size(); ++n)
            if (std::abs(out[n]) > std::abs(out[peak]))
                peak = n;
        ASSERT_GT(std::abs(out[peak]), 0.5f);
        const double y0            = out[peak - 1];
        const double y1            = out[peak];
        const double y2            = out[peak + 1];
        const double refine        = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
        const double measuredDelay = static_cast<double>(peak) + refine - static_cast<double>(impulseIndex);

        const double designedDelay = asrc.designedLatencySeconds() * kFs;
        // The sim's event interleaving contributes up to ~1 sample of offset on
        // top of the designed figure.
        EXPECT_NEAR(measuredDelay, designedDelay, 2.5);
    }

    TEST(Latency, DesignedLatencyConsistency) {
        srt::Config cfg;
        cfg.channels = 1;
        srt::AsyncSampleRateConverter asrc(cfg);
        const double                  groupDelay = asrc.filterBank().groupDelaySamples();
        EXPECT_NEAR(groupDelay, 24.0, 0.1); // ~T/2 for balanced (T = 48)
        EXPECT_NEAR(asrc.designedLatencySeconds(), (static_cast<double>(cfg.targetLatencyFrames) + groupDelay) / kFs,
                    1e-12);
        // The whitepaper budget: ~1 ms core latency for the default config.
        EXPECT_LT(asrc.designedLatencySeconds(), 2e-3);
    }

} // namespace
