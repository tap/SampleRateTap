// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Contract battery for the single stages (milestone M3): the structural
// leg (an impulse reproduces the table bit for bit through the whole
// machine, every format), the committed scipy reference vectors (an engine
// we did not write), the accounting from every position, chunking and
// pull parity, reset, flush, the latency by impulse, channels, DC gain
// exact in every format, the zero-skipping MAC counts, and DspTap's
// decimators as a second golden at the integer ratios they share.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "reference/reference_vectors.h"
#include "tap/dsp/decimate.h"
#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    template <typename S>
    class stage_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double, std::int16_t, std::int32_t>;
    TYPED_TEST_SUITE(stage_test, sample_types, );

    template <typename S>
    class golden_test : public ::testing::Test {};
    using golden_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(golden_test, golden_types, );

    template <typename S>
    S full_scale() {
        if constexpr (std::is_floating_point_v<S>) {
            return S{1};
        }
        else {
            return std::numeric_limits<S>::max();
        }
    }

    template <typename S>
    S to_sample(double v) {
        if constexpr (std::is_floating_point_v<S>) {
            return static_cast<S>(v);
        }
        else {
            return static_cast<S>(std::llround(v * static_cast<double>(std::numeric_limits<S>::max())));
        }
    }

    // ------------------------------------------------------------------
    // Structural correctness: an impulse at input d reproduces one stored
    // coefficient per output, through the trait's own mac + finalize, so
    // the prediction is exact in every format. The L-phase machine: output
    // n reads row phase(n) at storage index T - 1 - (floor(nM/L) - d). The
    // decimator: x[d] sits in sub-line d, read by branch j = (M - d) mod M
    // at s = k - 1 (k - 0 for d == 0), index T - 1 - s.
    template <typename S, rational_ratio R>
    void check_impulse_reproduces_table(const profile& p) {
        using tr                = tap::dsp::sample_traits<S>;
        constexpr std::size_t l = R::k_up;
        constexpr std::size_t m = R::k_down;
        for (std::size_t d = 0; d < m; ++d) {
            basic_stage<S, R> c(1, p);
            const std::size_t t_len = c.row_length();
            const std::size_t n_in  = (c.taps() + 2) * m + d;
            std::vector<S>    x(n_in, tr::silence());
            x[d] = full_scale<S>();
            std::vector<S>    y(c.outputs_for(n_in));
            const std::size_t made = c.process(x.data(), n_in, y.data());
            ASSERT_EQ(made, y.size());
            for (std::size_t n = 0; n < made; ++n) {
                S expected = tr::silence();
                if constexpr (R::k_up == 1) {
                    const std::size_t j = (m - d) % m;
                    if (!(d > 0 && n == 0)) {
                        const std::size_t s = d == 0 ? n : n - 1;
                        if (s < t_len) {
                            expected = basic_stage<S, R>::finalize_output(
                                tr::mac(typename tr::accum{}, x[d], c.coefficient(j, t_len - 1 - s)));
                        }
                    }
                }
                else {
                    const std::size_t newest = n * m / l;
                    if (newest >= d && newest - d < t_len) {
                        expected = basic_stage<S, R>::finalize_output(
                            tr::mac(typename tr::accum{}, x[d], c.coefficient((n * m) % l, t_len - 1 - (newest - d))));
                    }
                }
                ASSERT_EQ(y[n], expected) << "d=" << d << " n=" << n;
            }
        }
    }

    TYPED_TEST(stage_test, ImpulseReproducesTableEveryRatio) {
        check_impulse_reproduces_table<TypeParam, up_2>(profile::economy());
        check_impulse_reproduces_table<TypeParam, down_2>(profile::economy());
        check_impulse_reproduces_table<TypeParam, up_3>(profile::economy());
        check_impulse_reproduces_table<TypeParam, down_3>(profile::economy());
        check_impulse_reproduces_table<TypeParam, up_6>(profile::economy());
        check_impulse_reproduces_table<TypeParam, down_6>(profile::economy());
        check_impulse_reproduces_table<TypeParam, up_8>(profile::economy());
        check_impulse_reproduces_table<TypeParam, down_8>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_3_2>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_2_3>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_4_3>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_3_4>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_8_3>(profile::economy());
        check_impulse_reproduces_table<TypeParam, ratio_3_8>(profile::economy());
        check_impulse_reproduces_table<TypeParam, down_2>(profile::transparent());
        check_impulse_reproduces_table<TypeParam, up_3>(profile::super_economy());
    }

    // The table IS the design: going up the phases are design_stage's
    // branches bit for bit (float: the float cast); the structural zeros
    // are exactly 0 in every format; every row sums to exactly unity in
    // the fixed-point formats.
    TYPED_TEST(stage_test, TableIsTheDesignAndRowsSumToUnity) {
        using sample = TypeParam;
        using tr     = tap::dsp::sample_traits<sample>;
        {
            basic_stage<sample, up_3> c(1);
            const std::vector<double> h = design_stage<up_3>(profile::economy());
            for (std::size_t p = 0; p < 3; ++p) {
                for (std::size_t t = 0; t < c.row_length(); ++t) {
                    const std::size_t i = p + (c.row_length() - 1 - t) * 3;
                    const double      v = i < h.size() ? h[i] : 0.0;
                    if constexpr (std::is_floating_point_v<sample>) {
                        EXPECT_EQ(c.coefficient(p, t), static_cast<sample>(v)) << p << " " << t;
                    }
                    if (v == 0.0) {
                        EXPECT_EQ(c.coefficient(p, t), static_cast<typename tr::coeff>(0)) << p << " " << t;
                    }
                }
            }
        }
        if constexpr (tr::k_is_fixed_point) {
            const auto                unity = static_cast<std::int64_t>(tr::k_coeff_scale);
            basic_stage<sample, up_2> up(1);
            for (std::size_t p = 0; p < 2; ++p) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < up.row_length(); ++t) {
                    sum += up.coefficient(p, t);
                }
                EXPECT_EQ(sum, unity) << "up phase " << p;
            }
            basic_stage<sample, ratio_2_3> mixed(1);
            for (std::size_t p = 0; p < 2; ++p) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < mixed.row_length(); ++t) {
                    sum += mixed.coefficient(p, t);
                }
                EXPECT_EQ(sum, unity * static_cast<std::int64_t>(basic_stage<sample, ratio_2_3>::k_table_gain))
                    << "mixed phase " << p;
            }
            basic_stage<sample, down_3> down(1);
            std::int64_t                whole = 0;
            for (std::size_t j = 0; j < 3; ++j) {
                for (std::size_t t = 0; t < down.row_length(); ++t) {
                    whole += down.coefficient(j, t);
                }
            }
            // A Q15 decimator holds each branch at unity (its finalize
            // divides by M); the other formats hold the whole filter at it.
            EXPECT_EQ(whole, unity * static_cast<std::int64_t>(basic_stage<sample, down_3>::k_table_gain))
                << "decimator, all branches";
            if constexpr (basic_stage<sample, down_3>::k_branch_quantized) {
                for (std::size_t j = 0; j < 3; ++j) {
                    std::int64_t branch = 0;
                    for (std::size_t t = 0; t < down.row_length(); ++t) {
                        branch += down.coefficient(j, t);
                    }
                    EXPECT_EQ(branch, unity) << "Q15 decimator branch " << j;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // The scipy leg: committed upfirdn vectors, sample for sample from
    // n = 0. float: 3e-5 absolute at 0.9 peak, the float32 coefficient
    // floor (bridge's tolerance; measured 6e-8 here); double: the
    // reference's own float32 cast, 1e-7.
    template <typename S, rational_ratio R, std::size_t N>
    void check_matches_scipy(const std::array<float, N>& ref, const profile& p) {
        basic_stage<S, R> c(1, p);
        std::vector<S>    x(rational_ref::k_input.begin(), rational_ref::k_input.end());
        std::vector<S>    y(c.outputs_for(x.size()));
        ASSERT_EQ(c.process(x.data(), x.size(), y.data()), y.size());
        ASSERT_EQ(y.size(), N);
        const double tol   = std::is_same_v<S, float> ? 3e-5 : 1e-7;
        double       worst = 0.0;
        for (std::size_t n = 0; n < N; ++n) {
            worst = std::max(worst, std::fabs(static_cast<double>(y[n]) - static_cast<double>(ref[n])));
        }
        EXPECT_LE(worst, tol);
    }

    TYPED_TEST(golden_test, MatchesScipyEveryRatioEconomy) {
        using sample = TypeParam;
        check_matches_scipy<sample, up_2>(rational_ref::k_up_2, profile::economy());
        check_matches_scipy<sample, down_2>(rational_ref::k_down_2, profile::economy());
        check_matches_scipy<sample, up_3>(rational_ref::k_up_3, profile::economy());
        check_matches_scipy<sample, down_3>(rational_ref::k_down_3, profile::economy());
        check_matches_scipy<sample, up_6>(rational_ref::k_up_6, profile::economy());
        check_matches_scipy<sample, down_6>(rational_ref::k_down_6, profile::economy());
        check_matches_scipy<sample, up_8>(rational_ref::k_up_8, profile::economy());
        check_matches_scipy<sample, down_8>(rational_ref::k_down_8, profile::economy());
        check_matches_scipy<sample, ratio_3_2>(rational_ref::k_ratio_3_2, profile::economy());
        check_matches_scipy<sample, ratio_2_3>(rational_ref::k_ratio_2_3, profile::economy());
        check_matches_scipy<sample, ratio_4_3>(rational_ref::k_ratio_4_3, profile::economy());
        check_matches_scipy<sample, ratio_3_4>(rational_ref::k_ratio_3_4, profile::economy());
        check_matches_scipy<sample, ratio_8_3>(rational_ref::k_ratio_8_3, profile::economy());
        check_matches_scipy<sample, ratio_3_8>(rational_ref::k_ratio_3_8, profile::economy());
    }

    TYPED_TEST(golden_test, MatchesScipyHalfBandTransparent) {
        using sample = TypeParam;
        check_matches_scipy<sample, up_2>(rational_ref::k_up_2_transparent, profile::transparent());
        check_matches_scipy<sample, down_2>(rational_ref::k_down_2_transparent, profile::transparent());
    }

    // ------------------------------------------------------------------
    // Accounting: from every position of the superblock (or decimation
    // phase), outputs_for(n) is exactly what process(n) writes, and
    // frames_needed(k) is the smallest n with outputs_for(n) >= k.
    template <rational_ratio R>
    void check_accounting_from_every_position() {
        constexpr std::size_t period = R::k_up * R::k_down;
        converter<R>          c(1);
        // Enough input for the largest probe: frames_needed(2 period) is
        // at most (2 period + 1) M; outputs sized for the whole of x.
        std::vector<float> x((2 * period + 2) * R::k_down + 8, 0.25f);
        std::vector<float> y(c.outputs_for(x.size()) + 8);
        for (std::size_t pos = 0; pos < 2 * period; ++pos) {
            for (std::size_t n = 0; n <= 2 * period; ++n) {
                converter<R> probe = c;
                EXPECT_EQ(probe.process(x.data(), n, y.data()), c.outputs_for(n)) << "pos " << pos << " n " << n;
            }
            EXPECT_EQ(c.frames_needed(0), 0u);
            for (std::size_t k = 1; k <= 2 * period; ++k) {
                const std::size_t n = c.frames_needed(k);
                EXPECT_GE(c.outputs_for(n), k) << "pos " << pos << " k " << k;
                EXPECT_LT(c.outputs_for(n - 1), k) << "pos " << pos << " k " << k;
                // process() writes everything those n inputs make ready: at
                // least k (an interpolator's last input can complete more).
                converter<R> probe = c;
                ASSERT_LE(n, x.size());
                EXPECT_GE(probe.process(x.data(), n, y.data()), k) << "pos " << pos << " k " << k;
            }
            c.process(x.data(), 1, y.data());
        }
    }

    TEST(Stage, AccountingExactFromEveryPosition) {
        check_accounting_from_every_position<up_2>();
        check_accounting_from_every_position<down_2>();
        check_accounting_from_every_position<up_3>();
        check_accounting_from_every_position<down_3>();
        check_accounting_from_every_position<down_8>();
        check_accounting_from_every_position<ratio_3_2>();
        check_accounting_from_every_position<ratio_2_3>();
        check_accounting_from_every_position<ratio_3_8>();
        check_accounting_from_every_position<ratio_8_3>();
    }

    // ------------------------------------------------------------------
    // Bit-exact repeatability (R10): any chunking, pull against process,
    // reset.
    template <typename S, rational_ratio R>
    std::vector<S> run_whole(const std::vector<S>& x) {
        basic_stage<S, R> c(1);
        std::vector<S>    y(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), y.data());
        return y;
    }

    template <typename S, rational_ratio R>
    void check_chunking(const std::vector<S>& x) {
        const auto ref = run_whole<S, R>(x);
        for (const std::size_t chunk : {1u, 5u, 63u, 64u, 65u, 200u}) {
            basic_stage<S, R> c(1);
            std::vector<S>    y;
            std::vector<S>    buf(c.outputs_for(chunk) + R::k_up + 2);
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

    TYPED_TEST(stage_test, ChunkingIsBitIdentical) {
        using sample = TypeParam;
        std::vector<sample> x(rational_ref::k_input.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = to_sample<sample>(rational_ref::k_input[i]);
        }
        check_chunking<sample, up_3>(x);
        check_chunking<sample, down_3>(x);
        check_chunking<sample, ratio_2_3>(x);
        check_chunking<sample, ratio_8_3>(x);
    }

    template <rational_ratio R>
    void check_pull_matches_process() {
        const auto&        x = rational_ref::k_input;
        converter<R>       a(1);
        std::vector<float> ya(a.outputs_for(x.size()));
        a.process(x.data(), x.size(), ya.data());

        converter<R> b(1);
        std::size_t  fed  = 0;
        std::size_t  call = 0;
        auto         pop  = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            const std::size_t dribble = 1 + (call++ % 3); // 1..3 frames per call
            std::size_t       n       = 0;
            while (n < max_frames && n < dribble && fed < x.size()) {
                dst[n++] = x[fed++];
            }
            return n;
        };
        const std::size_t  need = b.frames_needed(ya.size());
        std::vector<float> yb(ya.size());
        ASSERT_EQ(b.pull(yb.data(), yb.size(), pop), ya.size());
        EXPECT_TRUE(ya == yb);
        EXPECT_EQ(fed, need); // the pull drew exactly frames_needed: no more, no less
    }

    TEST(Stage, PullMatchesProcessBitExact) {
        check_pull_matches_process<up_2>();
        check_pull_matches_process<down_2>();
        check_pull_matches_process<down_3>();
        check_pull_matches_process<ratio_3_2>();
        check_pull_matches_process<ratio_2_3>();
        check_pull_matches_process<ratio_3_8>();
    }

    TEST(Stage, PullShortReturnsOnDryThenResumes) {
        const auto&          x = rational_ref::k_input;
        converter<ratio_2_3> a(1);
        std::vector<float>   ya(a.outputs_for(x.size()));
        a.process(x.data(), x.size(), ya.data());

        converter<ratio_2_3> b(1);
        std::vector<float>   yb(ya.size());
        std::size_t          fed   = 0;
        std::size_t          limit = 101;
        auto                 pop   = [&](float* dst, std::size_t max_frames) noexcept -> std::size_t {
            std::size_t n = 0;
            while (n < max_frames && fed < limit) {
                dst[n++] = x[fed++];
            }
            return n;
        };
        const std::size_t first = b.pull(yb.data(), yb.size(), pop);
        EXPECT_LT(first, yb.size());
        EXPECT_GT(first, 0u);
        limit                  = x.size();
        const std::size_t rest = b.pull(yb.data() + first, yb.size() - first, pop);
        ASSERT_EQ(first + rest, ya.size());
        EXPECT_TRUE(ya == yb);
    }

    TYPED_TEST(stage_test, ResetReproducesBitExactly) {
        using sample = TypeParam;
        std::vector<sample> x(rational_ref::k_input.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = to_sample<sample>(rational_ref::k_input[i]);
        }
        basic_stage<sample, ratio_3_4> c(1);
        std::vector<sample>            y1(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), y1.data());
        std::vector<sample> scratch(c.outputs_for(123) + 2);
        c.process(x.data(), 123, scratch.data()); // leave it mid-stream
        c.reset();
        std::vector<sample> y2(y1.size());
        ASSERT_EQ(c.process(x.data(), x.size(), y2.data()), y2.size());
        EXPECT_TRUE(y1 == y2);
    }

    // ------------------------------------------------------------------
    // flush: writes flush_output_frames() frames, after which the tail has
    // decayed to silence; and the flushed tail is what a longer zero-padded
    // stream would have produced (bit for bit).
    template <rational_ratio R>
    void check_flush() {
        converter<R>       c(1);
        const std::size_t  n_in = 300;
        std::vector<float> x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 0.02 * static_cast<double>(i)));
        }
        std::vector<float> y(c.outputs_for(n_in));
        c.process(x.data(), n_in, y.data());
        const std::size_t  expect_tail = c.flush_output_frames();
        std::vector<float> tail(expect_tail);
        ASSERT_EQ(c.flush(tail.data()), expect_tail);
        ASSERT_GE(tail.size(), 2u);
        EXPECT_LT(std::fabs(tail[tail.size() - 1]), 1e-4f);
        // The same stream zero-padded by window_frames(): identical outputs.
        converter<R>       d(1);
        std::vector<float> xz(x);
        xz.resize(n_in + d.window_frames(), 0.0f);
        std::vector<float> yz(d.outputs_for(xz.size()));
        d.process(xz.data(), xz.size(), yz.data());
        ASSERT_EQ(yz.size(), y.size() + tail.size());
        for (std::size_t n = 0; n < y.size(); ++n) {
            ASSERT_EQ(yz[n], y[n]) << n;
        }
        for (std::size_t n = 0; n < tail.size(); ++n) {
            ASSERT_EQ(yz[y.size() + n], tail[n]) << n;
        }
    }

    TEST(Stage, FlushDrainsTailAndEqualsZeroPadding) {
        check_flush<up_2>();
        check_flush<down_2>();
        check_flush<down_3>();
        check_flush<ratio_3_2>();
        check_flush<ratio_2_3>();
        check_flush<ratio_3_8>();
    }

    // ------------------------------------------------------------------
    // Latency (R7): (N - 1) / (2 M) output frames, exact; verified by
    // impulse: an impulse at input d, chosen so that the centre tap lands
    // on an output exactly (c + d L == 0 mod M), peaks at output
    // (c + d L) / M, i.e. at latency + d L / M.
    template <rational_ratio R>
    void check_latency_by_impulse() {
        constexpr std::size_t l = R::k_up;
        constexpr std::size_t m = R::k_down;
        converter<R>          c(1);
        const std::size_t     centre = (c.taps() - 1) / 2;
        EXPECT_EQ(c.latency_output_frames(), (tap::dsp::exact_ratio{centre, m}));
        EXPECT_NEAR(c.latency_seconds(48000.0), static_cast<double>(centre) / static_cast<double>(m) / 48000.0, 1e-15);
        std::size_t d = 0;
        while ((centre + d * l) % m != 0) {
            ++d;
        }
        std::vector<float> x(c.taps() * m + 16, 0.0f);
        x[d] = 1.0f;
        std::vector<float> y(c.outputs_for(x.size()));
        c.process(x.data(), x.size(), y.data());
        std::size_t peak = 0;
        for (std::size_t n = 1; n < y.size(); ++n) {
            if (y[n] > y[peak]) {
                peak = n;
            }
        }
        EXPECT_EQ(peak, (centre + d * l) / m);
        // The centre tap is exactly 1 in the design; through the gain rule
        // the peak output is the unit impulse scaled by L / M... exactly
        // 1 going up, 1 / M for a decimator, the phase's normalization for
        // a mixed down ratio (within rounding).
        if constexpr (R::k_down == 1) {
            EXPECT_EQ(y[peak], 1.0f);
        }
    }

    TEST(Stage, LatencyIsTheExactRationalAndMatchesTheImpulse) {
        check_latency_by_impulse<up_2>();
        check_latency_by_impulse<down_2>();
        check_latency_by_impulse<up_3>();
        check_latency_by_impulse<down_3>();
        check_latency_by_impulse<up_8>();
        check_latency_by_impulse<down_8>();
        check_latency_by_impulse<ratio_3_2>();
        check_latency_by_impulse<ratio_2_3>();
        check_latency_by_impulse<ratio_4_3>();
        check_latency_by_impulse<ratio_3_4>();
        check_latency_by_impulse<ratio_8_3>();
        check_latency_by_impulse<ratio_3_8>();
    }

    // ------------------------------------------------------------------
    // DC gain is exactly 1 at every output in every format (every row sums
    // to unity, exactly in fixed point): a constant in gives the constant
    // out once the window is full.
    TYPED_TEST(stage_test, DcGainIsExactlyOne) {
        using sample       = TypeParam;
        const sample level = to_sample<sample>(0.5);
        auto         check = [&](auto& c, const char* name) {
            const std::size_t   n_in = 4 * c.window_frames() + 64;
            std::vector<sample> x(n_in, level);
            std::vector<sample> y(c.outputs_for(n_in));
            c.process(x.data(), n_in, y.data());
            for (std::size_t n = y.size() / 2; n < y.size(); ++n) {
                if constexpr (std::is_same_v<sample, double>) {
                    EXPECT_NEAR(y[n], 0.5, 1e-14) << name << " n=" << n;
                }
                else if constexpr (std::is_same_v<sample, float>) {
                    EXPECT_NEAR(y[n], 0.5f, 2e-7f) << name << " n=" << n;
                }
                else {
                    EXPECT_EQ(y[n], level) << name << " n=" << n; // exact: the rows sum to unity exactly
                }
            }
        };
        basic_stage<sample, up_2>      a(1);
        basic_stage<sample, down_3>    b(1);
        basic_stage<sample, ratio_2_3> d(1);
        basic_stage<sample, ratio_3_2> e(1);
        basic_stage<sample, down_8>    f(1);
        check(a, "up_2");
        check(b, "down_3");
        check(d, "ratio_2_3");
        check(e, "ratio_3_2");
        check(f, "down_8");
    }

    // ------------------------------------------------------------------
    // The zero taps are never multiplied (R4): MACs per superblock equal
    // the design's nonzero count for every interpolator and decimator; a
    // mixed stage pays its T per output less the padding.
    TEST(Stage, MacsPerOutputAreTheNonzeroCounts) {
        converter<up_2> u2(1);
        EXPECT_EQ(u2.macs_per_superblock(), 23u); // 2 m + 1, m = 11
        EXPECT_DOUBLE_EQ(u2.macs_per_output(), 11.5);
        converter<down_2> d2(1);
        EXPECT_EQ(d2.macs_per_superblock(), 23u);
        EXPECT_DOUBLE_EQ(d2.macs_per_output(), 23.0); // of 43 taps: the half-band's half
        converter<down_3> d3(1);
        EXPECT_EQ(d3.macs_per_superblock(), 45u); // 2 m (M - 1) + 1
        converter<up_8> u8(1);
        EXPECT_EQ(u8.macs_per_superblock(), 169u); // 2 * 12 * 7 + 1
        converter<down_8> d8(1);
        EXPECT_EQ(d8.macs_per_superblock(), 169u);
        converter<ratio_2_3> m23(1);
        EXPECT_EQ(m23.macs_per_superblock(), 65u); // N over 2 phases: 33 + 32
        EXPECT_DOUBLE_EQ(m23.macs_per_output(), 32.5);
        converter<ratio_3_8> m38(1);
        EXPECT_EQ(m38.macs_per_superblock(), 191u);
        EXPECT_EQ(m38.row_length(), stage_taps_per_phase<ratio_3_8>(profile::economy()));
        converter<up_2> tr(1, profile::transparent());
        EXPECT_EQ(tr.taps(), 123u);
        EXPECT_EQ(tr.macs_per_superblock(), 63u);
    }

    // ------------------------------------------------------------------
    // Channels are independent and share the row per frame: two channels
    // carrying two different streams equal the two mono runs bit for bit.
    TEST(Stage, TwoChannelsAreIndependent) {
        const auto&        x = rational_ref::k_input;
        std::vector<float> xb(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            xb[i] = -0.5f * x[(i * 7) % x.size()];
        }
        auto run_pair = [&](auto tag) {
            using stage_ratio = decltype(tag);
            converter<stage_ratio> a(1), b(1), s(2);
            std::vector<float>     ya(a.outputs_for(x.size())), yb(ya.size()), inter(2 * x.size()), ys(2 * ya.size());
            a.process(x.data(), x.size(), ya.data());
            b.process(xb.data(), xb.size(), yb.data());
            for (std::size_t i = 0; i < x.size(); ++i) {
                inter[2 * i]     = x[i];
                inter[2 * i + 1] = xb[i];
            }
            ASSERT_EQ(s.process(inter.data(), x.size(), ys.data()), ya.size());
            for (std::size_t n = 0; n < ya.size(); ++n) {
                ASSERT_EQ(ys[2 * n], ya[n]) << n;
                ASSERT_EQ(ys[2 * n + 1], yb[n]) << n;
            }
        };
        run_pair(up_3{});
        run_pair(down_3{});
        run_pair(ratio_3_8{});
    }

    // ------------------------------------------------------------------
    // Second golden (PLAN.md section 5, leg 2): DspTap's decimate.h at the
    // integer ratios it shares (by 2, 3, 6 at 16 kHz out) is a different
    // design on the same substrate (a 7 kHz Kaiser lowpass of 81 / 121 /
    // 239 taps against this engine's half-, third- and sixth-band of
    // 43 / 65 / 143). On a tone inside both passbands each must follow the
    // analytic sine within its own documented ripple after its own delay:
    // this engine within 3e-4 (0.0025 dB), decimate.h within 0.1 dB (its
    // header's bound); a larger disagreement is a finding.
    template <std::size_t M>
    void check_against_dsptap_decimator() {
        const double       fs_in = 16000.0 * M;
        const double       f0    = 997.0;
        const std::size_t  n_in  = 4096 * M;
        std::vector<float> x(n_in);
        for (std::size_t i = 0; i < n_in; ++i) {
            x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * f0 * static_cast<double>(i) / fs_in));
        }
        converter<ratio<1, M>>              ours(1);
        tap::dsp::basic_decimator<float, M> theirs;
        std::vector<float>                  yo(ours.outputs_for(n_in)), yt(theirs.outputs_for(n_in));
        ours.process(x.data(), n_in, yo.data());
        theirs.process(x.data(), n_in, yt.data());
        ASSERT_EQ(yo.size(), yt.size());
        const double delay_ours   = ours.latency_output_frames().value() * static_cast<double>(M); // input samples
        const double delay_theirs = static_cast<double>(theirs.latency_input_samples());
        double       worst_ours = 0.0, worst_theirs = 0.0;
        for (std::size_t k = yo.size() / 4; k < yo.size() - yo.size() / 8; ++k) {
            const double t_out   = static_cast<double>(k) * static_cast<double>(M);
            const double ideal_o = 0.5 * std::sin(2.0 * std::numbers::pi * f0 * (t_out - delay_ours) / fs_in);
            const double ideal_t = 0.5 * std::sin(2.0 * std::numbers::pi * f0 * (t_out - delay_theirs) / fs_in);
            worst_ours           = std::max(worst_ours, std::fabs(yo[k] - ideal_o));
            worst_theirs         = std::max(worst_theirs, std::fabs(yt[k] - ideal_t));
        }
        EXPECT_LT(worst_ours, 0.5 * 3e-4) << "by " << M;
        EXPECT_LT(worst_theirs, 0.5 * 0.0116) << "by " << M; // 0.1 dB
        std::printf("[ measured ] decimate by %zu at 997 Hz: this engine %.2e, decimate.h %.2e peak error (of 0.5)\n",
                    M, worst_ours, worst_theirs);
    }

    TEST(Stage, AgreesWithDspTapDecimatorsWithinTheDesignDifference) {
        check_against_dsptap_decimator<2>();
        check_against_dsptap_decimator<3>();
        check_against_dsptap_decimator<6>();
    }

    TEST(Stage, ZeroChannelsThrows) {
        EXPECT_THROW((converter<up_2>(0)), std::invalid_argument);
    }

} // namespace
