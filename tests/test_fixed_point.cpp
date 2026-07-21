#include <cmath>
#include <cstdint>
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

    using q15 = tap::samplerate::sample_traits<std::int16_t>;
    using q31 = tap::samplerate::sample_traits<std::int32_t>;

    TEST(FixedPoint, CoefficientConversionRoundsAndSaturates) {
        EXPECT_EQ(q15::make_coeff(0.0), 0);
        EXPECT_EQ(q15::make_coeff(1.0), 16384); // Q1.14
        EXPECT_EQ(q15::make_coeff(-1.0), -16384);
        EXPECT_EQ(q15::make_coeff(10.0), 32767); // saturates
        EXPECT_EQ(q15::make_coeff(-10.0), -32768);
        EXPECT_EQ(q31::make_coeff(1.0), 1073741824);  // Q1.30
        EXPECT_EQ(q31::make_coeff(10.0), 2147483647); // saturates
    }

    TEST(FixedPoint, FinalizeSaturates) {
        // Far beyond full scale in the accumulator domain must clamp, not wrap.
        EXPECT_EQ(q15::finalize(std::int64_t{1} << 40), 32767);
        EXPECT_EQ(q15::finalize(-(std::int64_t{1} << 40)), -32768);
        EXPECT_EQ(q31::finalize(std::int64_t{1} << 60), 2147483647);
        EXPECT_EQ(q31::finalize(-(std::int64_t{1} << 60)), -2147483648LL);
    }

    // "Note that for DC, this should get you infinite S/N ratio... for every
    // phase or fractional delay, the FIR coefficients must add to 1."
    //  -- R. Bristow-Johnson, music-dsp. With row-sum-preserving quantization
    // the property survives fixed point exactly: measured 0 LSB deviation over
    // a 256-point mu sweep for both formats (tolerance 1 for safety only).
    TEST(FixedPoint, DcGainIsUnityQ15) {
        const tap::samplerate::polyphase_filter_bank<std::int16_t> bank(tap::samplerate::filter_spec::balanced(), k_fs);
        std::vector<std::int16_t>                      dc(bank.taps(), 32767);
        for (int i = 0; i < 16; ++i) {
            const double mu = static_cast<double>(i) / 16.0;
            EXPECT_NEAR(tap::samplerate::interpolate(bank, dc.data(), mu), 32767, 1) << "mu=" << mu;
        }
    }

    TEST(FixedPoint, DcGainIsUnityQ31) {
        const tap::samplerate::polyphase_filter_bank<std::int32_t> bank(tap::samplerate::filter_spec::balanced(), k_fs);
        std::vector<std::int32_t>                      dc(bank.taps(), 2147483647);
        for (int i = 0; i < 16; ++i) {
            const double mu = static_cast<double>(i) / 16.0;
            EXPECT_NEAR(tap::samplerate::interpolate(bank, dc.data(), mu), 2147483647.0, 1.0) << "mu=" << mu;
        }
    }

    // The mechanism behind the previous two tests: every quantized row sums to
    // exactly k_coeff_scale (the largest-remainder correction in the bank ctor).
    template <typename S>
    void check_row_sums_exact() {
        const tap::samplerate::polyphase_filter_bank<S> bank(tap::samplerate::filter_spec::balanced(), k_fs);
        const auto                          scale = static_cast<std::int64_t>(tap::samplerate::sample_traits<S>::k_coeff_scale);
        for (std::size_t p = 0; p < bank.num_phases(); ++p) {
            std::int64_t sum = 0;
            for (std::size_t t = 0; t < bank.taps(); ++t) {
                sum += bank.phase(p)[t];
            }
            ASSERT_EQ(sum, scale) << "row " << p;
        }
    }
    TEST(FixedPoint, RowSumsAreExactQ15) {
        check_row_sums_exact<std::int16_t>();
    }
    TEST(FixedPoint, RowSumsAreExactQ31) {
        check_row_sums_exact<std::int32_t>();
    }

    // End-to-end quality across a +200 ppm clock crossing, like the float suite:
    // resample a sine, fit and remove the fundamental, measure the residual.
    template <tap::samplerate::sample_type S>
    double measure_snr_db(double freq_hz, double amp) {
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::basic_async_sample_rate_converter<S> asrc(cfg);
        srt_test::two_clock_sim_t<S>              sim{
                         .asrc = asrc, .fs_in = k_fs * (1.0 + k_eps), .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const double nu_in      = freq_hz / k_fs;
        const double full_scale = static_cast<double>(std::numeric_limits<S>::max());
        sim.gen                 = [&](std::uint64_t i) {
            const double v = amp * std::sin(2.0 * std::numbers::pi * nu_in * static_cast<double>(i));
            return tap::samplerate::detail::round_sat<S>(v * full_scale);
        };
        std::vector<float> tail; // normalized to [-1, 1] for the analysis helpers
        tail.reserve(48000);
        const double total = 40.0;
        sim.run(total, [&](const S* x, std::size_t frames, double t) {
            if (t >= total - 1.0) {
                for (std::size_t n = 0; n < frames; ++n) {
                    tail.push_back(static_cast<float>(static_cast<double>(x[n]) / full_scale));
                }
            }
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, tap::samplerate::converter_state::locked);
        const auto fit = srt_test::fit_sine_tracked(tail, nu_in * (1.0 + k_eps));
        EXPECT_NEAR(fit.amplitude, amp, 0.01);
        const double snr = srt_test::snr_db(fit);
        std::printf("[ measured ] %5.0f Hz, %d-bit fixed: SNR %.1f dB\n", freq_hz, int(sizeof(S) * 8), snr);
        return snr;
    }

    // Q15's floor is the format itself: input quantization, output
    // requantization and Q14 coefficient noise over 48 taps stack to ~77 dB
    // measured for a half-scale sine. Q31 matches the float datapath
    // (133/105 dB measured), whose high-frequency residual comes from the
    // phase-table linear interpolation. Thresholds sit ~4 dB under measured.
    TEST(FixedPoint, AsrcQualityQ15_997Hz) {
        EXPECT_GT(measure_snr_db<std::int16_t>(997.0, 0.5), 73.0);
    }
    TEST(FixedPoint, AsrcQualityQ31_997Hz) {
        EXPECT_GT(measure_snr_db<std::int32_t>(997.0, 0.5), 124.0); // measured ~133 dB
    }
    TEST(FixedPoint, AsrcQualityQ31_19_5kHz) {
        EXPECT_GT(measure_snr_db<std::int32_t>(19500.0, 0.5), 96.0); // measured ~105 dB
    }

    TEST(FixedPoint, FullScaleSineDoesNotWrapQ15) {
        // Drive at 99% of full scale: any internal overflow/wraparound would
        // produce gross discontinuities; saturating finalize must keep the
        // second difference at the analytic bound for a clean sine.
        tap::samplerate::config cfg;
        cfg.channels = 1;
        tap::samplerate::async_sample_rate_converter_q15    asrc(cfg);
        srt_test::two_clock_sim_t<std::int16_t> sim{
            .asrc = asrc, .fs_in = k_fs * (1.0 + 500e-6), .fs_out = k_fs, .channels = 1, .chunk_in = 1, .chunk_out = 1};
        const double nu = 1000.0 / k_fs;
        sim.gen         = [&](std::uint64_t i) {
            return tap::samplerate::detail::round_sat<std::int16_t>(
                0.99 * 32767.0 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        };
        std::vector<double> tail;
        sim.run(8.0, [&](const std::int16_t* x, std::size_t frames, double t) {
            if (t > 4.0) {
                for (std::size_t n = 0; n < frames; ++n) {
                    tail.push_back(static_cast<double>(x[n]) / 32768.0);
                }
            }
        });
        const double omega = 2.0 * std::numbers::pi * nu;
        const double bound = 1.5 * 0.99 * omega * omega + 4.0 / 32768.0; // + quantization
        for (std::size_t n = 1; n + 1 < tail.size(); ++n) {
            ASSERT_LT(std::abs(tail[n + 1] - 2.0 * tail[n] + tail[n - 1]), bound) << "n=" << n;
        }
    }

} // namespace
