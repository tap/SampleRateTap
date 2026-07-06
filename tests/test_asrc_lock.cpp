#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/two_clock_sim.hpp"

namespace {

    constexpr double kFs = 48000.0;

    srt::Config monoConfig() {
        srt::Config cfg;
        cfg.channels = 1;
        return cfg;
    }

    TEST(AsrcLock, LocksAndHoldsAtConstantOffset) {
        srt::AsyncSampleRateConverter asrc(monoConfig());
        srt_test::TwoClockSim         sim{.asrc = asrc, .fsIn = kFs * (1.0 + 200e-6), .fsOut = kFs, .channels = 1};
        bool                          lockedBy2s = false;
        double                        ppmSum     = 0.0;
        double                        fillSum    = 0.0;
        std::size_t                   tailBlocks = 0;
        sim.run(60.0, [&](const float*, std::size_t, double t) {
            const auto st = asrc.status();
            if (t < 2.0 && st.state == srt::State::Locked)
                lockedBy2s = true;
            if (t > 30.0) { // average over many block-beat cycles
                ppmSum += st.ppm;
                fillSum += st.fifoFillFrames;
                ++tailBlocks;
            }
        });
        const auto st = asrc.status();
        EXPECT_TRUE(lockedBy2s);
        EXPECT_EQ(st.state, srt::State::Locked);
        EXPECT_EQ(st.underruns, 0u);
        EXPECT_EQ(st.overruns, 0u);
        EXPECT_EQ(st.resyncs, 0u);
        const double meanPpm  = ppmSum / static_cast<double>(tailBlocks);
        const double meanFill = fillSum / static_cast<double>(tailBlocks);
        // With 32-frame blocks the occupancy observable carries a +/-16 frame
        // sawtooth at the block-beat frequency; the means must still center on
        // the true values.
        EXPECT_NEAR(meanPpm, 200.0, 10.0);
        EXPECT_NEAR(meanFill, 48.0, 4.0);
    }

    TEST(AsrcLock, TracksDriftRampWithoutUnlocking) {
        srt::AsyncSampleRateConverter asrc(monoConfig());
        srt_test::TwoClockSim sim{.asrc = asrc, .fsIn = kFs, .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        // Input clock drifts 0 -> +300 ppm over 30 s (10 ppm/s, far faster than
        // real oscillator wander), then holds for the loop to reconverge.
        sim.fsInScale          = [](double t) { return 1.0 + 300e-6 * std::min(t, 30.0) / 30.0; };
        bool unlockedAfterLock = false;
        bool everLocked        = false;
        sim.run(45.0, [&](const float*, std::size_t, double) {
            const auto st = asrc.status();
            if (st.state == srt::State::Locked)
                everLocked = true;
            else if (everLocked)
                unlockedAfterLock = true;
        });
        const auto st = asrc.status();
        EXPECT_TRUE(everLocked);
        EXPECT_FALSE(unlockedAfterLock);
        EXPECT_EQ(st.underruns, 0u);
        EXPECT_EQ(st.resyncs, 0u);
        EXPECT_NEAR(st.ppm, 300.0, 5.0);
    }

    TEST(AsrcLock, WholeSampleSlipsAreGlitchFree) {
        // At +500 ppm a forward slip happens every 2000 output samples. A clean
        // sine's second difference is bounded by A*omega^2; any window-shift
        // discontinuity would blow far past that bound.
        srt::AsyncSampleRateConverter asrc(monoConfig());
        srt_test::TwoClockSim         sim{
                    .asrc = asrc, .fsIn = kFs * (1.0 + 500e-6), .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const double amp = 0.5;
        const double nu  = 1000.0 / kFs;
        sim.gen          = [&](std::uint64_t i) {
            return static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        };
        std::vector<float> tail;
        sim.run(8.0, [&](const float* x, std::size_t frames, double t) {
            if (t > 4.0)
                tail.insert(tail.end(), x, x + frames);
        });
        ASSERT_GT(tail.size(), 100000u);
        const double omega         = 2.0 * std::numbers::pi * nu;
        const double analyticBound = amp * omega * omega; // |d2/dn2 sin| max
        double       maxSecondDiff = 0.0;
        for (std::size_t n = 1; n + 1 < tail.size(); ++n) {
            const double d2 =
                std::abs(static_cast<double>(tail[n + 1]) - 2.0 * tail[n] + static_cast<double>(tail[n - 1]));
            maxSecondDiff = std::max(maxSecondDiff, d2);
        }
        EXPECT_LT(maxSecondDiff, 1.5 * analyticBound);
        EXPECT_EQ(asrc.status().underruns, 0u);
    }

    TEST(AsrcLock, RecoversFromConsumerStall) {
        // Producer keeps pushing while the consumer stops pulling: occupancy blows
        // through the high watermark, the converter hard-resyncs, then relocks.
        srt::AsyncSampleRateConverter asrc(monoConfig());
        srt_test::TwoClockSim         sim{.asrc = asrc, .fsIn = kFs * (1.0 + 100e-6), .fsOut = kFs, .channels = 1};
        sim.run(10.0, [&](const float*, std::size_t, double) {});
        ASSERT_EQ(asrc.status().state, srt::State::Locked);

        // Stall: push 3000 frames with no pulls (FIFO capacity is 1024 mono).
        std::vector<float> burst(3000, 0.0f);
        for (std::size_t off = 0; off < burst.size(); off += 100)
            asrc.push(burst.data() + off, 100);
        EXPECT_GT(asrc.status().overruns, 0u);

        // Resume pulling; the converter must resync and relock without underruns
        // turning permanent.
        srt_test::TwoClockSim resume{.asrc = asrc, .fsIn = kFs * (1.0 + 100e-6), .fsOut = kFs, .channels = 1};
        resume.run(10.0, [&](const float*, std::size_t, double) {});
        const auto st = asrc.status();
        EXPECT_EQ(st.state, srt::State::Locked);
        EXPECT_GE(st.resyncs, 1u);
    }

} // namespace
