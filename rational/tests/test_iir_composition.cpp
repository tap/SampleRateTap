// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The composition tests (IIR-c, docs/IIR_PLAN.md I10, the maintainer's
// decision on question 16): the recursive stages and the guard band filter
// satisfy tap::dsp::sync_stage, and a mixed chain is a caller's
// tap::dsp::chain, never a named chain or a coverage-matrix row:
//   - the concepts: sync_stage for both guard band forms in every format;
//     the linear form an exact-latency stage, the Chebyshev a nominal one;
//     a chain of near-linear stages and the linear form an exact_delay_stage
//     for the round-trip scaffold, one holding the Chebyshev not (and its
//     exact latency refused at compile time: compile_fail/
//     guard_band_exact_latency.cpp);
//   - mixed chains, typed over float / double / Q15 / Q31: an IIR up2 and an
//     FIR up3, a guard band filter before an interpolator (FIR or IIR) and
//     after a decimator (FIR, IIR, a recursive cascade, a named FIR chain),
//     each with both forms: bit-identical for any chunking, and
//     FlushEqualsZeroPaddingBitForBit with the honest limit stated exactly
//     where it applies (a floating stage that holds a zero-input limit
//     cycle under flush-to-zero: the Chebyshev II of order 7 or more, and the
//     near-linear transparent stage at divisor 1, question 19; flush() is
//     bit for bit where that stage is the chain's last, and within the
//     residue of zero padding where stages follow it);
//   - latency: an all-FIR chain holding the linear form keeps R7's exact
//     latency, and a chain holding a recursive stage composes the nominal,
//     both against the impulse response;
//   - the offering table (I10): by 2 down and up, both recursive variants,
//     every profile, with no guard band filter or either form, its
//     multiplies per output and nominal latency, measured and pinned
//     (iir_composition.TheOfferingTableIsMeasured; the READMEs and the book
//     quote it).
// Every test runs under the host's flush-to-zero (the fixture), as I9 asks
// of a recursive test.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "support/flush_to_zero.h"
#include "support/iir_signals.h"
#include "tap/dsp/chain.h"
#include "tap/dsp/subband.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational;       // NOLINT(google-build-using-namespace)
    using namespace tap::sr::rational::test; // NOLINT(google-build-using-namespace)

    constexpr guard_phase k_chebyshev   = guard_phase::nonlinear;
    constexpr guard_phase k_fir_form    = guard_phase::linear;
    constexpr iir_phase   k_nonlinear   = iir_phase::nonlinear;
    constexpr iir_phase   k_near_linear = iir_phase::near_linear;

    // ------------------------------------------------------------------
    // The concepts
    // ------------------------------------------------------------------

    template <typename T>
    concept exact_latency = requires(const T& t) {
        { t.latency_output_frames() } -> std::same_as<tap::dsp::exact_ratio>;
    };

    template <typename S>
    constexpr bool k_both_forms_are_sync_stages = tap::dsp::sync_stage<guard_band_filter<S, 1, k_chebyshev>>
                                                  && tap::dsp::sync_stage<guard_band_filter<S, 4, k_fir_form>>;
    static_assert(k_both_forms_are_sync_stages<float> && k_both_forms_are_sync_stages<double>
                  && k_both_forms_are_sync_stages<std::int16_t> && k_both_forms_are_sync_stages<std::int32_t>);
    static_assert(exact_latency<guard_band_filter<float, 1, k_fir_form>>
                      && !tap::dsp::reports_nominal_latency<guard_band_filter<float, 1, k_fir_form>>,
                  "the linear form: an exact latency, as an FIR stage");
    static_assert(!exact_latency<guard_band_filter<float, 1, k_chebyshev>>
                      && tap::dsp::reports_nominal_latency<guard_band_filter<float, 1, k_chebyshev>>,
                  "the Chebyshev II: a nominal latency, as a recursive stage");
    static_assert(tap::dsp::exact_delay_stage<tap::dsp::chain<basic_iir_stage<float, 1, down2, k_near_linear>,
                                                              guard_band_filter<float, 1, k_fir_form>>>,
                  "a near-linear stage and the linear form enter the round-trip scaffold");
    static_assert(!tap::dsp::exact_delay_stage<tap::dsp::chain<basic_iir_stage<float, 1, down2, k_near_linear>,
                                                               guard_band_filter<float, 1, k_chebyshev>>>,
                  "the Chebyshev II has no exact delay");

    // ------------------------------------------------------------------
    // The mixed chains
    // ------------------------------------------------------------------

    /// One mixed chain at a profile: its type, how to build it, and which of
    /// its floating stages may hold the zero-input limit cycle (the honest
    /// limit): one that is not the chain's last, and the last.
    template <typename S, std::size_t Ch, guard_phase G>
    struct mixed {
        using guard_t = guard_band_filter<S, Ch, G>;
        /// The named setting at p behind the half-band kind the filter serves.
        static guard_band_settings settings(const profile& p, half_band_kind kind) {
            return p.stopband_atten_db == 120.0     ? guard_band_settings::transparent(kind)
                   : p.passband_frac == 1.0 / 3.0   ? guard_band_settings::super_economy(kind)
                   : p.passband_frac == 19.0 / 48.0 ? guard_band_settings::balanced(kind)
                                                    : guard_band_settings::economy(kind);
        }
        /// Whether the guard band filter at p behind kind may hold the limit
        /// cycle (the floating Chebyshev II of order 7 or more).
        static bool guard_cycles(const profile& p, half_band_kind kind) {
            return std::is_floating_point_v<S> && G == k_chebyshev && settings(p, kind).order >= 7;
        }
        /// Whether a near-linear stage at divisor 1 and transparent may.
        static bool near_linear_cycles(const profile& p) {
            return std::is_floating_point_v<S> && p.stopband_atten_db == 120.0;
        }

        // An IIR up2 and an FIR up3 (I10's example): 8 -> 48 kHz with a guard
        // band filter before them.
        using up_t = tap::dsp::chain<guard_t, basic_iir_stage<S, Ch, up2, k_nonlinear>, basic_stage<S, Ch, up3>>;
        static up_t make_up(const profile& p) {
            return up_t(guard_t{settings(p, half_band_kind::nonlinear)}, basic_iir_stage<S, Ch, up2, k_nonlinear>{p},
                        basic_stage<S, Ch, up3>{p.relaxed({2, 1})});
        }
        // The mirror: an FIR down3 and an IIR down2, then the guard band filter.
        using down_t = tap::dsp::chain<basic_stage<S, Ch, down3>, basic_iir_stage<S, Ch, down2, k_nonlinear>, guard_t>;
        static down_t make_down(const profile& p) {
            return down_t(basic_stage<S, Ch, down3>{p.relaxed({2, 1})}, basic_iir_stage<S, Ch, down2, k_nonlinear>{p},
                          guard_t{settings(p, half_band_kind::nonlinear)});
        }
        // Before an FIR interpolator and after an FIR decimator.
        using fir_up_t   = tap::dsp::chain<guard_t, basic_stage<S, Ch, up2>>;
        using fir_down_t = tap::dsp::chain<basic_stage<S, Ch, down2>, guard_t>;
        static fir_up_t make_fir_up(const profile& p) {
            return fir_up_t(guard_t{settings(p, half_band_kind::fir)}, basic_stage<S, Ch, up2>{p});
        }
        static fir_down_t make_fir_down(const profile& p) {
            return fir_down_t(basic_stage<S, Ch, down2>{p}, guard_t{settings(p, half_band_kind::fir)});
        }
        // Before a near-linear interpolator, and after a near-linear cascade by 8.
        using nl_up_t     = tap::dsp::chain<guard_t, basic_iir_stage<S, Ch, up2, k_near_linear>>;
        using cascade_t   = tap::dsp::chain<iir_down_by<S, Ch, k_near_linear, 8>, guard_t>;
        using named_fir_t = tap::dsp::chain<down_by8<S, Ch>, guard_t>;
        // down_by8's stages flattened (a tap::dsp::chain reports no
        // window_frames(), so a chain holding a chain cannot flush).
        using flat_fir_t = tap::dsp::chain<basic_stage<S, Ch, down4>, basic_stage<S, Ch, down2>, guard_t>;
        static nl_up_t make_nl_up(const profile& p) {
            return nl_up_t(guard_t{settings(p, half_band_kind::near_linear)},
                           basic_iir_stage<S, Ch, up2, k_near_linear>{p});
        }
        static cascade_t make_cascade(const profile& p) {
            return cascade_t(iir_down_by<S, Ch, k_near_linear, 8>{p},
                             guard_t{settings(p, half_band_kind::near_linear)});
        }
        static named_fir_t make_named_fir(const profile& p) {
            return named_fir_t(down_by8<S, Ch>{p}, guard_t{settings(p, half_band_kind::fir)});
        }
        static flat_fir_t make_flat_fir(const profile& p) {
            return flat_fir_t(basic_stage<S, Ch, down4>{p.relaxed({2, 1})}, basic_stage<S, Ch, down2>{p},
                              guard_t{settings(p, half_band_kind::fir)});
        }
    };

    template <typename S>
    class iir_composition_test : public flush_to_zero_fixture {};
    using sample_types = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(iir_composition_test, sample_types, );

    class iir_composition : public flush_to_zero_fixture {};

    template <typename Chain, typename Make>
    void expect_chunking_is_bit_identical(Make make, std::size_t frames) {
        using sample         = typename Chain::sample;
        constexpr auto C     = Chain::channels();
        const auto     x     = noise<sample>(frames * C, 0x2468u);
        Chain          whole = make();
        const auto     ref   = run(whole, x);
        for (const std::size_t chunk : {std::size_t{1}, std::size_t{5}, std::size_t{64}, std::size_t{129}}) {
            Chain               c = make();
            std::vector<sample> y;
            for (std::size_t at = 0; at < frames; at += chunk) {
                const std::size_t   n = std::min(chunk, frames - at);
                std::vector<sample> part(c.outputs_for(n) * C);
                ASSERT_EQ(c.process(x.data() + at * C, n, part.data()), part.size() / C);
                y.insert(y.end(), part.begin(), part.end());
            }
            EXPECT_TRUE(y == ref) << "chunk " << chunk;
        }
    }

    TYPED_TEST(iir_composition_test, MixedChainsAreBitIdenticalForAnyChunking) {
        using sample    = TypeParam;
        const auto both = [&]<guard_phase G>() {
            using m = mixed<sample, 2, G>;
            // balanced too: in Q15 its linear form runs a Q15 row two taps
            // longer than the double row.
            for (const profile& p : {profile::economy(), profile::balanced(), profile::transparent()}) {
                expect_chunking_is_bit_identical<typename m::up_t>([&] { return m::make_up(p); }, 300);
                expect_chunking_is_bit_identical<typename m::down_t>([&] { return m::make_down(p); }, 900);
                expect_chunking_is_bit_identical<typename m::fir_up_t>([&] { return m::make_fir_up(p); }, 300);
                expect_chunking_is_bit_identical<typename m::fir_down_t>([&] { return m::make_fir_down(p); }, 600);
                expect_chunking_is_bit_identical<typename m::nl_up_t>([&] { return m::make_nl_up(p); }, 300);
                expect_chunking_is_bit_identical<typename m::cascade_t>([&] { return m::make_cascade(p); }, 1600);
                expect_chunking_is_bit_identical<typename m::named_fir_t>([&] { return m::make_named_fir(p); }, 1600);
            }
        };
        both.template operator()<k_chebyshev>();
        both.template operator()<k_fir_form>();
    }

    // ------------------------------------------------------------------
    // Flush (I6) over mixed chains, and the honest limit
    // ------------------------------------------------------------------

    /// The residue a chain's flush may leave where one of its floating
    /// stages holds the limit cycle: the guard band filter's bound (16 times
    /// the smallest normal) and the near-linear stage's (8), through the
    /// stages after them, with room: 64 times the smallest normal.
    template <typename S>
    constexpr double k_cycle_residue =
        std::is_floating_point_v<S> ? 64.0 * static_cast<double>(std::numeric_limits<S>::min()) : 0.0;

    /// Where no control bit governs a floating S (double on the M33, float
    /// and double on Hexagon), the fixture's flush-to-zero reaches nothing
    /// and a chain holding a recursive stage flushes to within the subnormal
    /// residue instead (test_iir_subnormals.cpp pins a stage's).
    template <typename S>
    constexpr bool k_no_flush_control = std::is_floating_point_v<S> && !tap::dsp::k_subnormal_control<S>;
    template <typename S>
    constexpr double k_subnormal_residue =
        std::is_floating_point_v<S> ? 1024.0 * static_cast<double>(std::numeric_limits<S>::denorm_min()) : 0.0;

    /// flush() equals zero padding: bit for bit where `within` is 0, else to
    /// within it; past the flush, silence where `after` is 0, else at most it.
    template <typename Chain, typename Make>
    void expect_flush_is_zero_padding(Make make, std::size_t fed_max, double within, double after) {
        using sample     = typename Chain::sample;
        constexpr auto C = Chain::channels();
        const auto     x = noise<sample>(fed_max * C, 0x6161u);
        for (std::size_t fed = fed_max - 1; fed <= fed_max; ++fed) {
            Chain               flushed = make();
            std::vector<sample> scratch(flushed.outputs_for(fed) * C + C);
            flushed.process(x.data(), fed, scratch.data());
            Chain               padded = flushed;
            const std::size_t   expect = flushed.flush_output_frames();
            std::vector<sample> a(expect * C + C, sample{1});
            ASSERT_EQ(flushed.flush(a.data()), expect);
            // The padded run, streamed in blocks of zeros (the double windows
            // run to tens of thousands of frames), to 512 frames past the
            // flush's.
            constexpr std::size_t k_block = 256;
            std::vector<sample>   z(k_block * C, sample{});
            std::vector<sample>   b((padded.outputs_for(k_block) + 1) * C);
            std::size_t           made = 0;
            while (made < expect + 512) {
                const std::size_t got = padded.process(z.data(), k_block, b.data());
                for (std::size_t j = 0; j < got * C; ++j) {
                    const std::size_t i = made * C + j;
                    if (i < expect * C) {
                        if (within == 0.0) {
                            ASSERT_EQ(a[i], b[j]) << "fed " << fed << " sample " << i;
                        }
                        else {
                            ASSERT_LE(std::fabs(to_unit(a[i]) - to_unit(b[j])), within) << "fed " << fed;
                        }
                    }
                    else if (after == 0.0) {
                        ASSERT_EQ(b[j], sample{}) << "fed " << fed << " sample " << i << ": silence past the flush";
                    }
                    else {
                        ASSERT_LE(std::fabs(to_unit(b[j])), after) << "fed " << fed << ": the residue past the flush";
                    }
                }
                made += got;
            }
        }
    }

    TYPED_TEST(iir_composition_test, FlushEqualsZeroPaddingBitForBitInMixedChains) {
        // Over chains holding recursive stages and both guard band forms, at
        // super_economy (where the order behind the nonlinear half-band, 5,
        // drains and the order behind the others, 7, may cycle), economy and
        // transparent, each filter at its half-band's setting. Bit for bit
        // and silent after, but for the
        // honest limit: a floating stage that holds the zero-input limit
        // cycle leaves its residue after the flush, and where stages follow it
        // the chain's flush (which feeds each later stage exact zeros where
        // padding feeds it the residue) agrees with padding to within it. In
        // fixed point every stage drains: bit for bit everywhere.
        using sample    = TypeParam;
        const auto both = [&]<guard_phase G>() {
            using m = mixed<sample, 1, G>;
            for (const profile& p : {profile::super_economy(), profile::economy(), profile::transparent()}) {
                const bool   nonlinear_guard_cycles   = m::guard_cycles(p, half_band_kind::nonlinear);
                const bool   fir_guard_cycles         = m::guard_cycles(p, half_band_kind::fir);
                const bool   near_linear_guard_cycles = m::guard_cycles(p, half_band_kind::near_linear);
                const bool   nl_cycles                = m::near_linear_cycles(p);
                const double r                        = k_cycle_residue<sample>;
                const double none                     = k_no_flush_control<sample> ? k_subnormal_residue<sample> : 0.0;
                const auto   limit                    = [&](bool followed, bool last) {
                    // (within, after) for a chain whose cycling stage is
                    // followed by others, or is the last.
                    return std::array<double, 2>{std::max(none, followed ? r : 0.0),
                                                 std::max(none, followed || last ? r : 0.0)};
                };
                // The guard band filter first, before interpolators.
                auto l = limit(nonlinear_guard_cycles, false);
                expect_flush_is_zero_padding<typename m::up_t>([&] { return m::make_up(p); }, 120, l[0], l[1]);
                l = limit(fir_guard_cycles, false);
                expect_flush_is_zero_padding<typename m::fir_up_t>([&] { return m::make_fir_up(p); }, 120, l[0], l[1]);
                // ... before the near-linear interpolator, itself the cycling
                // stage at transparent, and the chain's last.
                l = limit(near_linear_guard_cycles, nl_cycles);
                expect_flush_is_zero_padding<typename m::nl_up_t>([&] { return m::make_nl_up(p); }, 120, l[0], l[1]);
                // The guard band filter last, after decimators: its own flush
                // is bit for bit; past it, its residue.
                l = limit(false, nonlinear_guard_cycles);
                expect_flush_is_zero_padding<typename m::down_t>([&] { return m::make_down(p); }, 600, l[0], l[1]);
                l = limit(false, fir_guard_cycles);
                expect_flush_is_zero_padding<typename m::fir_down_t>([&] { return m::make_fir_down(p); }, 300, l[0],
                                                                     l[1]);
                // After the near-linear cascade by 8, whose last stage (divisor
                // 1) cycles at transparent and is followed by the filter.
                l = limit(nl_cycles, near_linear_guard_cycles);
                expect_flush_is_zero_padding<typename m::cascade_t>([&] { return m::make_cascade(p); }, 800, l[0],
                                                                    l[1]);
                // After down_by8's FIR stages: the linear form keeps the chain
                // an FIR chain, exact without flush-to-zero too.
                l = G == k_fir_form ? std::array<double, 2>{0.0, 0.0} : limit(false, fir_guard_cycles);
                expect_flush_is_zero_padding<typename m::flat_fir_t>([&] { return m::make_flat_fir(p); }, 800, l[0],
                                                                     l[1]);
            }
        };
        both.template operator()<k_chebyshev>();
        both.template operator()<k_fir_form>();
    }

    // ------------------------------------------------------------------
    // Latency (I5): exact where every stage is, nominal where one is not
    // ------------------------------------------------------------------

    TEST_F(iir_composition, TheLinearFormKeepsAnAllFirChainsExactLatency) {
        // R7's exact latency, summed by tap::dsp::chain with the linear form's
        // (N - 1) / 2: after the FIR half-band decimator (19/2 + 49/2 = 34
        // frames at economy), after the named FIR chain by 8, and before the
        // FIR half-band interpolator (49/2 x 2 + 19 = 68 output frames), where
        // the impulse response delays every passband frequency by exactly that.
        using li_t = guard_band_filter<double, 1, k_fir_form>;
        const tap::dsp::chain<basic_stage<double, 1, down2>, li_t> down(basic_stage<double, 1, down2>{}, li_t{});
        EXPECT_EQ(down.latency_output_frames(), (tap::dsp::exact_ratio{34, 1}));
        const tap::dsp::chain<down_by8<double, 1>, li_t> by8(down_by8<double, 1>{}, li_t{});
        EXPECT_EQ(by8.latency_output_frames(), (by8.stage<0>().latency_output_frames() + tap::dsp::exact_ratio{49, 2}));
        EXPECT_EQ(by8.nominal_latency_output_frames(), by8.latency_output_frames().value());
        EXPECT_EQ(by8.latency_deviation_output_frames(), 0.0);
        // In Q15 the linear form runs DspTap's Q15 rows: economy's is as long
        // as the double row (34 frames again), super_economy's and balanced's
        // two taps longer, and the chain's exact latency follows (39/2 and
        // 59/2 after the decimator).
        using lq_t = guard_band_filter<std::int16_t, 1, k_fir_form>;
        using dq_t = basic_stage<std::int16_t, 1, down2>;
        const tap::dsp::chain<dq_t, lq_t> eco_q15(dq_t{}, lq_t{});
        EXPECT_EQ(eco_q15.latency_output_frames(), (tap::dsp::exact_ratio{34, 1}));
        const tap::dsp::chain<dq_t, lq_t> se_q15(dq_t{profile::super_economy()},
                                                 lq_t{guard_band_settings::super_economy(half_band_kind::fir)});
        EXPECT_EQ(se_q15.latency_output_frames(),
                  (se_q15.stage<0>().latency_output_frames() + tap::dsp::exact_ratio{39, 2}));
        const tap::dsp::chain<dq_t, lq_t> bal_q15(dq_t{profile::balanced()},
                                                  lq_t{guard_band_settings::balanced(half_band_kind::fir)});
        EXPECT_EQ(bal_q15.latency_output_frames(),
                  (bal_q15.stage<0>().latency_output_frames() + tap::dsp::exact_ratio{59, 2}));
        tap::dsp::chain<li_t, basic_stage<double, 1, up2>> up(li_t{}, basic_stage<double, 1, up2>{});
        EXPECT_EQ(up.latency_output_frames(), (tap::dsp::exact_ratio{68, 1}));
        std::vector<double> x(256, 0.0);
        x[0]         = 1.0;
        const auto h = run(up, x);
        for (const double t : {0.0, 0.25, 0.5, 0.9}) {
            const double f = t * (3.0 / 8.0) / 2.0; // the passband, cycles per output frame
            EXPECT_NEAR(measured_group_delay(h, f), 68.0, 1e-6) << t;
        }
    }

    TEST_F(iir_composition, TheNominalLatencyComposesOverMixedChains) {
        // A chain holding the Chebyshev II has no exact latency
        // (compile_fail/guard_band_exact_latency.cpp); its nominal composes:
        // before the nonlinear interpolator, twice the filter's (it runs at
        // the input rate) plus the stage's, and the chain's group delay at
        // every passband frequency matches its impulse response's.
        using nl_t       = guard_band_filter<double, 1, k_chebyshev>;
        using up_stage_t = basic_iir_stage<double, 1, up2, k_nonlinear>;
        const nl_t                        g;
        const up_stage_t                  u;
        tap::dsp::chain<nl_t, up_stage_t> up(nl_t{}, up_stage_t{});
        EXPECT_NEAR(up.nominal_latency_output_frames(),
                    2.0 * g.nominal_latency_output_frames() + u.nominal_latency_output_frames(), 1e-12);
        EXPECT_NEAR(up.latency_deviation_output_frames(),
                    2.0 * g.latency_deviation_output_frames() + u.latency_deviation_output_frames(), 1e-12);
        std::vector<double> x(4096, 0.0);
        x[0]         = 1.0;
        const auto h = run(up, x);
        for (const double t : {0.0, 0.3, 0.6, 0.9, 1.0}) {
            const double f = t * (3.0 / 8.0) / 2.0;
            EXPECT_NEAR(up.group_delay_output_frames(f), measured_group_delay(h, f), 1e-8) << t;
            EXPECT_NEAR(up.group_delay_output_frames(f),
                        2.0 * g.group_delay_output_frames(2.0 * f) + u.group_delay_output_frames(f), 1e-12);
        }
        // After the FIR decimator: its exact latency plus the filter's nominal.
        const tap::dsp::chain<basic_stage<double, 1, down2>, nl_t> down(basic_stage<double, 1, down2>{}, nl_t{});
        EXPECT_NEAR(down.nominal_latency_output_frames(), 19.0 / 2.0 + g.nominal_latency_output_frames(), 1e-12);
        EXPECT_NEAR(down.latency_deviation_output_frames(), g.latency_deviation_output_frames(), 1e-12);
    }

    // ------------------------------------------------------------------
    // The offering table (I10)
    // ------------------------------------------------------------------

    /// One row: multiplies per output frame per channel, the nominal latency
    /// and its deviation in output frames.
    struct offering {
        double macs;
        double nominal;
        double deviation;
    };

    template <iir_phase V, bool Down, int Guard, typename S = double>
    offering offered(const profile& p, const guard_band_settings& s) {
        using stage_t = basic_iir_stage<S, 1, std::conditional_t<Down, down2, up2>, V>;
        const stage_t st(p);
        if constexpr (Guard < 0) {
            return {st.macs_per_output(), st.nominal_latency_output_frames(), st.latency_deviation_output_frames()};
        }
        else {
            constexpr guard_phase G = Guard == 0 ? k_chebyshev : k_fir_form;
            using guard_t           = guard_band_filter<S, 1, G>;
            const guard_t g(s);
            if constexpr (Down) {
                const tap::dsp::chain<stage_t, guard_t> c(stage_t{p}, guard_t{s});
                return {st.macs_per_output() + g.macs_per_output(), c.nominal_latency_output_frames(),
                        c.latency_deviation_output_frames()};
            }
            else {
                const tap::dsp::chain<guard_t, stage_t> c(guard_t{s}, stage_t{p});
                return {st.macs_per_output() + g.macs_per_output() / 2.0, c.nominal_latency_output_frames(),
                        c.latency_deviation_output_frames()};
            }
        }
    }

    TEST_F(iir_composition, TheOfferingTableIsMeasured) {
        // I10's table: by 2, down and up, both recursive variants, every
        // profile, with no guard band filter, the Chebyshev II form or the
        // linear form: multiplies per output frame per channel (the stage's,
        // and the filter's at the low rate, over 2 going up) and the chain's
        // nominal latency in output frames with its passband deviation. Pinned
        // here; README.md and the book quote it.
        const std::array<profile, 4> profiles = {profile::super_economy(), profile::economy(), profile::balanced(),
                                                 profile::transparent()};
        // The named settings behind each variant's half-band (question 20).
        const auto setting = [](std::size_t i, half_band_kind kind) {
            return i == 0   ? guard_band_settings::super_economy(kind)
                   : i == 1 ? guard_band_settings::economy(kind)
                   : i == 2 ? guard_band_settings::balanced(kind)
                            : guard_band_settings::transparent(kind);
        };
        const std::array<const char*, 4> names = {"super_economy", "economy", "balanced", "transparent"};
        // [direction][variant][guard][profile]: down / up, nonlinear /
        // near-linear, none / Chebyshev II / linear.
        std::array<std::array<std::array<std::array<offering, 4>, 3>, 2>, 2> m{};
        for (std::size_t i = 0; i < 4; ++i) {
            const profile&            p  = profiles[i];
            const guard_band_settings sn = setting(i, half_band_kind::nonlinear);
            const guard_band_settings sl = setting(i, half_band_kind::near_linear);
            m[0][0][0][i]                = offered<k_nonlinear, true, -1>(p, sn);
            m[0][0][1][i]                = offered<k_nonlinear, true, 0>(p, sn);
            m[0][0][2][i]                = offered<k_nonlinear, true, 1>(p, sn);
            m[0][1][0][i]                = offered<k_near_linear, true, -1>(p, sl);
            m[0][1][1][i]                = offered<k_near_linear, true, 0>(p, sl);
            m[0][1][2][i]                = offered<k_near_linear, true, 1>(p, sl);
            m[1][0][0][i]                = offered<k_nonlinear, false, -1>(p, sn);
            m[1][0][1][i]                = offered<k_nonlinear, false, 0>(p, sn);
            m[1][0][2][i]                = offered<k_nonlinear, false, 1>(p, sn);
            m[1][1][0][i]                = offered<k_near_linear, false, -1>(p, sl);
            m[1][1][1][i]                = offered<k_near_linear, false, 0>(p, sl);
            m[1][1][2][i]                = offered<k_near_linear, false, 1>(p, sl);
        }
        const std::array<const char*, 2> dirs     = {"down 2", "up 2"};
        const std::array<const char*, 2> variants = {"nonlinear", "near-linear"};
        const std::array<const char*, 3> guards   = {"no guard band filter", "+ Chebyshev II", "+ linear form"};
        for (std::size_t d = 0; d < 2; ++d) {
            for (std::size_t v = 0; v < 2; ++v) {
                for (std::size_t g = 0; g < 3; ++g) {
                    std::printf("[ measured ] %s %s %s:", dirs[d], variants[v], guards[g]);
                    for (std::size_t i = 0; i < 4; ++i) {
                        const offering& o = m[d][v][g][i];
                        std::printf(" %s %.1f MACs, %.4f (+%.4f) frames;", names[i], o.macs, o.nominal, o.deviation);
                    }
                    std::printf("\n");
                }
            }
        }
        // The pins, a row per line printed above ((down, up) x (nonlinear,
        // near-linear) x (none, Chebyshev II, linear form)) and a column per
        // profile: multiplies per output frame per channel, exact, and the
        // nominal latency and its deviation in output frames, to 0.0005. The
        // near-linear rows with a filter run the FIR and near-linear setting,
        // order 7 at super_economy and 11 at transparent (question 20).
        constexpr std::array<std::array<double, 4>, 12> k_macs      = {{
            {4, 4, 4, 8},
            {19, 24, 24, 28},
            {34, 54, 62, 80},
            {5, 7, 8, 19},
            {25, 27, 28, 49},
            {43, 57, 66, 121},
            {2, 2, 2, 4},
            {9.5, 12, 12, 14},
            {17, 27, 31, 40},
            {2.5, 3.5, 4, 9.5},
            {12.5, 13.5, 14, 24.5},
            {21.5, 28.5, 33, 60.5},
        }};
        constexpr std::array<std::array<double, 4>, 12> k_nominal   = {{
            {1.2666, 1.1996, 1.1568, 2.0824},
            {1.7558, 1.7349, 1.5954, 2.4639},
            {15.7666, 25.6996, 29.6568, 37.5824},
            {5, 7, 8, 19},
            {5.7461, 7.5353, 8.4387, 19.6370},
            {23.5, 31.5, 36.5, 69.5},
            {2.5332, 2.3993, 2.3136, 4.1649},
            {3.5117, 3.4698, 3.1909, 4.9278},
            {31.5332, 51.3993, 59.3136, 75.1649},
            {10, 14, 16, 38},
            {11.4921, 15.0705, 16.8773, 39.2740},
            {47, 63, 73, 139},
        }};
        constexpr std::array<std::array<double, 4>, 12> k_deviation = {{
            {0.8406, 1.2803, 1.6133, 3.8764},
            {2.6843, 5.7880, 7.0324, 10.6321},
            {0.8406, 1.2803, 1.6133, 3.8764},
            {0.0078, 0.0087, 0.0134, 0.0002},
            {3.3794, 4.5165, 5.4325, 14.0711},
            {0.0078, 0.0087, 0.0134, 0.0002},
            {1.6813, 2.5605, 3.2266, 7.7528},
            {5.3686, 11.5761, 14.0648, 21.2643},
            {1.6813, 2.5605, 3.2266, 7.7528},
            {0.0157, 0.0175, 0.0268, 0.0005},
            {6.7588, 9.0330, 10.8650, 28.1421},
            {0.0157, 0.0175, 0.0268, 0.0005},
        }};
        for (std::size_t d = 0; d < 2; ++d) {
            for (std::size_t v = 0; v < 2; ++v) {
                for (std::size_t g = 0; g < 3; ++g) {
                    const std::size_t row = 6 * d + 3 * v + g;
                    for (std::size_t i = 0; i < 4; ++i) {
                        const offering& o = m[d][v][g][i];
                        EXPECT_EQ(o.macs, k_macs[row][i]) << dirs[d] << " " << variants[v] << " " << guards[g];
                        EXPECT_NEAR(o.nominal, k_nominal[row][i], 0.0005) << dirs[d] << " " << variants[v];
                        EXPECT_NEAR(o.deviation, k_deviation[row][i], 0.0005) << dirs[d] << " " << variants[v];
                    }
                }
            }
        }
        // The linear form adds an exact delay: with the near-linear variant the
        // nominal is the delay path plus (N - 1) / 2, exactly.
        EXPECT_EQ(m[0][1][2][1].nominal, 7.0 + 24.5);
        EXPECT_EQ(m[1][1][2][1].nominal, 14.0 + 49.0);

        // In Q15 the linear form runs DspTap's Q15 rows at the 70 dB
        // profiles, two taps longer at super_economy and balanced (32 and 40,
        // 60, for 30 and 38, 58; economy's 50 and transparent's double rows
        // as above): its rows where they differ, (down, up) x (nonlinear,
        // near-linear), every profile, and its deviation the double table's.
        std::array<std::array<std::array<offering, 4>, 2>, 2> q{};
        for (std::size_t i = 0; i < 4; ++i) {
            const profile&            p  = profiles[i];
            const guard_band_settings sn = setting(i, half_band_kind::nonlinear);
            const guard_band_settings sl = setting(i, half_band_kind::near_linear);
            q[0][0][i]                   = offered<k_nonlinear, true, 1, std::int16_t>(p, sn);
            q[0][1][i]                   = offered<k_near_linear, true, 1, std::int16_t>(p, sl);
            q[1][0][i]                   = offered<k_nonlinear, false, 1, std::int16_t>(p, sn);
            q[1][1][i]                   = offered<k_near_linear, false, 1, std::int16_t>(p, sl);
        }
        constexpr std::array<std::array<double, 4>, 4> k_macs_q15    = {{
            {36, 54, 64, 80},
            {45, 57, 68, 121},
            {18, 27, 32, 40},
            {22.5, 28.5, 34, 60.5},
        }};
        constexpr std::array<std::array<double, 4>, 4> k_nominal_q15 = {{
            {16.7666, 25.6996, 30.6568, 37.5824},
            {24.5, 31.5, 37.5, 69.5},
            {33.5332, 51.3993, 61.3136, 75.1649},
            {49, 63, 75, 139},
        }};
        for (std::size_t d = 0; d < 2; ++d) {
            for (std::size_t v = 0; v < 2; ++v) {
                std::printf("[ measured ] Q15 %s %s + linear form:", dirs[d], variants[v]);
                for (std::size_t i = 0; i < 4; ++i) {
                    const offering& o = q[d][v][i];
                    std::printf(" %s %.1f MACs, %.4f (+%.4f) frames;", names[i], o.macs, o.nominal, o.deviation);
                    EXPECT_EQ(o.macs, k_macs_q15[2 * d + v][i]) << "Q15 " << dirs[d] << " " << variants[v];
                    EXPECT_NEAR(o.nominal, k_nominal_q15[2 * d + v][i], 0.0005)
                        << "Q15 " << dirs[d] << " " << variants[v];
                    EXPECT_NEAR(o.deviation, m[d][v][2][i].deviation, 1e-12) << "Q15 " << dirs[d] << " " << variants[v];
                }
                std::printf("\n");
            }
        }
    }

} // namespace
