/// @file ratio.h
/// @brief ratio<L, M>: the compile-time ratio type of the rational engine, and its traits.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The charter is a static_assert, not a runtime check (PLAN.md R1): a ratio
// of the rational engine is L/M with L and M of the form 2^a * 3^b, in
// lowest terms, and not 1. Everything a stage needs to know about its
// ratio — the band index of its one Nyquist filter (R2), the composite
// rate it runs at, the direction, the exponents the chain factoring reads
// (R3) — is a constexpr number of ratio_traits, after bridge's
// ratio_traits<D, K> and DspTap's decimate_traits<M>.
//
// Contract, as numbers:
//   - k_up = L and k_down = M: the output / input frame ratio. n input
//     frames yield n * L / M output frames, exactly over a superblock of M
//     inputs and L outputs (the mixed-ratio schedule is bridge's, M3).
//   - k_band = max(L, M): the stage's filter is a k_band-th-band (Nyquist)
//     filter at the composite rate L * f_in with its cutoff at the lower
//     rate's Nyquist — an L-th band when L > M, an M-th band when M > L
//     (PLAN.md R2, 2.5). For every ratio of the charter the composite rate
//     is k_band times the lower rate, which is what makes the lower-rate
//     passband fraction of profile (design.h) the designer's own argument.
//   - k_composite_factor = L: the filter rate over the input rate.
//   - k_is_interpolator (M == 1), k_is_decimator (L == 1), k_is_mixed
//     (both > 1); k_is_up (L > M).
//   - k_pow2_up / k_pow3_up / k_pow2_down / k_pow3_down: the exponents a
//     and b of L and M, read by the chain factoring (M4).
//
// ratio<4, 1> and ratio<1, 4> are valid ratios (the chain by 4 is named by
// them) but no single 4th-band stage is shipped for them: a 4 is two
// half-band stages (R3, decision 6). The stage headers, not this one, say
// which ratios have a stage.

#pragma once

#include <cstddef>
#include <numeric>
#include <type_traits>

namespace tap::sr::rational {

    namespace detail {

        /// True when n is of the form 2^a * 3^b (n >= 1).
        constexpr bool is_two_three_smooth(unsigned n) noexcept {
            if (n == 0) {
                return false;
            }
            while (n % 2 == 0) {
                n /= 2;
            }
            while (n % 3 == 0) {
                n /= 3;
            }
            return n == 1;
        }

        /// The exponent of base in n (n >= 1).
        constexpr unsigned exponent_of(unsigned n, unsigned base) noexcept {
            unsigned e = 0;
            while (n > 0 && n % base == 0) {
                n /= base;
                ++e;
            }
            return e;
        }

    } // namespace detail

    // ANCHOR: rational_ratio
    /// The ratio L/M of one stage or chain, as a type (R1): L output
    /// frames per M input frames. The charter's static_asserts are the
    /// whole definition; ratio_traits holds the derived numbers.
    template <unsigned L, unsigned M>
    struct ratio {
        static_assert(detail::is_two_three_smooth(L) && detail::is_two_three_smooth(M),
                      "tap::sr::rational: L and M must be of the form 2^a * 3^b (the charter, PLAN.md R1); "
                      "a ratio with another factor is outside this engine");
        static_assert(std::gcd(L, M) == 1,
                      "tap::sr::rational: L/M must be in lowest terms, gcd(L, M) == 1 (the charter, PLAN.md R1)");
        static_assert(L != M, "tap::sr::rational: L/M == 1 is no conversion (the charter, PLAN.md R1)");

        static constexpr unsigned k_up   = L; ///< output frames per superblock
        static constexpr unsigned k_down = M; ///< input frames per superblock
    };
    // ANCHOR_END: rational_ratio

    /// True for a ratio<L, M> specialization.
    template <typename T>
    struct is_ratio : std::false_type {};
    template <unsigned L, unsigned M>
    struct is_ratio<ratio<L, M>> : std::true_type {};

    /// A ratio<L, M> specialization.
    template <typename T>
    concept rational_ratio = is_ratio<T>::value;

    // ANCHOR: rational_ratio_traits
    /// Compile-time facts of one ratio; see the file header.
    template <rational_ratio R>
    struct ratio_traits {
        static constexpr unsigned k_up   = R::k_up;
        static constexpr unsigned k_down = R::k_down;

        static constexpr bool k_is_up           = k_up > k_down;
        static constexpr bool k_is_interpolator = k_down == 1;
        static constexpr bool k_is_decimator    = k_up == 1;
        static constexpr bool k_is_mixed        = k_up > 1 && k_down > 1;

        /// The band index of the stage's Nyquist filter: max(L, M) (R2).
        static constexpr unsigned k_band = k_up > k_down ? k_up : k_down;
        /// The filter's rate over the input rate: L (the composite rate).
        static constexpr unsigned k_composite_factor = k_up;
        /// The lower of the two rates over the input rate, as (num, den):
        /// 1/1 going up, L/M going down. The composite rate is k_band times
        /// this rate in both cases.
        static constexpr unsigned k_lower_rate_num = k_is_up ? 1u : k_up;
        static constexpr unsigned k_lower_rate_den = k_is_up ? 1u : k_down;

        static constexpr unsigned k_pow2_up   = detail::exponent_of(k_up, 2);
        static constexpr unsigned k_pow3_up   = detail::exponent_of(k_up, 3);
        static constexpr unsigned k_pow2_down = detail::exponent_of(k_down, 2);
        static constexpr unsigned k_pow3_down = detail::exponent_of(k_down, 3);

        /// L / M as a double; informational (nothing routes by it, D12).
        static constexpr double k_rate_ratio = static_cast<double>(k_up) / static_cast<double>(k_down);

        static_assert(k_composite_factor == k_band * k_lower_rate_num / k_lower_rate_den
                          || k_composite_factor * k_lower_rate_den == k_band * k_lower_rate_num,
                      "the composite rate is k_band times the lower rate for every ratio of the charter");
    };
    // ANCHOR_END: rational_ratio_traits

    /// The single-stage vocabulary of the 48 kHz family (PLAN.md section 1):
    /// the integer factors and the mixed ratios one stage serves, as named
    /// ratios. by_4 is a ratio the chain by 4 is named by, not a stage (R3,
    /// decision 6: two half-bands).
    using up_2   = ratio<2, 1>;
    using down_2 = ratio<1, 2>;
    using up_3   = ratio<3, 1>;
    using down_3 = ratio<1, 3>;
    using up_6   = ratio<6, 1>;
    using down_6 = ratio<1, 6>;
    using up_8   = ratio<8, 1>;
    using down_8 = ratio<1, 8>;
    /// The mixed ratios, named by L/M.
    using ratio_3_2 = ratio<3, 2>;
    using ratio_2_3 = ratio<2, 3>;
    using ratio_4_3 = ratio<4, 3>;
    using ratio_3_4 = ratio<3, 4>;
    using ratio_8_3 = ratio<8, 3>;
    using ratio_3_8 = ratio<3, 8>;

} // namespace tap::sr::rational
