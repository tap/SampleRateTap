#include <cmath>
#include <cstdint>
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

    using Q15 = srt::SampleTraits<std::int16_t>;
    using Q31 = srt::SampleTraits<std::int32_t>;

    TEST(FixedPoint, CoefficientConversionRoundsAndSaturates) {
        EXPECT_EQ(Q15::makeCoeff(0.0), 0);
        EXPECT_EQ(Q15::makeCoeff(1.0), 16384); // Q1.14
        EXPECT_EQ(Q15::makeCoeff(-1.0), -16384);
        EXPECT_EQ(Q15::makeCoeff(10.0), 32767); // saturates
        EXPECT_EQ(Q15::makeCoeff(-10.0), -32768);
        EXPECT_EQ(Q31::makeCoeff(1.0), 1073741824);  // Q1.30
        EXPECT_EQ(Q31::makeCoeff(10.0), 2147483647); // saturates
    }

    TEST(FixedPoint, FinalizeSaturates) {
        // Far beyond full scale in the accumulator domain must clamp, not wrap.
        EXPECT_EQ(Q15::finalize(std::int64_t{1} << 40), 32767);
        EXPECT_EQ(Q15::finalize(-(std::int64_t{1} << 40)), -32768);
        EXPECT_EQ(Q31::finalize(std::int64_t{1} << 60), 2147483647);
        EXPECT_EQ(Q31::finalize(-(std::int64_t{1} << 60)), -2147483648LL);
    }

    TEST(FixedPoint, DcGainIsUnityQ15) {
        const srt::PolyphaseFilterBank<std::int16_t> bank(srt::FilterSpec::balanced(), kFs);
        std::vector<std::int16_t>                    dc(bank.taps(), 32767);
        for (int i = 0; i < 16; ++i) {
            const double mu = static_cast<double>(i) / 16.0;
            // 12 LSB = a -0.003 dB branch-gain deviation: per-tap rounding of
            // the compensated (rect-smoothed) rows biases a few LSB further
            // than the plain Kaiser rows did — still 3x inside the +/-0.01 dB
            // passband contract (37 LSB at this scale).
            EXPECT_NEAR(srt::interpolate(bank, dc.data(), mu), 32767, 12) << "mu=" << mu;
        }
    }

    TEST(FixedPoint, DcGainIsUnityQ31) {
        const srt::PolyphaseFilterBank<std::int32_t> bank(srt::FilterSpec::balanced(), kFs);
        std::vector<std::int32_t>                    dc(bank.taps(), 2147483647);
        for (int i = 0; i < 16; ++i) {
            const double mu = static_cast<double>(i) / 16.0;
            EXPECT_NEAR(srt::interpolate(bank, dc.data(), mu), 2147483647.0, 256.0) << "mu=" << mu;
        }
    }

    // End-to-end quality across a +200 ppm clock crossing, like the float suite:
    // resample a sine, fit and remove the fundamental, measure the residual.
    template <srt::SampleType S>
    double measureSnrDb(double freqHz, double amp) {
        srt::Config cfg;
        cfg.channels = 1;
        srt::BasicAsyncSampleRateConverter<S> asrc(cfg);
        srt_test::TwoClockSimT<S>             sim{
                        .asrc = asrc, .fsIn = kFs * (1.0 + kEps), .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const double nuIn      = freqHz / kFs;
        const double fullScale = static_cast<double>(std::numeric_limits<S>::max());
        sim.gen                = [&](std::uint64_t i) {
            const double v = amp * std::sin(2.0 * std::numbers::pi * nuIn * static_cast<double>(i));
            return srt::detail::roundSat<S>(v * fullScale);
        };
        std::vector<float> tail; // normalized to [-1, 1] for the analysis helpers
        tail.reserve(48000);
        const double total = 40.0;
        sim.run(total, [&](const S* x, std::size_t frames, double t) {
            if (t >= total - 1.0)
                for (std::size_t n = 0; n < frames; ++n)
                    tail.push_back(static_cast<float>(static_cast<double>(x[n]) / fullScale));
        });
        EXPECT_EQ(asrc.status().underruns, 0u);
        EXPECT_EQ(asrc.status().state, srt::State::Locked);
        const auto fit = srt_test::fitSineTracked(tail, nuIn * (1.0 + kEps));
        EXPECT_NEAR(fit.amplitude, amp, 0.01);
        const double snr = srt_test::snrDb(fit);
        std::printf("[ measured ] %5.0f Hz, %d-bit fixed: SNR %.1f dB\n", freqHz, int(sizeof(S) * 8), snr);
        return snr;
    }

    // Q15's floor is the format itself: input quantization, output
    // requantization and Q14 coefficient noise over 48 taps stack to ~77 dB
    // measured for a half-scale sine. Q31 matches the float datapath
    // (133/105 dB measured), whose high-frequency residual comes from the
    // phase-table linear interpolation. Thresholds sit ~4 dB under measured.
    TEST(FixedPoint, AsrcQualityQ15_997Hz) {
        EXPECT_GT(measureSnrDb<std::int16_t>(997.0, 0.5), 73.0);
    }
    TEST(FixedPoint, AsrcQualityQ31_997Hz) {
        EXPECT_GT(measureSnrDb<std::int32_t>(997.0, 0.5), 124.0); // measured ~133 dB
    }
    TEST(FixedPoint, AsrcQualityQ31_19_5kHz) {
        EXPECT_GT(measureSnrDb<std::int32_t>(19500.0, 0.5), 96.0); // measured ~105 dB
    }

    TEST(FixedPoint, FullScaleSineDoesNotWrapQ15) {
        // Drive at 99% of full scale: any internal overflow/wraparound would
        // produce gross discontinuities; saturating finalize must keep the
        // second difference at the analytic bound for a clean sine.
        srt::Config cfg;
        cfg.channels = 1;
        srt::AsyncSampleRateConverterQ15     asrc(cfg);
        srt_test::TwoClockSimT<std::int16_t> sim{
            .asrc = asrc, .fsIn = kFs * (1.0 + 500e-6), .fsOut = kFs, .channels = 1, .chunkIn = 1, .chunkOut = 1};
        const double nu = 1000.0 / kFs;
        sim.gen         = [&](std::uint64_t i) {
            return srt::detail::roundSat<std::int16_t>(
                0.99 * 32767.0 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        };
        std::vector<double> tail;
        sim.run(8.0, [&](const std::int16_t* x, std::size_t frames, double t) {
            if (t > 4.0)
                for (std::size_t n = 0; n < frames; ++n)
                    tail.push_back(static_cast<double>(x[n]) / 32768.0);
        });
        const double omega = 2.0 * std::numbers::pi * nu;
        const double bound = 1.5 * 0.99 * omega * omega + 4.0 / 32768.0; // + quantization
        for (std::size_t n = 1; n + 1 < tail.size(); ++n)
            ASSERT_LT(std::abs(tail[n + 1] - 2.0 * tail[n] + tail[n - 1]), bound) << "n=" << n;
    }

} // namespace
