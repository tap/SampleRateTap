#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.hpp"
#include "support/sine_analysis.hpp"
#include "support/two_clock_sim.hpp"

namespace {

    constexpr double kFs  = 48000.0;
    constexpr double kEps = 200e-6;
    constexpr double kAmp = 0.5;

    // Resamples a sine across a +200 ppm clock offset (sample-synchronous
    // transfer) and measures the residual after removing the fitted fundamental
    // from the last second of output. The output normalized frequency is the
    // input normalized frequency scaled by fsIn/fsOut.
    double measureSnrDb(const srt::FilterSpec& spec, double freqHz) {
        srt::Config cfg;
        cfg.channels = 1;
        cfg.filter   = spec;
        srt::AsyncSampleRateConverter asrc(cfg);
        srt_test::TwoClockSim         sim{
                    .asrc = asrc, .fsIn = kFs * (1.0 + kEps), .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const double nuIn = freqHz / kFs;
        sim.gen           = [&](std::uint64_t i) {
            return static_cast<float>(kAmp * std::sin(2.0 * std::numbers::pi * nuIn * static_cast<double>(i)));
        };
        std::vector<float> tail;
        tail.reserve(48000);
        // Long run: the 0.05 Hz locked loop must fully forget the acquisition
        // transient before the measurement window.
        const double total = 40.0;
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
        std::printf("[ measured ] %5.0f Hz, %zu phases: SNR %.1f dB\n", freqHz, spec.numPhases, snr);
        return snr;
    }

    // Thresholds sit 4-7 dB under measured performance (135/120/113/106 dB for
    // balanced at 997/6k/12k/19.5k; 133/108 dB for transparent). The residual at
    // high frequencies is dominated by the linear interpolation between adjacent
    // phase-table rows, which falls ~12 dB per doubling of numPhases and rises
    // ~12 dB per octave of signal frequency.
    TEST(AsrcQuality, Balanced997Hz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::balanced(), 997.0), 128.0);
    }
    TEST(AsrcQuality, Balanced6kHz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::balanced(), 6000.0), 114.0);
    }
    TEST(AsrcQuality, Balanced12kHz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::balanced(), 12000.0), 106.0);
    }
    TEST(AsrcQuality, Balanced19_5kHz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::balanced(), 19500.0), 100.0);
    }
    TEST(AsrcQuality, Transparent997Hz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::transparent(), 997.0), 128.0);
    }
    TEST(AsrcQuality, Transparent19_5kHz) {
        EXPECT_GT(measureSnrDb(srt::FilterSpec::transparent(), 19500.0), 103.0);
    }

} // namespace
