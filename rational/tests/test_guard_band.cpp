// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The guard band filter's battery (IIR-c, docs/IIR_PLAN.md I7 and section
// 4), typed over the formats where the promise is per format:
//   - the golden leg: the six named designs (question 20: a setting's order
//     follows the half-band it serves) in both forms against scipy
//     (tests/reference/guard_band_reference_vectors.h, tools/reference/
//     make_guard_band_reference_vectors.py: the Chebyshev II as scipy's own
//     design through sosfilt, the type II FIR's committed taps through
//     lfilter), within a tolerance per format, and the Q15 / Q31 outputs
//     bit-pinned (FNV-1a-64);
//   - the spec per form, measured from the shipping filter: the zero at
//     Nyquist (exact in every format's table), the stopband from the stated
//     edge at A, the passband flat within 0.01 dB to p; the order rule (I7,
//     questions 14 and 20) per half-band kind; the linear form's symmetry,
//     bit for bit;
//   - the machine: chunking, pull against process, a dry source, reset, the
//     1:1 accounting, channels in the type (D18), the count-independent base
//     (K12), validate() against the constructor (D17), the run-time designs;
//   - latency (I5): the Chebyshev's nominal and group delay against its
//     impulse response and the golden leg's, the linear form's exact
//     (N - 1) / 2;
//   - flush (I6): FlushEqualsZeroPaddingBitForBit, an exactly zero state
//     after the window, and the honest limit (question 19): the floating
//     Chebyshev II of order 7 or more settles in a limit cycle below 16 times
//     the smallest normal;
//   - fixed point: DC, full-scale drive with no internal wrap and saturation
//     at the output, the decay to an exactly zero state, the Q15 floors and
//     the quantized linear tables' attained stopbands;
//   - the honest limits: a non-finite input poisons the Chebyshev until
//     reset() and clears from the linear form after its window; the
//     floating-point environment never touched, and the debug-build
//     precondition (the nonlinear form's only).
// The decimating chains' aliases
// (guard_band.AliasesLandOnlyAboveThePassbandEdge) are here too; the chains
// themselves are test_iir_composition.cpp's.
// Every test runs under the host's flush-to-zero (the fixture), as I9 asks of
// a recursive test; the QEMU legs run the battery but for the host
// measurements bare_metal_main.cpp and ci.yml name.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "reference/guard_band_reference_vectors.h"
#include "support/flush_to_zero.h"
#include "support/iir_signals.h"
#include "tap/dsp/analysis/sine_analysis.h"
#include "tap/dsp/chain.h"
#include "tap/sr/rational/rational.h"
#include "tap_sr_test/expect_rejected.h"

namespace {

    using namespace tap::sr::rational;       // NOLINT(google-build-using-namespace)
    using namespace tap::sr::rational::test; // NOLINT(google-build-using-namespace)

    constexpr guard_phase k_nonlinear = guard_phase::nonlinear;
    constexpr guard_phase k_linear    = guard_phase::linear;

    constexpr half_band_kind k_fir_kind         = half_band_kind::fir;
    constexpr half_band_kind k_nonlinear_kind   = half_band_kind::nonlinear;
    constexpr half_band_kind k_near_linear_kind = half_band_kind::near_linear;

    /// A profile's named setting behind a half-band kind (profile order:
    /// super_economy, economy, balanced, transparent).
    guard_band_settings named(int profile_index, half_band_kind kind) {
        switch (profile_index) {
        case 0:
            return guard_band_settings::super_economy(kind);
        case 1:
            return guard_band_settings::economy(kind);
        case 2:
            return guard_band_settings::balanced(kind);
        default:
            return guard_band_settings::transparent(kind);
        }
    }
    constexpr std::array<const char*, 4> k_profile_names = {"super_economy", "economy", "balanced", "transparent"};

    /// The six distinct named designs (docs/IIR_PLAN.md question 20: the
    /// order is the half-band's): super_economy and transparent behind the
    /// nonlinear half-band and behind the FIR and near-linear ones, economy and
    /// balanced behind every kind. Every test over the named settings runs
    /// these six.
    struct named_design {
        half_band_kind kind;
        int            profile_index;
        const char*    name;
    };
    constexpr std::size_t                         k_designs = 6;
    constexpr std::array<named_design, k_designs> k_named   = {{
        {half_band_kind::nonlinear, 0, "super_economy (nonlinear)"},
        {half_band_kind::fir, 0, "super_economy (FIR, near-linear)"},
        {half_band_kind::fir, 1, "economy"},
        {half_band_kind::fir, 2, "balanced"},
        {half_band_kind::nonlinear, 3, "transparent (nonlinear)"},
        {half_band_kind::fir, 3, "transparent (FIR, near-linear)"},
    }};
    guard_band_settings                           named(int design) {
        const named_design& d = k_named[static_cast<std::size_t>(design)];
        return named(d.profile_index, d.kind);
    }
    const char* design_name(int design) {
        return k_named[static_cast<std::size_t>(design)].name;
    }

    // ------------------------------------------------------------------
    // The honest limit's numbers (the file header of guard_band.h)
    // ------------------------------------------------------------------

    /// The bound pinned for the floating Chebyshev II's zero-input limit
    /// cycle under flush-to-zero: sixteen times the smallest normal
    /// (TheChebyshevLimitCycleIsBounded measures up to 8.7 in float and 10.4
    /// in double at the named order 7 and 8.4 and 7.7 at transparent's order
    /// 11, over 20 starts per design; DspTap's IIR-a up to 13.1 in its own
    /// starts).
    template <typename S>
    constexpr double k_cycle_bound =
        std::is_floating_point_v<S> ? 16.0 * static_cast<double>(std::numeric_limits<S>::min()) : 0.0;

    /// Where no control bit governs a floating S (double on the M33, float
    /// and double on Hexagon: tap/dsp/subnormals.h), the fixture's
    /// flush-to-zero reaches nothing and the state sticks at the subnormal
    /// residue instead (test_iir_subnormals.cpp pins it): a few times that,
    /// through a chain's stages.
    template <typename S>
    constexpr bool k_no_flush_control = std::is_floating_point_v<S> && !tap::dsp::k_subnormal_control<S>;
    template <typename S>
    constexpr double k_subnormal_residue =
        std::is_floating_point_v<S> ? 256.0 * static_cast<double>(std::numeric_limits<S>::denorm_min()) : 0.0;

    /// What a filter leaves after its flush window here: 0 (an exactly zero
    /// state), the limit cycle's bound (the floating Chebyshev II of order 7
    /// or more), or the subnormal residue.
    template <typename S, guard_phase P>
    double flush_residue(const guard_band_settings& s) {
        if constexpr (P == k_linear || !std::is_floating_point_v<S>) {
            return 0.0;
        }
        else if constexpr (k_no_flush_control<S>) {
            return std::max(k_subnormal_residue<S>, s.order >= 7 ? k_cycle_bound<S> : 0.0);
        }
        else {
            return s.order >= 7 ? k_cycle_bound<S> : 0.0;
        }
    }

    // ------------------------------------------------------------------
    // The fixtures
    // ------------------------------------------------------------------

    template <typename S>
    class guard_band_test : public flush_to_zero_fixture {};
    using sample_types = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(guard_band_test, sample_types, );

    template <typename S>
    class guard_band_fixed_test : public flush_to_zero_fixture {};
    using fixed_types = ::testing::Types<std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(guard_band_fixed_test, fixed_types, );

    template <typename S>
    class guard_band_float_test : public flush_to_zero_fixture {};
    using floating_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(guard_band_float_test, floating_types, );

    class guard_band : public flush_to_zero_fixture {};

    /// Both forms over S, Ch.
    template <typename S, std::size_t Ch, typename F>
    void for_each_form(F&& f) {
        f.template operator()<guard_band_filter<S, Ch, k_nonlinear>>();
        f.template operator()<guard_band_filter<S, Ch, k_linear>>();
    }

    /// The impulse response of a double filter at s, `n` samples.
    template <guard_phase P>
    std::vector<double> impulse_response(const guard_band_settings& s, std::size_t n = 2048) {
        guard_band_filter<double, 1, P> g(s);
        std::vector<double>             x(n, 0.0);
        x[0] = 1.0;
        return run(g, x);
    }

    // ------------------------------------------------------------------
    // The golden leg
    // ------------------------------------------------------------------

    template <typename S, guard_phase P>
    std::vector<S> golden_run(const rational_guard_ref::guard_case& c) {
        guard_band_filter<S, 1, P> g(named(c.setting));
        EXPECT_EQ(g.order(), c.order);
        EXPECT_NEAR(g.stop_edge(), c.stop_edge, 1e-12) << "the stop edge is the golden leg's";
        if constexpr (P == k_linear) {
            EXPECT_EQ(static_cast<int>(g.taps()), c.taps);
        }
        std::vector<S> in(rational_guard_ref::k_input.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = from_q15<S>(rational_guard_ref::k_input[i]);
        }
        return run(g, in);
    }

    template <typename S>
    std::vector<S> golden_dispatch(const rational_guard_ref::guard_case& c) {
        return c.form == 0 ? golden_run<S, k_nonlinear>(c) : golden_run<S, k_linear>(c);
    }

    /// The tolerance per format and form against the golden leg, in units of
    /// full scale (the input peaks at 0.9), and what it covers:
    ///   - double: the two structures' rounding (scipy's sections in its own
    ///     pairing and order against DspTap's; the FIR's tap order);
    ///   - float: float coefficients, states and products (the Chebyshev,
    ///     I14) or float taps accumulated in double (the FIR);
    ///   - Q15: the Chebyshev's output rounding and section truncations, the
    ///     FIR's Q1.14 taps (each up to half an LSB of 2^-14 off the design);
    ///   - Q31: the same at Q2.30 / Q1.30.
    /// The measured worst of each is printed and stated in PLAN.md section 6.
    template <typename S>
    double golden_tolerance(int form) {
        if constexpr (std::is_same_v<S, double>) {
            return 1e-12;
        }
        else if constexpr (std::is_same_v<S, float>) {
            return 2e-6;
        }
        else if constexpr (std::is_same_v<S, std::int16_t>) {
            return form == 0 ? 2.0 / 32768.0 : 6e-4;
        }
        else {
            return form == 0 ? 1e-8 : 1e-8;
        }
    }

    TYPED_TEST(guard_band_test, MatchesTheGoldenLeg) {
        using sample = TypeParam;
        std::array<double, 2> worst{};
        for (const auto& c : rational_guard_ref::k_cases) {
            const std::vector<sample> y = golden_dispatch<sample>(c);
            ASSERT_EQ(static_cast<int>(y.size()), c.frames);
            // The fixed-point outputs saturate at full scale; the golden leg
            // is clamped the same way, never the floating formats'.
            const double lsb = 1.0 / k_full<sample>;
            const double top = std::is_floating_point_v<sample> ? 1e300 : 1.0 - lsb;
            double       err = 0.0;
            for (std::size_t i = 0; i < y.size(); ++i) {
                err = std::max(err, std::fabs(to_unit(y[i]) - std::clamp(c.expected[i], -top - lsb, top)));
            }
            EXPECT_LE(err, golden_tolerance<sample>(c.form))
                << (c.form == 0 ? "nonlinear " : "linear ") << design_name(c.setting);
            worst[static_cast<std::size_t>(c.form)] = std::max(worst[static_cast<std::size_t>(c.form)], err);
        }
        std::printf("[ measured ] %zu-byte samples against the golden leg: %.3g (Chebyshev II) and %.3g (type II FIR) "
                    "of full scale at worst\n",
                    sizeof(sample), worst[0], worst[1]);
    }

    // The Q15 and Q31 outputs, bit for bit: FNV-1a-64 over every golden
    // case's output, per form (six designs each). A change that moves a pin
    // is a numeric change to the fixed-point datapath.
    TYPED_TEST(guard_band_fixed_test, OutputsAreBitPinned) {
        using sample                               = TypeParam;
        std::array<std::uint64_t, 2>           h   = {k_fnv_basis, k_fnv_basis};
        constexpr std::array<std::uint64_t, 2> q15 = {0x301dd766a0e0b594ull, 0x5dae9a6656e5a72eull};
        constexpr std::array<std::uint64_t, 2> q31 = {0x1c2769d7466eb346ull, 0x0db8537ebb0bb20cull};
        for (const auto& c : rational_guard_ref::k_cases) {
            auto& slot = h[static_cast<std::size_t>(c.form)];
            slot       = fnv1a64(slot, golden_dispatch<sample>(c));
        }
        const auto& pins = std::is_same_v<sample, std::int16_t> ? q15 : q31;
        for (std::size_t i = 0; i < 2; ++i) {
            std::printf("[ measured ] %s %s: 0x%016llxull\n", std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31",
                        i == 0 ? "Chebyshev II" : "type II FIR", static_cast<unsigned long long>(h[i]));
            EXPECT_EQ(h[i], pins[i]) << i;
        }
    }

    // ------------------------------------------------------------------
    // The spec, per form
    // ------------------------------------------------------------------

    /// The worst combined alias in the guard band: a tone at the higher rate
    /// that a half-band (its response `hb` at the higher rate) aliases onto f
    /// in [p, 1/2] of the low rate, then the Chebyshev II, in dB, on 1001
    /// points (IIR-0's rule, rational/tools/spikes/iir/python/tables.py).
    template <typename Hb>
    double worst_guard_band_alias(Hb hb, const tap::dsp::cheby2_design& g, double p) {
        double worst = 0.0;
        for (int k = 0; k <= 1000; ++k) {
            const double f_low = p + (0.5 - p) * static_cast<double>(k) / 1000.0;
            worst              = std::max(worst, hb(0.5 - f_low / 2.0) * std::abs(tap::dsp::cheby2_response(g, f_low)));
        }
        return db(worst);
    }

    /// The worst guard band alias at orders 3 to 11 behind the nonlinear,
    /// the near-linear and the FIR half-band at the spec point's divisor-1
    /// design, and the order the rule picks behind each (0: none up to 11).
    struct rule_row {
        std::array<std::array<double, 5>, 3> worst{};
        std::array<int, 3>                   pick{};
    };
    template <int A, int Num, int Den>
    rule_row order_by_the_rule(const profile& fir_profile) {
        const double                                 p = static_cast<double>(Num) / static_cast<double>(Den);
        const std::array<tap::dsp::cheby2_design, 5> g = {
            tap::dsp::k_guard_band_cheby2<A, Num, Den, 3>, tap::dsp::k_guard_band_cheby2<A, Num, Den, 5>,
            tap::dsp::k_guard_band_cheby2<A, Num, Den, 7>, tap::dsp::k_guard_band_cheby2<A, Num, Den, 9>,
            tap::dsp::k_guard_band_cheby2<A, Num, Den, 11>};
        // The FIR half-band's Nyquist design (DC gain 2: each branch sums to 1).
        // The recursive designs are named in the lambda, not captured: GCC
        // 15.3 fails internally (mark_use) on a captured reference to them.
        const std::vector<double> fir = design_stage<down2>(fir_profile);
        const auto                hb  = [&](std::size_t kind, double f_hi) {
            switch (kind) {
            case 0:
                return std::abs(tap::dsp::halfband_response(tap::dsp::k_nonlinear_halfband<A, Num, Den, 1>, f_hi));
            case 1:
                return std::abs(tap::dsp::halfband_response(tap::dsp::k_near_linear_halfband<A, Num, Den, 1>, f_hi));
            default:
                return std::abs(dtft(fir, f_hi)) / 2.0;
            }
        };
        rule_row r;
        for (std::size_t kind = 0; kind < 3; ++kind) {
            for (std::size_t i = 0; i < g.size(); ++i) {
                r.worst[kind][i] = worst_guard_band_alias([&](double f_hi) { return hb(kind, f_hi); }, g[i], p);
                if (r.pick[kind] == 0 && r.worst[kind][i] <= -static_cast<double>(A) / 2.0) {
                    r.pick[kind] = g[i].order;
                }
            }
        }
        return r;
    }

    TEST_F(guard_band, OrderIsTheLowestOddOrderWhoseGuardBandAliasIsAtHalfA) {
        // I7's rule (the maintainer's decisions, questions 14 and 20): the
        // lowest odd order whose worst alias in the guard band, behind the
        // half-band the filter serves at its own design, is at or below -A/2.
        // Behind the nonlinear half-band it gives 5 / 7 / 7 / 7; behind the
        // near-linear and the FIR half-band, whose transitions leave more in
        // the guard band, 7 / 7 / 7 / 11. Each kind's named setting is its
        // pick, and the worst alias at it is pinned as measured.
        const std::array<rule_row, 4> rows = {
            order_by_the_rule<70, 1, 3>(profile::super_economy()), order_by_the_rule<70, 3, 8>(profile::economy()),
            order_by_the_rule<70, 19, 48>(profile::balanced()), order_by_the_rule<120, 5, 12>(profile::transparent())};
        // rule_row's kinds, in its order, and the half_band_kind each names.
        const std::array<const char*, 3>    kinds      = {"nonlinear", "near-linear", "FIR"};
        const std::array<half_band_kind, 3> kind_value = {k_nonlinear_kind, k_near_linear_kind, k_fir_kind};
        // The worst alias at each kind's named order, dB, per profile.
        constexpr std::array<std::array<double, 4>, 3> k_at_named = {
            {{-40.11, -43.47, -40.23, -61.59}, {-39.56, -39.17, -37.77, -64.63}, {-40.88, -36.96, -36.95, -62.14}}};
        for (std::size_t i = 0; i < 4; ++i) {
            const double a = named(static_cast<int>(i), k_fir_kind).stopband_atten_db;
            for (std::size_t kind = 0; kind < 3; ++kind) {
                const auto& w           = rows[i].worst[kind];
                const int   named_order = named(static_cast<int>(i), kind_value[kind]).order;
                const auto  at          = static_cast<std::size_t>((named_order - 3) / 2);
                std::printf("[ measured ] %s behind the %s half-band: the worst guard band alias at orders 3 / 5 / 7 "
                            "/ 9 / 11: %.2f / %.2f / %.2f / %.2f / %.2f dB; the rule picks %d\n",
                            k_profile_names[i], kinds[kind], w[0], w[1], w[2], w[3], w[4], rows[i].pick[kind]);
                EXPECT_EQ(rows[i].pick[kind], named_order) << k_profile_names[i] << " " << kinds[kind];
                EXPECT_LE(w[at], -a / 2.0) << k_profile_names[i] << " " << kinds[kind];
                EXPECT_NEAR(w[at], k_at_named[kind][i], 0.05) << k_profile_names[i] << " " << kinds[kind];
            }
        }
    }

    /// The worst stopband from the stop edge, the passband's extremes to p
    /// and the response at Nyquist of a sequence, on 2048 points per band.
    struct band_measure {
        double stopband_db = 0.0;
        double pass_max_db = -400.0;
        double pass_min_db = 400.0;
        double nyquist_db  = 0.0;
    };
    band_measure measure_bands(const std::vector<double>& h, double p, double stop_edge) {
        band_measure m;
        double       worst = 0.0;
        for (int k = 0; k <= 2048; ++k) {
            const double t = static_cast<double>(k) / 2048.0;
            worst          = std::max(worst, std::abs(dtft(h, stop_edge + (0.5 - stop_edge) * t)));
            const double v = db(std::abs(dtft(h, p * t)));
            m.pass_max_db  = std::max(m.pass_max_db, v);
            m.pass_min_db  = std::min(m.pass_min_db, v);
        }
        m.stopband_db = db(worst);
        m.nyquist_db  = db(std::abs(dtft(h, 0.5)));
        return m;
    }

    TEST_F(guard_band, EachFormMeetsItsSpec) {
        // From the shipping double filter's impulse response, at every named
        // setting: the stopband from the stated edge at -A dB or below, the
        // passband within 0.01 dB to p (the Chebyshev II monotone from 0 dB
        // down to -0.01 dB at p, the FIR 0.01 dB peak to peak), and the zero
        // at Nyquist.
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_settings                       s = named(si);
            const double                                    a = s.stopband_atten_db;
            const guard_band_filter<double, 1, k_nonlinear> nl(s);
            const guard_band_filter<double, 1, k_linear>    li(s);
            const auto mn = measure_bands(impulse_response<k_nonlinear>(s), s.passband_frac, nl.stop_edge());
            const auto ml = measure_bands(impulse_response<k_linear>(s, li.taps()), s.passband_frac, li.stop_edge());
            std::printf("[ measured ] %s: Chebyshev II stopband %.3f dB, passband %.5f .. %.5f dB, Nyquist %.0f dB; "
                        "type II FIR (N = %zu) stopband %.2f dB, passband %.5f dB peak to peak, Nyquist %.0f dB\n",
                        design_name(si), mn.stopband_db, mn.pass_min_db, mn.pass_max_db, mn.nyquist_db, li.taps(),
                        ml.stopband_db, ml.pass_max_db - ml.pass_min_db, ml.nyquist_db);
            EXPECT_LE(mn.stopband_db, -a + 1e-3) << "equiripple at -A from the stop edge";
            EXPECT_LE(mn.pass_max_db, 1e-9);
            EXPECT_GE(mn.pass_min_db, -0.01 - 1e-6);
            EXPECT_LE(mn.nyquist_db, -200.0);
            EXPECT_LE(ml.stopband_db, -a);
            EXPECT_LE(ml.pass_max_db - ml.pass_min_db, 0.01);
            EXPECT_LE(ml.nyquist_db, -200.0);
            EXPECT_NEAR(nl.stop_edge(), li.stop_edge(), 1e-15) << "both forms reach A from the Chebyshev's stop edge";
        }
    }

    TYPED_TEST(guard_band_test, TheZeroAtNyquistIsExactInEveryFormat) {
        // In the table: the Chebyshev's first-order section's numerator is
        // b0 (1 + z^-1) in every format, b0 == b1 bit for bit; the FIR's taps
        // are symmetric, so their alternating sum is exactly zero. Through the
        // filter: a 0.9 full-scale Nyquist tone leaves, past the window, an
        // exact zero in fixed point (the FIR's products cancel in the exact
        // accumulator; the Chebyshev's first-order section, the smallest pole
        // radius and so the first, cancels its numerator exactly in direct
        // form I and the rest drain) and a floating residue at the format's
        // rounding.
        using sample = TypeParam;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_settings                 s = named(si);
            guard_band_filter<sample, 1, k_nonlinear> nl(s);
            guard_band_filter<sample, 1, k_linear>    li(s);
            int                                       first_order = 0;
            for (std::size_t i = 0; i < nl.table().size; ++i) {
                const auto& c = nl.table().sections[i];
                if (c[2] == 0 && c[4] == 0) {
                    ++first_order;
                    EXPECT_EQ(c[0], c[1]) << "b0 == b1: the zero at z = -1";
                    EXPECT_EQ(i, 0u) << "the first-order section has the smallest pole radius";
                }
            }
            EXPECT_EQ(first_order, 1);
            // The alternating sum, taken a mirrored pair at a time: tap k and
            // tap N - 1 - k enter with opposite signs (N - 1 is odd), so each
            // pair is exactly zero in any arithmetic when the taps are equal.
            double     alternating = 0.0;
            const auto q           = li.coefficients();
            for (std::size_t k = 0; k < q.size() / 2; ++k) {
                const double pair = static_cast<double>(q[k]) - static_cast<double>(q[q.size() - 1 - k]);
                alternating += (k % 2 == 0 ? 1.0 : -1.0) * pair;
            }
            EXPECT_EQ(alternating, 0.0);
            std::vector<sample> x(4000);
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = from_unit<sample>(i % 2 == 0 ? 0.9 : -0.9);
            }
            const auto   yn  = run(nl, x);
            const auto   yl  = run(li, x);
            const double tol = std::is_same_v<sample, float> ? 1e-6 : 1e-14;
            double       wn  = 0.0;
            double       wl  = 0.0;
            for (std::size_t i = 2000; i < x.size(); ++i) {
                wn = std::max(wn, std::fabs(to_unit(yn[i])));
                wl = std::max(wl, std::fabs(to_unit(yl[i])));
            }
            if constexpr (std::is_floating_point_v<sample>) {
                EXPECT_LE(wn, tol) << design_name(si);
                EXPECT_LE(wl, tol) << design_name(si);
            }
            else {
                EXPECT_EQ(wn, 0.0) << design_name(si);
                EXPECT_EQ(wl, 0.0) << design_name(si);
            }
        }
    }

    TYPED_TEST(guard_band_test, TheLinearFormIsSymmetricBitForBit) {
        // The design and the format's taps are symmetric bit for bit (the
        // linear phase and the zero at Nyquist), and in fixed point the
        // quantized taps sum to the design's sum within one coefficient LSB
        // (the half quantized with its sum preserved, then mirrored).
        using sample = TypeParam;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_filter<sample, 1, k_linear> li(named(si));
            const auto                                   h = li.design_taps();
            const auto                                   q = li.coefficients();
            ASSERT_EQ(h.size(), q.size());
            ASSERT_EQ(h.size() % 2, 0u) << "type II: even length";
            double design_sum = 0.0;
            double table_sum  = 0.0;
            for (std::size_t k = 0; k < h.size(); ++k) {
                EXPECT_EQ(h[k], h[h.size() - 1 - k]);
                EXPECT_EQ(q[k], q[q.size() - 1 - k]);
                design_sum += h[k];
                table_sum += static_cast<double>(q[k]);
            }
            if constexpr (!std::is_floating_point_v<sample>) {
                EXPECT_LE(std::fabs(table_sum - design_sum * tap::dsp::sample_traits<sample>::k_coeff_scale), 1.0)
                    << design_name(si);
            }
        }
    }

    // ------------------------------------------------------------------
    // The machine
    // ------------------------------------------------------------------

    TYPED_TEST(guard_band_test, ChunkingIsBitIdentical) {
        using sample = TypeParam;
        for_each_form<sample, 2>([]<typename Filter>() {
            for (int si = 0; si < static_cast<int>(k_designs); ++si) {
                const auto x = noise<sample>(2 * 601, 0x5151u + static_cast<std::uint32_t>(si));
                Filter     whole(named(si));
                const auto ref = run(whole, x);
                for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7},
                                                std::size_t{64}, std::size_t{97}}) {
                    Filter              g(named(si));
                    std::vector<sample> y;
                    for (std::size_t at = 0; at < x.size() / 2; at += chunk) {
                        const std::size_t   n = std::min(chunk, x.size() / 2 - at);
                        std::vector<sample> part(g.outputs_for(n) * 2);
                        ASSERT_EQ(g.process(x.data() + 2 * at, n, part.data()), n);
                        y.insert(y.end(), part.begin(), part.end());
                    }
                    EXPECT_TRUE(y == ref) << "chunk " << chunk << " " << design_name(si);
                }
            }
        });
    }

    TYPED_TEST(guard_band_test, PullMatchesProcessBitExact) {
        using sample = TypeParam;
        for_each_form<sample, 2>([]<typename Filter>() {
            const auto x = noise<sample>(2 * 500, 0x7171u);
            Filter     whole;
            const auto ref = run(whole, x);
            for (const std::size_t chunk : {std::size_t{1}, std::size_t{3}, std::size_t{5}, std::size_t{64}}) {
                Filter              g;
                std::size_t         fed = 0;
                std::vector<sample> y;
                auto                pop = [&](sample* dst, std::size_t max) noexcept {
                    const std::size_t n = std::min(max, x.size() / 2 - fed);
                    for (std::size_t i = 0; i < 2 * n; ++i) {
                        dst[i] = x[2 * fed + i];
                    }
                    fed += n;
                    return n;
                };
                while (y.size() < ref.size()) {
                    const std::size_t   want = std::min(chunk, (ref.size() - y.size()) / 2);
                    std::vector<sample> part(2 * want);
                    ASSERT_EQ(g.pull(part.data(), want, pop), want);
                    y.insert(y.end(), part.begin(), part.end());
                }
                EXPECT_TRUE(y == ref) << "chunk " << chunk;
            }
        });
    }

    TEST_F(guard_band, PullShortReturnsOnDryThenResumes) {
        for_each_form<double, 1>([]<typename Filter>() {
            const auto  x = noise<double>(300, 0x9191u);
            Filter      whole;
            const auto  ref = run(whole, x);
            Filter      g;
            std::size_t avail = 0;
            std::size_t fed   = 0;
            auto        pop   = [&](double* dst, std::size_t max) noexcept {
                const std::size_t n = std::min(max, avail - fed);
                for (std::size_t i = 0; i < n; ++i) {
                    dst[i] = x[fed + i];
                }
                fed += n;
                return n;
            };
            std::vector<double> y;
            while (avail < x.size()) {
                avail = std::min(x.size(), avail + 7); // the source refills by 7 frames
                std::vector<double> part(64);
                const std::size_t   got = g.pull(part.data(), 64, pop);
                EXPECT_LE(got, 64u);
                y.insert(y.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(got));
            }
            ASSERT_EQ(y.size(), ref.size()) << "a 1:1 filter delivers every frame it was given";
            EXPECT_TRUE(y == ref) << "a dry source resumes bit for bit";
        });
    }

    TEST_F(guard_band, TheAccountingIsOneToOne) {
        // A guard band filter changes no rate: one output per input from any
        // state, and its flush writes its window.
        for_each_form<float, 2>([]<typename Filter>() {
            static_assert(Filter::k_up == 1 && Filter::k_down == 1);
            Filter             g;
            const auto         x = noise<float>(2 * 37, 0x3131u);
            std::vector<float> y(2 * 37);
            for (std::size_t lead = 0; lead < 37; lead += 9) {
                g.process(x.data(), lead, y.data());
                for (std::size_t n = 0; n < 70; n += 7) {
                    EXPECT_EQ(g.outputs_for(n), n);
                    EXPECT_EQ(g.frames_needed(n), n);
                }
            }
            EXPECT_EQ(g.flush_output_frames(), g.window_frames());
            std::vector<float> tail(2 * g.window_frames());
            EXPECT_EQ(g.flush(tail.data()), g.window_frames());
        });
    }

    TYPED_TEST(guard_band_test, ResetReproducesBitExactly) {
        using sample = TypeParam;
        for_each_form<sample, 1>([]<typename Filter>() {
            const auto          x = noise<sample>(333, 0x4141u);
            Filter              g(guard_band_settings::transparent(k_fir_kind));
            const auto          first = run(g, x);
            std::vector<sample> tail(g.flush_output_frames());
            g.flush(tail.data());
            g.reset();
            EXPECT_TRUE(run(g, x) == first);
        });
    }

    TYPED_TEST(guard_band_test, ChannelsAreIndependentAndInTheType) {
        using sample = TypeParam;
        static_assert(guard_band_filter<sample, 3, k_nonlinear>::channels() == 3, "the count is the type's (D18)");
        static_assert(
            std::is_base_of_v<guard_band_filter_base<sample, k_linear>, guard_band_filter<sample, 1, k_linear>>
                && std::is_base_of_v<guard_band_filter_base<sample, k_linear>, guard_band_filter<sample, 5, k_linear>>,
            "every count shares one count-independent base (K12)");
        for_each_form<sample, 2>([]<typename Filter>() {
            using mono_t          = guard_band_filter<sample, 1, Filter::k_phase>;
            const auto          a = noise<sample>(401, 0x1111u);
            const auto          b = noise<sample>(401, 0x2222u, 0.5);
            std::vector<sample> ab(2 * a.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                ab[2 * i]     = a[i];
                ab[2 * i + 1] = b[i];
            }
            Filter     g;
            mono_t     ma;
            mono_t     mb;
            const auto y  = run(g, ab);
            const auto ya = run(ma, a);
            const auto yb = run(mb, b);
            ASSERT_EQ(y.size(), 2 * ya.size());
            for (std::size_t i = 0; i < ya.size(); ++i) {
                EXPECT_EQ(y[2 * i], ya[i]);
                EXPECT_EQ(y[2 * i + 1], yb[i]);
            }
        });
    }

    TEST_F(guard_band, ValidateAgreesWithTheConstructor) {
        using nl_q15 = guard_band_filter<std::int16_t, 1, k_nonlinear>;
        using li_f   = guard_band_filter<float, 1, k_linear>;
        using nl_f   = guard_band_filter<float, 1, k_nonlinear>;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            EXPECT_EQ(nl_q15::validate(named(si)), nullptr);
            EXPECT_EQ(li_f::validate(named(si)), nullptr);
            TAP_SR_EXPECT_ACCEPTED(nl_q15{named(si)});
            TAP_SR_EXPECT_ACCEPTED(li_f{named(si)});
            EXPECT_TRUE(li_f{named(si)}.tabulated());
        }
        // Bad settings: validate(s)'s text, std::invalid_argument, both forms.
        for (const guard_band_settings bad :
             {guard_band_settings{0.5, 70.0, 7}, guard_band_settings{0.0, 70.0, 7}, guard_band_settings{0.375, 70.0, 6},
              guard_band_settings{0.375, 70.0, 1}, guard_band_settings{0.375, 70.0, 17},
              guard_band_settings{0.375, 0.005, 7}, guard_band_settings{0.375, 5000.0, 7},
              guard_band_settings{std::numeric_limits<double>::quiet_NaN(), 70.0, 7}}) {
            EXPECT_STREQ(nl_f::validate(bad), k_guard_band_settings_reason);
            EXPECT_STREQ(li_f::validate(bad), k_guard_band_settings_reason);
            TAP_SR_EXPECT_REJECTED_WITH(nl_f{bad}, std::invalid_argument, k_guard_band_settings_reason);
            TAP_SR_EXPECT_REJECTED_WITH(li_f{bad}, std::invalid_argument, k_guard_band_settings_reason);
        }
        // A custom setting designs at construction in both forms (here a short
        // one: 40 dB from the order-5 edge at 1/4 of the rate).
        const guard_band_settings custom{0.25, 40.0, 5};
        EXPECT_EQ(nl_q15::validate(custom), nullptr);
        EXPECT_EQ(li_f::validate(custom), nullptr);
        EXPECT_FALSE(nl_q15{custom}.tabulated());
        EXPECT_FALSE(li_f{custom}.tabulated());
        // A custom setting equal to a named one by number is that one.
        EXPECT_TRUE(
            (li_f{guard_band_settings{.passband_frac = 0.375, .stopband_atten_db = 70.0, .order = 7}}.tabulated()));
    }

    TEST_F(guard_band, AnUnmeetableLinearSettingIsRefused) {
        // The linear form's run-time design searches every even length up to
        // 512 taps for A + 1 dB from the Chebyshev's stop edge; 300 dB from the
        // order-3 edge at 0.3 (0.4999994 of the rate) meets none, and the
        // Chebyshev form designs it. A host measurement: the search runs 256
        // designs through 4097-point responses (about 7 s on x86-64).
        const guard_band_settings deep{0.3, 300.0, 3};
        using li_d = guard_band_filter<double, 1, k_linear>;
        EXPECT_STREQ(li_d::validate(deep), k_guard_band_linear_unmeetable_reason);
        TAP_SR_EXPECT_REJECTED_WITH(li_d{deep}, std::runtime_error, k_guard_band_linear_unmeetable_reason);
        EXPECT_EQ((guard_band_filter<double, 1, k_nonlinear>::validate(deep)), nullptr);
    }

    TEST_F(guard_band, TheRunTimeDesignIsTheCompileTimeOneAtANamedSpec) {
        // The Chebyshev II designed at run time (design_cheby2_lowpass, then
        // cheby2_linf_gains) at a committed spec point and order is the
        // compile-time design within a few ulps (GCC may contract the run-time
        // evaluation), its L-inf gains 1 within rounding.
        const guard_band_settings custom{.passband_frac = 3.0 / 8.0, .stopband_atten_db = 70.0, .order = 9};
        const guard_band_filter<double, 1, k_nonlinear> run_time(custom);
        EXPECT_FALSE(run_time.tabulated());
        const auto& committed = tap::dsp::k_guard_band_cheby2<70, 3, 8, 9>;
        ASSERT_EQ(run_time.design().count, committed.count);
        for (std::size_t i = 0; i < committed.count; ++i) {
            const auto& a = run_time.design().sections[i];
            const auto& b = committed.sections[i];
            EXPECT_NEAR(a.b0, b.b0, 1e-13);
            EXPECT_NEAR(a.b1, b.b1, 1e-13);
            EXPECT_NEAR(a.b2, b.b2, 1e-13);
            EXPECT_NEAR(a.a1, b.a1, 1e-13);
            EXPECT_NEAR(a.a2, b.a2, 1e-13);
        }
        EXPECT_NEAR(run_time.stop_edge(), committed.stop_edge, 1e-15);
    }

    TEST_F(guard_band, CustomSettingsMeetTheirSpec) {
        // A custom setting, both forms, from the shipping double filter: the
        // Chebyshev II at -A from its edge and flat within 0.01 dB; the
        // linear form's Kaiser design at -A from the same edge (A + 1 dB on
        // its search grid) and flat within 0.01 dB, longer than the committed
        // Parks-McClellan row at the same spec (64 taps). A host measurement.
        const guard_band_settings s{.passband_frac = 3.0 / 8.0, .stopband_atten_db = 70.0, .order = 9};
        const guard_band_filter<double, 1, k_linear> li(s);
        EXPECT_FALSE(li.tabulated());
        const auto mn = measure_bands(impulse_response<k_nonlinear>(s), s.passband_frac, s.stop_edge());
        const auto ml = measure_bands(impulse_response<k_linear>(s, li.taps()), s.passband_frac, li.stop_edge());
        std::printf("[ measured ] economy at order 9, custom: Chebyshev II stopband %.3f dB, passband down to %.5f dB; "
                    "Kaiser N = %zu (the committed row 64), stopband %.2f dB, passband %.5f dB peak to peak\n",
                    mn.stopband_db, mn.pass_min_db, li.taps(), ml.stopband_db, ml.pass_max_db - ml.pass_min_db);
        EXPECT_LE(mn.stopband_db, -70.0 + 1e-3);
        EXPECT_GE(mn.pass_min_db, -0.01 - 1e-6);
        EXPECT_LE(ml.stopband_db, -70.0);
        EXPECT_LE(ml.pass_max_db - ml.pass_min_db, 0.01);
        constexpr std::size_t committed = tap::dsp::k_guard_band_fir<70, 3, 8, 9>.taps.size();
        EXPECT_EQ(committed, 64u);
        EXPECT_GT(li.taps(), committed) << "Kaiser is not equiripple";
        EXPECT_EQ(li.latency_output_frames(), (tap::dsp::exact_ratio{li.taps() - 1, 2}));
    }

    TEST_F(guard_band, EveryValidDesignFitsTheFixedPointFormats) {
        // The Chebyshev II's coefficients in Q2.30 (|c| < 2): its denominators
        // by stability (|a1| < 2 r < 2, a2 = r^2 < 1) and its numerators,
        // below 2 at every point of a grid over the settings (p up to 0.499,
        // A from 0.011 dB, every order), so the fixed-point formats take any
        // valid setting and validate() has no format rejection.
        double worst = 0.0;
        for (const double a : {0.011, 0.1, 3.0, 40.0, 70.0, 120.0, 300.0}) {
            for (const double p : {0.01, 0.2, 0.375, 0.45, 0.49, 0.499}) {
                for (int order = 3; order <= 15; order += 2) {
                    const guard_band_settings s{p, a, order};
                    ASSERT_EQ(validate(s), nullptr);
                    const guard_band_filter<std::int32_t, 1, k_nonlinear> g(s);
                    for (std::size_t i = 0; i < g.design().count; ++i) {
                        const auto& c = g.design().sections[i];
                        for (const double v : {c.b0, c.b1, c.b2, c.a1, c.a2}) {
                            worst = std::max(worst, std::fabs(v));
                        }
                    }
                }
            }
        }
        std::printf("[ measured ] the largest Chebyshev II coefficient over the grid: %.6f (Q2.30 holds below 2)\n",
                    worst);
        EXPECT_LT(worst, 2.0);
    }

    // ------------------------------------------------------------------
    // Latency (I5)
    // ------------------------------------------------------------------

    TEST_F(guard_band, NominalLatencyAndGroupDelayMatchTheImpulseResponse) {
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_settings                       s = named(si);
            const guard_band_filter<double, 1, k_nonlinear> g(s);
            const auto                                      h = impulse_response<k_nonlinear>(s, 4096);
            for (const double t : {0.0, 0.3, 0.7, 1.0}) {
                const double f = t * s.passband_frac;
                EXPECT_NEAR(measured_group_delay(h, f), g.group_delay_output_frames(f), 1e-9) << t;
                EXPECT_LE(std::fabs(g.group_delay_output_frames(f) - g.nominal_latency_output_frames()),
                          g.latency_deviation_output_frames() + 1e-12);
            }
            EXPECT_EQ(g.nominal_latency_output_frames(), g.group_delay_output_frames(0.0)) << "the nominal is DC's";
            std::printf("[ measured ] %s: the Chebyshev II delays %.4f frames at DC and %.4f at the passband edge "
                        "(deviation %.4f)\n",
                        design_name(si), g.nominal_latency_output_frames(),
                        g.group_delay_output_frames(s.passband_frac), g.latency_deviation_output_frames());
        }
        // economy, as the header states it: 0.54 frames at DC, 5.04 at the
        // edge, deviation 4.51.
        const guard_band_filter<double, 1, k_nonlinear> eco;
        EXPECT_NEAR(eco.nominal_latency_output_frames(), 0.5353, 0.0005);
        EXPECT_NEAR(eco.group_delay_output_frames(3.0 / 8.0), 5.0430, 0.0005);
        EXPECT_NEAR(eco.latency_deviation_output_frames(), 4.5078, 0.0005);
    }

    TEST_F(guard_band, GroupDelayMatchesTheGoldenLeg) {
        // scipy.signal.group_delay over scipy's own design's direct form, at 0,
        // half and all of the passband edge.
        double worst = 0.0;
        for (const auto& c : rational_guard_ref::k_cases) {
            if (c.form != 0) {
                continue;
            }
            const guard_band_filter<double, 1, k_nonlinear> g(named(c.setting));
            for (std::size_t k = 0; k < 3; ++k) {
                const double tau = g.group_delay_output_frames(c.gd_f[k]);
                worst            = std::max(worst, std::fabs(tau - c.gd[k]));
                EXPECT_NEAR(tau, c.gd[k], 1e-8) << design_name(c.setting);
            }
        }
        std::printf("[ measured ] group delay against the golden leg's: %.3g frames at worst\n", worst);
    }

    TEST_F(guard_band, TheLinearLatencyIsExact) {
        // (N - 1) / 2 frames, exact; the impulse response delays every
        // passband frequency by it.
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_settings                    s = named(si);
            const guard_band_filter<double, 1, k_linear> g(s);
            EXPECT_EQ(g.latency_output_frames(), (tap::dsp::exact_ratio{g.taps() - 1, 2}));
            EXPECT_EQ(g.latency_seconds(16000.0), static_cast<double>(g.taps() - 1) / 2.0 / 16000.0);
            const auto h = impulse_response<k_linear>(s, g.taps());
            for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                EXPECT_NEAR(measured_group_delay(h, t * s.passband_frac), g.latency_output_frames().value(), 1e-9);
            }
        }
        EXPECT_EQ((guard_band_filter<float, 1, k_linear>{}.latency_output_frames()), (tap::dsp::exact_ratio{49, 2}));
    }

    // ------------------------------------------------------------------
    // Flush (I6) and the honest limit (question 19)
    // ------------------------------------------------------------------

    /// flush() equals zero padding bit for bit, and past it the output is
    /// silence (or, under the honest limit, the residue).
    template <typename Filter>
    void expect_flush_is_zero_padding(const guard_band_settings& s, std::size_t fed_max, double residue) {
        using sample     = typename Filter::sample;
        constexpr auto C = Filter::channels();
        const auto     x = noise<sample>(fed_max * C, 0x6161u);
        for (std::size_t fed = fed_max - 3; fed <= fed_max; ++fed) {
            Filter              flushed(s);
            std::vector<sample> scratch(fed * C + C);
            flushed.process(x.data(), fed, scratch.data());
            Filter              padded = flushed;
            const std::size_t   expect = flushed.flush_output_frames();
            std::vector<sample> a(expect * C + C, sample{1});
            ASSERT_EQ(flushed.flush(a.data()), expect);
            constexpr std::size_t k_block = 256;
            std::vector<sample>   z(k_block * C, sample{});
            std::vector<sample>   b(k_block * C);
            std::size_t           made = 0;
            while (made < expect + 512) {
                const std::size_t got = padded.process(z.data(), k_block, b.data());
                for (std::size_t j = 0; j < got * C; ++j) {
                    const std::size_t i = made * C + j;
                    if (i < expect * C) {
                        ASSERT_EQ(a[i], b[j]) << "fed " << fed << " sample " << i
                                              << ": the stage's own flush is bit "
                                                 "for bit";
                    }
                    else if (residue == 0.0) {
                        ASSERT_EQ(b[j], sample{}) << "fed " << fed << " sample " << i << ": silence past the flush";
                    }
                    else {
                        ASSERT_LE(std::fabs(to_unit(b[j])), residue) << "fed " << fed << ": the residue past it";
                    }
                }
                made += got;
            }
            if constexpr (!Filter::k_is_linear) {
                for (const auto v : flushed.states()) {
                    if (residue == 0.0) {
                        ASSERT_EQ(v, typename Filter::state{}) << "an exactly zero state after the window (I6)";
                    }
                    else {
                        ASSERT_LE(std::fabs(static_cast<double>(v)), residue) << "the honest limit";
                    }
                }
            }
        }
    }

    TYPED_TEST(guard_band_test, FlushEqualsZeroPaddingBitForBit) {
        using sample = TypeParam;
        for_each_form<sample, 2>([]<typename Filter>() {
            for (int si = 0; si < static_cast<int>(k_designs); ++si) {
                expect_flush_is_zero_padding<Filter>(named(si), 300, flush_residue<sample, Filter::k_phase>(named(si)));
            }
        });
    }

    /// The largest state left, in units of the smallest normal, after a
    /// drive and then `silence` zeros: a full-scale square wave of the
    /// period (start < 10) or noise.
    template <typename S>
    double settled_state(const guard_band_settings& s, int start, std::size_t silence) {
        guard_band_filter<S, 1, k_nonlinear> g(s);
        std::vector<S>                       x(2000);
        const int                            period = 3 + 4 * start;
        std::uint32_t                        seed   = 0x1234u + 77u * static_cast<std::uint32_t>(start);
        for (std::size_t k = 0; k < x.size(); ++k) {
            if (start < 10) {
                x[k] = (static_cast<int>(k) % period) < period / 2 ? S(1) : S(-1);
            }
            else {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                x[k] = static_cast<S>((static_cast<double>(seed % 65536u) - 32768.0) / 32768.0);
            }
        }
        std::vector<S> y(x.size());
        g.process(x.data(), x.size(), y.data());
        std::vector<S> z(silence, S{});
        std::vector<S> w(silence);
        g.process(z.data(), z.size(), w.data());
        double worst = 0.0;
        for (const auto v : g.states()) {
            worst = std::max(worst, std::fabs(static_cast<double>(v)));
        }
        return worst / static_cast<double>(std::numeric_limits<S>::min());
    }

    TYPED_TEST(guard_band_float_test, TheChebyshevLimitCycleIsBounded) {
        // The honest limit (the maintainer's decision on question 19): under
        // the host's flush-to-zero the floating Chebyshev II of order 7 or
        // more holds a zero-input limit cycle from some states. At the named
        // orders 7 and 11 its state settles below 16 times the smallest
        // normal; orders 3 and 5 drain to an exactly zero state within the
        // window at every named spec point. Twenty starts per design (square
        // waves and noise), settled over two windows: the six named designs,
        // and orders 3 and 5 at each profile's numbers where none is named.
        using sample = TypeParam;
        if constexpr (k_no_flush_control<sample>) {
            GTEST_SKIP() << "no control bit governs this type here: the subnormal residue is the limit "
                            "(test_iir_subnormals.cpp)";
        }
        else {
            std::vector<std::pair<guard_band_settings, std::string>> designs;
            for (int si = 0; si < static_cast<int>(k_designs); ++si) {
                designs.emplace_back(named(si), design_name(si));
            }
            for (int pi = 0; pi < 4; ++pi) {
                for (const int order : {3, 5}) {
                    guard_band_settings custom = named(pi, k_fir_kind);
                    custom.order               = order;
                    if (pi != 0 || order != 5) { // super_economy's order 5 is named
                        designs.emplace_back(custom, std::string(k_profile_names[static_cast<std::size_t>(pi)])
                                                         + " (custom order " + std::to_string(order) + ")");
                    }
                }
            }
            int cycling_at_named = 0;
            for (const auto& [s, name] : designs) {
                const guard_band_filter<sample, 1, k_nonlinear> probe(s);
                double                                          worst   = 0.0;
                int                                             cycling = 0;
                for (int start = 0; start < 20; ++start) {
                    const double left = settled_state<sample>(s, start, 2 * probe.window_frames());
                    worst             = std::max(worst, left);
                    cycling += left > 0.0 ? 1 : 0;
                }
                std::printf("[ measured ] %s %s, order %d: %d of 20 starts hold a cycle, at most %.1f x the smallest "
                            "normal\n",
                            std::is_same_v<sample, float> ? "float" : "double", name.c_str(), s.order, cycling, worst);
                if (s.order < 7) {
                    EXPECT_EQ(worst, 0.0) << "orders 3 and 5 drain: " << name;
                }
                else {
                    EXPECT_LE(worst, 16.0) << name;
                    cycling_at_named += cycling;
                }
            }
            EXPECT_GT(cycling_at_named, 0) << "the limit cycle the header states, from some starts";
        }
    }

    TYPED_TEST(guard_band_float_test, SilenceDecaysToExactZero) {
        // Under the host's flush-to-zero: the linear form's output is exactly
        // zero after its window from any drive (with or without the bits: an
        // FIR), and the Chebyshev's state is exactly zero after its window
        // from a full-scale start, but for the honest limit.
        using sample = TypeParam;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_settings s = named(si);
            for_each_form<sample, 1>([&s]<typename Filter>() {
                Filter              g(s);
                const auto          x = noise<sample>(1000, 0x8181u, 1.0);
                std::vector<sample> out(1000);
                g.process(x.data(), 1000, out.data());
                std::vector<sample> z(g.window_frames(), sample{});
                std::vector<sample> y(z.size());
                g.process(z.data(), z.size(), y.data());
                std::vector<sample> more(64, sample{});
                std::vector<sample> w(64);
                g.process(more.data(), 64, w.data());
                const double residue = flush_residue<sample, Filter::k_phase>(s);
                for (const sample v : w) {
                    if (residue == 0.0) {
                        ASSERT_EQ(v, sample{});
                    }
                    else {
                        ASSERT_LE(std::fabs(static_cast<double>(v)), residue);
                    }
                }
            });
        }
    }

    TYPED_TEST(guard_band_test, NeverTouchesTheFloatingPointEnvironment) {
        using sample = TypeParam;
        for_each_form<sample, 1>([]<typename Filter>() {
            const std::uint64_t before = tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask;
            Filter              g;
            const auto          x = noise<sample>(200, 0x9999u);
            std::vector<sample> out(200);
            g.process(x.data(), 200, out.data());
            std::size_t         fed = 0;
            std::vector<sample> pulled(8);
            g.pull(pulled.data(), 8, [&](sample* dst, std::size_t max) noexcept {
                const std::size_t n = std::min(max, std::size_t{4});
                for (std::size_t i = 0; i < n; ++i) {
                    dst[i] = x[fed + i];
                }
                fed += n;
                return n;
            });
            std::vector<sample> tail(g.flush_output_frames());
            g.flush(tail.data());
            g.reset();
            EXPECT_EQ(tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask, before);
        });
    }

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
    // The precondition (I9): a debug assertion in the nonlinear form, where a
    // bit governs float; the linear form, an FIR, has none.
    TEST(GuardBandDebug, ThePreconditionFiresWithoutFlushToZeroInTheChebyshevOnly) {
        if constexpr (tap::dsp::k_subnormal_control<float>) {
            guard_band_filter<float, 1, k_nonlinear> nl;
            guard_band_filter<float, 1, k_linear>    li;
            std::array<float, 2>                     x{0.5f, 0.25f};
            std::array<float, 2>                     y{};
            const std::uint64_t                      saved = tap::sr::rational::test::set_flush_to_zero(false);
            EXPECT_DEATH(static_cast<void>(nl.process(x.data(), 2, y.data())), "");
            EXPECT_EQ(li.process(x.data(), 2, y.data()), 2u) << "the FIR needs no flush-to-zero";
            tap::sr::rational::test::write_floating_point_control(saved);
            const std::uint64_t on = tap::sr::rational::test::set_flush_to_zero(true);
            EXPECT_EQ(nl.process(x.data(), 2, y.data()), 2u) << "with the bits set it does not fire";
            tap::sr::rational::test::write_floating_point_control(on);
        }
    }
#endif

    // ------------------------------------------------------------------
    // Fixed point
    // ------------------------------------------------------------------

    TYPED_TEST(guard_band_fixed_test, DcGainIsTheDesignsAndSaturatesAtTheOutput) {
        // The Chebyshev II passes DC at unity (each section at unity DC gain):
        // full scale of either sign within 1 LSB. The FIR passes DC at its
        // taps' sum (within the 0.01 dB ripple: 0.99951 to 1.00049), so a
        // full-scale DC comes out at the sum times it, saturated at the rail
        // where the sum is above 1 (balanced), never wrapped.
        using sample = TypeParam;
        using tr     = tap::dsp::sample_traits<sample>;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            for (const sample level :
                 {std::numeric_limits<sample>::max(), std::numeric_limits<sample>::min(), sample{12345}}) {
                guard_band_filter<sample, 1, k_nonlinear> nl(named(si));
                guard_band_filter<sample, 1, k_linear>    li(named(si));
                std::vector<sample>                       x(3000, level);
                const auto                                yn  = run(nl, x);
                const auto                                yl  = run(li, x);
                double                                    sum = 0.0;
                for (const auto c : li.coefficients()) {
                    sum += static_cast<double>(c) / tr::k_coeff_scale;
                }
                const double expect = std::clamp(static_cast<double>(level) * sum,
                                                 static_cast<double>(std::numeric_limits<sample>::min()),
                                                 static_cast<double>(std::numeric_limits<sample>::max()));
                for (std::size_t i = x.size() / 2; i < x.size(); ++i) {
                    ASSERT_LE(std::llabs(static_cast<long long>(yn[i]) - static_cast<long long>(level)), 1)
                        << design_name(si);
                    ASSERT_LE(std::fabs(static_cast<double>(yl[i]) - expect), 1.0) << design_name(si);
                }
            }
        }
    }

    /// A full-scale square wave of the period and a sine at +12 dB clipped
    /// to full scale: the headroom drives (I8).
    template <typename S>
    std::vector<std::vector<S>> full_scale_drives(std::size_t n) {
        std::vector<std::vector<S>> out;
        for (const std::size_t period : {2u, 3u, 4u, 6u, 10u, 40u, 400u}) {
            std::vector<S> x(n);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = (i % period) < (period + 1) / 2 ? std::numeric_limits<S>::max() : std::numeric_limits<S>::min();
            }
            out.push_back(x);
        }
        for (const double f : {0.011, 0.13, 0.23, 0.37, 0.45}) {
            std::vector<S> x(n);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = from_unit<S>(
                    std::clamp(4.0 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i)), -1.0, 1.0));
            }
            out.push_back(x);
        }
        return out;
    }

    TYPED_TEST(guard_band_fixed_test, FullScaleDriveNeverWrapsInside) {
        // The Chebyshev II: every state inside its two guard bits (|state| < 4
        // of full scale) and every output within 2 LSB (Q15) or 2e-8 (Q31) of
        // the double filter clamped to the format (in Q31 the Q2.30
        // coefficients' own quantization shows, as DspTap's battery states). The FIR: every output the
        // exact sum of its quantized taps' products rounded once and clamped
        // (within half an LSB in Q15, where the int64 accumulation is exact,
        // and one LSB in Q31, whose products drop 16 bits each). Past full
        // scale both saturate at the output, never wrap.
        using sample         = TypeParam;
        using tr             = tap::dsp::sample_traits<sample>;
        double       largest = 0.0;
        double       error   = 0.0;
        std::size_t  clipped = 0;
        const double lsb     = 1.0 / k_full<sample>;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            for (const auto& x : full_scale_drives<sample>(1200)) {
                guard_band_filter<sample, 1, k_nonlinear> st(named(si));
                guard_band_filter<double, 1, k_nonlinear> ref(named(si));
                guard_band_filter<sample, 1, k_linear>    li(named(si));
                std::vector<double>                       xd(x.size());
                for (std::size_t i = 0; i < x.size(); ++i) {
                    xd[i] = to_unit(x[i]);
                }
                std::vector<sample> y(x.size());
                double              state_peak = 0.0;
                for (std::size_t i = 0; i < x.size(); i += 8) {
                    const std::size_t n = std::min<std::size_t>(8, x.size() - i);
                    st.process(x.data() + i, n, y.data() + i);
                    for (const auto v : st.states()) {
                        state_peak = std::max(state_peak, std::fabs(static_cast<double>(v)));
                    }
                }
                const auto   yd   = run(ref, xd);
                const auto   yl   = run(li, x);
                const double unit = std::is_same_v<sample, std::int16_t> ? 536870912.0 : 1152921504606846976.0;
                largest           = std::max(largest, state_peak / unit);
                ASSERT_LT(state_peak / unit, 4.0) << "two guard bits";
                const auto q = li.coefficients();
                for (std::size_t i = 0; i < y.size(); ++i) {
                    const double clamped = std::clamp(yd[i], -1.0, 1.0 - lsb);
                    error                = std::max(error, std::fabs(to_unit(y[i]) - clamped));
                    ASSERT_LE(std::fabs(to_unit(y[i]) - clamped),
                              (std::is_same_v<sample, std::int16_t> ? 2.0 * lsb : 2e-8))
                        << design_name(si) << " output " << i;
                    double exact = 0.0;
                    for (std::size_t k = 0; k < q.size() && k <= i; ++k) {
                        exact += static_cast<double>(q[k]) / tr::k_coeff_scale * xd[i - k];
                    }
                    const double fir = std::clamp(exact, -1.0, 1.0 - lsb);
                    ASSERT_LE(std::fabs(to_unit(yl[i]) - fir), (std::is_same_v<sample, std::int16_t> ? 0.5 : 1.0) * lsb)
                        << design_name(si) << " FIR output " << i;
                    if (std::fabs(yd[i]) > 1.0) {
                        ASSERT_EQ(y[i], yd[i] > 0.0 ? std::numeric_limits<sample>::max()
                                                    : std::numeric_limits<sample>::min());
                        ++clipped;
                    }
                    if (std::fabs(exact) > 1.0) {
                        ASSERT_EQ(yl[i], exact > 0.0 ? std::numeric_limits<sample>::max()
                                                     : std::numeric_limits<sample>::min());
                        ++clipped;
                    }
                }
            }
        }
        std::printf("[ measured ] %s: the Chebyshev II's largest state under full-scale drive is %.3f of full scale "
                    "(limit 4), its outputs within %.3g of the double filter's; %zu outputs past full scale, every "
                    "one at the rail\n",
                    std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31", largest, error, clipped);
        EXPECT_GT(clipped, 0u) << "the drives reach the output's saturation";
    }

    TYPED_TEST(guard_band_fixed_test, FixedPointSilenceIsExactlyZeroAfterTheWindow) {
        // After full-scale drive, window_frames() zeros leave the Chebyshev's
        // state exactly zero (magnitude truncation leaves no limit cycle, I8),
        // at every named setting and at orders 3 to 11 at economy's numbers.
        using sample                            = TypeParam;
        std::size_t                      margin = std::numeric_limits<std::size_t>::max();
        std::size_t                      most   = 0;
        std::vector<guard_band_settings> settings;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            settings.push_back(named(si));
        }
        for (const int order : {3, 5, 9, 11}) {
            settings.push_back(guard_band_settings{3.0 / 8.0, 70.0, order});
        }
        for (const auto& s : settings) {
            for (const auto& x : full_scale_drives<sample>(900)) {
                guard_band_filter<sample, 1, k_nonlinear> g(s);
                std::vector<sample>                       y(x.size());
                g.process(x.data(), x.size(), y.data());
                std::size_t frames  = 0;
                sample      zero[1] = {sample{}};
                sample      sink[1];
                while (std::any_of(g.states().begin(), g.states().end(), [](auto v) { return v != 0; })
                       && frames < 100000) {
                    g.process(zero, 1, sink);
                    ++frames;
                }
                ASSERT_LE(frames, g.window_frames()) << "order " << s.order;
                margin = std::min(margin, g.window_frames() - frames);
                most   = std::max(most, frames);
            }
        }
        std::printf("[ measured ] %s: every Chebyshev II state reaches exactly zero within %zu frames, inside its "
                    "window (least slack %zu frames)\n",
                    std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31", most, margin);
    }

    TEST_F(guard_band, Q15FloorsAgainstDouble) {
        // A -1 dBFS sine through each Q15 form against the double filter: the
        // Chebyshev II's error is its output's own rounding (-101 dB RMS for
        // uniform rounding), the recursion adding little; the FIR's adds its
        // Q1.14 taps' quantization, stated as measured.
        double worst_nl = -400.0;
        double worst_li = -400.0;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const double        amp = std::pow(10.0, -1.0 / 20.0);
            const auto          xq  = sine<std::int16_t>(6000, 0.0123, amp);
            std::vector<double> xd(xq.size());
            for (std::size_t i = 0; i < xq.size(); ++i) {
                xd[i] = to_unit(xq[i]);
            }
            const auto rms = [&]<guard_phase P>() {
                guard_band_filter<std::int16_t, 1, P> q(named(si));
                guard_band_filter<double, 1, P>       d(named(si));
                const auto                            y  = run(q, xq);
                const auto                            yd = run(d, xd);
                double                                e2 = 0.0;
                for (std::size_t i = 0; i < y.size(); ++i) {
                    const double e = to_unit(y[i]) - yd[i];
                    e2 += e * e;
                }
                return 10.0 * std::log10(e2 / static_cast<double>(y.size()));
            };
            worst_nl = std::max(worst_nl, rms.template operator()<k_nonlinear>());
            worst_li = std::max(worst_li, rms.template operator()<k_linear>());
        }
        std::printf("[ measured ] Q15 against the double filter, -1 dBFS sine: Chebyshev II %.1f dB RMS, type II FIR "
                    "%.1f dB RMS at worst\n",
                    worst_nl, worst_li);
        EXPECT_LE(worst_nl, -96.0);
        EXPECT_LE(worst_li, -76.0) << "the Q1.14 taps' quantization";
    }

    TYPED_TEST(guard_band_fixed_test, QuantizedLinearTablesAttainTheirStatedStopbands) {
        // The linear form's quantized taps, from the stop edge and over the
        // passband on 16385 points each: in Q31 (Q1.30 taps) the design's
        // stopband and ripple; in Q15 (Q1.14 taps) a format limit, stated as
        // measured and pinned: the 70 dB settings attain 64.6 to 67.1 dB and
        // transparent 70.9, flat to 0.0131 dB peak to peak (an honest limit: at
        // Q15 the Chebyshev II form, whose Q2.30 coefficients keep its design,
        // holds the 70 dB tier).
        using sample                                         = TypeParam;
        using tr                                             = tap::dsp::sample_traits<sample>;
        constexpr std::array<double, k_designs> q15_stopband = {-67.1, -68.9, -66.75, -64.55, -70.85, -69.15};
        constexpr std::array<double, k_designs> q15_ripple   = {0.0101, 0.0112, 0.0101, 0.0132, 0.0126, 0.0149};
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            const guard_band_filter<sample, 1, k_linear> li(named(si));
            std::vector<double>                          h;
            for (const auto c : li.coefficients()) {
                h.push_back(static_cast<double>(c) / tr::k_coeff_scale);
            }
            double worst = 0.0;
            double lo    = 1e300;
            double hi    = 0.0;
            for (int k = 0; k <= 16384; ++k) {
                const double t = static_cast<double>(k) / 16384.0;
                worst          = std::max(worst, std::abs(dtft(h, li.stop_edge() + (0.5 - li.stop_edge()) * t)));
                const double v = std::abs(dtft(h, li.passband() * t));
                lo             = std::min(lo, v);
                hi             = std::max(hi, v);
            }
            const double attained = db(worst);
            const double ripple   = db(hi / lo);
            std::printf(
                "[ measured ] %s %s: the quantized type II FIR attains %.2f dB from its stop edge, flat to %.4f "
                "dB peak to peak\n",
                std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31", design_name(si), attained, ripple);
            if constexpr (std::is_same_v<sample, std::int16_t>) {
                EXPECT_LE(attained, q15_stopband[static_cast<std::size_t>(si)]) << design_name(si);
                EXPECT_LE(ripple, q15_ripple[static_cast<std::size_t>(si)]) << design_name(si);
            }
            else {
                EXPECT_LE(attained, -named(si).stopband_atten_db) << design_name(si);
                EXPECT_LE(ripple, 0.01) << design_name(si);
            }
        }
    }

    // ------------------------------------------------------------------
    // Honest limits: non-finite input (I9)
    // ------------------------------------------------------------------

    TYPED_TEST(guard_band_float_test, NonFiniteInputPoisonsTheChebyshevUntilReset) {
        // A recursive state never clears a non-finite input on its own, and
        // no per-sample check is paid; reset() clears it.
        using sample = TypeParam;
        guard_band_filter<sample, 1, k_nonlinear> g;
        std::vector<sample>                       x(64, sample{});
        x[3] = std::numeric_limits<sample>::quiet_NaN();
        std::vector<sample> y(64);
        g.process(x.data(), 64, y.data());
        std::vector<sample> z(4096, sample{});
        std::vector<sample> w(z.size());
        g.process(z.data(), z.size(), w.data());
        EXPECT_TRUE(std::isnan(w.back())) << "still poisoned after 4096 zeros";
        g.reset();
        g.process(z.data(), z.size(), w.data());
        EXPECT_EQ(w.back(), sample{}) << "reset() clears it";
    }

    TYPED_TEST(guard_band_float_test, NonFiniteInputClearsFromTheLinearFormAfterItsWindow) {
        // As from an FIR stage (test_non_finite.cpp): a NaN, +Inf, -Inf burst
        // reaches exactly the outputs whose window of N inputs covers it, and
        // every output after them is the burst-free twin's, bit for bit.
        using sample = TypeParam;
        for (int si = 0; si < static_cast<int>(k_designs); ++si) {
            guard_band_filter<sample, 1, k_linear> g(named(si));
            guard_band_filter<sample, 1, k_linear> twin(named(si));
            const auto                             clean = noise<sample>(400, 0x4242u);
            auto                                   x     = clean;
            x[100]                                       = std::numeric_limits<sample>::quiet_NaN();
            x[101]                                       = std::numeric_limits<sample>::infinity();
            x[102]                                       = -std::numeric_limits<sample>::infinity();
            const auto        y                          = run(g, x);
            const auto        yt                         = run(twin, clean);
            const std::size_t n                          = g.taps();
            for (std::size_t i = 0; i < y.size(); ++i) {
                if (i < 100 || i >= 103 + n - 1) {
                    ASSERT_EQ(y[i], yt[i]) << design_name(si) << " output " << i;
                }
                else {
                    ASSERT_FALSE(std::isfinite(y[i])) << "inside the burst's reach, output " << i;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // The aliases a decimating chain leaves (I7, section 4)
    // ------------------------------------------------------------------

    /// The level, dB re the tone, of the alias a tone at f_hi (cycles per
    /// higher-rate sample) leaves at f_low through a decimating chain.
    template <typename Chain>
    double alias_level(Chain& c, double f_hi, double f_low) {
        constexpr std::size_t         k_span = 2048;
        const auto                    x      = sine<double>(4 * k_span, f_hi, 0.5);
        const auto                    y      = run(c, x);
        const std::span<const double> tail(y.data() + k_span, k_span);
        return db(tap::dsp::analysis::fit_sine(tail, f_low).amplitude / 0.5);
    }

    TEST_F(guard_band, AliasesLandOnlyAboveThePassbandEdge) {
        // Decimating by 2 with the FIR half-band and both recursive variants,
        // alone and with each guard band form after it at the named setting
        // for that half-band's kind, at every profile:
        //   - a tone whose alias lands in the passband is -A down with or
        //     without the filter (the half-band's job, docs/IIR_PLAN.md 2.3);
        //   - one whose alias lands past the filter's stop edge is -A down
        //     with either form, and only with it (the half-band alone leaves
        //     it near -3 dB, the FIR half-band near -6);
        //   - in the guard band between the passband and the stop edge, the
        //     worst alias after either form is at or below -A/2 behind every
        //     half-band: I7's order rule per kind (questions 14 and 20,
        //     computed from the designs by
        //     OrderIsTheLowestOddOrderWhoseGuardBandAliasIsAtHalfA).
        // Tones on the 2048-point grid of the analyzed span, 16 across the
        // guard band.
        constexpr double k_grid = 2048.0;
        for (int pi = 0; pi < 4; ++pi) {
            const profile p = pi == 0   ? profile::super_economy()
                              : pi == 1 ? profile::economy()
                              : pi == 2 ? profile::balanced()
                                        : profile::transparent();
            // The settings behind each slot group's half-band: FIR, nonlinear,
            // near-linear.
            const std::array<guard_band_settings, 3> s = {named(pi, k_fir_kind), named(pi, k_nonlinear_kind),
                                                          named(pi, k_near_linear_kind)};
            const double                             a = s[0].stopband_atten_db;
            using fir_t                                = basic_stage<double, 1, down2>;
            using iir_t                                = basic_iir_stage<double, 1, down2, iir_phase::nonlinear>;
            using near_t                               = basic_iir_stage<double, 1, down2, iir_phase::near_linear>;
            using nl_t                                 = guard_band_filter<double, 1, k_nonlinear>;
            using li_t                                 = guard_band_filter<double, 1, k_linear>;
            // Slots: FIR, nonlinear, near-linear half-band, each alone / + the
            // Chebyshev II / + the linear form.
            std::array<double, 9> in_pass{};
            std::array<double, 9> past_edge{};
            std::array<double, 9> guard{};
            in_pass.fill(-400.0);
            past_edge.fill(-400.0);
            guard.fill(-400.0);
            const double pass    = p.passband_frac;
            const auto   measure = [&](std::size_t slot, auto make) {
                const double edge = s[slot / 3].stop_edge();
                for (const double t : {0.2, 0.6, 0.95}) {
                    const double f_low = std::floor(t * pass * k_grid) / k_grid;
                    auto         c     = make();
                    in_pass[slot]      = std::max(in_pass[slot], alias_level(c, 0.5 - f_low / 2.0, f_low));
                }
                for (int k = 1; k <= 16; ++k) {
                    const double t     = static_cast<double>(k) / 16.0 - 1.0 / 64.0;
                    const double f_low = std::floor((pass + (0.5 - pass) * t) * k_grid) / k_grid;
                    auto         c     = make();
                    const double level = alias_level(c, 0.5 - f_low / 2.0, f_low);
                    guard[slot]        = std::max(guard[slot], level);
                    if (f_low >= edge) {
                        past_edge[slot] = std::max(past_edge[slot], level);
                    }
                }
            };
            measure(0, [&] { return tap::dsp::chain<fir_t>(fir_t{p}); });
            measure(1, [&] { return tap::dsp::chain<fir_t, nl_t>(fir_t{p}, nl_t{s[0]}); });
            measure(2, [&] { return tap::dsp::chain<fir_t, li_t>(fir_t{p}, li_t{s[0]}); });
            measure(3, [&] { return tap::dsp::chain<iir_t>(iir_t{p}); });
            measure(4, [&] { return tap::dsp::chain<iir_t, nl_t>(iir_t{p}, nl_t{s[1]}); });
            measure(5, [&] { return tap::dsp::chain<iir_t, li_t>(iir_t{p}, li_t{s[1]}); });
            measure(6, [&] { return tap::dsp::chain<near_t>(near_t{p}); });
            measure(7, [&] { return tap::dsp::chain<near_t, nl_t>(near_t{p}, nl_t{s[2]}); });
            measure(8, [&] { return tap::dsp::chain<near_t, li_t>(near_t{p}, li_t{s[2]}); });
            std::printf("[ measured ] %s, decimating by 2, alone / + Chebyshev II / + linear form, behind the FIR, "
                        "the nonlinear and the near-linear half-band: in the passband at most %.1f dB; in the guard "
                        "band %.1f / %.1f / %.1f, %.1f / %.1f / %.1f, %.1f / %.1f / %.1f dB; past the stop edge "
                        "alone %.1f, %.1f, %.1f dB and with a filter at most %.1f dB\n",
                        k_profile_names[static_cast<std::size_t>(pi)],
                        *std::max_element(in_pass.begin(), in_pass.end()), guard[0], guard[1], guard[2], guard[3],
                        guard[4], guard[5], guard[6], guard[7], guard[8], past_edge[0], past_edge[3], past_edge[6],
                        std::max({past_edge[1], past_edge[2], past_edge[4], past_edge[5], past_edge[7], past_edge[8]}));
            for (std::size_t slot = 0; slot < 9; ++slot) {
                EXPECT_LE(in_pass[slot], -a) << k_profile_names[static_cast<std::size_t>(pi)] << " slot " << slot;
                if (slot % 3 == 0) {
                    EXPECT_GT(past_edge[slot], -10.0) << "the half-band alone leaves the guard band, slot " << slot;
                    continue;
                }
                EXPECT_LE(past_edge[slot], -a) << "past the filter's stop edge, slot " << slot;
                EXPECT_LE(guard[slot], -a / 2.0) << "I7's rule behind every half-band, "
                                                 << k_profile_names[static_cast<std::size_t>(pi)] << " slot " << slot;
            }
        }
    }

} // namespace
