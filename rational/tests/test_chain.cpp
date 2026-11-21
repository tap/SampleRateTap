// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Contract battery for the chains (milestone M4): the design divisors a
// chain assigns its stages (compile-time, the named chains pinned), a chain
// equal to its stages run in sequence bit for bit in every format, the
// committed sequenced-upfirdn vectors (the scipy leg, independent of
// chain<1>'s bookkeeping), accounting exact from every position of every
// named chain, chunking, reset, flush as zero padding, the latency as the
// exact rational sum and by impulse, MACs per output as the stages' sum,
// the chains by 4, 8 and 16 (the aliases: their matrix rows and the
// cheapest chains of their ratios), DC gain exact in every format,
// channels.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "coverage/matrix_rows.h"
#include "reference/reference_vectors.h"
#include "tap/sr/rational/rational.h"
#include "tap_sr_test/expect_rejected.h"

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
    static_assert(up3_up4_up4<float, 1>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{3, 1}, exact_ratio{12, 1}});
    static_assert(down4_down4_down3<float, 1>::k_divisors
                  == std::array{exact_ratio{12, 1}, exact_ratio{3, 1}, exact_ratio{1, 1}});
    static_assert(up2_up3_up4<float, 1>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}, exact_ratio{6, 1}});
    static_assert(down2_down4_down2<float, 1>::k_divisors
                  == std::array{exact_ratio{8, 1}, exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(down4_down2<float, 1>::k_divisors == std::array{exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(up2_up4<float, 1>::k_divisors == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}});
    static_assert(ratio3over4_down2<float, 1>::k_divisors == std::array{exact_ratio{2, 1}, exact_ratio{1, 1}});
    // Chains through the mixed ratios that no matrix row runs since X-a3:
    // chains all the same, each stage at its divisor.
    static_assert(basic_chain<float, 1, ratio3over8, down3>::k_divisors
                  == std::array{exact_ratio{3, 1}, exact_ratio{1, 1}});
    static_assert(basic_chain<float, 1, up3, ratio8over3>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{3, 1}});
    static_assert(basic_chain<float, 1, up2, ratio4over3, up3>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{2, 1}, exact_ratio{2, 1}});
    static_assert(basic_chain<float, 1, down3, ratio3over4, down2>::k_divisors
                  == std::array{exact_ratio{2, 1}, exact_ratio{2, 1}, exact_ratio{1, 1}});
    static_assert(basic_chain<float, 1, up2>::k_divisors == std::array{exact_ratio{1, 1}});
    // A chain written outside the matrix's rules still gets a divisor: the
    // lattice floor (4/3 then down 2, 12 -> 16 -> 8: the 4/3's lower rate is
    // 12 over r_min 8, 3/2, floored to 1).
    static_assert(basic_chain<float, 1, ratio4over3, down2>::k_divisors
                  == std::array{exact_ratio{1, 1}, exact_ratio{1, 1}});
    // The chain's own ratio, reduced, and its stage count.
    static_assert(up3_up4_up4<float, 1>::k_up == 48 && up3_up4_up4<float, 1>::k_down == 1);
    static_assert(down4_down2<float, 1>::k_up == 1 && down4_down2<float, 1>::k_down == 8);
    static_assert(up2_ratio4over3<float, 1>::k_up == 8 && up2_ratio4over3<float, 1>::k_down == 3);
    static_assert(down4_down3_down2<float, 1>::k_stages == 3);

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
        EXPECT_EQ(e2.pinned_taps_per_branch(2), 5u);
        EXPECT_EQ(e2.pinned_taps_per_branch(3), 4u);
        EXPECT_TRUE(e2.relaxations.empty());
        const profile tb = profile::transparent().relaxed(exact_ratio{147, 160});
        EXPECT_DOUBLE_EQ(tb.passband_frac, 5.0 / 12.0 * 160.0 / 147.0);
        EXPECT_EQ(tb.pinned_taps_per_branch(2), 44u);
        EXPECT_EQ(tb.pinned_taps_per_branch(8), 44u);
        // A divisor without a row: no pins, the search.
        const profile e5 = profile::economy().relaxed(exact_ratio{5, 1});
        EXPECT_EQ(e5.pinned_taps_per_branch(2), 0u);
        EXPECT_DOUBLE_EQ(e5.passband_frac, 3.0 / 40.0);
        // relaxed(1) is the profile itself, pins and tables included.
        const profile e1 = profile::balanced().relaxed(exact_ratio{1, 1});
        EXPECT_EQ(e1.pinned_taps_per_branch(4), 12u);
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
        basic_stage<S, 1, R> s(p.relaxed(divisor));
        std::vector<S>       out(s.outputs_for(in.size()));
        const std::size_t    made = s.process(in.data(), in.size(), out.data());
        out.resize(made);
        return out;
    }

    template <typename S, rational_ratio... Rs>
    std::vector<S> run_stages_in_sequence(const std::vector<S>& x, const profile& p) {
        constexpr auto divisors = basic_chain<S, 1, Rs...>::k_divisors;
        std::vector<S> cur      = x;
        std::size_t    i        = 0;
        ((cur = run_one_stage<S, Rs>(cur, p, divisors[i++])), ...);
        return cur;
    }

    template <typename S, rational_ratio... Rs>
    void check_chain_is_its_stages(const profile& p) {
        const auto               x = reference_input<S>();
        basic_chain<S, 1, Rs...> c(p);
        std::vector<S>           y(c.outputs_for(x.size()));
        ASSERT_EQ(c.process(x.data(), x.size(), y.data()), y.size());
        const auto ref = run_stages_in_sequence<S, Rs...>(x, p);
        ASSERT_EQ(y.size(), ref.size());
        EXPECT_TRUE(y == ref);
    }

    TYPED_TEST(chain_test, ChainEqualsItsStagesRunInSequenceBitForBit) {
        using sample = TypeParam;
        check_chain_is_its_stages<sample, up2, up2>(profile::economy());
        check_chain_is_its_stages<sample, down3, down2>(profile::economy());
        check_chain_is_its_stages<sample, ratio3over4, down2>(profile::economy());
        check_chain_is_its_stages<sample, up2, ratio4over3>(profile::economy());
        check_chain_is_its_stages<sample, up2, up8, up3>(profile::economy());
        check_chain_is_its_stages<sample, down3, down8, down2>(profile::economy());
        check_chain_is_its_stages<sample, up3, up4, up4>(profile::economy());
        check_chain_is_its_stages<sample, down4, down4, down3>(profile::economy());
        check_chain_is_its_stages<sample, down2, down4, down2>(profile::transparent());
        check_chain_is_its_stages<sample, ratio3over8, down3>(profile::transparent());
        check_chain_is_its_stages<sample, up3, ratio8over3>(profile::super_economy());
        check_chain_is_its_stages<sample, down2, down6, down2>(profile::balanced());
        // The 120 dB tier's own chains (X4), at the profile that selects them.
        check_chain_is_its_stages<sample, up2, ratio3over2>(profile::transparent());
        check_chain_is_its_stages<sample, up2, up6, up4>(profile::transparent());
        check_chain_is_its_stages<sample, down4, down6, down2>(profile::transparent());
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
        basic_chain<S, 1, Rs...> c(profile::economy());
        constexpr std::size_t    n_in = basic_chain<S, 1, Rs...>::k_up > basic_chain<S, 1, Rs...>::k_down ? 96 : 480;
        std::vector<S>           x(rational_ref::k_input.begin(), rational_ref::k_input.begin() + n_in);
        std::vector<S>           y(c.outputs_for(x.size()));
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
        check_matches_scipy<sample, rational_ref::k_chain_up2_up2.size(), up2, up2>(rational_ref::k_chain_up2_up2);
        check_matches_scipy<sample, rational_ref::k_chain_down3_down2.size(), down3, down2>(
            rational_ref::k_chain_down3_down2);
        check_matches_scipy<sample, rational_ref::k_chain_ratio3over4_down2.size(), ratio3over4, down2>(
            rational_ref::k_chain_ratio3over4_down2);
        check_matches_scipy<sample, rational_ref::k_chain_up2_ratio4over3.size(), up2, ratio4over3>(
            rational_ref::k_chain_up2_ratio4over3);
        check_matches_scipy<sample, rational_ref::k_chain_up3_up4_up4.size(), up3, up4, up4>(
            rational_ref::k_chain_up3_up4_up4);
        check_matches_scipy<sample, rational_ref::k_chain_down4_down4_down3.size(), down4, down4, down3>(
            rational_ref::k_chain_down4_down4_down3);
        check_matches_scipy<sample, rational_ref::k_chain_down4_down2.size(), down4, down2>(
            rational_ref::k_chain_down4_down2);
        check_matches_scipy<sample, rational_ref::k_chain_up2_up4.size(), up2, up4>(rational_ref::k_chain_up2_up4);
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
        Chain                 c;
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
        check_accounting_from_every_position<up2_ratio3over2<float, 1>>();
        check_accounting_from_every_position<up2_up2<float, 1>>();
        check_accounting_from_every_position<up2_up3<float, 1>>();
        check_accounting_from_every_position<up2_up4<float, 1>>();
        check_accounting_from_every_position<up3_up4<float, 1>>();
        check_accounting_from_every_position<up2_up6<float, 1>>();
        check_accounting_from_every_position<up2_up4_up2<float, 1>>();
        check_accounting_from_every_position<up2_up8<float, 1>>();
        check_accounting_from_every_position<up2_up3_up4<float, 1>>();
        check_accounting_from_every_position<up2_up4_up4<float, 1>>();
        check_accounting_from_every_position<up3_up4_up4<float, 1>>();
        check_accounting_from_every_position<up2_up6_up4<float, 1>>();
        check_accounting_from_every_position<up2_ratio4over3<float, 1>>();
        check_accounting_from_every_position<ratio3over4_down2<float, 1>>();
        check_accounting_from_every_position<down2_down2<float, 1>>();
        check_accounting_from_every_position<down3_down2<float, 1>>();
        check_accounting_from_every_position<down4_down2<float, 1>>();
        check_accounting_from_every_position<down4_down3<float, 1>>();
        check_accounting_from_every_position<down6_down2<float, 1>>();
        check_accounting_from_every_position<down2_down4_down2<float, 1>>();
        check_accounting_from_every_position<down8_down2<float, 1>>();
        check_accounting_from_every_position<down4_down3_down2<float, 1>>();
        check_accounting_from_every_position<down4_down4_down2<float, 1>>();
        check_accounting_from_every_position<down4_down4_down3<float, 1>>();
        check_accounting_from_every_position<down4_down6_down2<float, 1>>();
    }

    // ------------------------------------------------------------------
    // Bit-exact repeatability: any chunking, reset.
    template <typename S, rational_ratio... Rs>
    void check_chunking(const std::vector<S>& x) {
        using chain_type = basic_chain<S, 1, Rs...>;
        chain_type     whole;
        std::vector<S> ref(whole.outputs_for(x.size()));
        whole.process(x.data(), x.size(), ref.data());
        for (const std::size_t chunk : {1u, 7u, 63u, 64u, 65u, 200u}) {
            chain_type     c;
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
        check_chunking<sample, up3, up4, up4>(x);
        check_chunking<sample, down4, down4, down3>(x);
        check_chunking<sample, ratio3over8, down3>(x);
    }

    // A chain of L-phase stages and one of decimators (left at decimation
    // phases 1, 4 and 1 by 100 frames; the audit's F12).
    template <typename C>
    void check_chain_reset_reproduces(const std::vector<typename C::sample>& x) {
        using sample = typename C::sample;
        C                   c;
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
        check_chain_reset_reproduces<basic_chain<sample, 1, up2, ratio4over3, up3>>(x);
        check_chain_reset_reproduces<basic_chain<sample, 1, down3, down8, down2>>(x);
        check_chain_reset_reproduces<down4_down4_down3<sample, 1>>(x);
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
        using stage_8        = basic_stage<float, 1, up8>;
        using stage_2        = basic_stage<float, 1, up2>;
        using chain_t        = tap::dsp::chain<stage_8, stage_2>;
        const auto         x = reference_input<float>();
        stage_8            used(profile::economy());
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
        chain_t from_used(std::move(used), stage_2(profile::economy()));
        chain_t fresh(stage_8{profile::economy()}, stage_2{profile::economy()});
        ASSERT_EQ(from_used.outputs_for(64), fresh.outputs_for(64));
        EXPECT_EQ(from_used.frames_needed(100), fresh.frames_needed(100));
        std::vector<float> a(fresh.outputs_for(x.size())), b(a.size());
        ASSERT_EQ(fresh.process(x.data(), x.size(), a.data()), a.size());
        ASSERT_EQ(from_used.process(x.data(), x.size(), b.data()), b.size());
        EXPECT_TRUE(a == b);
    }
    template <typename C>
    concept hands_out_a_mutable_stage = requires(C& c) { c.template stage<0>().reset(); };
    static_assert(!hands_out_a_mutable_stage<up3_up4_up4<float, 1>>);

    // ------------------------------------------------------------------
    // flush: flush_output_frames() frames, equal to zero-padding the chain's
    // input bit for bit (a prefix of the padded stream), and silence after.
    template <typename S, rational_ratio... Rs>
    void check_flush(const profile& p) {
        using chain_type = basic_chain<S, 1, Rs...>;
        const auto     x = reference_input<S>();
        chain_type     c(p);
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
        check_flush<sample, up2, up2>(profile::economy());
        check_flush<sample, down3, down2>(profile::economy());
        check_flush<sample, ratio3over4, down2>(profile::economy());
        check_flush<sample, up2, up8, up3>(profile::economy());
        check_flush<sample, down3, down8, down2>(profile::transparent());
        check_flush<sample, ratio3over8, down3>(profile::economy());
        check_flush<sample, up3, up4, up4>(profile::economy());
        check_flush<sample, down4, down4, down3>(profile::transparent());
        check_flush<sample, down4, down6, down2>(profile::transparent());
    }

    // ------------------------------------------------------------------
    // Latency: the exact rational sum of the stages' (N - 1) / 2 at the
    // output rate, and the impulse peaks within half a frame of it.
    template <typename Chain>
    void check_latency_by_impulse(exact_ratio expect, double out_rate_hz) {
        Chain c;
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
        // Economy, the relaxation tables' m (X17's pins): up 3 (m = 10,
        // N = 59: 29 frames at 3 r) then up 4 at divisor 3 (m = 4, N = 31:
        // 15 at 12 r) then up 4 at divisor 12 (m = 3, N = 23: 11 at 48 r):
        // 29 * 16 + 15 * 4 + 11 = 464 + 60 + 11 = 535.
        check_latency_by_impulse<up3_up4_up4<float, 1>>(exact_ratio{535, 1}, 384000.0);
        check_latency_by_impulse<down4_down4_down3<float, 1>>(exact_ratio{535, 48}, 8000.0);
        check_latency_by_impulse<up2_up4<float, 1>>(exact_ratio{91, 1}, 96000.0);
        check_latency_by_impulse<down4_down2<float, 1>>(exact_ratio{91, 8}, 11025.0);
        // A chain with a stage at the searched divisor 16 (no row of the
        // tables since X-a3), and the mixed ratios' chains no row runs.
        check_latency_by_impulse<basic_chain<float, 1, up2, up8, up3>>(exact_ratio{581, 1}, 384000.0);
        check_latency_by_impulse<basic_chain<float, 1, ratio3over8, down3>>(exact_ratio{271, 24}, 11025.0);
        check_latency_by_impulse<basic_chain<float, 1, up3, ratio8over3>>(exact_ratio{271, 3}, 88200.0);
        check_latency_by_impulse<up2_up2<float, 1>>(exact_ratio{47, 1}, 32000.0);
        check_latency_by_impulse<down2_down2<float, 1>>(exact_ratio{47, 4}, 8000.0);
    }

    // ------------------------------------------------------------------
    // MACs per output: the stages' trimmed-row counts scaled by their output
    // rates over the chain's (the matrix's numbers; test_matrix.cpp pins
    // every row, this pins the shape of the sum).
    TEST(Chain, MacsPerOutputIsTheStagesSumAtTheOutputRate) {
        up3_up4_up4<float, 1> up;
        // 41/3 per output at 3 r (over 16 for the chain's rate) + (2 * 4 * 3 + 1) / 4 at 12 r (over 4) + (2 * 3 * 3 +
        // 1) / 4
        EXPECT_EQ(up.macs_per_output_exact(), (exact_ratio{43, 6}));
        EXPECT_DOUBLE_EQ(up.macs_per_output(), 43.0 / 6.0);
        down4_down4_down3<float, 1> down;
        EXPECT_EQ(down.macs_per_output_exact(), (exact_ratio{344, 1})); // 19 * 12 + 25 * 3 + 41
        basic_chain<float, 1, up2> one;
        EXPECT_EQ(one.macs_per_output_exact(), (exact_ratio{21, 2}));
        EXPECT_DOUBLE_EQ(one.macs_per_output(), (basic_stage<float, 1, up2>().macs_per_output()));
    }

    // ------------------------------------------------------------------
    // The chains by 4, 8 and 16 (chain.h's aliases; docs/X4_X8_PLAN.md X1,
    // X2, X4 and section 5): each a plain alias of the coverage matrix's
    // chain for its ratio at its attenuation tier, in both families.
    static_assert(std::is_same_v<down_by4<float, 2>, basic_chain<float, 2, down2, down2>>);
    static_assert(std::is_same_v<down_by8<float, 2>, basic_chain<float, 2, down4, down2>>);
    static_assert(std::is_same_v<down_by16<float, 2>, basic_chain<float, 2, down2, down4, down2>>);
    static_assert(std::is_same_v<down_by16_transparent<float, 2>, basic_chain<float, 2, down8, down2>>);
    static_assert(std::is_same_v<up_by4<float, 2>, basic_chain<float, 2, up2, up2>>);
    static_assert(std::is_same_v<up_by8<float, 2>, basic_chain<float, 2, up2, up4>>);
    static_assert(std::is_same_v<up_by16<float, 2>, basic_chain<float, 2, up2, up4, up2>>);
    static_assert(std::is_same_v<up_by16_transparent<float, 2>, basic_chain<float, 2, up2, up8>>);
    // In every format, as the named chains are.
    static_assert(std::is_same_v<down_by16<std::int16_t, 1>, down2_down4_down2<std::int16_t, 1>>);
    static_assert(std::is_same_v<up_by8<std::int32_t, 1>, up2_up4<std::int32_t, 1>>);
    static_assert(std::is_same_v<down_by4<double, 1>, down2_down2<double, 1>>);

    namespace by_matrix {

        // The generated row table (tests/coverage/matrix_rows.h) read at
        // compile time: per row its rates, each tier's stages as (L, M) and
        // the pins. A cross-family row's bridge stage is (0, 0), never an
        // alias's.
        template <unsigned K>
        struct bridge_down {};
        template <unsigned K>
        struct bridge_up {};

        struct stage_code {
            unsigned       up                                  = 0;
            unsigned       down                                = 0;
            constexpr bool operator==(const stage_code&) const = default;
        };
        using stage_codes = std::array<stage_code, 6>;

        template <typename D>
        constexpr stage_code code_of() {
            if constexpr (rational_ratio<D>) {
                return {D::k_up, D::k_down};
            }
            else {
                return {};
            }
        }
        template <typename... Ds>
        constexpr stage_codes codes_of() {
            stage_codes c{};
            std::size_t i = 0;
            ((c[i++] = code_of<Ds>()), ...);
            return c;
        }

        template <typename C>
        struct chain_codes;
        template <tap::dsp::sample_type S, std::size_t Ch, rational_ratio... Rs>
        struct chain_codes<basic_chain<S, Ch, Rs...>> {
            static constexpr stage_codes k_value = codes_of<Rs...>();
        };

        struct row {
            std::uint64_t                 src;
            std::uint64_t                 dst;
            std::array<stage_codes, 2>    tiers; ///< the 70 dB tier's chain, the 120 dB tier's
            std::array<std::uint64_t, 16> pins;  ///< (MACs num, den, latency num, den) per profile, its tier's chain
        };

#define TAP_SR_BY_UNPAREN(...) __VA_ARGS__
#define ROW(SRC, DST, FLAG, DESCS, DIVS, TDESCS, TDIVS, ...)                                                           \
    row{SRC, DST, {codes_of<TAP_SR_BY_UNPAREN DESCS>(), codes_of<TAP_SR_BY_UNPAREN TDESCS>()}, {__VA_ARGS__}},
        constexpr std::array k_rows = {TAP_SR_RATIONAL_MATRIX_ROWS(ROW)};
#undef ROW
#undef TAP_SR_BY_UNPAREN
        static_assert(k_rows.size() == k_matrix_rows);

        constexpr bool in_48k_family(std::uint64_t hz) {
            return hz % 1000 == 0;
        }
        constexpr bool in_44k1_family(std::uint64_t hz) {
            return hz % 11025 == 0;
        }

        /// The within-family rows at ratio up / down, counted per family, and
        /// whether every one of them runs `want` at the tier.
        struct family_rows {
            std::size_t in_48k  = 0;
            std::size_t in_44k1 = 0;
            bool        all_run = true;
        };
        constexpr family_rows rows_at(std::uint64_t up, std::uint64_t down, std::size_t tier, const stage_codes& want) {
            family_rows out;
            for (const row& r : k_rows) {
                const bool a = in_48k_family(r.src) && in_48k_family(r.dst);
                const bool b = in_44k1_family(r.src) && in_44k1_family(r.dst);
                if ((a || b) && r.dst * down == r.src * up) {
                    ++(a ? out.in_48k : out.in_44k1);
                    out.all_run = out.all_run && r.tiers[tier] == want;
                }
            }
            return out;
        }

        template <typename Alias>
        constexpr family_rows rows_of(std::size_t tier) {
            return rows_at(Alias::k_up, Alias::k_down, tier, chain_codes<Alias>::k_value);
        }

        // Every alias is its rows' chain at its tier, in both families: the
        // 1/4 rows (32 -> 8 ... 384 -> 96, 44.1 -> 11.025 ... 176.4 -> 44.1),
        // the 1/8 rows and the 1/16 rows, and their mirrors.
        constexpr bool runs_in_both(family_rows f, std::size_t n48, std::size_t n44) {
            return f.all_run && f.in_48k == n48 && f.in_44k1 == n44;
        }
        static_assert(runs_in_both(rows_of<down_by4<float, 1>>(0), 5, 3));
        static_assert(runs_in_both(rows_of<down_by4<float, 1>>(1), 5, 3)); // no twin: one chain at both tiers
        static_assert(runs_in_both(rows_of<up_by4<float, 1>>(0), 5, 3));
        static_assert(runs_in_both(rows_of<up_by4<float, 1>>(1), 5, 3));
        static_assert(runs_in_both(rows_of<down_by8<float, 1>>(0), 3, 2));
        static_assert(runs_in_both(rows_of<down_by8<float, 1>>(1), 3, 2)); // no twin
        static_assert(runs_in_both(rows_of<up_by8<float, 1>>(0), 3, 2));
        static_assert(runs_in_both(rows_of<up_by8<float, 1>>(1), 3, 2));
        static_assert(runs_in_both(rows_of<down_by16<float, 1>>(0), 2, 1));
        static_assert(runs_in_both(rows_of<down_by16_transparent<float, 1>>(1), 2, 1));
        static_assert(runs_in_both(rows_of<up_by16<float, 1>>(0), 2, 1));
        static_assert(runs_in_both(rows_of<up_by16_transparent<float, 1>>(1), 2, 1));
        // By 16 the tiers differ, which is why the twin exists (X4).
        static_assert(!rows_of<down_by16<float, 1>>(1).all_run);
        static_assert(!rows_of<up_by16<float, 1>>(1).all_run);

        /// Profile i in the rows' order: super_economy, economy, balanced,
        /// transparent.
        profile profile_at(std::size_t i) {
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

        /// The alias's MACs per output and latency at every profile of the
        /// tier against every within-family row of its ratio, the row's
        /// pins at that profile.
        template <typename Alias>
        void expect_pins(std::size_t tier, const char* name) {
            std::size_t checked = 0;
            for (std::size_t prof = 0; prof < 4; ++prof) {
                if ((prof == 3) != (tier == 1)) {
                    continue;
                }
                Alias             c(profile_at(prof));
                const exact_ratio macs = c.macs_per_output_exact();
                const exact_ratio lat  = c.latency_output_frames();
                for (const row& r : k_rows) {
                    const bool within = (in_48k_family(r.src) && in_48k_family(r.dst))
                                        || (in_44k1_family(r.src) && in_44k1_family(r.dst));
                    if (!within || r.dst * Alias::k_down != r.src * Alias::k_up) {
                        continue;
                    }
                    EXPECT_EQ(macs, (exact_ratio{r.pins[4 * prof], r.pins[4 * prof + 1]}))
                        << name << " " << r.src << " -> " << r.dst << " profile " << prof;
                    EXPECT_EQ(lat, (exact_ratio{r.pins[4 * prof + 2], r.pins[4 * prof + 3]}))
                        << name << " " << r.src << " -> " << r.dst << " profile " << prof;
                    ++checked;
                }
            }
            EXPECT_GT(checked, 0u) << name;
        }

    } // namespace by_matrix

    TEST(Chain, ByAliasesAreTheirMatrixChains) {
        using by_matrix::expect_pins;
        // A plain alias at the three 70 dB profiles, a _transparent one at
        // transparent; by 4 and by 8 at both tiers, being both tiers' chain.
        for (const std::size_t tier : {std::size_t{0}, std::size_t{1}}) {
            expect_pins<down_by4<float, 1>>(tier, "down_by4");
            expect_pins<up_by4<float, 1>>(tier, "up_by4");
            expect_pins<down_by8<float, 1>>(tier, "down_by8");
            expect_pins<up_by8<float, 1>>(tier, "up_by8");
        }
        expect_pins<down_by16<float, 1>>(0, "down_by16");
        expect_pins<up_by16<float, 1>>(0, "up_by16");
        expect_pins<down_by16_transparent<float, 1>>(1, "down_by16_transparent");
        expect_pins<up_by16_transparent<float, 1>>(1, "up_by16_transparent");
    }

    /// MACs per output frame per channel of the chain C at every profile, in
    /// the order super_economy, economy, balanced, transparent.
    template <typename C>
    std::array<exact_ratio, 4> macs_at_every_profile() {
        std::array<exact_ratio, 4> out{};
        for (std::size_t i = 0; i < 4; ++i) {
            out[i] = C(by_matrix::profile_at(i)).macs_per_output_exact();
        }
        return out;
    }
    std::array<exact_ratio, 4> macs_list(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) {
        return {exact_ratio{a, 1}, exact_ratio{b, 1}, exact_ratio{c, 1}, exact_ratio{d, 1}};
    }

    // matrix.py's `by` mode searches every chain of one to three stages of
    // the vocabulary (four by 16) whose product is the ratio and whose
    // intermediate rates lie between the input and output rates, the same
    // chain spec (docs/X4_X8_PLAN.md 3.1: 29, 52 and 462 chains), and prints
    // the minimum per ratio and profile with R3's choice. Its table on X17's
    // pins, MACs per output frame down (up is the mirror: the same chain
    // reversed costs the same MACs per input frame, so the factor over them
    // per output frame), pinned through the chains that attain it:
    //
    //   ratio  super_economy      economy                  balanced               transparent
    //   1/4    ↓2·↓2 35           ↓2·↓2 43                 ↓2·↓2 47               ↓2·↓2 89
    //   1/8    ↓4·↓2 67           ↓4·↓2 71                 ↓2·↓2·↓2 83 (R3 87)    ↓4·↓2 149
    //   1/16   ↓4·↓2·↓2 111       ↓2·↓4·↓2 143 (ties       ↓4·↓2·↓2 147           ↓4·↓2·↓2 261 (R3
    //                             ↓4·↓2·↓2, less latency)                         keeps ↓8·↓2 at 277)
    //
    // The aliases are R3's choice where their tier is selected (economy, and
    // transparent for the _transparent ones; X4): by 4 and by 8 at every
    // profile, by 8 at balanced within R3's 10 % with fewer stages (+4.8 %).
    // By 16 the 70 dB tier's chain is the cheapest at economy only: at
    // super_economy ↓4·↓2·↓2 costs 111 against its 139, at balanced 147
    // against 159, and the 120 dB tier's ↓8·↓2 is R3's keep at transparent,
    // 277 against 261 (+6.1 %), fewer stages within its 10 %.
    TEST(Chain, ByAliasesAreTheCheapestWithinR3) {
        using halfbands8    = basic_chain<float, 1, down2, down2, down2>;
        using halfbands16   = basic_chain<float, 1, down2, down2, down2, down2>;
        using top4_16       = basic_chain<float, 1, down4, down2, down2>;
        using top4_16_up    = basic_chain<float, 1, up2, up2, up4>;
        using fourths16     = basic_chain<float, 1, down4, down4>;
        using single4       = basic_chain<float, 1, down4>;
        using single8       = basic_chain<float, 1, down8>;
        using retired8      = basic_chain<float, 1, down3, ratio3over4, down2>; // the 48 family's 1/8 until X-a3
        using retired8_44k1 = basic_chain<float, 1, ratio3over8, down3>;        // the 44.1 family's
        using by4           = down_by4<float, 1>;
        using by8           = down_by8<float, 1>;
        using by16          = down_by16<float, 1>;
        using by16_tr       = down_by16_transparent<float, 1>;
        using up8           = up_by8<float, 1>;
        using up16          = up_by16<float, 1>;
        using up16_tr       = up_by16_transparent<float, 1>;

        // The table's minima, through the chains that attain them.
        EXPECT_EQ(macs_at_every_profile<by4>(), macs_list(35, 43, 47, 89));
        const auto m8 = macs_at_every_profile<by8>();
        EXPECT_EQ(m8, macs_list(67, 71, 87, 149));
        const auto m8_halfbands = macs_at_every_profile<halfbands8>();
        EXPECT_EQ(m8_halfbands, macs_list(71, 79, 83, 165)); // the minimum at balanced only
        const auto m16 = macs_at_every_profile<by16>();
        EXPECT_EQ(m16, macs_list(139, 143, 159, 301));
        const auto m16_tr = macs_at_every_profile<by16_tr>();
        EXPECT_EQ(m16_tr, macs_list(159, 163, 167, 277));
        const auto m16_top4 = macs_at_every_profile<top4_16>();
        EXPECT_EQ(m16_top4, macs_list(111, 143, 147, 261)); // the minimum at every profile, tied at economy
        // Up mirrors down: the same MACs over the factor, per output frame.
        EXPECT_EQ(macs_at_every_profile<up8>()[1], (exact_ratio{71, 8}));
        EXPECT_EQ(macs_at_every_profile<up16>()[1], (exact_ratio{143, 16}));
        EXPECT_EQ(macs_at_every_profile<up16_tr>()[3], (exact_ratio{277, 16}));
        EXPECT_EQ(macs_at_every_profile<top4_16_up>()[3], (exact_ratio{261, 16}));

        // R3 at the tier's selecting profile. By 16 at economy the tie goes
        // to the lower latency: 189/16 output frames against 203/16.
        EXPECT_EQ(by16(profile::economy()).latency_output_frames(), (exact_ratio{189, 16}));
        EXPECT_EQ(top4_16(profile::economy()).latency_output_frames(), (exact_ratio{203, 16}));
        // The difference R3 keeps by 16 at transparent: 277 against 261,
        // within 10 % with two stages for three.
        static_assert(by16_tr::k_stages == 2 && top4_16::k_stages == 3);
        EXPECT_LT(m16_tr[3].value(), 1.1 * m16_top4[3].value());
        EXPECT_NEAR(m16_tr[3].value() / m16_top4[3].value() - 1.0, 0.0613, 5e-4);
        // By 8 at balanced the same way: 87 against three half-bands' 83.
        EXPECT_LT(m8[2].value(), 1.1 * m8_halfbands[2].value());

        // The named competitors, each dearer than the alias at economy: the
        // single stage, the half-band cascade, the chains the 4th-band stage
        // retired (X-a3) and, by 16, the two 4th-band stages.
        EXPECT_EQ(macs_at_every_profile<single4>()[1], (exact_ratio{61, 1}));
        EXPECT_EQ(macs_at_every_profile<single8>()[1], (exact_ratio{141, 1}));
        EXPECT_EQ(macs_at_every_profile<retired8>()[1], (exact_ratio{87, 1}));
        EXPECT_EQ(macs_at_every_profile<retired8_44k1>()[1], (exact_ratio{120, 1}));
        EXPECT_EQ(macs_at_every_profile<halfbands16>()[1], (exact_ratio{151, 1}));
        EXPECT_EQ(macs_at_every_profile<fourths16>()[1], (exact_ratio{161, 1}));
        // X5's trade, stated in chain.h: the single stage (by 16, the two
        // 4th-band stages) is lower latency at more MACs. Output frames at
        // economy: 39/4 against 47/4, 79/8 against 91/8, 171/16 against 189/16.
        EXPECT_EQ(single4().latency_output_frames(), (exact_ratio{39, 4}));
        EXPECT_EQ(by4().latency_output_frames(), (exact_ratio{47, 4}));
        EXPECT_EQ(single8().latency_output_frames(), (exact_ratio{79, 8}));
        EXPECT_EQ(by8().latency_output_frames(), (exact_ratio{91, 8}));
        EXPECT_EQ(fourths16().latency_output_frames(), (exact_ratio{171, 16}));
    }

    // ------------------------------------------------------------------
    // DC gain 1 through a chain: exact in the fixed-point formats (every
    // row sums to unity exactly, so a constant propagates exactly through
    // every stage), within rounding of the three stages' sums in float and
    // double (the single stages' bounds, test_stage.cpp, times the stages).
    template <typename C>
    void check_dc_gain() {
        using sample           = typename C::sample;
        const sample        dc = to_sample<sample>(0.5);
        C                   c;
        const std::size_t   n_in = c.frames_needed(400);
        std::vector<sample> x(n_in, dc);
        std::vector<sample> y(c.outputs_for(n_in));
        const std::size_t   made = c.process(x.data(), n_in, y.data());
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

    TYPED_TEST(chain_test, DcGainIsExactlyOne) {
        using sample = TypeParam;
        check_dc_gain<basic_chain<sample, 1, down3, ratio3over4, down2>>(); // a mixed ratio's rows at gain 2 in Q15
        check_dc_gain<down4_down4_down3<sample, 1>>();
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
        up2_up6<float, 2>  c;
        std::vector<float> y(2 * c.outputs_for(x.size()));
        const std::size_t  made = c.process(stereo.data(), x.size(), y.data());
        up2_up6<float, 1>  l;
        up2_up6<float, 1>  r;
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

    // The family's error model (A3): a chain's validate() is its stages' at
    // their design divisors, the first reason winning — nullptr exactly where
    // the chain constructs, else the constructor's text.
    TEST(Chain, ValidateAgreesWithTheConstructor) {
        using c3 = basic_chain<float, 1, up2, ratio4over3, up3>;
        static_assert(noexcept(c3::validate(profile::economy())));
        for (const profile& p :
             {profile::super_economy(), profile::economy(), profile::balanced(), profile::transparent()}) {
            EXPECT_EQ(c3::validate(p), nullptr);
            EXPECT_EQ((basic_chain<float, 2, up2, ratio4over3, up3>::validate(p)), nullptr);
            EXPECT_EQ((basic_chain<std::int16_t, 1, down2, down8, down2>::validate(p)), nullptr);
            TAP_SR_EXPECT_ACCEPTED((basic_chain<float, 2, up2, ratio4over3, up3>(p)));
        }
        profile bad       = profile::economy();
        bad.passband_frac = 0.5;
        EXPECT_EQ(c3::validate(bad), validate(bad.relaxed(c3::k_divisors[0])));
        TAP_SR_EXPECT_REJECTED_WITH(c3{bad}, std::invalid_argument, c3::validate(bad));
    }

} // namespace
