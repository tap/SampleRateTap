// 16 kHz -> 16 kHz quality coverage (a real deployment rate, e.g.
// reference-microphone processing). Same methodology as
// test_asrc_quality.cpp, configured through Config::forSampleRate — the
// rate-scaling rule this suite originally established by hand:
//
//  1. FilterSpec band edges are absolute Hz and the presets assume ~48 kHz,
//     so passbandHz/stopbandHz must scale with the rate.
//  2. ServoConfig bandwidths are absolute Hz too. The slip-sawtooth beat
//     sits at ppm * fs = 3.2 Hz instead of 9.6 Hz, so with default servo
//     settings the 3-pole quiet smoother rejects it (16/48)^3 ~ 28.6 dB
//     less and the measurement becomes servo-FM-limited: measured ~32 dB
//     below the 48 kHz figures at every tone, falling 6 dB/octave of
//     signal frequency (the small-index FM sideband signature). Scaling
//     the servo bandwidths by 16/48 keeps the loop identical in
//     normalized (per-sample) terms and restores the 48 kHz structure.
//
// This suite doubles as the regression test for Config::forSampleRate
// itself (including its inverse hold-time scaling, which the hand-scaled
// original did not apply — re-measured identical within noise).
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/sine_analysis.hpp"
#include "support/two_clock_sim.hpp"

namespace {

    constexpr double kFs  = 16000.0;
    constexpr double kEps = 200e-6;
    constexpr double kAmp = 0.5;

    // Resamples a sine across a +200 ppm clock offset (sample-synchronous
    // transfer) and measures the residual after removing the fitted fundamental
    // from the last second of output. Mirrors measureSnrDb in
    // test_asrc_quality.cpp at fs = 16 kHz, with all rate adaptation coming
    // from Config::forSampleRate (filter band edges, servo bandwidths and
    // hold times).
    double measureSnrDb16k(double freqHz) {
        srt::Config cfg = srt::Config::forSampleRate(kFs);
        cfg.channels    = 1;
        srt::AsyncSampleRateConverter asrc(cfg);
        srt_test::TwoClockSim         sim{
                    .asrc = asrc, .fsIn = kFs * (1.0 + kEps), .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const double nuIn = freqHz / kFs;
        sim.gen           = [&](std::uint64_t i) {
            return static_cast<float>(kAmp * std::sin(2.0 * std::numbers::pi * nuIn * static_cast<double>(i)));
        };
        std::vector<float> tail;
        tail.reserve(16000);
        // Long run: the locked loop must fully forget the acquisition transient
        // before the measurement window. The quiet loop is scaled to ~0.017 Hz,
        // so the 48 kHz test's 40 s becomes 120 s here — the identical number
        // of samples and of loop time constants (a 40 s run still sits ~15 dB
        // above the settled residual at every tone).
        const double total = 120.0;
        sim.run(total, [&](const float* x, std::size_t frames, double t) {
            if (t >= total - 1.0)
                tail.insert(tail.end(), x, x + frames);
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, srt::State::Locked);
        const double nuOutExpected = nuIn * (1.0 + kEps);
        const auto   fit           = srt_test::fitSineTracked(tail, nuOutExpected);
        EXPECT_NEAR(fit.amplitude, kAmp, 0.01);
        // The tracked frequency must still match the true clock ratio closely.
        EXPECT_NEAR(fit.freqNorm / nuOutExpected, 1.0, 2e-6);
        const double snr = srt_test::snrDb(fit);
        std::printf("[ measured ] %5.0f Hz: SNR %.1f dB\n", freqHz, snr);
        return snr;
    }

    // Thresholds sit ~4 dB under measured performance, the convention of
    // test_asrc_quality.cpp. Measured (balanced-at-16k, +200 ppm):
    // 333 Hz: 136.6 dB, 2 kHz: 121.9 dB, 4 kHz: 114.3 dB, 6.5 kHz: 106.5 dB.
    // The interpolation residual depends on the normalized frequency f/fs and
    // the tones sit at the same f/fs as the 48 kHz suite's 997 Hz/6 k/12 k/
    // 19.5 k, which measure 135.0/120.0/112.8/105.8 dB on the same host —
    // matching within ~1 dB, as expected.
    // Fast deterministic check of the scaling rule itself (the sims below are
    // the behavioral validation).
    TEST(AsrcQuality16k, ForSampleRateScalesHzFieldsOnly) {
        const srt::Config c = srt::Config::forSampleRate(16000.0);
        const srt::Config d; // 48 kHz defaults
        const double      r = 16000.0 / 48000.0;
        EXPECT_DOUBLE_EQ(c.sampleRateHz, 16000.0);
        EXPECT_DOUBLE_EQ(c.filter.passbandHz, d.filter.passbandHz * r);
        EXPECT_DOUBLE_EQ(c.filter.stopbandHz, d.filter.stopbandHz * r);
        EXPECT_EQ(c.filter.numPhases, d.filter.numPhases);
        EXPECT_EQ(c.filter.tapsPerPhase, d.filter.tapsPerPhase);
        EXPECT_DOUBLE_EQ(c.servo.quietBandwidthHz, d.servo.quietBandwidthHz * r);
        EXPECT_DOUBLE_EQ(c.servo.acquireSmootherHz, d.servo.acquireSmootherHz * r);
        EXPECT_DOUBLE_EQ(c.servo.quietHoldSeconds, d.servo.quietHoldSeconds / r);
        EXPECT_DOUBLE_EQ(c.servo.lockThresholdFrames, d.servo.lockThresholdFrames);
        EXPECT_DOUBLE_EQ(c.servo.maxDeviationPpm, d.servo.maxDeviationPpm);
        EXPECT_EQ(c.targetLatencyFrames, d.targetLatencyFrames);
    }

    TEST(AsrcQuality16k, Balanced333Hz) {
        EXPECT_GT(measureSnrDb16k(333.0), 132.0);
    }
    TEST(AsrcQuality16k, Balanced2kHz) {
        EXPECT_GT(measureSnrDb16k(2000.0), 117.0);
    }
    TEST(AsrcQuality16k, Balanced4kHz) {
        EXPECT_GT(measureSnrDb16k(4000.0), 110.0);
    }
    TEST(AsrcQuality16k, Balanced6_5kHz) {
        EXPECT_GT(measureSnrDb16k(6500.0), 102.0);
    }

} // namespace
