// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// M1 battery for ratio.h: the charter's accepted set (every ratio of the
// 48 and 44.1 kHz families' single-stage vocabulary and the chain-named
// by-4, by-16), the traits' numbers (band index, composite factor, lower
// rate, exponents, direction flags), the version macros, and that the
// composite rate is k_band times the lower rate for every ratio the
// engine names. The rejected set (ratio<5, 1>, <4, 2>, <2, 2>, <1, 1>) is
// compile_fail/, run as rational.Ratio.ChartersFailToCompileWithTheMessage.

#include <cstddef>
#include <numeric>

#include <gtest/gtest.h>

#include "tap/sr/rational/rational.h"

namespace {

    using namespace tap::sr::rational; // NOLINT(google-build-using-namespace)

    TEST(Ratio, VersionIsTheFamilys) {
        EXPECT_EQ(TAP_SR_VERSION_MAJOR, 0);
        EXPECT_EQ(TAP_SR_VERSION_MINOR, 5); // 0.5.0 from M6 (R11)
        EXPECT_EQ(TAP_SR_VERSION_PATCH, 0);
    }

    TEST(Ratio, TheCharterAcceptsTheVocabulary) {
        // Compile-time: every named ratio instantiates (PLAN.md section 1).
        static_assert(up_2::k_up == 2 && up_2::k_down == 1);
        static_assert(down_8::k_up == 1 && down_8::k_down == 8);
        static_assert(ratio_3_8::k_up == 3 && ratio_3_8::k_down == 8);
        static_assert(is_ratio<ratio<16, 1>>::value); // the 44.1 family's by-16 chain, named by it
        static_assert(is_ratio<ratio<4, 1>>::value);  // the by-4 chain (two half-bands, R3)
        static_assert(is_ratio<ratio<32, 1>>::value); // 12 -> 384 kHz
        static_assert(is_ratio<ratio<48, 1>>::value); // 8 -> 384 kHz
        static_assert(is_ratio<ratio<1, 48>>::value);
        static_assert(is_ratio<ratio<9, 8>>::value); // 2^a 3^b with b = 2 is in the form (not among the 14 rates)
        static_assert(!is_ratio<int>::value);
        static_assert(rational_ratio<up_3> && !rational_ratio<double>);
        SUCCEED();
    }

    TEST(Ratio, TraitsOfAnInterpolator) {
        using t = ratio_traits<up_3>;
        static_assert(t::k_up == 3 && t::k_down == 1);
        static_assert(t::k_is_up && t::k_is_interpolator && !t::k_is_decimator && !t::k_is_mixed);
        static_assert(t::k_band == 3);
        static_assert(t::k_composite_factor == 3);
        static_assert(t::k_lower_rate_num == 1 && t::k_lower_rate_den == 1);
        static_assert(t::k_pow2_up == 0 && t::k_pow3_up == 1 && t::k_pow2_down == 0 && t::k_pow3_down == 0);
        EXPECT_DOUBLE_EQ(t::k_rate_ratio, 3.0);
    }

    TEST(Ratio, TraitsOfADecimator) {
        using t = ratio_traits<down_6>;
        static_assert(!t::k_is_up && !t::k_is_interpolator && t::k_is_decimator && !t::k_is_mixed);
        static_assert(t::k_band == 6);
        static_assert(t::k_composite_factor == 1); // the filter runs at the input rate
        static_assert(t::k_lower_rate_num == 1 && t::k_lower_rate_den == 6);
        static_assert(t::k_pow2_down == 1 && t::k_pow3_down == 1);
        EXPECT_DOUBLE_EQ(t::k_rate_ratio, 1.0 / 6.0);
    }

    TEST(Ratio, TraitsOfTheMixedRatios) {
        // 3/2 up: the lower rate is the input, the band is L = 3 at 3 f_in.
        using u = ratio_traits<ratio_3_2>;
        static_assert(u::k_is_up && u::k_is_mixed && u::k_band == 3 && u::k_composite_factor == 3);
        static_assert(u::k_lower_rate_num == 1 && u::k_lower_rate_den == 1);
        // 2/3 down: the lower rate is the output, 2/3 f_in; the composite
        // rate 2 f_in is 3 times it: an M-th band (R2).
        using d = ratio_traits<ratio_2_3>;
        static_assert(!d::k_is_up && d::k_is_mixed && d::k_band == 3 && d::k_composite_factor == 2);
        static_assert(d::k_lower_rate_num == 2 && d::k_lower_rate_den == 3);
        // 3/8 down (128 -> 48 kHz): band 8 at 3 f_in = 8 * (3/8 f_in).
        using e = ratio_traits<ratio_3_8>;
        static_assert(e::k_band == 8 && e::k_composite_factor == 3 && e::k_lower_rate_num == 3
                      && e::k_lower_rate_den == 8);
        static_assert(e::k_pow2_down == 3 && e::k_pow3_up == 1);
        SUCCEED();
    }

    // The property design.h relies on: for every ratio of the charter the
    // composite rate L f_in equals k_band times the lower rate, so the
    // profile's lower-rate passband fraction is the designer's argument.
    template <unsigned L, unsigned M>
    constexpr bool composite_is_band_times_lower() {
        using t = ratio_traits<ratio<L, M>>;
        return t::k_composite_factor * t::k_lower_rate_den == t::k_band * t::k_lower_rate_num;
    }

    TEST(Ratio, CompositeRateIsTheBandTimesTheLowerRate) {
        static_assert(composite_is_band_times_lower<2, 1>() && composite_is_band_times_lower<1, 2>());
        static_assert(composite_is_band_times_lower<3, 1>() && composite_is_band_times_lower<1, 3>());
        static_assert(composite_is_band_times_lower<6, 1>() && composite_is_band_times_lower<1, 6>());
        static_assert(composite_is_band_times_lower<8, 1>() && composite_is_band_times_lower<1, 8>());
        static_assert(composite_is_band_times_lower<3, 2>() && composite_is_band_times_lower<2, 3>());
        static_assert(composite_is_band_times_lower<4, 3>() && composite_is_band_times_lower<3, 4>());
        static_assert(composite_is_band_times_lower<8, 3>() && composite_is_band_times_lower<3, 8>());
        static_assert(composite_is_band_times_lower<16, 1>() && composite_is_band_times_lower<1, 16>());
        SUCCEED();
    }

    TEST(Ratio, SmoothnessHelperMatchesTheDefinition) {
        for (unsigned n = 1; n <= 400; ++n) {
            unsigned r = n;
            while (r % 2 == 0) {
                r /= 2;
            }
            while (r % 3 == 0) {
                r /= 3;
            }
            EXPECT_EQ(detail::is_two_three_smooth(n), r == 1) << n;
        }
        EXPECT_FALSE(detail::is_two_three_smooth(0));
        EXPECT_EQ(detail::exponent_of(48, 2), 4u);
        EXPECT_EQ(detail::exponent_of(48, 3), 1u);
        EXPECT_EQ(detail::exponent_of(1, 2), 0u);
    }

} // namespace
