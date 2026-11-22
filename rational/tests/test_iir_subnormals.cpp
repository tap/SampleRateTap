// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The recursive stages' honest limit without the host's flush-to-zero
// (docs/IIR_PLAN.md I6 and I9), in an executable of its own built with
// TAP_DSP_ALLOW_SUBNORMALS (tests/CMakeLists.txt: the macro is an ODR rule,
// one setting per image, so it cannot share tap_sr_rational_tests):
//   - WithoutFlushToZeroTheResidueIsBounded: with the bits cleared (or on a
//     target where none governs the type: double on the M33, Hexagon), the
//     silence after signal leaves each stage's states at a residue of a few
//     multiples of the smallest subnormal, which the stated bound holds, and
//     the output past the flush window is that residue, not silence; the
//     counterpart of IirStage.SilenceDecaysToExactZero;
//   - NeverTouchesTheFloatingPointEnvironment in the other host state (the
//     bits clear), the counterpart of IirStage's;
//   - the precondition compiled out: in a Debug build these calls would
//     assert in tap_sr_rational_tests (IirStageDebug), and here they run.
// Every case runs on the M33, M55 and Hexagon legs.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "support/flush_to_zero.h"
#include "tap/sr/rational/rational.h"

#if !defined(TAP_DSP_ALLOW_SUBNORMALS)
#error "tap_sr_rational_iir_subnormals is built with TAP_DSP_ALLOW_SUBNORMALS (tests/CMakeLists.txt)"
#endif

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    template <typename T>
    class iir_subnormals : public tap::sr::rational::test::no_flush_to_zero_fixture {};
    using floating_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(iir_subnormals, floating_types, );

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

    template <typename S>
    std::vector<S> noise(std::size_t n, std::uint32_t seed) {
        std::vector<S> x(n);
        std::uint32_t  s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = static_cast<S>(0.9 * (static_cast<double>(s % 65536u) - 32768.0) / 32768.0);
        }
        return x;
    }

    /// The bound, in multiples of the smallest subnormal: a section with a
    /// pole of radius r sticks at k of them for k <= 1 / (2 (1 - r)) under
    /// round to nearest (iir_kernels.h, subnormals.h), a path of sections at
    /// most their sum, the output a half-sum of two paths. Stated as one
    /// number for every stage the engine offers: 16 (measured at most 7 in
    /// float and double, states and output alike, on x86-64; the test prints
    /// it).
    constexpr double k_residue_bound = 16.0;

    template <typename Stage>
    void expect_bounded_residue(double& worst_state, double& worst_out) {
        using sample          = typename Stage::sample;
        constexpr double dmin = static_cast<double>(std::numeric_limits<sample>::denorm_min());
        for (int pi = 0; pi < 4; ++pi) {
            Stage               st(named(pi));
            const auto          x = noise<sample>(1200, 0x7777u + static_cast<std::uint32_t>(pi));
            std::vector<sample> y(st.outputs_for(x.size()));
            st.process(x.data(), x.size(), y.data());
            std::vector<sample> tail(st.flush_output_frames() + 1);
            st.flush(tail.data());
            // Past the flush: more zeros, and the output there is the residue.
            std::vector<sample> z(2048, sample{});
            std::vector<sample> w(st.outputs_for(z.size()));
            st.process(z.data(), z.size(), w.data());
            double out = 0.0;
            for (const sample v : w) {
                out = std::max(out, std::fabs(static_cast<double>(v)));
            }
            double state = 0.0;
            for (const auto v : st.states()) {
                state = std::max(state, std::fabs(static_cast<double>(v)));
            }
            worst_state = std::max(worst_state, state / dmin);
            worst_out   = std::max(worst_out, out / dmin);
            EXPECT_LE(state, k_residue_bound * dmin) << "profile " << pi;
            EXPECT_LE(out, k_residue_bound * dmin) << "a residue, not a signal; profile " << pi;
            EXPECT_LT(state, static_cast<double>(std::numeric_limits<sample>::min())) << "only subnormals are left";
        }
    }

    TYPED_TEST(iir_subnormals, WithoutFlushToZeroTheResidueIsBounded) {
        using sample = TypeParam;
        EXPECT_FALSE(tap::dsp::subnormals_flushed<sample>())
            << "the fixture cleared the bits, or none governs sample here";
        double state = 0.0;
        double out   = 0.0;
        expect_bounded_residue<basic_iir_stage<sample, 1, down2, iir_phase::nonlinear>>(state, out);
        expect_bounded_residue<basic_iir_stage<sample, 1, up2, iir_phase::nonlinear>>(state, out);
        expect_bounded_residue<basic_iir_stage<sample, 1, down2, iir_phase::near_linear>>(state, out);
        expect_bounded_residue<basic_iir_stage<sample, 1, up2, iir_phase::near_linear>>(state, out);
        std::printf("[ measured ] %s without flush-to-zero: past the window the states hold %.0f x and the output "
                    "%.0f x the smallest subnormal at worst (bound %.0f)\n",
                    std::is_same_v<sample, float> ? "float" : "double", state, out, k_residue_bound);
    }

    TYPED_TEST(iir_subnormals, NeverTouchesTheFloatingPointEnvironment) {
        using sample = TypeParam;
        basic_iir_stage<sample, 2, down2, iir_phase::nonlinear> a;
        basic_iir_stage<sample, 2, up2, iir_phase::near_linear> b;
        const auto                                              x = noise<sample>(256, 0x8888u);
        std::vector<sample>                                     y(4 * x.size());
        const std::uint64_t before = tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask;
        a.process(x.data(), x.size() / 2, y.data());
        b.process(x.data(), x.size() / 2, y.data());
        std::vector<sample> tail(std::max(a.flush_output_frames(), b.flush_output_frames()) * 2 + 2);
        a.flush(tail.data());
        b.flush(tail.data());
        EXPECT_EQ(tap::dsp::floating_point_control() & tap::sr::rational::test::k_control_mask, before);
    }

} // namespace
