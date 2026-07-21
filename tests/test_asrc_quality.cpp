#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.h"
#include "support/sine_analysis.h"
#include "support/two_clock_sim.h"

namespace {

    constexpr double k_fs  = 48000.0;
    constexpr double k_eps = 200e-6;
    constexpr double k_amp = 0.5;

    // Resamples a sine across a +200 ppm clock offset (sample-synchronous
    // transfer) and measures the residual after removing the fitted fundamental
    // from the last second of output. The output normalized frequency is the
    // input normalized frequency scaled by fsIn/fsOut.
    double measure_snr_db(const tap::samplerate::filter_spec& spec, double freq_hz) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        cfg.filter   = spec;
        tap::samplerate::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim                      sim{
                                 .asrc = asrc, .fs_in = k_fs * (1.0 + k_eps), .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const double nu_in = freq_hz / k_fs;
        sim.gen            = [&](std::uint64_t i) {
            return static_cast<float>(k_amp * std::sin(2.0 * std::numbers::pi * nu_in * static_cast<double>(i)));
        };
        std::vector<float> tail;
        tail.reserve(48000);
        // Long run: the 0.05 Hz locked loop must fully forget the acquisition
        // transient before the measurement window.
        const double total = 40.0;
        sim.run(total, [&](const float* x, std::size_t frames, double t) {
            if (t >= total - 1.0) {
                tail.insert(tail.end(), x, x + frames);
            }
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, tap::samplerate::converter_state::locked);
        const double nu_out_expected = nu_in * (1.0 + k_eps);
        const auto   fit             = srt_test::fit_sine_tracked(tail, nu_out_expected);
        EXPECT_NEAR(fit.amplitude, k_amp, 0.01);
        // The tracked frequency must still match the true clock ratio closely.
        EXPECT_NEAR(fit.freq_norm / nu_out_expected, 1.0, 2e-6);
        const double snr = srt_test::snr_db(fit);
        std::printf("[ measured ] %5.0f Hz, %zu phases: SNR %.1f dB\n", freq_hz, spec.num_phases, snr);
        return snr;
    }

    // Thresholds sit 4-7 dB under measured performance (135/120/113/106 dB for
    // balanced at 997/6k/12k/19.5k; 133/108 dB for transparent). The residual at
    // high frequencies is dominated by the linear interpolation between adjacent
    // phase-table rows, which falls ~12 dB per doubling of numPhases and rises
    // ~12 dB per octave of signal frequency.
    TEST(AsrcQuality, Balanced997Hz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::balanced(), 997.0), 128.0);
    }
    TEST(AsrcQuality, Balanced6kHz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::balanced(), 6000.0), 114.0);
    }
    TEST(AsrcQuality, Balanced12kHz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::balanced(), 12000.0), 106.0);
    }
    TEST(AsrcQuality, Balanced19_5kHz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::balanced(), 19500.0), 100.0);
    }
    TEST(AsrcQuality, Transparent997Hz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::transparent(), 997.0), 128.0);
    }
    TEST(AsrcQuality, Transparent19_5kHz) {
        EXPECT_GT(measure_snr_db(tap::samplerate::filter_spec::transparent(), 19500.0), 103.0);
    }

} // namespace
