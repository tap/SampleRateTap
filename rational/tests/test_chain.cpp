// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Contract battery for the chains (milestone M4): the design divisors a
// chain assigns its stages (compile-time, the named chains pinned), a chain
// equal to its stages run in sequence bit for bit in every format, the
// committed sequenced-upfirdn vectors (the scipy leg, independent of
// chain<>'s bookkeeping), accounting exact from every position of every
// named chain, chunking, reset, flush as zero padding, the latency as the
// exact rational sum and by impulse, MACs per output as the stages' sum,
// DC gain exact in every format, channels.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)
    using tap::dsp::exact_ratio;

    template <typename S>
    class chain_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(chain_test, sample_types, );

    template <typename S>
    class chain_golden_test : public ::testing::Test {};
    using golden_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(chain_golden_test, golden_types, );

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return static_cast<S>(std::llround(v * static_cast<double>(std::numeric_limits<S>::max())));
        }
    }

    template <typename S>
    std::vector<S> reference_input() {
        std::vector<S> x(rational_ref::k_input.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = to_sample<S>(rational_ref::k_input[i]);
        }
        return x;
    }

    // ------------------------------------------------------------------
    // The design divisors (chain.h, PLAN.md 3.1): a stage's lower rate over
    // the chain's lowest, the largest 2^a 3^b at or below it.
    static_assert(up_2_up_8_up_3<float>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}, exact_ratio{16, 1}});
    static_assert(down_3_down_8_down_2<float>::k_divisors
                  == std::array{exact_ratio{16, 1}, exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(up_2_up_6_up_2<float>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}, exact_ratio{12, 1}});
    static_assert(ratio_3_8_down_3<float>::k_divisors == std::array{exact_ratio{3, 1}, exact_ratio{1, 1}});
    static_assert(up_3_ratio_8_3<float>::k_divisors == std::array{exact_ratio{1, 1}, exact_ratio{3, 1}});
    static_assert(up_2_ratio_4_3_up_3<float>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}, exact_ratio{2, 1}});
    static_assert(down_3_ratio_3_4_down_2<float>::k_divisors
                  == std::array{exact_ratio{2, 1}, exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(ratio_3_4_down_2<float>::k_divisors == std::array{exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(basic_chain<float, up_2>::k_divisors == std::array{exact_ratio{1, 1}});
    // A chain written outside the matrix's rules still gets a divisor: the
    // lattice floor (4/3 then down 2, 12 -> 16 -> 8: the 4/3's lower rate is
    // 12 over r_min 8, 3/2, floored to 1).
    static_assert(basic_chain<float, ratio_4_3, down_2>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{1, 1}});
    // The chain's own ratio, reduced, and its stage count.
    static_assert(up_2_up_8_up_3<float>::k_up == 48 && up_2_up_8_up_3<float>::k_down == 1);
    static_assert(ratio_3_8_down_3<float>::k_up == 1 && ratio_3_8_down_3<float>::k_down == 8);
    static_assert(up_2_ratio_4_3<float>::k_up == 8 && up_2_ratio_4_3<float>::k_down == 3);
    static_assert(down_2_down_6_down_2<float>::k_stages == 3);

    TEST(Chain, DesignDivisorIsTheLatticeFloorOrTheFractionBelowOne) {
        using tap::sr::rational::detail::design_divisor;
        EXPECT_EQ(design_divisor(exact_ratio{1, 1}), (exact_ratio{1, 1}));
        EXPECT_EQ(design_divisor(exact_ratio{441, 80}), (exact_ratio{4, 1}));      // 44.1 / 8: 5.51 -> 4
        EXPECT_EQ(design_divisor(exact_ratio{441, 40}), (exact_ratio{9, 1}));      // 88.2 / 8: 11.025 -> 9
        EXPECT_EQ(design_divisor(exact_ratio{147, 160}), (exact_ratio{147, 160})); // below 1: itself
        EXPECT_EQ(design_divisor(exact_ratio{5, 1}), (exact_ratio{4, 1}));
        EXPECT_EQ(design_divisor(exact_ratio{7, 1}), (exact_ratio{6, 1}));
        EXPECT_EQ(design_divisor(exact_ratio{48, 1}), (exact_ratio{48, 1}));
        EXPECT_EQ(design_divisor(exact_ratio{100, 1}), (exact_ratio{96, 1}));
        EXPECT_EQ(design_divisor(exact_ratio{3, 2}), (exact_ratio{1, 1}));
    }

    TEST(Chain, RelaxedProfileCarriesTheTablesPins) {
        const profile e2 = profile::economy().relaxed(exact_ratio{2, 1});
        EXPECT_DOUBLE_EQ(e2.passband_frac, 3.0 / 16.0);
        EXPECT_EQ(e2.stopband_atten_db, 70.0);
        EXPECT_EQ(e2.pinned_taps_per_branch(2), 7u);
        EXPECT_EQ(e2.pinned_taps_per_branch(3), 5u);
        EXPECT_TRUE(e2.relaxations.empty());
        const profile tb = profile::transparent().relaxed(exact_ratio{147, 160});
        EXPECT_DOUBLE_EQ(tb.passband_frac, 5.0 / 12.0 * 160.0 / 147.0);
        EXPECT_EQ(tb.pinned_taps_per_branch(2), 45u);
        EXPECT_EQ(tb.pinned_taps_per_branch(8), 46u);
        // A divisor without a row: no pins, the search.
        const profile e5 = profile::economy().relaxed(exact_ratio{5, 1});
        EXPECT_EQ(e5.pinned_taps_per_branch(2), 0u);
        EXPECT_DOUBLE_EQ(e5.passband_frac, 3.0 / 40.0);
        // relaxed(1) is the profile itself, pins and tables included.
        const profile e1 = profile::balanced().relaxed(exact_ratio{1, 1});
        EXPECT_EQ(e1.pinned_taps_per_branch(4), 14u);
        EXPECT_EQ(e1.relaxations.size(), k_balanced_relaxations.size());
        // A custom profile relaxes to a search at the scaled passband.
        profile custom         = profile::economy();
        custom.passband_frac   = 0.3;
        custom.taps_per_branch = {0, 0, 0, 0, 0};
        custom.relaxations     = {};
        EXPECT_DOUBLE_EQ(custom.relaxed(exact_ratio{3, 1}).passband_frac, 0.1);
        EXPECT_EQ(custom.relaxed(exact_ratio{3, 1}).pinned_taps_per_branch(2), 0u);
    }

    // ------------------------------------------------------------------
    // A chain is its stages run in sequence, each at the relaxed profile:
    // bit for bit, every format.
    template <typename S, rational_ratio R>
    std::vector<S> run_one_stage(const std::vector<S>& in, const profile& p, exact_ratio divisor) {
        basic_stage<S, R> s(1, p.relaxed(divisor));
        std::vector<S>    out(s.outputs_for(in.size()));
        const std::size_t made = s.process(in.data(), in.size(), out.data());
        out.resize(made);
        return out;
    }

    template <typename S, rational_ratio... Rs>
    std::vector<S> run_stages_in_sequence(const std::vector<S>& x, const profile& p) {
        constexpr auto divisors = basic_chain<S, Rs...>::k_divisors;
        std::vector<S> cur      = x;
        std::size_t    i        = 0;
        ((cur = run_one_stage<S, Rs>(cur, p, divisors[i++])), ...);
        return cur;
    }

    template <typename S, rational_ratio... Rs>
    void check_chain_is_its_stages(const profile& p) {
        const auto            x = reference_input<S>();
        basic_chain<S, Rs...> c(1, p);
        std::vector<S>        y(c.outputs_for(x.size()));
        ASSERT_EQ(c.process(x.data(), x.size(), y.data()), y.size());
        const auto ref = run_stages_in_sequence<S, Rs...>(x, p);
        ASSERT_EQ(y.size(), ref.size());
        EXPECT_TRUE(y == ref);
    }

    TYPED_TEST(chain_test, ChainEqualsItsStagesRunInSequenceBitForBit) {
        using sample = TypeParam;
        check_chain_is_its_stages<sample, up_2, up_2>(profile::economy());
        check_chain_is_its_stages<sample, down_3, down_2>(profile::economy());
        check_chain_is_its_stages<sample, ratio_3_4, down_2>(profile::economy());
        check_chain_is_its_stages<sample, up_2, ratio_4_3>(profile::economy());
        check_chain_is_its_stages<sample, up_2, up_8, up_3>(profile::economy());
        check_chain_is_its_stages<sample, down_3, down_8, down_2>(profile::economy());
        check_chain_is_its_stages<sample, ratio_3_8, down_3>(profile::transparent());
        check_chain_is_its_stages<sample, up_3, ratio_8_3>(profile::super_economy());
        check_chain_is_its_stages<sample, down_2, down_6, down_2>(profile::balanced());
    }

    // ------------------------------------------------------------------
    // The scipy leg: the committed vectors are upfirdn applied in sequence
    // over the relaxed designs (tools/reference/make_reference_vectors.py),
    // compared sample for sample from n = 0 at the single stages'
    // tolerances (float: the float32 coefficient floor, 3e-5; double: the
    // reference's own float32 cast, 1e-7).
    // The up chains run on the first 96 frames of the input (their vectors
    // are L times longer), the down chains on all 480.
    template <typename S, std::size_t N, rational_ratio... Rs>
    void check_matches_scipy(const std::array<float, N>& ref) {
        basic_chain<S, Rs...> c(1, profile::economy());
        constexpr std::size_t n_in = basic_chain<S, Rs...>::k_up > basic_chain<S, Rs...>::k_down ? 96 : 480;
        std::vector<S>        x(rational_ref::k_input.begin(), rational_ref::k_input.begin() + n_in);
        std::vector<S>        y(c.outputs_for(x.size()));
        ASSERT_EQ(c.process(x.data(), x.size(), y.data()), y.size());
        ASSERT_EQ(y.size(), N);
        const double tol   = std::is_same_v<S, float> ? 3e-5 : 1e-7;
        double       worst = 0.0;
        for (std::size_t n = 0; n < N; ++n) {
            worst = std::max(worst, std::fabs(static_cast<double>(y[n]) - static_cast<double>(ref[n])));
        }
        EXPECT_LE(worst, tol);
    }

    TYPED_TEST(chain_golden_test, MatchesSequencedUpfirdn) {
        using sample = TypeParam;
        check_matches_scipy<sample, rational_ref::k_chain_up_2_up_2.size(), up_2, up_2>(
            rational_ref::k_chain_up_2_up_2);
        check_matches_scipy<sample, rational_ref::k_chain_down_3_down_2.size(), down_3, down_2>(
            rational_ref::k_chain_down_3_down_2);
        check_matches_scipy<sample, rational_ref::k_chain_ratio_3_4_down_2.size(), ratio_3_4, down_2>(
            rational_ref::k_chain_ratio_3_4_down_2);
        check_matches_scipy<sample, rational_ref::k_chain_up_2_ratio_4_3.size(), up_2, ratio_4_3>(
            rational_ref::k_chain_up_2_ratio_4_3);
        check_matches_scipy<sample, rational_ref::k_chain_up_2_up_8_up_3.size(), up_2, up_8, up_3>(
            rational_ref::k_chain_up_2_up_8_up_3);
        check_matches_scipy<sample, rational_ref::k_chain_down_3_down_8_down_2.size(), down_3, down_8, down_2>(
            rational_ref::k_chain_down_3_down_8_down_2);
        check_matches_scipy<sample, rational_ref::k_chain_ratio_3_8_down_3.size(), ratio_3_8, down_3>(
            rational_ref::k_chain_ratio_3_8_down_3);
    }

    // ------------------------------------------------------------------
    // Accounting from every position of the chain's superblock (k_up k_down
    // inputs): outputs_for(n) is exactly what process(n) writes, and
    // frames_needed(k) the smallest n with outputs_for(n) >= k.
    // One superblock of positions (period inputs), probes up to a
    // superblock and two more: every phase of every stage is visited, at a
    // cost the M33 leg can carry (period is 48 for the by-48 chains).
    template <typename Chain>
    void check_accounting_from_every_position() {
        constexpr std::size_t period = Chain::k_up * Chain::k_down;
        constexpr std::size_t probe  = period + 2;
        Chain                 c(1);
        std::vector<float>    x((probe + 2) * Chain::k_down + 8, 0.25f);
        std::vector<float>    y(c.outputs_for(x.size()) + 8);
        for (std::size_t pos = 0; pos < period; ++pos) {
            for (std::size_t n = 0; n <= probe; ++n) {
                Chain probe_chain = c;
                EXPECT_EQ(probe_chain.process(x.data(), n, y.data()), c.outputs_for(n)) << "pos " << pos << " n " << n;
            }
            EXPECT_EQ(c.frames_needed(0), 0u);
            for (std::size_t k = 1; k <= probe; ++k) {
                const std::size_t n = c.frames_needed(k);
                EXPECT_GE(c.outputs_for(n), k) << "pos " << pos << " k " << k;
                EXPECT_LT(c.outputs_for(n - 1), k) << "pos " << pos << " k " << k;
                Chain probe_chain = c;
                ASSERT_LE(n, x.size());
                EXPECT_GE(probe_chain.process(x.data(), n, y.data()), k) << "pos " << pos << " k " << k;
            }
            c.process(x.data(), 1, y.data());
        }
    }

    TEST(Chain, AccountingExactFromEveryPositionOfEveryNamedChain) {
        check_accounting_from_every_position<up_2_up_2<float>>();
        check_accounting_from_every_position<up_2_up_3<float>>();
        check_accounting_from_every_position<up_2_ratio_4_3_up_3<float>>();
        check_accounting_from_every_position<up_3_ratio_8_3<float>>();
        check_accounting_from_every_position<up_2_up_6<float>>();
        check_accounting_from_every_position<up_2_up_8<float>>();
        check_accounting_from_every_position<up_2_up_6_up_2<float>>();
        check_accounting_from_every_position<up_2_up_8_up_2<float>>();
        check_accounting_from_every_position<up_2_up_8_up_3<float>>();
        check_accounting_from_every_position<up_2_ratio_4_3<float>>();
        check_accounting_from_every_position<ratio_3_4_down_2<float>>();
        check_accounting_from_every_position<down_2_down_2<float>>();
        check_accounting_from_every_position<down_3_down_2<float>>();
        check_accounting_from_every_position<down_3_ratio_3_4_down_2<float>>();
        check_accounting_from_every_position<ratio_3_8_down_3<float>>();
        check_accounting_from_every_position<down_6_down_2<float>>();
        check_accounting_from_every_position<down_8_down_2<float>>();
        check_accounting_from_every_position<down_2_down_6_down_2<float>>();
        check_accounting_from_every_position<down_2_down_8_down_2<float>>();
        check_accounting_from_every_position<down_3_down_8_down_2<float>>();
    }

    // ------------------------------------------------------------------
    // Bit-exact repeatability: any chunking, reset.
    template <typename S, rational_ratio... Rs>
    void check_chunking(const std::vector<S>& x) {
        using chain_type = basic_chain<S, Rs...>;
        chain_type     whole(1);
        std::vector<S> ref(whole.outputs_for(x.size()));
        whole.process(x.data(), x.size(), ref.data());
        for (const std::size_t chunk : {1u, 7u, 63u, 64u, 65u, 200u}) {
            chain_type     c(1);
            std::vector<S> y;
            std::vector<S> buf(c.outputs_for(chunk) + chain_type::k_up + 2);
            for (std::size_t pos = 0; pos < x.size(); pos += chunk) {
                const std::size_t n    = pos + chunk <= x.size() ? chunk : x.size() - pos;
                const std::size_t want = c.outputs_for(n);
                ASSERT_LE(want, buf.size());
                ASSERT_EQ(c.process(x.data() + pos, n, buf.data()), want) << "chunk " << chunk << " at " << pos;
                y.insert(y.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(want));
            }
            EXPECT_TRUE(y == ref) << "chunk " << chunk;
        }
    }

    TYPED_TEST(chain_test, ChunkingIsBitIdentical) {
        using sample = TypeParam;
        const auto x = reference_input<sample>();
        check_chunking<sample, up_2, up_8, up_3>(x);
        check_chunking<sample, down_3, down_8, down_2>(x);
        check_chunking<sample, ratio_3_8, down_3>(x);
    }

    // A chain of L-phase stages and one of decimators (left at decimation
    // phases 1, 4 and 1 by 100 frames; the audit's F12).
    template <typename C>
    void check_chain_reset_reproduces(const std::vector<typename C::sample>& x) {
        using sample = typename C::sample;
        C                   c(1);
        std::vector<sample> a(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), a.data());
        std::vector<sample> scratch(c.outputs_for(100));
        c.process(x.data(), 100, scratch.data()); // mid-stream
        c.reset();
        std::vector<sample> b(c.outputs_for(x.size()));
        ASSERT_EQ(a.size(), b.size());
        c.process(x.data(), x.size(), b.data());
        EXPECT_TRUE(a == b);
    }

    TYPED_TEST(chain_test, ResetReproducesBitExactly) {
        using sample = TypeParam;
        const auto x = reference_input<sample>();
        check_chain_reset_reproduces<up_2_ratio_4_3_up_3<sample>>(x);
        check_chain_reset_reproduces<down_3_down_8_down_2<sample>>(x);
    }

    // A stage's pull() can stop with up to L - 1 outputs banked, outside the
    // bound tap::dsp::chain sizes its scratch by (stage.h's contract); a
    // chain therefore resets the stages it is given and exposes them
    // read-only. Pinned on the audit's scenario (2026-10, F01): an up-by-8
    // stage pulled one frame, then moved into a chain, equals a chain built
    // from fresh stages bit for bit, and the chain's accounting is the fresh
    // chain's. (Under the sanitizer leg, the overflow this guards against
    // was a heap-buffer-overflow in process().)
    TEST(Chain, APulledStageMovedIntoAChainIsReset) {
        using stage_8        = basic_stage<float, up_8>;
        using stage_2        = basic_stage<float, up_2>;
        using chain_t        = tap::dsp::chain<stage_8, stage_2>;
        const auto         x = reference_input<float>();
        stage_8            used(1, profile::economy());
        std::vector<float> one(8);
        std::size_t        fed = 0;
        auto               pop = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            const std::size_t n = max_frames < x.size() - fed ? max_frames : x.size() - fed;
            std::copy_n(x.data() + fed, n, dst);
            fed += n;
            return n;
        };
        ASSERT_EQ(used.pull(one.data(), 1, pop), 1u); // leaves 7 outputs banked
        ASSERT_GT(used.outputs_for(64), 8u * 64u);    // past the process-reached bound
        chain_t from_used(1, std::move(used), stage_2(1, profile::economy()));
        chain_t fresh(1, stage_8(1, profile::economy()), stage_2(1, profile::economy()));
        ASSERT_EQ(from_used.outputs_for(64), fresh.outputs_for(64));
        EXPECT_EQ(from_used.frames_needed(100), fresh.frames_needed(100));
        std::vector<float> a(fresh.outputs_for(x.size())), b(a.size());
        ASSERT_EQ(fresh.process(x.data(), x.size(), a.data()), a.size());
        ASSERT_EQ(from_used.process(x.data(), x.size(), b.data()), b.size());
        EXPECT_TRUE(a == b);
    }
    template <typename C>
    concept hands_out_a_mutable_stage = requires(C& c) { c.template stage<0>().reset(); };
    static_assert(!hands_out_a_mutable_stage<up_2_ratio_4_3_up_3<float>>);

    // ------------------------------------------------------------------
    // flush: flush_output_frames() frames, equal to zero-padding the chain's
    // input bit for bit (a prefix of the padded stream), and silence after.
    template <typename S, rational_ratio... Rs>
    void check_flush(const profile& p) {
        using chain_type = basic_chain<S, Rs...>;
        const auto     x = reference_input<S>();
        chain_type     c(1, p);
        std::vector<S> y(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), y.data());
        chain_type        padded = c;
        const std::size_t expect = c.flush_output_frames();
        std::vector<S>    tail(expect + 4, to_sample<S>(0.5));
        ASSERT_EQ(c.flush(tail.data()), expect);
        const std::size_t zeros = 2 * padded.frames_needed(expect + 1) + 64;
        std::vector<S>    z(zeros, tap::dsp::sample_traits<S>::silence());
        std::vector<S>    yz(padded.outputs_for(zeros));
        const std::size_t made = padded.process(z.data(), zeros, yz.data());
        ASSERT_GT(made, expect);
        for (std::size_t n = 0; n < expect; ++n) {
            ASSERT_EQ(yz[n], tail[n]) << n;
        }
        for (std::size_t n = expect; n < made; ++n) {
            ASSERT_EQ(yz[n], tap::dsp::sample_traits<S>::silence()) << n;
        }
    }

    TYPED_TEST(chain_test, FlushEqualsZeroPaddingBitForBit) {
        using sample = TypeParam;
        check_flush<sample, up_2, up_2>(profile::economy());
        check_flush<sample, down_3, down_2>(profile::economy());
        check_flush<sample, ratio_3_4, down_2>(profile::economy());
        check_flush<sample, up_2, up_8, up_3>(profile::economy());
        check_flush<sample, down_3, down_8, down_2>(profile::transparent());
        check_flush<sample, ratio_3_8, down_3>(profile::economy());
    }

    // ------------------------------------------------------------------
    // Latency: the exact rational sum of the stages' (N - 1) / 2 at the
    // output rate, and the impulse peaks within half a frame of it.
    template <typename Chain>
    void check_latency_by_impulse(exact_ratio expect, double out_rate_hz) {
        Chain c(1);
        EXPECT_EQ(c.latency_output_frames(), expect);
        EXPECT_NEAR(c.latency_seconds(out_rate_hz), expect.value() / out_rate_hz, 1e-15);
        const std::size_t  n_in = c.frames_needed(static_cast<std::size_t>(2.0 * expect.value()) + 16);
        std::vector<float> x(n_in, 0.0f);
        x[0] = 1.0f;
        std::vector<float> y(c.outputs_for(n_in));
        c.process(x.data(), n_in, y.data());
        std::size_t peak = 0;
        for (std::size_t n = 1; n < y.size(); ++n) {
            if (std::fabs(y[n]) > std::fabs(y[peak])) {
                peak = n;
            }
        }
        EXPECT_LE(std::fabs(static_cast<double>(peak) - expect.value()), 0.5 + 1e-9) << "peak " << peak;
    }

    TEST(Chain, LatencyIsTheExactRationalSumAndMatchesTheImpulse) {
        // Economy: up 2 (N = 43: 21 frames at 2 r) then up 8 at divisor 2
        // (m = 4, N = 63: 31 at 16 r) then up 3 at divisor 16 (m = 3,
        // N = 17: 8 at 48 r): 21 * 24 + 31 * 3 + 8 = 605... measured 629
        // with the relaxation table's m (up 8 at 2: m = 5, N = 79: 39 at
        // 16 r -> 39 * 3 = 117; 504 + 117 + 8 = 629).
        check_latency_by_impulse<up_2_up_8_up_3<float>>(exact_ratio{629, 1}, 384000.0);
        check_latency_by_impulse<down_3_down_8_down_2<float>>(exact_ratio{629, 48}, 8000.0);
        check_latency_by_impulse<ratio_3_8_down_3<float>>(exact_ratio{295, 24}, 11025.0);
        check_latency_by_impulse<up_3_ratio_8_3<float>>(exact_ratio{295, 3}, 88200.0);
        check_latency_by_impulse<up_2_up_2<float>>(exact_ratio{55, 1}, 32000.0);
        check_latency_by_impulse<down_2_down_2<float>>(exact_ratio{55, 4}, 8000.0);
    }

    // ------------------------------------------------------------------
    // MACs per output: the stages' trimmed-row counts scaled by their output
    // rates over the chain's (the matrix's numbers; test_matrix.cpp pins
    // every row, this pins the shape of the sum).
    TEST(Chain, MacsPerOutputIsTheStagesSumAtTheOutputRate) {
        up_2_up_8_up_3<float> up(1);
        // 23/2 per output at 2 r (over 24 for the chain's rate) + (2 * 5 * 7 + 1) / 8 at 16 r (over 3) + (2 * 3 * 2 +
        // 1) / 3
        EXPECT_EQ(up.macs_per_output_exact(), (exact_ratio{373, 48}));
        EXPECT_DOUBLE_EQ(up.macs_per_output(), 373.0 / 48.0);
        down_3_down_8_down_2<float> down(1);
        EXPECT_EQ(down.macs_per_output_exact(), (exact_ratio{373, 1})); // 13 * 16 + 71 * 2 + 23
        basic_chain<float, up_2> one(1);
        EXPECT_EQ(one.macs_per_output_exact(), (exact_ratio{23, 2}));
        EXPECT_DOUBLE_EQ(one.macs_per_output(), (basic_stage<float, up_2>(1).macs_per_output()));
    }

    // ------------------------------------------------------------------
    // DC gain 1 through a chain: exact in the fixed-point formats (every
    // row sums to unity exactly, so a constant propagates exactly through
    // every stage), within rounding of the three stages' sums in float and
    // double (the single stages' bounds, test_stage.cpp, times the stages).
    TYPED_TEST(chain_test, DcGainIsExactlyOne) {
        using sample                       = TypeParam;
        const sample                    dc = to_sample<sample>(0.5);
        down_3_ratio_3_4_down_2<sample> c(1);
        const std::size_t               n_in = c.frames_needed(400);
        std::vector<sample>             x(n_in, dc);
        std::vector<sample>             y(c.outputs_for(n_in));
        const std::size_t               made = c.process(x.data(), n_in, y.data());
        ASSERT_GE(made, 400u);
        for (std::size_t n = made - 100; n < made; ++n) {
            if constexpr (std::is_same_v<sample, double>) {
                EXPECT_NEAR(y[n], 0.5, 3e-14) << n;
            }
            else if constexpr (std::is_same_v<sample, float>) {
                EXPECT_NEAR(y[n], 0.5f, 6e-7f) << n;
            }
            else {
                EXPECT_EQ(y[n], dc) << n;
            }
        }
    }

    TEST(Chain, TwoChannelsAreIndependentAndEqualTheMonoRuns) {
        const auto         x = reference_input<float>();
        std::vector<float> left(x);
        std::vector<float> right(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            right[i] = -0.5f * x[(i * 7) % x.size()];
        }
        std::vector<float> stereo(2 * x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            stereo[2 * i]     = left[i];
            stereo[2 * i + 1] = right[i];
        }
        up_2_up_6<float>   c(2);
        std::vector<float> y(2 * c.outputs_for(x.size()));
        const std::size_t  made = c.process(stereo.data(), x.size(), y.data());
        up_2_up_6<float>   l(1);
        up_2_up_6<float>   r(1);
        std::vector<float> yl(l.outputs_for(x.size()));
        std::vector<float> yr(r.outputs_for(x.size()));
        ASSERT_EQ(l.process(left.data(), x.size(), yl.data()), made);
        ASSERT_EQ(r.process(right.data(), x.size(), yr.data()), made);
        for (std::size_t n = 0; n < made; ++n) {
            ASSERT_EQ(y[2 * n], yl[n]) << n;
            ASSERT_EQ(y[2 * n + 1], yr[n]) << n;
        }
        EXPECT_EQ(c.channels(), 2u);
    }

    TEST(Chain, ZeroChannelsThrows) {
        EXPECT_THROW((up_2_up_2<float>(0)), std::invalid_argument);
    }

} // namespace
