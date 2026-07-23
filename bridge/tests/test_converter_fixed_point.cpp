// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Contract battery for the fixed-point converters (milestone M4). The engine
// is format-generic; what needs proving is the numeric contract of each
// format through the whole streaming path: Q31 tracks the float golden model
// at the format-negligible level, Q15's floor is the 16-bit format itself,
// full scale saturates instead of wrapping, and the row-sum DC guarantee
// survives end to end for every phase. Thresholds sit ~4 dB under measured
// (printed [ measured ] for the record), per the family convention.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "tap/dsp/analysis/sine_analysis.h"
#include "tap/ratio/converter.h"

namespace {

    using tap::ratio::basic_converter;
    using tap::ratio::direction;
    using tap::ratio::profile;

    namespace an = tap::dsp::analysis;

    template <typename S>
    constexpr double full_scale() {
        return static_cast<double>(std::numeric_limits<S>::max());
    }

    // ------------------------------------------------------------------
    // Q31 parity with the float golden model, on the same deterministic
    // noise the scipy leg uses: per-sample agreement at the float32 I/O
    // rounding level (~1e-7 relative; Q31's own quantization sits far
    // below). This is what transfers the committed scipy reference to the
    // fixed-point path without fixed-point vectors of its own.
    template <direction D>
    void check_q31_parity(const profile& p) {
        basic_converter<float, D>        cf(1, p);
        basic_converter<std::int32_t, D> cq(1, p);

        const auto&               n_in = ratio_ref::k_input;
        std::vector<std::int32_t> xq(n_in.size());
        for (std::size_t i = 0; i < n_in.size(); ++i) {
            xq[i] = static_cast<std::int32_t>(std::lround(static_cast<double>(n_in[i]) * full_scale<std::int32_t>()));
        }
        std::vector<float> yf(cf.outputs_for(n_in.size()));
        ASSERT_EQ(cf.process(n_in.data(), n_in.size(), yf.data()), yf.size());
        std::vector<std::int32_t> yq(yf.size());
        ASSERT_EQ(cq.process(xq.data(), xq.size(), yq.data()), yq.size());

        double worst = 0.0;
        for (std::size_t n = 0; n < yf.size(); ++n) {
            const double d =
                std::abs(static_cast<double>(yq[n]) / full_scale<std::int32_t>() - static_cast<double>(yf[n]));
            worst = std::max(worst, d);
        }
        std::printf("[ measured ] q31 vs float parity, worst |diff| = %.3e (%.1f dB)\n", worst,
                    20.0 * std::log10(worst + 1e-18));
        EXPECT_LT(worst, 1e-6); // measured ~1e-7 class: float32 I/O rounding
    }

    TEST(FixedPoint, Q31MatchesFloatDownEconomy) {
        check_q31_parity<direction::down_to_44k1>(profile::economy());
    }
    TEST(FixedPoint, Q31MatchesFloatUpTransparent) {
        check_q31_parity<direction::up_to_48k>(profile::transparent());
    }

    // ------------------------------------------------------------------
    // Sine quality through the full engine, measured like the float suite:
    // fit and subtract the fundamental, everything left is the residual.
    template <typename S, direction D>
    double measure_sine_snr_db(const profile& p, double freq_hz, double amp) {
        basic_converter<S, D> c(1, p);
        constexpr double      fs_in  = tap::ratio::ratio_traits<D>::k_input_rate_hz;
        constexpr double      fs_out = tap::ratio::ratio_traits<D>::k_output_rate_hz;
        const std::size_t     n_in   = 1 << 16;
        std::vector<S>        x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            const double v = amp * std::sin(2.0 * std::numbers::pi * freq_hz / fs_in * static_cast<double>(i));
            if constexpr (std::is_floating_point_v<S>) {
                x[i] = static_cast<S>(v);
            }
            else {
                x[i] = tap::dsp::detail::round_sat<S>(v * full_scale<S>());
            }
        }
        std::vector<S> y(c.outputs_for(n_in));
        c.process(x.data(), n_in, y.data());
        const auto         skip = static_cast<std::size_t>(c.latency_input_frames()) * 2;
        std::vector<float> tail(y.size() - skip);
        for (std::size_t i = 0; i < tail.size(); ++i) {
            if constexpr (std::is_floating_point_v<S>) {
                tail[i] = y[i + skip];
            }
            else {
                tail[i] = static_cast<float>(static_cast<double>(y[i + skip]) / full_scale<S>());
            }
        }
        const auto   fit = an::fit_sine_tracked(tail, freq_hz / fs_out);
        const double snr = an::snr_db(fit);
        std::printf("[ measured ] %zu-bit %5.0f Hz: SNR %.1f dB\n", sizeof(S) * 8, freq_hz, snr);
        return snr;
    }

    TEST(FixedPoint, Q15SineQualityEconomyDown) {
        // Q15's floor is the format (input quantization + output requant +
        // Q1.14 coefficient noise over 78 taps), of the same order as
        // economy's imaging floor. Measured 76.1 dB.
        EXPECT_GT((measure_sine_snr_db<std::int16_t, direction::down_to_44k1>(profile::economy(), 997.0, 0.5)), 72.0);
    }
    TEST(FixedPoint, Q15SineQualityTransparentDown) {
        // LOWER than economy, on purpose pinned: Q1.14 coefficient noise
        // stacks with tap count, so transparent's 184 taps cost ~3.7 dB over
        // economy's 78 while the 120 dB filter buys nothing a 16-bit format
        // can express. At Q15, economy is the better pairing in both compute
        // AND noise — the profile guidance the README states. Measured 72.8 dB.
        EXPECT_GT((measure_sine_snr_db<std::int16_t, direction::down_to_44k1>(profile::transparent(), 997.0, 0.5)),
                  68.0);
    }
    TEST(FixedPoint, Q31SineQualityTransparentDown) {
        // Q31 reaches the float-class figure; the residual is the filter,
        // not the format.
        EXPECT_GT((measure_sine_snr_db<std::int32_t, direction::down_to_44k1>(profile::transparent(), 997.0, 0.5)),
                  115.0);
    }
    TEST(FixedPoint, Q31SineQualityEconomyUp) {
        EXPECT_GT((measure_sine_snr_db<std::int32_t, direction::up_to_48k>(profile::economy(), 997.0, 0.5)), 80.0);
    }

    // ------------------------------------------------------------------
    // Full-scale drive must saturate, never wrap: a 99%-of-full-scale sine
    // through Q15 keeps the second difference at the analytic bound for a
    // clean sine — wraparound would blow it up by orders of magnitude.
    TEST(FixedPoint, FullScaleSineDoesNotWrapQ15) {
        basic_converter<std::int16_t, direction::down_to_44k1> c(1);
        const std::size_t                                      n_in = 1 << 15;
        const double                                           nu   = 1000.0 / 48000.0;
        std::vector<std::int16_t>                              x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] = tap::dsp::detail::round_sat<std::int16_t>(
                0.99 * 32767.0 * std::sin(2.0 * std::numbers::pi * nu * static_cast<double>(i)));
        }
        std::vector<std::int16_t> y(c.outputs_for(n_in));
        const std::size_t         made  = c.process(x.data(), n_in, y.data());
        const double              omega = 2.0 * std::numbers::pi * (1000.0 / 44100.0);
        const double              bound = 1.5 * 0.99 * omega * omega + 4.0 / 32768.0; // + quantization
        const auto                skip  = static_cast<std::size_t>(c.latency_input_frames()) * 2;
        for (std::size_t n = skip; n + 1 < made; ++n) {
            const double d2 = std::abs(static_cast<double>(y[n + 1]) - 2.0 * y[n] + y[n - 1]) / 32768.0;
            ASSERT_LT(d2, bound) << "n=" << n;
        }
    }

    // ------------------------------------------------------------------
    // The row-sum DC guarantee, end to end through the engine, exhaustively:
    // full-scale DC in fixed point must emerge within one output LSB at
    // EVERY phase of the superblock (after the fill transient).
    template <typename S, direction D>
    void check_dc_every_phase() {
        basic_converter<S, D> c(1);
        constexpr std::size_t l    = tap::ratio::ratio_traits<D>::k_phases;
        const std::size_t     n_in = c.taps() + 2 * l + 8;
        std::vector<S>        x(n_in, std::numeric_limits<S>::max());
        std::vector<S>        y(c.outputs_for(n_in));
        const std::size_t     made = c.process(x.data(), n_in, y.data());
        ASSERT_GT(made, c.outputs_for(c.taps()) + l);
        // After the window fills with full-scale DC, every subsequent output
        // (covering at least one full superblock: all L phases) sits within
        // one LSB of full scale.
        const std::uint64_t fill = c.outputs_for(c.taps());
        for (std::size_t n = fill + 1; n < made; ++n) {
            ASSERT_NEAR(static_cast<double>(y[n]), full_scale<S>(), 1.5) << "n=" << n;
        }
    }

    TEST(FixedPoint, DcEveryPhaseQ15Down) {
        check_dc_every_phase<std::int16_t, direction::down_to_44k1>();
    }
    TEST(FixedPoint, DcEveryPhaseQ15Up) {
        check_dc_every_phase<std::int16_t, direction::up_to_48k>();
    }
    TEST(FixedPoint, DcEveryPhaseQ31Down) {
        check_dc_every_phase<std::int32_t, direction::down_to_44k1>();
    }

    // ------------------------------------------------------------------
    // The call shapes stay bit-identical for integer samples too.
    TEST(FixedPoint, PullMatchesProcessBitExactQ15) {
        std::vector<std::int16_t> x(ratio_ref::k_input.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = tap::dsp::detail::round_sat<std::int16_t>(static_cast<double>(ratio_ref::k_input[i]) * 32767.0);
        }
        basic_converter<std::int16_t, direction::down_to_44k1> a(1);
        std::vector<std::int16_t>                              ya(a.outputs_for(x.size()));
        a.process(x.data(), x.size(), ya.data());

        basic_converter<std::int16_t, direction::down_to_44k1> b(1);
        std::size_t                                            fed  = 0;
        std::size_t                                            call = 0;
        auto pop = [&](std::int16_t* dst, std::size_t max_frames) noexcept -> std::size_t {
            const std::size_t dribble = 1 + (call++ % 3);
            std::size_t       n       = 0;
            while (n < max_frames && n < dribble && fed < x.size()) {
                dst[n++] = x[fed++];
            }
            return n;
        };
        std::vector<std::int16_t> yb(ya.size());
        ASSERT_EQ(b.pull(yb.data(), yb.size(), pop), ya.size());
        for (std::size_t n = 0; n < ya.size(); ++n) {
            ASSERT_EQ(ya[n], yb[n]) << "n=" << n;
        }
    }

} // namespace
