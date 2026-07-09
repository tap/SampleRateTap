// Program-weighted quality: the metric that makes filter_spec::economy()'s
// promise testable, and the evidence that the k*fs transmission zeros do
// what the design says (see the book's epilogue chapter and
// notebooks/asrc_rbj_analysis.ipynb).
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "srt/asrc.h"
#include "support/multitone_analysis.h"
#include "support/sine_analysis.h"
#include "support/two_clock_sim.h"

namespace {

    constexpr double k_fs  = 48000.0;
    constexpr double k_eps = 200e-6;

    // ANCHOR: pw_measure
    // 24 pink-weighted tones, 60 Hz - 16 kHz, through a +200 ppm offset; the
    // residual after removing every tone is everything the converter got wrong,
    // weighted the way real program material weights it.
    double measure_program_snr_db(const srt::filter_spec& spec) {
        srt::config cfg;
        cfg.channels = 1;
        cfg.filter   = spec;
        srt::async_sample_rate_converter asrc(cfg);
        const double                     fs_in = k_fs * (1.0 + k_eps);
        srt_test::two_clock_sim          sim{
                     .asrc = asrc, .fs_in = fs_in, .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const auto comb = srt_test::tone_comb::pink(24, 60.0, 16000.0, 0.9);
        sim.gen         = [&](std::uint64_t i) { return static_cast<float>(comb.sample_at(i, fs_in)); };
        std::vector<float> tail;
        tail.reserve(48000);
        const double total = 40.0; // Quiet-stage settling, as in the sine suite
        sim.run(total, [&](const float* x, std::size_t frames, double t) {
            if (t >= total - 1.0) {
                tail.insert(tail.end(), x, x + frames);
            }
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, srt::converter_state::locked);
        const double snr = srt_test::program_weighted_snr_db(tail, comb, fs_in, k_fs);
        std::printf("[ measured ] program-weighted (24 pink tones), %zu phases x %zu taps: %.1f dB\n", spec.num_phases,
                    spec.taps_per_phase, snr);
        return snr;
    }
    // ANCHOR_END: pw_measure

    // Worst-case single sine near Nyquist, for the honesty line in economy()'s
    // documentation: this preset trades exactly this number.
    double measure_sine_snr_db(const srt::filter_spec& spec, double freq_hz) {
        srt::config cfg;
        cfg.channels = 1;
        cfg.filter   = spec;
        srt::async_sample_rate_converter asrc(cfg);
        srt_test::two_clock_sim          sim{
                     .asrc = asrc, .fs_in = k_fs * (1.0 + k_eps), .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const double nu_in = freq_hz / k_fs;
        sim.gen            = [&](std::uint64_t i) {
            return static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * nu_in * static_cast<double>(i)));
        };
        std::vector<float> tail;
        const double       total = 40.0;
        sim.run(total, [&](const float* x, std::size_t frames, double t) {
            if (t >= total - 1.0) {
                tail.insert(tail.end(), x, x + frames);
            }
        });
        const auto   fit = srt_test::fit_sine_tracked(tail, nu_in * (1.0 + k_eps));
        const double snr = srt_test::snr_db(fit);
        std::printf("[ measured ] economy %5.0f Hz sine: %.1f dB\n", freq_hz, snr);
        return snr;
    }

    // The instrument itself must not be the floor: a synthetic tail of exact
    // tones (with a deliberate 0.137 ppm ratio offset, mimicking servo
    // settling residue) must measure at the double-precision fit floor.
    TEST(ProgramWeighted, InstrumentFloor) {
        const auto         comb = srt_test::tone_comb::pink(24, 60.0, 16000.0, 0.9);
        const double       rho  = 1.0 + 0.137e-6;
        std::vector<float> tail(48000);
        for (std::size_t i = 0; i < tail.size(); ++i) {
            double v = 0.0;
            for (std::size_t k = 0; k < comb.freq_hz.size(); ++k) {
                v += comb.amplitude[k]
                     * std::sin(2.0 * std::numbers::pi * comb.freq_hz[k] * rho / k_fs * static_cast<double>(i)
                                + comb.phase[k]);
            }
            tail[i] = static_cast<float>(v);
        }
        const double snr = srt_test::program_weighted_snr_db(tail, comb, k_fs * (1.0 + k_eps), k_fs);
        std::printf("[ measured ] instrument floor (synthetic exact tones): %.1f dB\n", snr);
        // float storage of the tail quantizes at ~ -150 dB; the fit must reach
        // it (measured 151.9 dB).
        EXPECT_GT(snr, 145.0);
    }

    // Thresholds pinned 4-7 dB under first measurement, per suite convention.
    // The claim under test: economy() (2/3 the taps of balanced) stays within a
    // few dB of balanced() on PROGRAM-weighted material, because its k*fs zeros
    // hold the images of the energetic bottom octaves at balanced-class depth —
    // while its worst-case sine near Nyquist honestly reads ~96 dB-class.
    TEST(ProgramWeighted, BalancedBaseline) {
        // Measured 134.5 dB.
        EXPECT_GT(measure_program_snr_db(srt::filter_spec::balanced()), 128.0);
    }
    TEST(ProgramWeighted, EconomyNearBalanced) {
        // Measured 131.6 dB — 2.9 dB under balanced at 2/3 the per-sample
        // compute. This single number is the preset's reason to exist.
        const double eco = measure_program_snr_db(srt::filter_spec::economy());
        EXPECT_GT(eco, 125.0);
    }
    TEST(ProgramWeighted, EconomyWorstCaseSineIsDocumented) {
        // Measured 77.4 dB: the deliberate trade, kept visible. (The docs say
        // "96 dB-class"; the extra gap to 77 dB at 19.5 kHz is the L=512
        // interpolation floor at 0.40625 of the sample rate plus the design's
        // transition starting at 18 kHz.)
        EXPECT_GT(measure_sine_snr_db(srt::filter_spec::economy(), 19500.0), 70.0);
    }

} // namespace
