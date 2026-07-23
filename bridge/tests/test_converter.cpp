// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// Contract battery for the streaming converter (milestone M3, float golden
// model). Three independent legs, per PLAN.md section 6: structural
// correctness (the impulse response must reproduce the coefficient table bit
// for bit through the whole engine), the committed scipy reference vectors
// (an engine we did not write), and alias acceptance measured on real
// converted audio with the shared analysis instruments. Exhaustive where the
// period-L structure makes exhaustive cheap.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "tap/dsp/analysis/sine_analysis.h"
#include "tap/ratio/converter.h"

namespace {

    using tap::ratio::basic_converter;
    using tap::ratio::direction;
    using tap::ratio::k_schedule;
    using tap::ratio::profile;
    using tap::ratio::ratio_traits;

    template <direction D>
    using conv = basic_converter<float, D>;

    // ------------------------------------------------------------------
    // Structural correctness: a unit impulse must reproduce the quantized
    // coefficient table exactly (float ==). Output n dots the impulse at
    // window index T-1-floor(nM/L) of row phase(n), so every produced sample
    // IS one stored coefficient — schedule, table, history and alignment all
    // verified bit for bit in one sweep, no external reference needed.
    template <direction D>
    void check_impulse_reproduces_table(const profile& p) {
        conv<D>            c(1, p);
        const std::size_t  taps = c.taps();
        const std::size_t  n_in = taps + 4;
        std::vector<float> x(n_in, 0.0f);
        x[0] = 1.0f;
        std::vector<float> y(c.outputs_for(n_in));
        const std::size_t  made = c.process(x.data(), n_in, y.data());
        ASSERT_EQ(made, y.size());

        constexpr std::size_t l = ratio_traits<D>::k_phases;
        constexpr std::size_t m = ratio_traits<D>::k_decimation;
        for (std::size_t n = 0; n < made; ++n) {
            const std::size_t k        = n * m / l; // newest-input index for output n
            const std::size_t phase    = (n * m) % l;
            const float       expected = k < taps ? c.table().row(phase)[taps - 1 - k] : 0.0f;
            ASSERT_EQ(y[n], expected) << "n=" << n; // bit-exact, transient included
        }
    }

    TEST(Converter, ImpulseReproducesTableDown) {
        check_impulse_reproduces_table<direction::down_to_44k1>(profile::economy());
    }
    TEST(Converter, ImpulseReproducesTableUp) {
        check_impulse_reproduces_table<direction::up_to_48k>(profile::economy());
    }
    TEST(Converter, ImpulseReproducesTableDownTransparent) {
        check_impulse_reproduces_table<direction::down_to_44k1>(profile::transparent());
    }

    // ------------------------------------------------------------------
    // The independent golden leg: committed scipy.signal.upfirdn outputs
    // (float64 engine, same designs) over deterministic noise. The streaming
    // converter must match sample for sample from n = 0, transient included,
    // within the float32 coefficient-quantization floor (~-90 dB at the 0.9
    // peak; see tools/reference/make_reference_vectors.py).
    template <direction D, std::size_t NRef>
    void check_reference(const profile& p, const std::array<float, NRef>& ref) {
        conv<D> c(1, p);
        ASSERT_EQ(c.outputs_for(ratio_ref::k_input.size()), NRef);
        std::vector<float> y(NRef);
        ASSERT_EQ(c.process(ratio_ref::k_input.data(), ratio_ref::k_input.size(), y.data()), NRef);
        for (std::size_t n = 0; n < NRef; ++n) {
            ASSERT_NEAR(y[n], ref[n], 3e-5f) << "n=" << n;
        }
    }

    TEST(Converter, MatchesScipyDownEconomy) {
        check_reference<direction::down_to_44k1>(profile::economy(), ratio_ref::k_down_economy);
    }
    TEST(Converter, MatchesScipyDownTransparent) {
        check_reference<direction::down_to_44k1>(profile::transparent(), ratio_ref::k_down_transparent);
    }
    TEST(Converter, MatchesScipyUpEconomy) {
        check_reference<direction::up_to_48k>(profile::economy(), ratio_ref::k_up_economy);
    }
    TEST(Converter, MatchesScipyUpTransparent) {
        check_reference<direction::up_to_48k>(profile::transparent(), ratio_ref::k_up_transparent);
    }

    // ------------------------------------------------------------------
    // pull() is bit-identical to process() on the same stream, under a
    // deliberately awkward source (dribbling deliveries of varying size),
    // and a dry source short-returns then resumes exactly.
    TEST(Converter, PullMatchesProcessBitExact) {
        const auto& x = ratio_ref::k_input;

        conv<direction::down_to_44k1> a(1);
        std::vector<float>            ya(a.outputs_for(x.size()));
        a.process(x.data(), x.size(), ya.data());

        conv<direction::down_to_44k1> b(1);
        std::size_t                   fed  = 0;
        std::size_t                   call = 0;
        auto                          pop  = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            const std::size_t dribble = 1 + (call++ % 3); // 1..3 frames per call
            std::size_t       n       = 0;
            while (n < max_frames && n < dribble && fed < x.size()) {
                dst[n++] = x[fed++];
            }
            return n;
        };
        std::vector<float> yb(ya.size());
        const std::size_t  made = b.pull(yb.data(), yb.size(), pop);
        ASSERT_EQ(made, ya.size());
        for (std::size_t n = 0; n < ya.size(); ++n) {
            ASSERT_EQ(ya[n], yb[n]) << "n=" << n;
        }
    }

    TEST(Converter, PullShortReturnsOnDryThenResumes) {
        const auto& x = ratio_ref::k_input;

        conv<direction::up_to_48k> a(1);
        std::vector<float>         ya(a.outputs_for(x.size()));
        a.process(x.data(), x.size(), ya.data());

        conv<direction::up_to_48k> b(1);
        std::vector<float>         yb(ya.size());
        std::size_t                fed   = 0;
        std::size_t                limit = 100; // first tranche of input
        auto                       pop   = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            std::size_t n = 0;
            while (n < max_frames && fed < limit) {
                dst[n++] = x[fed++];
            }
            return n;
        };
        const std::size_t first = b.pull(yb.data(), yb.size(), pop);
        EXPECT_LT(first, yb.size()); // ran dry
        limit                    = x.size();
        const std::size_t second = b.pull(yb.data() + first * 1, yb.size() - first, pop);
        ASSERT_EQ(first + second, ya.size());
        for (std::size_t n = 0; n < ya.size(); ++n) {
            ASSERT_EQ(ya[n], yb[n]) << "n=" << n;
        }
    }

    // ------------------------------------------------------------------
    // frames_needed and outputs_for are exact from EVERY superblock position:
    // drive the engine one output at a time with a counting source and check
    // the pre-computed predictions against observed consumption, plus the
    // closed-form outputs_for against its defining inequality.
    template <direction D>
    void check_accounting_exhaustively() {
        constexpr std::size_t l = ratio_traits<D>::k_phases;
        conv<D>               c(1);
        std::size_t           consumed = 0;
        auto                  pop      = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            for (std::size_t i = 0; i < max_frames; ++i) {
                dst[i] = 0.0f;
            }
            consumed += max_frames;
            return max_frames;
        };
        float y = 0.0f;
        for (std::size_t pos = 0; pos < l; ++pos) {
            ASSERT_EQ(c.position(), pos);
            // Predictions from this exact state.
            std::vector<std::uint64_t> need(2 * l + 1);
            for (std::size_t n = 1; n <= 2 * l; ++n) {
                need[n] = c.frames_needed(n);
            }
            ASSERT_EQ(c.frames_needed(0), 0u);
            // outputs_for is the exact inverse of the need table: for every
            // input count a, it must equal the largest n with need[n] <= a.
            // (Not simply outputs_for(need[n]) == n: in the up direction an
            // advance-0 output can ride along for free, so outputs_for may
            // legitimately exceed n at the same input count.)
            {
                std::size_t n_reach = 0;
                for (std::uint64_t a = 0; a < need[2 * l]; ++a) {
                    while (n_reach + 1 <= 2 * l && need[n_reach + 1] <= a) {
                        ++n_reach;
                    }
                    ASSERT_EQ(c.outputs_for(a), n_reach) << "pos=" << pos << " a=" << a;
                }
            }
            // Walk one superblock producing single outputs; consumption must
            // track need[] exactly.
            const std::size_t base           = consumed;
            conv<D>           probe          = c; // copy: keep c at pos for its own next step
            std::size_t       probe_consumed = 0;
            auto              probe_pop      = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
                for (std::size_t i = 0; i < max_frames; ++i) {
                    dst[i] = 0.0f;
                }
                probe_consumed += max_frames;
                return max_frames;
            };
            for (std::size_t n = 1; n <= l; ++n) {
                ASSERT_EQ(probe.pull(&y, 1, probe_pop), 1u);
                ASSERT_EQ(probe_consumed, need[n]) << "pos=" << pos << " n=" << n;
            }
            static_cast<void>(base);
            // Advance the primary engine one output to the next position.
            ASSERT_EQ(c.pull(&y, 1, pop), 1u);
        }
    }

    TEST(Converter, AccountingExactFromEveryPositionUp) {
        check_accounting_exhaustively<direction::up_to_48k>();
    }
    TEST(Converter, AccountingExactFromEveryPositionDown) {
        check_accounting_exhaustively<direction::down_to_44k1>();
    }

    // ------------------------------------------------------------------
    // Alias acceptance on real converted audio (PLAN section 8), float engine.
    namespace an = tap::dsp::analysis;

    std::vector<float> run_sine_down(const profile& p, double freq_hz, double amp, std::size_t n_in) {
        conv<direction::down_to_44k1> c(1, p);
        std::vector<float>            x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] =
                static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * freq_hz / 48000.0 * static_cast<double>(i)));
        }
        std::vector<float> y(c.outputs_for(n_in));
        c.process(x.data(), n_in, y.data());
        // Drop the transient (group delay) before measurement.
        const auto skip = static_cast<std::size_t>(c.latency_input_frames()) * 2;
        return {y.begin() + static_cast<std::ptrdiff_t>(skip), y.end()};
    }

    TEST(Converter, PassbandSineEconomyHitsTheImagingFloor) {
        // 997 Hz through economy 48->44.1. The residual is NOT aliasing (a
        // 997 Hz tone's decimation aliases are ultrasonic by arithmetic) but
        // IMAGING LEAKAGE: the upsampling images at k*48000 +/- 997 Hz
        // survive at stopband depth and fold in-band (e.g. 47003 -> 2903 Hz).
        // That floor is bounded by the stopband (>= 71 dB below the tone,
        // deeper where the Kaiser sidelobes have decayed; measured ~89 dB
        // SNR here). This is economy's honest in-band contract — and exactly
        // what the k*fs image-zeros lever (PLAN M7) would deepen.
        const auto tail = run_sine_down(profile::economy(), 997.0, 0.5, 1 << 16);
        const auto fit  = an::fit_sine_tracked(tail, 997.0 / 44100.0);
        EXPECT_NEAR(fit.amplitude, 0.5, 1e-4);
        EXPECT_GT(an::snr_db(fit), 85.0); // measured ~89.2 dB
    }

    TEST(Converter, PassbandSineIsTransparentTransparent) {
        const auto tail = run_sine_down(profile::transparent(), 997.0, 0.5, 1 << 16);
        const auto fit  = an::fit_sine_tracked(tail, 997.0 / 44100.0);
        EXPECT_GT(an::snr_db(fit), 115.0);
    }

    // Goertzel power probe at one frequency, dBFS re 1.0 amplitude.
    double probe_dbfs(const std::vector<float>& y, double freq_norm) {
        const auto fit = an::fit_sine(std::span<const float>(y), freq_norm);
        return 20.0 * std::log10(fit.amplitude + 1e-12);
    }

    TEST(Converter, StopbandToneProductsAreBoundedByTheSpec) {
        // A 23 kHz tone at 48 k sits in the down direction's stopband. Two
        // distinct products, per the acceptance contract:
        //  - its decimation ALIAS folds to 44100 - 23000 = 21.1 kHz — above
        //    20 kHz by arithmetic (the charter's confinement claim) and
        //    attenuated >= 71 dB;
        //  - its upsampling IMAGE at 48000 - 23000 = 25 kHz survives at
        //    stopband depth and folds IN-BAND to 44100 - 25000 = 19.1 kHz.
        //    In-band products are bounded by the stopband, not absent —
        //    economy's honest limit (measured -85 dBFS for the -6 dBFS tone).
        const auto y = run_sine_down(profile::economy(), 23000.0, 0.5, 1 << 16);
        // Folded alias at 21.1 kHz: <= -(71) dB relative to the 0.5 FS tone.
        EXPECT_LT(probe_dbfs(y, (44100.0 - 23000.0) / 44100.0), -6.0 - 71.0 + 3.0); // 3 dB grace
        // Audible band: every product at least the stopband below the tone.
        for (double f = 100.0; f < 20000.0; f += 100.0) {
            ASSERT_LT(probe_dbfs(y, f / 44100.0), -6.0 - 71.0) << f << " Hz";
        }
        // And the worst in-band product is the predicted 19.1 kHz image.
        EXPECT_LT(probe_dbfs(y, 19100.0 / 44100.0), -80.0); // measured ~-85 dBFS
    }

    // ------------------------------------------------------------------
    // Multichannel: distinct tones per channel through one instance stay
    // independent (crosstalk at the float floor) and phase-coherent.
    TEST(Converter, TwoChannelsAreIndependent) {
        // transparent profile: its -121 dB floor puts filter spurs far below
        // the crosstalk threshold, so the fit at the other channel's tone
        // measures actual channel bleed and nothing else.
        conv<direction::down_to_44k1> c(2, profile::transparent());
        const std::size_t             n_in = 1 << 15;
        std::vector<float>            x(n_in * 2);
        for (std::size_t i = 0; i < n_in; ++i) {
            const auto t = static_cast<double>(i);
            x[i * 2]     = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 997.0 / 48000.0 * t));
            x[i * 2 + 1] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 6000.0 / 48000.0 * t));
        }
        std::vector<float> y(c.outputs_for(n_in) * 2);
        const std::size_t  made = c.process(x.data(), n_in, y.data());
        // Skip the startup transient (group delay), as every measurement
        // here does: its broadband ramp otherwise biases both fits.
        const auto skip = static_cast<std::size_t>(c.latency_input_frames()) * 2;
        ASSERT_GT(made, skip + 1024);
        std::vector<float> ch0(made - skip), ch1(made - skip);
        for (std::size_t i = 0; i < ch0.size(); ++i) {
            ch0[i] = y[(i + skip) * 2];
            ch1[i] = y[(i + skip) * 2 + 1];
        }
        // Each channel carries its own tone...
        EXPECT_NEAR(an::fit_sine_tracked(ch0, 997.0 / 44100.0).amplitude, 0.5, 1e-3);
        EXPECT_NEAR(an::fit_sine_tracked(ch1, 6000.0 / 44100.0).amplitude, 0.5, 1e-3);
        // ...and crosstalk is exactly zero: every stereo channel must be
        // bit-identical to a mono run of the same signal. (A fit at the other
        // channel's frequency cannot show this — the rectangular-window
        // leakage of the strong own-tone floors such a probe near -87 dB
        // regardless of bleed.)
        conv<direction::down_to_44k1> mono(1, profile::transparent());
        std::vector<float>            x0(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x0[i] = x[i * 2];
        }
        std::vector<float> y0(mono.outputs_for(n_in));
        ASSERT_EQ(mono.process(x0.data(), n_in, y0.data()), made);
        for (std::size_t i = 0; i < made; ++i) {
            ASSERT_EQ(y0[i], y[i * 2]) << "i=" << i;
        }
    }

    // ------------------------------------------------------------------
    // Lifecycle: flush drains the tail to silence with the predicted count;
    // reset reproduces the identical stream bit for bit.
    TEST(Converter, FlushDrainsTailToSilence) {
        conv<direction::down_to_44k1> c(1);
        const std::size_t             n_in = 480;
        std::vector<float>            x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 0.02 * static_cast<double>(i)));
        }
        std::vector<float> y(c.outputs_for(n_in));
        c.process(x.data(), n_in, y.data());
        const std::uint64_t expect_tail = c.flush_output_frames();
        std::vector<float>  tail(expect_tail);
        ASSERT_EQ(c.flush(tail.data()), expect_tail);
        // The tail decays to (near) silence: the very last output's window
        // holds at most one real sample (a down-direction skip can leave the
        // final zero unconsumed), so demand decay, not exact zeros.
        ASSERT_GE(tail.size(), 4u);
        EXPECT_LT(std::abs(tail[tail.size() - 1]), 1e-4f);
        EXPECT_LT(std::abs(tail[tail.size() - 2]), 1e-3f);
    }

    TEST(Converter, ResetReproducesBitExactly) {
        conv<direction::up_to_48k> c(1);
        const auto&                x = ratio_ref::k_input;
        std::vector<float>         y1(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), y1.data());
        c.reset();
        std::vector<float> y2(y1.size());
        ASSERT_EQ(c.process(x.data(), x.size(), y2.data()), y2.size());
        for (std::size_t n = 0; n < y1.size(); ++n) {
            ASSERT_EQ(y1[n], y2[n]) << "n=" << n;
        }
    }

    TEST(Converter, LatencyAndValidation) {
        conv<direction::down_to_44k1> c(1);
        EXPECT_NEAR(c.latency_input_frames(), 78.0 / 2.0, 0.51);
        EXPECT_EQ(c.channels(), 1u);
        EXPECT_THROW(conv<direction::down_to_44k1>(0), std::invalid_argument);
    }

} // namespace
