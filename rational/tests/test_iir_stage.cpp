// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The recursive stages' battery (IIR-b, docs/IIR_PLAN.md section 4), typed
// over the formats where the promise is per format:
//   - the golden leg: every stage the engine offers (both variants, down and
//     up, every named profile at the design divisors 1, 2, 4 and 8) against
//     its transfer function in ONE direct form through scipy.signal.lfilter
//     (tests/reference/iir_reference_vectors.h, tools/reference/
//     make_iir_reference_vectors.py), within a tolerance per format, and the
//     Q15 / Q31 outputs bit-pinned (FNV-1a-64);
//   - the machine: chunking, pull against process, a dry source, the
//     accounting from every position, reset, channels in the type (D18), the
//     count-independent base (K12), validate() against the constructor (D17)
//     through TAP_SR_EXPECT_REJECTED_WITH;
//   - the spec, measured from the shipping stages: stopband and passband
//     from each stage's impulse response, power complementarity for the
//     nonlinear variant, aliasing and imaging by DspTap's sine fits, the
//     near-linear passband phase error per stage at its divisor;
//   - latency (I5): the nominal and the group delay against the impulse
//     response's, the near-linear delay path exact and taken by
//     tap::dsp::subband_delta, whose round trip cancels to the plan's -60 dB
//     at the 70 dB profiles and -100 dB at transparent (question 5);
//   - flush (I6): FlushEqualsZeroPaddingBitForBit for the stages and for
//     chains holding each variant with FIR stages, under the host's
//     flush-to-zero, and an exactly zero state after the window;
//   - fixed point (I8): unity gain at DC, full-scale drive with no internal
//     wrap, saturation at the output, the Q15 floor, the decay to an exactly
//     zero state;
//   - subnormals and the environment (I9): exact silence under
//     flush-to-zero, the control bits never touched, the debug-build
//     precondition (its no-flush counterpart is test_iir_subnormals.cpp, an
//     executable of its own built with TAP_DSP_ALLOW_SUBNORMALS);
//   - the honest limit: a non-finite input poisons the state until reset().
// Every floating test runs under the host's flush-to-zero (the fixture), as
// I9 asks of a recursive test; the QEMU legs run the battery whole.

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
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "reference/iir_reference_vectors.h"
#include "support/flush_to_zero.h"
#include "tap/dsp/analysis/sine_analysis.h"
#include "tap/dsp/chain.h"
#include "tap/dsp/subband.h"
#include "tap/sr/rational/rational.h"
#include "tap_sr_test/expect_rejected.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::sr::rational::test::flush_to_zero_fixture;

    constexpr iir_phase k_nonlinear   = iir_phase::nonlinear;
    constexpr iir_phase k_near_linear = iir_phase::near_linear;

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    profile named(int i) {
        switch (i) {
        case 0:
            return profile::super_economy();
        case 1:
            return profile::economy();
        case 2:
            return profile::balanced();
        default:
            return profile::transparent();
        }
    }
    constexpr std::array<const char*, 4> k_names = {"super_economy", "economy", "balanced", "transparent"};

    template <typename S>
    constexpr double k_full = std::is_same_v<S, std::int16_t>   ? 32768.0
                              : std::is_same_v<S, std::int32_t> ? 2147483648.0
                                                                : 1.0;

    template <typename S>
    double to_unit(S v) {
        return static_cast<double>(v) / k_full<S>;
    }

    /// The reference input's Q15 integer k as S: k / 32768, k, k * 2^16.
    template <typename S>
    S from_q15(std::int16_t k) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(static_cast<double>(k) / 32768.0);
        }
        else if constexpr (std::is_same_v<S, std::int16_t>) {
            return k;
        }
        else {
            return static_cast<S>(static_cast<std::int32_t>(k) * 65536);
        }
    }

    template <typename S>
    S from_unit(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return tap::dsp::detail::round_sat<S>(v * k_full<S>);
        }
    }

    /// Runs a fresh stage over a whole stream.
    template <typename Stage>
    std::vector<typename Stage::sample> run(Stage& st, const std::vector<typename Stage::sample>& in) {
        std::vector<typename Stage::sample> out(st.outputs_for(in.size() / Stage::channels()) * Stage::channels());
        const std::size_t                   made = st.process(in.data(), in.size() / Stage::channels(), out.data());
        out.resize(made * Stage::channels());
        return out;
    }

    template <typename S>
    std::vector<S> noise(std::size_t n, std::uint32_t seed, double peak = 0.9) {
        std::vector<S> x(n);
        std::uint32_t  s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = from_unit<S>(peak * (static_cast<double>(s % 65536u) - 32768.0) / 32768.0);
        }
        return x;
    }

    template <typename S>
    std::vector<S> sine(std::size_t n, double f, double amp) {
        std::vector<S> x(n);
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = from_unit<S>(amp * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i)));
        }
        return x;
    }

    template <typename S>
    std::uint64_t fnv1a64(std::uint64_t h, const std::vector<S>& v) {
        for (const S s : v) {
            auto u = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<S>>(s));
            for (std::size_t b = 0; b < sizeof(S); ++b) {
                h ^= (u >> (8 * b)) & 0xffu;
                h *= 0x100000001b3ull;
            }
        }
        return h;
    }

    double db(double v) {
        return 20.0 * std::log10(std::max(v, 1e-300));
    }

    /// The group delay of a sequence at f (cycles per sample).
    double measured_group_delay(const std::vector<double>& h, double f) {
        std::complex<double> num = 0.0;
        std::complex<double> den = 0.0;
        for (std::size_t n = 0; n < h.size(); ++n) {
            const auto e = std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(n));
            den += h[n] * e;
            num += static_cast<double>(n) * h[n] * e;
        }
        return std::real(num / den);
    }

    std::complex<double> dtft(const std::vector<double>& h, double f) {
        std::complex<double> acc = 0.0;
        const auto           w   = std::polar(1.0, -2.0 * std::numbers::pi * f);
        std::complex<double> e   = 1.0;
        for (const double v : h) {
            acc += v * e;
            e *= w;
        }
        return acc;
    }

    /// H at the higher rate from the shipping interpolator: its impulse
    /// response is 2 h (the gain of 2 included), long enough that the tail
    /// is below 1e-20.
    template <iir_phase P>
    std::vector<double> impulse_response(const profile& p, std::size_t inputs = 2048) {
        basic_iir_stage<double, 1, up2, P> st(p);
        std::vector<double>                x(inputs, 0.0);
        x[0]                  = 1.0;
        std::vector<double> h = run(st, x);
        for (double& v : h) {
            v *= 0.5;
        }
        return h;
    }

    // ------------------------------------------------------------------
    // The golden leg
    // ------------------------------------------------------------------

    template <typename S>
    class iir_stage_test : public flush_to_zero_fixture {};
    using sample_types = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(iir_stage_test, sample_types, );

    template <typename S>
    class iir_fixed_test : public flush_to_zero_fixture {};
    using fixed_types = ::testing::Types<std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(iir_fixed_test, fixed_types, );

    template <typename S>
    class iir_float_test : public flush_to_zero_fixture {};
    using floating_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(iir_float_test, floating_types, );

    class iir_double : public flush_to_zero_fixture {};

    /// The golden case's stage over S, run on the reference input.
    template <typename S, iir_phase P, bool Down>
    std::vector<S> golden_run(const rational_iir_ref::iir_case& c, int& order) {
        using direction = std::conditional_t<Down, down2, up2>;
        const profile p = named(c.profile).relaxed({static_cast<std::uint64_t>(c.divisor), std::uint64_t{1}});
        basic_iir_stage<S, 1, direction, P> st(p);
        if constexpr (P == k_nonlinear) {
            order = st.nonlinear_design().order;
        }
        else {
            order = st.near_linear_design().order;
        }
        const std::size_t n = Down ? rational_iir_ref::k_input.size() : rational_iir_ref::k_input.size() / 2;
        std::vector<S>    in(n);
        for (std::size_t i = 0; i < n; ++i) {
            in[i] = from_q15<S>(rational_iir_ref::k_input[i]);
        }
        return run(st, in);
    }

    template <typename S>
    std::vector<S> golden_dispatch(const rational_iir_ref::iir_case& c, int& order) {
        if (c.phase == 0) {
            return c.down != 0 ? golden_run<S, k_nonlinear, true>(c, order)
                               : golden_run<S, k_nonlinear, false>(c, order);
        }
        return c.down != 0 ? golden_run<S, k_near_linear, true>(c, order)
                           : golden_run<S, k_near_linear, false>(c, order);
    }

    /// The tolerance per format against the golden leg, in units of full
    /// scale (the input peaks at 0.9), and what it covers:
    ///   - double: the two structures' rounding, the direct form's own error
    ///     at the high orders included (measured 1.1e-13 at worst);
    ///   - float: float coefficients, states and products (I14; measured
    ///     4.4e-7 at worst, transparent's near-linear N = 19);
    ///   - Q15: the output's own rounding and the recursion's truncations
    ///     (measured 1.4 LSB at worst; the bound 2 LSB);
    ///   - Q31: the same at 2^-31 with Q0.31 / Q2.30 coefficients (measured
    ///     2.6e-9 at worst).
    template <typename S>
    constexpr double k_golden_tolerance = std::is_same_v<S, double>         ? 1e-11
                                          : std::is_same_v<S, float>        ? 2e-6
                                          : std::is_same_v<S, std::int16_t> ? 2.0 / 32768.0
                                                                            : 1e-8;

    TYPED_TEST(iir_stage_test, MatchesTheDirectFormGoldenLeg) {
        using sample = TypeParam;
        double worst = 0.0;
        for (const auto& c : rational_iir_ref::k_cases) {
            int                       order = 0;
            const std::vector<sample> y     = golden_dispatch<sample>(c, order);
            ASSERT_EQ(static_cast<int>(y.size()), c.frames);
            EXPECT_EQ(order, c.order) << "the design the stage runs is the golden leg's";
            // The fixed-point outputs saturate at full scale (the
            // interpolators' peaks on full-band noise exceed it); the golden
            // leg is clamped the same way, never the floating formats'.
            const double lsb = 1.0 / k_full<sample>;
            const double top = std::is_floating_point_v<sample> ? 1e300 : 1.0 - lsb;
            double       err = 0.0;
            for (std::size_t i = 0; i < y.size(); ++i) {
                err = std::max(err, std::fabs(to_unit(y[i]) - std::clamp(c.expected[i], -top - lsb, top)));
            }
            EXPECT_LE(err, k_golden_tolerance<sample>)
                << (c.phase == 0 ? "nonlinear" : "near-linear") << (c.down != 0 ? " down " : " up ")
                << k_names[static_cast<std::size_t>(c.profile)] << " d" << c.divisor;
            worst = std::max(worst, err);
        }
        std::printf("[ measured ] %zu-byte samples against the direct-form golden leg: %.3g of full scale at worst "
                    "(tolerance %.3g)\n",
                    sizeof(sample), worst, k_golden_tolerance<sample>);
    }

    // The Q15 and Q31 outputs, bit for bit: FNV-1a-64 over every golden
    // case's output, per variant and direction (16 cases each). A change that
    // moves a pin is a numeric change to the fixed-point datapath.
    TYPED_TEST(iir_fixed_test, OutputsAreBitPinned) {
        using sample                             = TypeParam;
        std::array<std::uint64_t, 4>           h = {0xcbf29ce484222325ull, 0xcbf29ce484222325ull, 0xcbf29ce484222325ull,
                                                    0xcbf29ce484222325ull};
        constexpr std::array<std::uint64_t, 4> q15 = {0x061a00a0b2fffcc1ull, 0x7b02f4fc0b63e3fcull,
                                                      0x344fa8820daed655ull, 0x7aaf371510d61fa9ull};
        constexpr std::array<std::uint64_t, 4> q31 = {0x7ec24c57fefc638aull, 0x2a988e2bf9d73256ull,
                                                      0xfb461874b3512534ull, 0x4b257dc57874196eull};
        for (const auto& c : rational_iir_ref::k_cases) {
            int        order = 0;
            const auto y     = golden_dispatch<sample>(c, order);
            auto&      slot  = h[static_cast<std::size_t>(2 * c.phase + (c.down != 0 ? 0 : 1))];
            slot             = fnv1a64(slot, y);
        }
        const auto& pins = std::is_same_v<sample, std::int16_t> ? q15 : q31;
        for (std::size_t i = 0; i < 4; ++i) {
            std::printf("[ measured ] %s %s %s: 0x%016llxull\n", std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31",
                        i < 2 ? "nonlinear" : "near-linear", i % 2 == 0 ? "down" : "up",
                        static_cast<unsigned long long>(h[i]));
            EXPECT_EQ(h[i], pins[i]) << i;
        }
    }

    // ------------------------------------------------------------------
    // The machine
    // ------------------------------------------------------------------

    /// Every (variant, direction) of a by-2 stage at economy over S, Ch.
    template <typename S, std::size_t Ch, typename F>
    void for_each_shape(F&& f) {
        f.template operator()<basic_iir_stage<S, Ch, down2, k_nonlinear>>();
        f.template operator()<basic_iir_stage<S, Ch, up2, k_nonlinear>>();
        f.template operator()<basic_iir_stage<S, Ch, down2, k_near_linear>>();
        f.template operator()<basic_iir_stage<S, Ch, up2, k_near_linear>>();
    }

    TYPED_TEST(iir_stage_test, ChunkingIsBitIdentical) {
        using sample = TypeParam;
        for_each_shape<sample, 2>([]<typename Stage>() {
            for (int pi = 0; pi < 4; ++pi) {
                const auto x = noise<sample>(2 * 601, 0x5151u + static_cast<std::uint32_t>(pi));
                Stage      whole(named(pi));
                const auto ref = run(whole, x);
                for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7},
                                                std::size_t{64}, std::size_t{97}}) {
                    Stage               st(named(pi));
                    std::vector<sample> y;
                    for (std::size_t at = 0; at < x.size() / 2; at += chunk) {
                        const std::size_t   n = std::min(chunk, x.size() / 2 - at);
                        std::vector<sample> part(st.outputs_for(n) * 2);
                        ASSERT_EQ(st.process(x.data() + 2 * at, n, part.data()), part.size() / 2);
                        y.insert(y.end(), part.begin(), part.end());
                    }
                    EXPECT_TRUE(y == ref) << "chunk " << chunk << " profile " << k_names[static_cast<std::size_t>(pi)];
                }
            }
        });
    }

    TYPED_TEST(iir_stage_test, PullMatchesProcessBitExact) {
        using sample = TypeParam;
        for_each_shape<sample, 2>([]<typename Stage>() {
            const auto x = noise<sample>(2 * 500, 0x7171u);
            Stage      whole;
            const auto ref = run(whole, x);
            for (const std::size_t chunk : {std::size_t{1}, std::size_t{3}, std::size_t{5}, std::size_t{64}}) {
                Stage               st;
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
                    ASSERT_EQ(st.pull(part.data(), want, pop), want);
                    y.insert(y.end(), part.begin(), part.end());
                }
                EXPECT_TRUE(y == ref) << "chunk " << chunk;
            }
        });
    }

    TEST_F(iir_double, PullShortReturnsOnDryThenResumes) {
        for_each_shape<double, 1>([]<typename Stage>() {
            const auto  x = noise<double>(300, 0x9191u);
            Stage       whole;
            const auto  ref = run(whole, x);
            Stage       st;
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
                const std::size_t   got = st.pull(part.data(), 64, pop);
                EXPECT_LE(got, 64u);
                y.insert(y.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(got));
            }
            ASSERT_LE(y.size(), ref.size());
            EXPECT_TRUE(std::equal(y.begin(), y.end(), ref.begin())) << "a dry source resumes bit for bit";
        });
    }

    TEST_F(iir_double, AccountingExactFromEveryPosition) {
        // outputs_for(n) is exactly what process(n) writes, and
        // frames_needed(k) its inverse, from every phase and every banked
        // count a pull() can leave.
        for_each_shape<double, 1>([]<typename Stage>() {
            const auto x = noise<double>(64, 0x3131u);
            for (std::size_t lead = 0; lead < 4; ++lead) {
                for (std::size_t pulled = 0; pulled < 2; ++pulled) {
                    Stage               st;
                    std::vector<double> scratch(16);
                    st.process(x.data(), lead, scratch.data());
                    std::size_t fed = 0;
                    auto        pop = [&](double* dst, std::size_t max) noexcept {
                        const std::size_t n = std::min(max, std::size_t{1});
                        dst[0]              = x[10 + fed];
                        fed += n;
                        return n;
                    };
                    st.pull(scratch.data(), pulled, pop); // may bank an interpolator's second output
                    for (std::size_t n = 0; n < 9; ++n) {
                        Stage               probe = st;
                        std::vector<double> out(probe.outputs_for(n) + 1);
                        EXPECT_EQ(probe.process(x.data() + 20, n, out.data()), st.outputs_for(n));
                    }
                    for (std::size_t k = 1; k < 7; ++k) {
                        const std::size_t n = st.frames_needed(k);
                        EXPECT_GE(st.outputs_for(n), k);
                        if (n > 0) {
                            EXPECT_LT(st.outputs_for(n - 1), k);
                        }
                    }
                    EXPECT_LE(st.outputs_for(64), 64 * Stage::k_up / Stage::k_down + Stage::k_up)
                        << "within chain.h's bound plus what a pull() banks";
                }
            }
        });
    }

    TYPED_TEST(iir_stage_test, ResetReproducesBitExactly) {
        using sample = TypeParam;
        for_each_shape<sample, 1>([]<typename Stage>() {
            const auto x = noise<sample>(333, 0x4141u);
            Stage      st;
            const auto first = run(st, x);
            st.reset();
            EXPECT_TRUE(run(st, x) == first);
            EXPECT_EQ(st.decimation_phase() + st.banked_frames(), 1u * (Stage::k_is_decimator ? 1 : 0))
                << "333 inputs leave a decimator at phase 1";
        });
    }

    TYPED_TEST(iir_stage_test, ChannelsAreIndependentAndInTheType) {
        using sample = TypeParam;
        static_assert(basic_iir_stage<sample, 3, down2, k_nonlinear>::channels() == 3, "the count is the type's (D18)");
        static_assert(std::is_base_of_v<iir_stage_base<sample, down2, k_nonlinear>,
                                        basic_iir_stage<sample, 1, down2, k_nonlinear>>
                          && std::is_base_of_v<iir_stage_base<sample, down2, k_nonlinear>,
                                               basic_iir_stage<sample, 2, down2, k_nonlinear>>,
                      "every count shares one count-independent base (K12)");
        for_each_shape<sample, 2>([]<typename Stage>() {
            using mono_t =
                basic_iir_stage<sample, 1, std::conditional_t<Stage::k_is_decimator, down2, up2>, Stage::k_phase>;
            const auto          a = noise<sample>(401, 0x1111u);
            const auto          b = noise<sample>(401, 0x2222u, 0.5);
            std::vector<sample> ab(2 * a.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                ab[2 * i]     = a[i];
                ab[2 * i + 1] = b[i];
            }
            Stage      st;
            mono_t     ma;
            mono_t     mb;
            const auto y  = run(st, ab);
            const auto ya = run(ma, a);
            const auto yb = run(mb, b);
            ASSERT_EQ(y.size(), 2 * ya.size());
            for (std::size_t i = 0; i < ya.size(); ++i) {
                EXPECT_EQ(y[2 * i], ya[i]);
                EXPECT_EQ(y[2 * i + 1], yb[i]);
            }
        });
    }

    TEST(IirStage, ValidateAgreesWithTheConstructor) {
        using nl_down = basic_iir_stage<float, 1, down2, k_nonlinear>;
        using nr_down = basic_iir_stage<float, 1, down2, k_near_linear>;
        for (int pi = 0; pi < 4; ++pi) {
            for (const std::uint64_t d : {1u, 2u, 4u, 8u}) {
                const profile p = named(pi).relaxed({d, 1});
                EXPECT_EQ(nl_down::validate(p), nullptr);
                EXPECT_EQ(nr_down::validate(p), nullptr) << "every named profile reaches both variants (I3)";
                TAP_SR_EXPECT_ACCEPTED(nl_down{p});
                TAP_SR_EXPECT_ACCEPTED(nr_down{p});
            }
        }
        // A bad profile: validate(p)'s text, std::invalid_argument.
        profile bad       = profile::economy();
        bad.passband_frac = 0.5;
        ASSERT_NE(nl_down::validate(bad), nullptr);
        TAP_SR_EXPECT_REJECTED_WITH(nl_down{bad}, std::invalid_argument, nl_down::validate(bad));
        TAP_SR_EXPECT_REJECTED_WITH(nr_down{bad}, std::invalid_argument, nr_down::validate(bad));
        // The near-linear variant has committed designs only (I4): a named
        // profile at an untabulated divisor, and a custom profile, are refused.
        const profile third = profile::economy().relaxed({3, 1});
        EXPECT_STREQ(nr_down::validate(third), k_iir_near_linear_profile_reason);
        TAP_SR_EXPECT_REJECTED_WITH(nr_down{third}, std::invalid_argument, k_iir_near_linear_profile_reason);
        // ... which the nonlinear variant designs at run time.
        EXPECT_EQ(nl_down::validate(third), nullptr);
        nl_down run_time(third);
        EXPECT_FALSE(run_time.tabulated());
        EXPECT_TRUE(nl_down{profile::economy()}.tabulated());
        // A spec no order up to 33 meets: std::runtime_error.
        profile deep{.passband_frac = 0.45, .stopband_atten_db = 600.0, .taps_per_branch = {}, .relaxations = {}};
        EXPECT_STREQ(nl_down::validate(deep), k_iir_unmeetable_spec_reason);
        TAP_SR_EXPECT_REJECTED_WITH(nl_down{deep}, std::runtime_error, k_iir_unmeetable_spec_reason);
        // The cascades compose their stages' reasons, in order.
        EXPECT_EQ((iir_down_by<float, 1, k_near_linear, 16>::validate(profile::transparent())), nullptr);
        EXPECT_STREQ((iir_down_by<float, 1, k_near_linear, 4>::validate(bad)), nr_down::validate(bad.relaxed({2, 1})))
            << "stage order: the first stage, at divisor 2";
    }

    TEST(IirStage, TheRunTimeDesignIsTheCompileTimeOneAtANamedSpec) {
        // A custom profile with a named profile's numbers is that profile (the
        // same committed or constexpr design, I4); the run-time design of
        // the same spec point reaches the same order and coefficients within
        // a few ulps (GCC may contract the run-time evaluation).
        const auto&                               named_design = tap::dsp::k_nonlinear_halfband<70, 3, 8, 1>;
        const tap::dsp::nonlinear_halfband_design run          = tap::dsp::design_nonlinear_halfband(70.0, 3.0 / 8.0);
        ASSERT_EQ(run.order, named_design.order);
        for (std::size_t i = 0; i < run.count; ++i) {
            EXPECT_NEAR(run.coefficients[i], named_design.coefficients[i], 1e-14);
        }
        profile copy{.passband_frac = 3.0 / 8.0, .stopband_atten_db = 70.0, .taps_per_branch = {}, .relaxations = {}};
        EXPECT_TRUE((basic_iir_stage<float, 1, down2, k_nonlinear>{copy}.tabulated()));
    }

    // ------------------------------------------------------------------
    // The spec, measured from the shipping stages (double)
    // ------------------------------------------------------------------

    /// The worst stopband and the passband's deviation from unity of H, from
    /// the stage's impulse response, on 2048 points of each band.
    struct band_measure {
        double stopband_db  = 0.0;
        double passband_dev = 0.0; ///< max ||H| - 1|
        double complement   = 0.0; ///< max ||H(f)|^2 + |H(1/2 - f)|^2 - 1|
    };
    band_measure measure_bands(const std::vector<double>& h, double f_pass) {
        band_measure m;
        double       worst = 0.0;
        for (int g = 0; g <= 2048; ++g) {
            const double t  = static_cast<double>(g) / 2048.0;
            const double fs = 0.5 - f_pass + f_pass * t; // [1/2 - f_pass, 1/2]
            const double fp = f_pass * t;                // [0, f_pass]
            worst           = std::max(worst, std::abs(dtft(h, fs)));
            const double hp = std::abs(dtft(h, fp));
            m.passband_dev  = std::max(m.passband_dev, std::fabs(hp - 1.0));
            const double hm = std::abs(dtft(h, 0.5 - fp));
            m.complement    = std::max(m.complement, std::fabs(hp * hp + hm * hm - 1.0));
        }
        m.stopband_db = db(worst);
        return m;
    }

    TEST_F(iir_double, EveryStageMeetsItsSpecAtEveryProfileAndDivisor) {
        // Each stage at its divisor d (I13): stopband from 1/2 - f_pass to
        // 1/2 of its higher rate at -(A + 1) dB or below (the designs'
        // margin), f_pass = p / (2 d); the nonlinear variant power
        // complementary, so its passband is flat to 10^(-A/10) / 2.
        for (int pi = 0; pi < 4; ++pi) {
            for (const std::uint64_t d : {1u, 2u, 4u, 8u}) {
                const profile p      = named(pi).relaxed({d, 1});
                const double  a      = p.stopband_atten_db;
                const double  f_pass = p.passband_frac / 2.0;
                const auto    nl     = measure_bands(impulse_response<k_nonlinear>(p), f_pass);
                const auto    nr     = measure_bands(impulse_response<k_near_linear>(p), f_pass);
                std::printf("[ measured ] %s d%llu: nonlinear stopband %.1f dB, passband %.1e, |H|^2 + |H'|^2 - 1 "
                            "%.1e; near-linear stopband %.1f dB, passband %.1e\n",
                            k_names[static_cast<std::size_t>(pi)], static_cast<unsigned long long>(d), nl.stopband_db,
                            nl.passband_dev, nl.complement, nr.stopband_db, nr.passband_dev);
                EXPECT_LE(nl.stopband_db, -(a + 1.0)) << k_names[static_cast<std::size_t>(pi)] << " d" << d;
                EXPECT_LE(nr.stopband_db, -(a + 1.0)) << k_names[static_cast<std::size_t>(pi)] << " d" << d;
                EXPECT_LE(nl.complement, 1e-12) << "power complementary";
                EXPECT_LE(nl.passband_dev, std::pow(10.0, -a / 10.0)) << "flat to 10^(-A/10) / 2 (I3)";
                EXPECT_LE(nr.passband_dev, 2.0 * std::pow(10.0, -a / 20.0)) << "the mirrored stopband's sum";
            }
        }
    }

    TEST_F(iir_double, AliasingAndImagingMeasuredBySineFits) {
        // DspTap's sine fit at the alias (decimating) and the image
        // (interpolating) of a tone: a stopband tone at the higher rate folds
        // to the lower rate's passband at -A dB or below; a passband tone at
        // the lower rate images at its mirror at -A dB or below. Every tone
        // is on the 2048-point grid of the analyzed lower-rate span (4096 at
        // the higher rate), so the image's fit is orthogonal to the passband
        // tone beside it.
        constexpr std::size_t k_span = 2048;
        for (int pi = 0; pi < 4; ++pi) {
            const profile p   = named(pi);
            const double  a   = p.stopband_atten_db;
            const double  fpl = p.passband_frac; // the passband edge, cycles per lower-rate sample
            for (const double t : {0.1, 0.5, 0.95}) {
                const double f_low = std::floor(t * fpl * k_span) / k_span; // cycles per lower-rate sample
                const double f_in  = 0.5 - f_low / 2;                       // the stopband tone at the higher rate
                const auto   x     = sine<double>(4 * k_span, f_in, 0.5);
                basic_iir_stage<double, 1, down2, k_nonlinear>   dn(p);
                basic_iir_stage<double, 1, down2, k_near_linear> dr(p);
                const auto                                       yn = run(dn, x);
                const auto                                       yr = run(dr, x);
                const std::span<const double>                    tn(yn.data() + k_span, k_span);
                const std::span<const double>                    tr(yr.data() + k_span, k_span);
                // At the lower rate the tone at 1/2 - f_low/2 of the higher rate is at 1 - f_low: f_low.
                const double alias_n = db(tap::dsp::analysis::fit_sine(tn, f_low).amplitude / 0.5);
                const double alias_r = db(tap::dsp::analysis::fit_sine(tr, f_low).amplitude / 0.5);
                const auto   lo      = sine<double>(2 * k_span, f_low, 0.5);
                basic_iir_stage<double, 1, up2, k_nonlinear>   un(p);
                basic_iir_stage<double, 1, up2, k_near_linear> ur(p);
                const auto                                     zn = run(un, lo);
                const auto                                     zr = run(ur, lo);
                const std::span<const double>                  sn(zn.data() + 2 * k_span, 2 * k_span);
                const std::span<const double>                  sr(zr.data() + 2 * k_span, 2 * k_span);
                const double image_n = db(tap::dsp::analysis::fit_sine(sn, 0.5 - f_low / 2).amplitude / 0.5);
                const double image_r = db(tap::dsp::analysis::fit_sine(sr, 0.5 - f_low / 2).amplitude / 0.5);
                EXPECT_LE(std::max(alias_n, alias_r), -a) << k_names[static_cast<std::size_t>(pi)] << " " << t;
                EXPECT_LE(std::max(image_n, image_r), -a) << k_names[static_cast<std::size_t>(pi)] << " " << t;
                if (t == 0.95) {
                    std::printf("[ measured ] %s at 0.95 of the edge: alias %.1f / %.1f dB, image %.1f / %.1f dB "
                                "(nonlinear / near-linear)\n",
                                k_names[static_cast<std::size_t>(pi)], alias_n, alias_r, image_n, image_r);
                }
            }
        }
    }

    TEST_F(iir_double, NearLinearPhaseErrorPerStageIsTheMirroredStopband) {
        // The near-linear stage's passband phase error, arg H(f) + 2 pi f 2K,
        // at its divisor: IIR-0 found it equals asin of the stopband at the
        // mirror frequency. Pinned per stage, in degrees; a round trip down
        // and up adds the two stages' errors (one H, twice).
        double worst_70 = 0.0;
        double worst_tr = 0.0;
        for (int pi = 0; pi < 4; ++pi) {
            for (const std::uint64_t d : {1u, 2u, 4u, 8u}) {
                const profile                                  p = named(pi).relaxed({d, 1});
                basic_iir_stage<double, 1, up2, k_near_linear> st(p);
                const auto&                                    des  = st.near_linear_design();
                double                                         err  = 0.0;
                double                                         stop = 0.0;
                const double                                   edge = p.passband_frac / 2.0;
                for (int g = 0; g <= 1024; ++g) {
                    const double f   = edge * static_cast<double>(g) / 1024.0;
                    const auto   h   = tap::dsp::halfband_response(des, f);
                    const double phi = std::arg(h * std::polar(1.0, 2.0 * std::numbers::pi * f * 2.0 * des.delay));
                    err              = std::max(err, std::fabs(phi));
                    stop =
                        std::max(stop, std::asin(std::min(1.0, std::abs(tap::dsp::halfband_response(des, 0.5 - f)))));
                }
                EXPECT_LE(err, stop * (1.0 + 1e-6)) << "the phase error is at most asin of the mirrored stopband";
                (pi < 3 ? worst_70 : worst_tr) = std::max(pi < 3 ? worst_70 : worst_tr, err * 180.0 / std::numbers::pi);
            }
        }
        std::printf("[ measured ] near-linear passband phase error per stage: %.4f deg at the 70 dB profiles, %.6f "
                    "deg at transparent\n",
                    worst_70, worst_tr);
        // A round trip through one stage each way cancels to 2 sin(phi) ...:
        // -60 dB needs 0.057 deg summed, -100 dB 0.00057 deg.
        EXPECT_LE(2.0 * worst_70, 0.057);
        EXPECT_LE(2.0 * worst_tr, 0.00057);
    }

    // ------------------------------------------------------------------
    // Latency (I5)
    // ------------------------------------------------------------------

    TEST_F(iir_double, NominalLatencyAndGroupDelayMatchTheImpulseResponse) {
        for (int pi = 0; pi < 4; ++pi) {
            const profile p = named(pi);
            // Up: the impulse response IS 2 h at the output rate.
            basic_iir_stage<double, 1, up2, k_nonlinear>   un(p);
            basic_iir_stage<double, 1, up2, k_near_linear> ur(p);
            const auto                                     hn = impulse_response<k_nonlinear>(p);
            const auto                                     hr = impulse_response<k_near_linear>(p);
            for (const double t : {0.0, 0.3, 0.7, 1.0}) {
                const double f = t * p.passband_frac / 2.0; // cycles per output frame
                EXPECT_NEAR(measured_group_delay(hn, f), un.group_delay_output_frames(f), 1e-9) << t;
                EXPECT_NEAR(measured_group_delay(hr, f), ur.group_delay_output_frames(f), 1e-9) << t;
                EXPECT_LE(std::fabs(ur.group_delay_output_frames(f) - ur.nominal_latency_output_frames()),
                          ur.latency_deviation_output_frames() + 1e-12);
                EXPECT_LE(un.group_delay_output_frames(f) - un.nominal_latency_output_frames(),
                          un.latency_deviation_output_frames() + 1e-12);
            }
            EXPECT_NEAR(un.nominal_latency_output_frames(), measured_group_delay(hn, 0.0), 1e-9);
            EXPECT_EQ(ur.nominal_latency_output_frames(), ur.delay_path_output_frames().value());
            // Down: half the higher rate's delay, at half the frequency; the
            // decimator's impulse response delays by the same at each f.
            basic_iir_stage<double, 1, down2, k_nonlinear>   dn(p);
            basic_iir_stage<double, 1, down2, k_near_linear> dr(p);
            EXPECT_NEAR(dn.nominal_latency_output_frames(), un.nominal_latency_output_frames() / 2.0, 1e-12);
            EXPECT_EQ(dr.delay_path_output_frames(),
                      (tap::dsp::exact_ratio{static_cast<std::uint64_t>(dr.near_linear_design().delay), 1}));
            EXPECT_EQ(ur.delay_path_output_frames().value(), 2.0 * dr.delay_path_output_frames().value());
        }
        // economy at divisor 1, as the plan states them (2.2, I5): the
        // nonlinear decimator 1.20 frames at DC, 2.48 at the edge; the
        // near-linear one exactly 7 frames within 0.0088.
        basic_iir_stage<double, 1, down2, k_nonlinear>   dn;
        basic_iir_stage<double, 1, down2, k_near_linear> dr;
        EXPECT_NEAR(dn.nominal_latency_output_frames(), 1.20, 0.005);
        EXPECT_NEAR(dn.nominal_latency_output_frames() + dn.latency_deviation_output_frames(), 2.48, 0.005);
        EXPECT_EQ(dr.delay_path_output_frames(), (tap::dsp::exact_ratio{7, 1}));
        EXPECT_NEAR(dr.latency_deviation_output_frames(), 0.0088, 0.0001);
    }

    template <iir_phase P, bool Down>
    void expect_golden_group_delay(const rational_iir_ref::iir_case& c, double& worst) {
        using direction = std::conditional_t<Down, down2, up2>;
        const profile p = named(c.profile).relaxed({static_cast<std::uint64_t>(c.divisor), std::uint64_t{1}});
        const basic_iir_stage<double, 1, direction, P> st(p);
        for (std::size_t k = 0; k < 3; ++k) {
            const double tau = st.group_delay_output_frames(c.gd_f[k]);
            worst            = std::max(worst, std::fabs(tau - c.gd[k]));
            EXPECT_NEAR(tau, c.gd[k], 1e-8) << k_names[static_cast<std::size_t>(c.profile)] << " d" << c.divisor;
            EXPECT_LE(std::fabs(c.gd[k] - st.nominal_latency_output_frames()),
                      st.latency_deviation_output_frames() + 1e-8)
                << "the stated deviation bounds the golden leg's passband delay";
        }
        if constexpr (P == k_nonlinear) {
            EXPECT_NEAR(st.nominal_latency_output_frames(), c.gd[0], 1e-8) << "the nominal is the delay at DC";
        }
    }

    TEST_F(iir_double, GroupDelayMatchesTheGoldenLeg) {
        // The direct form's group delay (scipy.signal.group_delay over the
        // golden leg's b(z) / a(z)) at 0, half and all of each stage's
        // passband edge, against group_delay_output_frames(f): every offered
        // stage, measured 8.5e-11 frames apart at worst.
        double worst = 0.0;
        for (const auto& c : rational_iir_ref::k_cases) {
            if (c.phase == 0) {
                c.down != 0 ? expect_golden_group_delay<k_nonlinear, true>(c, worst)
                            : expect_golden_group_delay<k_nonlinear, false>(c, worst);
            }
            else {
                c.down != 0 ? expect_golden_group_delay<k_near_linear, true>(c, worst)
                            : expect_golden_group_delay<k_near_linear, false>(c, worst);
            }
        }
        std::printf("[ measured ] group delay against the golden leg's: %.3g output frames at worst\n", worst);
    }

    // The round-trip scaffold's view (X10): the near-linear stages and their
    // cascades report an exact delay path, the nonlinear ones none, so
    // tap::dsp::subband_delta takes the first and refuses the second at
    // compile time (compile_fail/iir_nonlinear_subband.cpp pins the refusal).
    static_assert(tap::dsp::exact_delay_stage<basic_iir_stage<float, 1, down2, k_near_linear>>);
    static_assert(tap::dsp::exact_delay_stage<iir_down_by<double, 2, k_near_linear, 16>>);
    static_assert(!tap::dsp::exact_delay_stage<basic_iir_stage<float, 1, down2, k_nonlinear>>);
    static_assert(!tap::dsp::exact_delay_stage<iir_up_by<double, 1, k_nonlinear, 4>>);
    static_assert(tap::dsp::reports_nominal_latency<iir_down_by<float, 1, k_nonlinear, 8>>);

    /// The round trip's op: removes the band, change = -down(x).
    template <typename S>
    struct remove_band {
        using sample = S;
        static constexpr std::size_t channels() noexcept { return 1; }
        std::size_t                  latency_frames() const noexcept { return 0; }
        void                         process(const S* in, std::size_t n, S* change) noexcept {
            for (std::size_t i = 0; i < n; ++i) {
                change[i] = -in[i];
            }
        }
        void        reset() noexcept {}
        std::size_t window_frames() const noexcept { return 0; }
    };

    /// The residual of removing a tone at f (cycles per full-rate sample)
    /// through the near-linear cascades by D, dB re the tone.
    template <std::size_t D>
    double round_trip_residual_db(const profile& p, double f, std::size_t extra_delay = 0) {
        using down_t = iir_down_by<double, 1, k_near_linear, D>;
        using up_t   = iir_up_by<double, 1, k_near_linear, D>;
        tap::dsp::subband_delta<down_t, up_t, remove_band<double>> rt(down_t(p), up_t(p), remove_band<double>{},
                                                                      extra_delay);
        const std::size_t                                          n = 512 * D + 4096;
        const auto                                                 x = sine<double>(n, f, 0.5);
        std::vector<double>                                        y(n);
        rt.process(x.data(), n, y.data());
        // The tone delayed by the aligned latency, minus the round trip, after
        // the transient: what is left at f.
        const std::size_t skip = 256 * D + rt.latency();
        if (extra_delay == 0) {
            const std::span<const double> tail(y.data() + skip, n - skip);
            return db(tap::dsp::analysis::fit_sine(tail, f).amplitude / 0.5);
        }
        return 0.0;
    }

    TEST_F(iir_double, RoundTripCancelsToTheProfilesThreshold) {
        // docs/IIR_PLAN.md question 5: through the near-linear cascades by 2,
        // 4, 8 and 16 and tap::dsp::subband_delta (T from the delay paths,
        // exact), removing a tone of the usable band (up to p of the low
        // rate) leaves it -60 dB down at the 70 dB profiles and -100 dB at
        // transparent: the ×4 / ×8 / ×16 plan's one threshold per profile.
        for (int pi = 0; pi < 4; ++pi) {
            const profile p         = named(pi);
            const double  threshold = pi < 3 ? -60.0 : -100.0;
            double        worst     = -400.0;
            for (const double t : {0.05, 0.3, 0.6, 0.9, 0.99}) {
                worst = std::max(worst, round_trip_residual_db<2>(p, t * p.passband_frac / 2.0));
                worst = std::max(worst, round_trip_residual_db<4>(p, t * p.passband_frac / 4.0));
                worst = std::max(worst, round_trip_residual_db<8>(p, t * p.passband_frac / 8.0));
                worst = std::max(worst, round_trip_residual_db<16>(p, t * p.passband_frac / 16.0));
            }
            std::printf("[ measured ] %s: the near-linear round trip leaves a removed tone %.1f dB down at worst "
                        "(threshold %.0f)\n",
                        k_names[static_cast<std::size_t>(pi)], worst, threshold);
            EXPECT_LE(worst, threshold) << k_names[static_cast<std::size_t>(pi)];
        }
    }

    TEST_F(iir_double, RoundTripNeedsTheExactDelayPath) {
        // T comes from the delay paths; one frame off and the removal fails.
        using down_t    = iir_down_by<double, 1, k_near_linear, 4>;
        using up_t      = iir_up_by<double, 1, k_near_linear, 4>;
        const profile p = profile::economy();
        const down_t  dn(p);
        const up_t    up(p);
        const auto    t = tap::dsp::subband_delta<down_t, up_t, remove_band<double>>::stages_delay_frames(dn, up);
        EXPECT_EQ(t.den, 1u);
        // 4 (3/2 + 7) + 2 (14 + 2 x 3) / 2 ... the composed delay paths, whole.
        EXPECT_EQ(t.num, static_cast<std::uint64_t>(4 * dn.nominal_latency_output_frames()
                                                    + up.nominal_latency_output_frames()));
        const double      f = 0.5 * p.passband_frac / 4.0;
        const std::size_t n = 8192;
        const auto        x = sine<double>(n, f, 0.5);
        // The same subtraction by hand with T + 1: the residual is the tone's
        // difference over one sample, 2 sin(pi f), far above the floor.
        down_t              d1(p);
        up_t                u1(p);
        std::vector<double> low(d1.outputs_for(n));
        low.resize(d1.process(x.data(), n, low.data()));
        std::vector<double> back(u1.outputs_for(low.size()));
        back.resize(u1.process(low.data(), low.size(), back.data()));
        for (const std::int64_t off : {std::int64_t{0}, std::int64_t{1}}) {
            const auto          tt = static_cast<std::size_t>(static_cast<std::int64_t>(t.num) + off);
            std::vector<double> r;
            for (std::size_t i = 2048; i < back.size(); ++i) {
                r.push_back(x[i - tt] - back[i]);
            }
            const double level = db(tap::dsp::analysis::fit_sine(r, f).amplitude / 0.5);
            if (off == 0) {
                EXPECT_LE(level, -60.0);
            }
            else {
                EXPECT_GE(level, -40.0) << "T + 1 does not cancel";
            }
        }
    }

    // ------------------------------------------------------------------
    // Flush (I6)
    // ------------------------------------------------------------------

    /// Whether a stage at p holds a zero-input limit cycle at a few times the
    /// smallest normal under flush-to-zero, so its state never reaches
    /// exactly zero (I6 cannot hold): the floating near-linear stage at
    /// transparent and divisor 1 (order 19, nine second-order sections),
    /// found by IIR-b. Once a section's product falls below the smallest
    /// normal it flushes, and the section, run without it for that sample,
    /// lifts the state again: the mechanism IIR-a found in the guard band
    /// filter's Chebyshev II cascades (iir_kernels.h). Measured 3.7 to 4.1
    /// times the smallest normal in float and double, from 12 to 20 of 39
    /// noise starts. Every other stage drains to exactly zero.
    template <typename S>
    bool holds_normal_limit_cycle(iir_phase phase, const profile& p) {
        return std::is_floating_point_v<S> && phase == k_near_linear && p.stopband_atten_db == 120.0
               && p.passband_frac == 5.0 / 12.0;
    }
    /// The bound pinned for it: eight times the smallest normal.
    template <typename S>
    constexpr double k_normal_residue =
        std::is_floating_point_v<S> ? 8.0 * static_cast<double>(std::numeric_limits<S>::min()) : 0.0;

    /// Where no control bit governs a floating S (double on the M33, float and
    /// double on Hexagon: tap/dsp/subnormals.h), the fixture's flush-to-zero
    /// reaches nothing and the state sticks at the subnormal residue, the
    /// honest limit test_iir_subnormals.cpp pins at 16 times the smallest
    /// subnormal for a stage; through a chain's FIR stages, a few times that.
    template <typename S>
    constexpr bool k_no_flush_control = std::is_floating_point_v<S> && !tap::dsp::k_subnormal_control<S>;
    template <typename S>
    constexpr double k_subnormal_residue =
        std::is_floating_point_v<S> ? 64.0 * static_cast<double>(std::numeric_limits<S>::denorm_min()) : 0.0;

    /// What a stage at p leaves after its flush window here: 0 (an exactly
    /// zero state), the limit cycle's bound, or the subnormal residue.
    template <typename S>
    double flush_residue(iir_phase phase, const profile& p) {
        if constexpr (k_no_flush_control<S>) {
            return k_subnormal_residue<S>;
        }
        else {
            return holds_normal_limit_cycle<S>(phase, p) ? k_normal_residue<S> : 0.0;
        }
    }

    /// flush() equals zero padding bit for bit, and past it the output is
    /// silence and the state exactly zero (under the host's flush-to-zero);
    /// where a stage holds the limit cycle above, both to within `residue`.
    template <typename Conv, typename Make>
    void expect_flush_is_zero_padding(Make make, std::size_t fed_max, double residue = 0.0) {
        using sample     = typename Conv::sample;
        constexpr auto C = Conv::channels();
        const auto     x = noise<sample>(fed_max * C, 0x6161u);
        for (std::size_t fed = fed_max - 5; fed <= fed_max; ++fed) {
            Conv                flushed = make();
            std::vector<sample> scratch(flushed.outputs_for(fed) * C + C);
            flushed.process(x.data(), fed, scratch.data());
            Conv                padded = flushed;
            const std::size_t   expect = flushed.flush_output_frames();
            std::vector<sample> a(expect * C + C, sample{1});
            ASSERT_EQ(flushed.flush(a.data()), expect);
            // The padded run, streamed in blocks of zeros (the double windows
            // run to tens of thousands of frames, past the M55 image's heap
            // if padded whole), until 512 frames past the flush's.
            constexpr std::size_t k_block = 256;
            std::vector<sample>   z(k_block * C, sample{});
            std::vector<sample>   b((padded.outputs_for(k_block) + 1) * C);
            std::size_t           made = 0;
            while (made < expect + 512) {
                const std::size_t got = padded.process(z.data(), k_block, b.data());
                for (std::size_t j = 0; j < got * C; ++j) {
                    const std::size_t i = made * C + j;
                    if (i < expect * C) {
                        if (residue == 0.0) {
                            ASSERT_EQ(a[i], b[j]) << "fed " << fed << " sample " << i;
                        }
                        else {
                            ASSERT_LE(std::fabs(to_unit(a[i]) - to_unit(b[j])), 2.0 * residue) << "fed " << fed;
                        }
                    }
                    else if (residue == 0.0) {
                        ASSERT_EQ(b[j], sample{}) << "fed " << fed << " sample " << i << ": silence past the flush";
                    }
                    else {
                        ASSERT_LE(std::fabs(to_unit(b[j])), residue) << "fed " << fed << ": the residue past the flush";
                    }
                }
                made += got;
            }
        }
    }

    TYPED_TEST(iir_stage_test, FlushEqualsZeroPaddingBitForBit) {
        using sample = TypeParam;
        for_each_shape<sample, 2>([]<typename Stage>() {
            for (int pi = 0; pi < 4; ++pi) {
                const double residue = flush_residue<sample>(Stage::k_phase, named(pi));
                // The stage alone: its flush runs the same block as process(),
                // so flush() is the padded prefix bit for bit even where the
                // state holds a residue; past it, silence or the residue.
                expect_flush_is_zero_padding<Stage>([pi] { return Stage(named(pi)); }, 300, residue);
                Stage               st(named(pi));
                const auto          x = noise<sample>(2 * 300, 0x6262u);
                std::vector<sample> out(st.outputs_for(300) * 2);
                st.process(x.data(), 300, out.data());
                std::vector<sample> tail(st.flush_output_frames() * 2 + 2);
                st.flush(tail.data());
                for (const auto v : st.states()) {
                    if (residue != 0.0) {
                        ASSERT_LE(std::fabs(static_cast<double>(v)), residue) << "the honest limit";
                    }
                    else {
                        ASSERT_EQ(v, typename Stage::state{}) << "an exactly zero state after the window (I6)";
                    }
                }
            }
        });
    }

    TYPED_TEST(iir_stage_test, FlushEqualsZeroPaddingBitForBitInChainsWithFirStages) {
        // Chains holding each IIR variant beside FIR stages (I10's mixed
        // chain, a caller's tap::dsp::chain), and the cascades.
        using sample      = TypeParam;
        const profile eco = profile::economy();
        // Where no bit governs the type, every chain flushes to within the
        // subnormal residue of zero padding, not bit for bit.
        const double none = k_no_flush_control<sample> ? k_subnormal_residue<sample> : 0.0;
        using mixed_down =
            tap::dsp::chain<basic_stage<sample, 1, down3>, basic_iir_stage<sample, 1, down2, k_nonlinear>>;
        expect_flush_is_zero_padding<mixed_down>(
            [&] {
                return mixed_down(basic_stage<sample, 1, down3>(eco.relaxed({2, 1})),
                                  basic_iir_stage<sample, 1, down2, k_nonlinear>(eco));
            },
            600, none);
        using mixed_up = tap::dsp::chain<basic_iir_stage<sample, 1, up2, k_near_linear>, basic_stage<sample, 1, up3>>;
        expect_flush_is_zero_padding<mixed_up>(
            [&] {
                return mixed_up(basic_iir_stage<sample, 1, up2, k_near_linear>(eco),
                                basic_stage<sample, 1, up3>(eco.relaxed({2, 1})));
            },
            200, none);
        using nl8 = iir_down_by<sample, 2, k_nonlinear, 8>;
        expect_flush_is_zero_padding<nl8>([&] { return nl8(eco); }, 800, none);
        using nr8 = iir_up_by<sample, 1, k_near_linear, 8>;
        expect_flush_is_zero_padding<nr8>([&] { return nr8(profile::balanced()); }, 100, none);
        // At transparent the cascade's first stage up is the near-linear
        // divisor-1 stage that holds the limit cycle: tap::dsp::chain's flush
        // feeds each later stage exact zeros where padding feeds it the
        // residue, so the two agree to within it, and past the flush the
        // output is the residue, not silence (the honest limit above).
        expect_flush_is_zero_padding<nr8>([&] { return nr8(profile::transparent()); }, 100,
                                          std::max(none, k_normal_residue<sample>));
    }

    TYPED_TEST(iir_float_test, SilenceDecaysToExactZero) {
        // Under the host's flush-to-zero the floating states reach exactly
        // zero within window_frames() (I9), from a full-scale start, on every
        // leg where a bit governs the type; elsewhere (double on the M33, float
        // and double on Hexagon) the subnormal residue is all that is left.
        using sample = TypeParam;
        for_each_shape<sample, 1>([]<typename Stage>() {
            for (int pi = 0; pi < 4; ++pi) {
                Stage               st(named(pi));
                const auto          x = noise<sample>(1000, 0x8181u, 1.0);
                std::vector<sample> out(st.outputs_for(1000));
                st.process(x.data(), 1000, out.data());
                std::vector<sample> z(st.window_frames(), sample{});
                std::vector<sample> y(st.outputs_for(z.size()));
                st.process(z.data(), z.size(), y.data());
                const double residue = flush_residue<sample>(Stage::k_phase, named(pi));
                for (const auto v : st.states()) {
                    if (residue != 0.0) {
                        ASSERT_LE(std::fabs(static_cast<double>(v)), residue);
                    }
                    else {
                        ASSERT_EQ(v, sample{}) << k_names[static_cast<std::size_t>(pi)];
                    }
                }
            }
        });
    }

    TYPED_TEST(iir_stage_test, NeverTouchesTheFloatingPointEnvironment) {
        using sample = TypeParam;
        for_each_shape<sample, 1>([]<typename Stage>() {
            const std::uint64_t before = tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask;
            Stage               st;
            const auto          x = noise<sample>(200, 0x9999u);
            std::vector<sample> out(st.outputs_for(200));
            st.process(x.data(), 200, out.data());
            std::size_t         fed = 0;
            std::vector<sample> pulled(8);
            st.pull(pulled.data(), 8, [&](sample* dst, std::size_t max) noexcept {
                const std::size_t n = std::min(max, std::size_t{4});
                for (std::size_t i = 0; i < n; ++i) {
                    dst[i] = x[fed + i];
                }
                fed += n;
                return n;
            });
            std::vector<sample> tail(st.flush_output_frames() + 1);
            st.flush(tail.data());
            st.reset();
            EXPECT_EQ(tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask, before);
            EXPECT_TRUE(tap::dsp::subnormals_flushed<sample>() || !tap::dsp::k_subnormal_control<sample>);
        });
    }

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
    // The precondition (I9): a debug assertion (TAP_EXPECTS), compiled in
    // where it can fire and be caught, and only where a bit governs float.
    TEST(IirStageDebug, ThePreconditionFiresWithoutFlushToZero) {
        if constexpr (tap::dsp::k_subnormal_control<float>) {
            basic_iir_stage<float, 1, down2, k_nonlinear> st;
            std::array<float, 2>                          x{0.5f, 0.25f};
            std::array<float, 2>                          y{};
            const std::uint64_t                           saved = tap::sr::rational::test::set_flush_to_zero(false);
            EXPECT_DEATH(static_cast<void>(st.process(x.data(), 2, y.data())), "");
            tap::sr::rational::test::write_floating_point_control(saved);
            const std::uint64_t on = tap::sr::rational::test::set_flush_to_zero(true);
            EXPECT_EQ(st.process(x.data(), 2, y.data()), 1u) << "with the bits set it does not fire";
            tap::sr::rational::test::write_floating_point_control(on);
        }
    }
#endif

    // ------------------------------------------------------------------
    // Fixed point (I8)
    // ------------------------------------------------------------------

    TYPED_TEST(iir_fixed_test, UnityGainAtDc) {
        // Full-scale DC of either sign through every stage settles at full
        // scale within the format's truncation (the allpass sections pass DC
        // at gain 1; floor truncation leaves at most 1 LSB).
        using sample = TypeParam;
        for_each_shape<sample, 1>([]<typename Stage>() {
            for (int pi = 0; pi < 4; ++pi) {
                for (const sample level :
                     {std::numeric_limits<sample>::max(), std::numeric_limits<sample>::min(), sample{12345}}) {
                    Stage               st(named(pi));
                    std::vector<sample> x(4000, level);
                    const auto          y = run(st, x);
                    for (std::size_t i = y.size() / 2; i < y.size(); ++i) {
                        ASSERT_LE(std::llabs(static_cast<long long>(y[i]) - static_cast<long long>(level)), 1)
                            << k_names[static_cast<std::size_t>(pi)];
                    }
                }
            }
        });
    }

    /// A full-scale square wave of the period (higher-rate samples) and a
    /// sine at +12 dB clipped to full scale: the headroom drives (I8).
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
        for (const double f : {0.011, 0.13, 0.23, 0.37}) {
            std::vector<S> x(n);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = from_unit<S>(
                    std::clamp(4.0 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i)), -1.0, 1.0));
            }
            out.push_back(x);
        }
        return out;
    }

    TYPED_TEST(iir_fixed_test, FullScaleDriveNeverWrapsInside) {
        // Every state stays inside its two guard bits (|state| < 4 of full
        // scale) and every output within 2 LSB of the double stage clamped
        // to the format, so nothing wrapped; saturation happens at the output
        // only (the interpolators' overshoot past full scale clamps).
        using sample        = TypeParam;
        double      largest = 0.0;
        std::size_t clipped = 0;
        for_each_shape<sample, 1>([&largest, &clipped]<typename Stage>() {
            using oracle_t =
                basic_iir_stage<double, 1, std::conditional_t<Stage::k_is_decimator, down2, up2>, Stage::k_phase>;
            for (int pi = 0; pi < 4; ++pi) {
                for (const auto& x : full_scale_drives<sample>(1200)) {
                    Stage               st(named(pi));
                    oracle_t            ref(named(pi));
                    std::vector<double> xd(x.size());
                    for (std::size_t i = 0; i < x.size(); ++i) {
                        xd[i] = to_unit(x[i]);
                    }
                    std::vector<sample> y(st.outputs_for(x.size()));
                    std::vector<double> yd(ref.outputs_for(x.size()));
                    double              state_peak = 0.0;
                    for (std::size_t i = 0; i < x.size(); i += 8) {
                        const std::size_t n = std::min<std::size_t>(8, x.size() - i);
                        st.process(x.data() + i, n, y.data() + (i * Stage::k_up / Stage::k_down));
                        for (const auto v : st.states()) {
                            state_peak = std::max(state_peak, std::fabs(static_cast<double>(v)));
                        }
                    }
                    ref.process(xd.data(), xd.size(), yd.data());
                    const double unit = std::is_same_v<sample, std::int16_t> ? 536870912.0 : 1152921504606846976.0;
                    largest           = std::max(largest, state_peak / unit);
                    ASSERT_LT(state_peak / unit, 4.0) << "two guard bits";
                    const double lsb = 1.0 / k_full<sample>;
                    for (std::size_t i = 0; i < y.size(); ++i) {
                        const double clamped = std::clamp(yd[i], -1.0, 1.0 - lsb);
                        ASSERT_LE(std::fabs(to_unit(y[i]) - clamped),
                                  (std::is_same_v<sample, std::int16_t> ? 2.0 * lsb : 1e-8))
                            << k_names[static_cast<std::size_t>(pi)] << " output " << i;
                        if (std::fabs(yd[i]) > 1.0) {
                            // Past full scale the output is the rail, its sign
                            // the reference's: saturated, never wrapped.
                            ASSERT_EQ(y[i], yd[i] > 0.0 ? std::numeric_limits<sample>::max()
                                                        : std::numeric_limits<sample>::min());
                            ++clipped;
                        }
                    }
                }
            }
        });
        std::printf("[ measured ] %s: the largest state under full-scale drive is %.3f of full scale (limit 4); %zu "
                    "outputs past full scale, every one at the rail\n",
                    std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31", largest, clipped);
        EXPECT_GT(clipped, 0u) << "the drives reach the output's saturation";
    }

    TEST_F(iir_double, Q15FloorIsTheOutputsOwnRounding) {
        // A -1 dBFS sine through every Q15 stage: the error against the double
        // stage is the output's own rounding (-101 dB RMS for uniform
        // rounding), the recursion adding nothing measurable (IIR-0: -98.1 dB
        // for the nonlinear decimator at economy).
        double worst = -400.0;
        for_each_shape<std::int16_t, 1>([&worst]<typename Stage>() {
            using oracle_t =
                basic_iir_stage<double, 1, std::conditional_t<Stage::k_is_decimator, down2, up2>, Stage::k_phase>;
            for (int pi = 0; pi < 4; ++pi) {
                const double        amp = std::pow(10.0, -1.0 / 20.0);
                const auto          xq  = sine<std::int16_t>(6000, 0.0123, amp);
                std::vector<double> xd(xq.size());
                for (std::size_t i = 0; i < xq.size(); ++i) {
                    xd[i] = to_unit(xq[i]);
                }
                Stage      st(named(pi));
                oracle_t   ref(named(pi));
                const auto y  = run(st, xq);
                const auto yd = run(ref, xd);
                double     e2 = 0.0;
                for (std::size_t i = 0; i < y.size(); ++i) {
                    const double e = to_unit(y[i]) - yd[i];
                    e2 += e * e;
                }
                const double rms = 10.0 * std::log10(e2 / static_cast<double>(y.size()));
                worst            = std::max(worst, rms);
            }
        });
        std::printf("[ measured ] Q15 floor against the double stage: %.1f dB RMS at worst\n", worst);
        EXPECT_LE(worst, -96.0);
    }

    TYPED_TEST(iir_fixed_test, FixedPointSilenceIsExactlyZeroAfterTheWindow) {
        // After full-scale drive, window_frames() zeros leave an exactly zero
        // state (no zero-input limit cycle: floor truncation in the
        // one-multiplier sections, magnitude truncation in the second-order
        // ones, I8), within the stated count (I6).
        using sample       = TypeParam;
        std::size_t margin = std::numeric_limits<std::size_t>::max();
        for_each_shape<sample, 1>([&margin]<typename Stage>() {
            for (int pi = 0; pi < 4; ++pi) {
                for (const std::uint64_t d : {1u, 2u, 4u, 8u}) {
                    for (const auto& x : full_scale_drives<sample>(900)) {
                        Stage               st(named(pi).relaxed({d, 1}));
                        std::vector<sample> y(st.outputs_for(x.size()));
                        st.process(x.data(), x.size(), y.data());
                        std::size_t frames  = 0;
                        sample      zero[1] = {sample{}};
                        sample      sink[4];
                        while (std::any_of(st.states().begin(), st.states().end(), [](auto v) { return v != 0; })
                               && frames < 100000) {
                            st.process(zero, 1, sink);
                            ++frames;
                        }
                        ASSERT_LE(frames, st.window_frames()) << k_names[static_cast<std::size_t>(pi)] << " d" << d;
                        margin = std::min(margin, st.window_frames() - frames);
                    }
                }
            }
        });
        std::printf("[ measured ] %s: every state reaches exactly zero inside its window (least slack %zu frames)\n",
                    std::is_same_v<sample, std::int16_t> ? "Q15" : "Q31", margin);
    }

    // ------------------------------------------------------------------
    // Honest limits
    // ------------------------------------------------------------------

    TYPED_TEST(iir_float_test, NonFiniteInputPoisonsTheStateUntilReset) {
        // I9, decided: a recursive state never clears a non-finite input on
        // its own (an FIR stage does after one window), and no per-sample
        // check is paid; reset() clears it.
        using sample = TypeParam;
        for_each_shape<sample, 1>([]<typename Stage>() {
            Stage               st;
            std::vector<sample> x(64, sample{});
            x[3] = std::numeric_limits<sample>::quiet_NaN();
            std::vector<sample> y(st.outputs_for(64));
            st.process(x.data(), 64, y.data());
            std::vector<sample> z(4096, sample{});
            std::vector<sample> w(st.outputs_for(z.size()));
            st.process(z.data(), z.size(), w.data());
            EXPECT_TRUE(std::isnan(w.back())) << "still poisoned after 4096 zeros";
            st.reset();
            st.process(z.data(), z.size(), w.data());
            EXPECT_EQ(w.back(), sample{}) << "reset() clears it";
        });
    }

    // ------------------------------------------------------------------
    // The cascades (I13)
    // ------------------------------------------------------------------

    TEST(IirChain, CascadesCostTheRelaxedRow) {
        // docs/IIR_PLAN.md 2.4's relaxed rows, MACs per output frame per
        // channel going down by 2, 4, 8 and 16, and the same over the factor
        // going up; and what the unrelaxed cascade would cost (every stage at
        // the profile's own design).
        const profile eco = profile::economy();
        const profile tr  = profile::transparent();
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 2>(eco).macs_per_output()), 4.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 4>(eco).macs_per_output()), 8.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 8>(eco).macs_per_output()), 16.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 16>(eco).macs_per_output()), 24.0);
        EXPECT_EQ((iir_down_by<float, 1, k_near_linear, 2>(eco).macs_per_output()), 7.0);
        EXPECT_EQ((iir_down_by<float, 1, k_near_linear, 4>(eco).macs_per_output()), 13.0);
        EXPECT_EQ((iir_down_by<float, 1, k_near_linear, 8>(eco).macs_per_output()), 21.0);
        EXPECT_EQ((iir_down_by<float, 1, k_near_linear, 16>(eco).macs_per_output()), 29.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 2>(tr).macs_per_output()), 8.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 4>(tr).macs_per_output()), 16.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 8>(tr).macs_per_output()), 28.0);
        EXPECT_EQ((iir_down_by<float, 1, k_nonlinear, 16>(tr).macs_per_output()), 44.0);
        EXPECT_EQ((iir_up_by<float, 1, k_nonlinear, 16>(eco).macs_per_output()), 24.0 / 16.0);
        EXPECT_EQ((iir_up_by<float, 1, k_near_linear, 8>(eco).macs_per_output()), 21.0 / 8.0);
        // Unrelaxed: the same chain with every stage at economy itself.
        using st = basic_iir_stage<float, 1, down2, k_nonlinear>;
        tap::dsp::chain<st, st, st, st> flat(st{eco}, st{eco}, st{eco}, st{eco});
        double                          total = 0.0;
        total += flat.stage<0>().macs_per_output() * 8 + flat.stage<1>().macs_per_output() * 4
                 + flat.stage<2>().macs_per_output() * 2 + flat.stage<3>().macs_per_output();
        EXPECT_EQ(total, 60.0) << "2.4's unrelaxed row by 16";
    }

    TYPED_TEST(iir_stage_test, CascadeIsItsStagesAtTheirDivisorsBitForBit) {
        using sample      = TypeParam;
        const profile eco = profile::economy();
        using st          = basic_iir_stage<sample, 1, down2, k_near_linear>;
        using up          = basic_iir_stage<sample, 1, up2, k_nonlinear>;
        iir_down_by<sample, 1, k_near_linear, 8> cascade(eco);
        tap::dsp::chain<st, st, st>              manual(st{eco.relaxed({4, 1})}, st{eco.relaxed({2, 1})}, st{eco});
        const auto                               x = noise<sample>(800, 0xabcdu);
        EXPECT_TRUE(run(cascade, x) == run(manual, x));
        EXPECT_EQ(cascade.nominal_latency_output_frames(), manual.nominal_latency_output_frames());
        iir_up_by<sample, 1, k_nonlinear, 4> up4(eco);
        tap::dsp::chain<up, up>              up_manual(up{eco}, up{eco.relaxed({2, 1})});
        EXPECT_TRUE(run(up4, x) == run(up_manual, x));
        EXPECT_EQ((iir_down_by<sample, 1, k_near_linear, 8>::k_divisors), (std::array<std::size_t, 3>{4, 2, 1}));
        EXPECT_EQ((iir_up_by<sample, 1, k_nonlinear, 16>::k_divisors), (std::array<std::size_t, 4>{1, 2, 4, 8}));
    }

} // namespace
