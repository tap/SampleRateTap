// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// The golden cross-validation (PLAN section 6.3; HANDOFF's central idea,
// relocated to where it works): SampleRateTap's fractional_resampler — the
// async engine's datapath, mu-interpolated over a power-of-two phase table —
// is driven at PINNED eps = L/M - 1, no servo, over the identical input and
// the identical plain-Kaiser prototype. The exact rational machine and the
// interpolated one must agree within the interpolation floor of the async
// table (the documented ~ -12 dB per doubling of its L), exhaustively over
// every phase of the superblock, in both directions.
//
// Alignment: the resampler is primed with T-1 zeros followed by the signal,
// so its first window equals the converter's zero-primed first window
// exactly; from there both machines advance at the same rational rate — the
// resampler's Q0.64 accumulator drifts from exact rational by 2^-64 per
// output, ~5e-16 samples over this whole test. Phase identity phase(n) =
// (nM mod L)/L holds for both, so agreement is checked phase-by-phase.

#include <cmath>
#include <cstdio>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "srt/polyphase_filter.h"
#include "tap/ratio/converter.h"

namespace {

    using tap::ratio::basic_converter;
    using tap::ratio::direction;
    using tap::ratio::profile;
    using tap::ratio::ratio_traits;

    template <direction D>
    void check_cross_validation(std::size_t async_phases, double tolerance) {
        using traits    = ratio_traits<D>;
        const profile p = profile::economy();

        // ---- RatioTap: the exact rational machine.
        basic_converter<float, D> exact(1, p);
        const auto&               x = ratio_ref::k_input;
        std::vector<float>        y_exact(exact.outputs_for(x.size()));
        ASSERT_EQ(exact.process(x.data(), x.size(), y_exact.data()), y_exact.size());

        // ---- SampleRateTap: the same prototype (plain Kaiser, same cutoff,
        // beta and taps-per-phase; image_zeros off — the compensated design
        // is a different filter), decomposed over the async engine's
        // power-of-two mu-interpolated table.
        tap::samplerate::filter_spec spec;
        spec.num_phases        = async_phases;
        spec.taps_per_phase    = p.taps<D>();
        spec.passband_hz       = p.passband_hz;
        spec.stopband_hz       = traits::k_stopband_edge_hz;
        spec.stopband_atten_db = p.stopband_atten_db;
        spec.image_zeros       = false;
        const tap::samplerate::polyphase_filter_bank<float> bank(spec, traits::k_input_rate_hz);
        tap::samplerate::fractional_resampler<float>        rs(bank, 1);

        // Zero-prepend alignment: prime() consumes the first T frames, so
        // T-1 zeros + x makes the primed window [0, ..., 0, x[0]] — the
        // converter's zero-primed state — with mu = 0 = phase(0).
        const std::size_t  taps = p.taps<D>();
        std::vector<float> src(taps - 1, 0.0f);
        src.insert(src.end(), x.begin(), x.end());
        std::size_t fed = 0;
        auto        pop = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            std::size_t n = 0;
            while (n < max_frames && fed < src.size()) {
                dst[n++] = src[fed++];
            }
            return n;
        };
        ASSERT_TRUE(rs.prime(pop));

        // Pinned rational rate: (1 + eps) input frames per output frame.
        const double eps = static_cast<double>(traits::k_decimation) / static_cast<double>(traits::k_phases) - 1.0;

        // Group-delay skew cancellation: a length-LT linear-phase prototype
        // delays by (LT-1)/(2L) = T/2 - 1/(2L) input samples, which DEPENDS
        // on L — the exact table (L) and the async bank (async_phases) center
        // the same continuous filter apart by delta = 1/(2L) - 1/(2L_async)
        // input samples (~0.0024 for L=147 vs 512; ~2e-3 signal error at 0.9
        // peak, exactly what an uncompensated run measures). Advance the
        // resampler's phase accumulator once by delta — folded into its first
        // step's eps — and the machines are concentric thereafter.
        const double delta =
            1.0 / (2.0 * static_cast<double>(traits::k_phases)) - 1.0 / (2.0 * static_cast<double>(async_phases));
        std::vector<float> y_async(y_exact.size());
        ASSERT_EQ(rs.process(y_async.data(), 1, eps + delta, pop), 1u);
        const std::size_t made = 1 + rs.process(y_async.data() + 1, y_async.size() - 1, eps, pop);
        ASSERT_GE(made + taps, y_exact.size()); // resampler stops when src dries near the end

        // One-output offset between the machines' conventions: the resampler
        // consumes its advance BEFORE each dot, so it never emits the mu = 0
        // frame over [0...0, x[0]] — its output n is the converter's output
        // n + 1, at phase ((n+1)M mod L). Exhaustive: every phase of the
        // superblock is visited many times across the run; track the worst
        // disagreement per phase and demand every one of the L phases was
        // seen and bounded.
        constexpr std::size_t l = traits::k_phases;
        std::vector<double>   worst_by_phase(l, -1.0);
        double                worst = 0.0;
        for (std::size_t n = 0; n + 1 < y_exact.size() && n < made; ++n) {
            const std::size_t phase = ((n + 1) * traits::k_decimation) % l;
            const double      d     = std::abs(static_cast<double>(y_async[n]) - static_cast<double>(y_exact[n + 1]));
            worst_by_phase[phase]   = std::max(worst_by_phase[phase], d);
            worst                   = std::max(worst, d);
        }
        std::size_t phases_seen = 0;
        for (std::size_t ph = 0; ph < l; ++ph) {
            if (worst_by_phase[ph] >= 0.0) {
                ++phases_seen;
                ASSERT_LT(worst_by_phase[ph], tolerance) << "phase " << ph;
            }
        }
        EXPECT_EQ(phases_seen, l); // all phases exercised
        std::printf("[ measured ] cross-validation %s, async L=%zu: worst |diff| = %.3e (%.1f dB), %zu/%zu phases\n",
                    D == direction::down_to_44k1 ? "down" : "up  ", async_phases, worst,
                    20.0 * std::log10(worst + 1e-18), phases_seen, l);
    }

    // Measured floors: down 3.5e-6 (-109 dB), up 1.2e-5 (-99 dB) — and the
    // SAME at async L=512 and L=1024. That equality is itself evidence: the
    // async table's mu-interpolation residual (its documented -12 dB per
    // doubling of L) is already below the one deliberate filter difference
    // between the machines — RatioTap's per-branch DC normalization, a
    // ~5e-6-level perturbation the async bank does not apply. The exact and
    // interpolated machines agree to the last systematic difference we chose
    // to introduce, on every phase.
    TEST(CrossValidation, DownEconomyAgainstAsync512) {
        check_cross_validation<direction::down_to_44k1>(512, 1e-5);
    }
    TEST(CrossValidation, DownEconomyAgainstAsync1024) {
        check_cross_validation<direction::down_to_44k1>(1024, 1e-5);
    }
    TEST(CrossValidation, UpEconomyAgainstAsync512) {
        check_cross_validation<direction::up_to_48k>(512, 3e-5);
    }
    TEST(CrossValidation, UpEconomyAgainstAsync1024) {
        check_cross_validation<direction::up_to_48k>(1024, 3e-5);
    }

} // namespace
