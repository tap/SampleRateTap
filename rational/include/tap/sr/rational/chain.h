/// @file chain.h
/// @brief basic_chain<S, Ch, R...>: stages in sequence at one profile, each designed at its own rate; the named chains.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A conversion by a ratio no single stage of the vocabulary serves is a
// chain of stages (PLAN.md section 3): tap::dsp::chain<> over basic_stage,
// written by the caller as a type and never looked up from a rate pair
// (D12). What this header adds to the DspTap helper is the profile rule:
// the chain's profile declares f_pass = p * r_min at the chain's lowest
// rate (3.1), so each stage is designed at p over its own DESIGN DIVISOR,
// its lower rate over r_min (profile::relaxed; the divisors are the pinned
// rows of the relaxation tables), and the chain constructs its stages that
// way. Within a family the divisor is an exact 2^a 3^b; in general it is
// the largest such number at or below the quotient (a passband at or above
// f_pass, so the stage still meets 2.1(a)), or the quotient itself below 1
// (the stage at bridge's rate under a 48-family r_min, 147/160; that case
// only arises in a chain through bridge, which the caller composes from
// tap::dsp::chain with profile::relaxed, as bridge/examples does).
//
// Contract: tap::dsp::chain's (process in chunks, outputs_for and
// frames_needed exact, flush = zero padding, reset, the latency as the
// exact rational sum at the output rate) over basic_stage's; noexcept and
// allocation-free after construction (R9). macs_per_output() is the chain's
// cost per output frame per channel: each stage's trimmed-row count scaled
// by its output rate over the chain's, exact.
//
// The named chains are the coverage matrix's within-family chains (3.3 /
// 3.4, generated from the pinned lengths by tools/coverage/matrix.py and
// pinned per row by tests/test_matrix.cpp): one alias per distinct chain of
// two or more stages, named by its stages in order; a one-stage chain is
// basic_converter<S, R>. The names are the C ABI's enumerators (M6).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "tap/dsp/chain.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/rational/design.h"
#include "tap/sr/rational/ratio.h"
#include "tap/sr/rational/stage.h"

namespace tap::sr::rational {

    namespace detail {

        using tap::dsp::exact_ratio;

        constexpr bool ratio_less(exact_ratio a, exact_ratio b) noexcept {
            return a.num * b.den < b.num * a.den;
        }
        constexpr exact_ratio ratio_over(exact_ratio a, exact_ratio b) noexcept {
            return exact_ratio{a.num * b.den, a.den * b.num};
        }

        // ANCHOR: rational_design_divisor
        /// The design divisor of a stage whose lower rate over the chain's
        /// lowest is q (see the file header): the largest 2^a 3^b at or
        /// below q, or q itself below 1.
        constexpr exact_ratio design_divisor(exact_ratio q) noexcept {
            if (ratio_less(q, exact_ratio{1, 1})) {
                return q;
            }
            std::uint64_t best = 1;
            for (std::uint64_t three = 1; three <= 27; three *= 3) {
                for (std::uint64_t d = three; d * q.den <= q.num; d *= 2) {
                    best = d > best ? d : best;
                }
            }
            return exact_ratio{best, 1};
        }

        /// The rates of a chain relative to its input: rates[i] before stage
        /// i, rates[n] the output; and the lowest of them.
        template <rational_ratio... Rs>
        struct chain_rates {
            static constexpr std::size_t                           k_stages = sizeof...(Rs);
            static constexpr std::array<exact_ratio, k_stages + 1> k_rates  = [] {
                std::array<exact_ratio, k_stages + 1> r{};
                r[0]          = exact_ratio{1, 1};
                std::size_t i = 0;
                ((r[i + 1] = r[i] * exact_ratio{Rs::k_up, Rs::k_down}, ++i), ...);
                return r;
            }();
            static constexpr exact_ratio k_lowest = [] {
                exact_ratio low = k_rates[0];
                for (const auto& r : k_rates) {
                    low = ratio_less(r, low) ? r : low;
                }
                return low;
            }();
            /// Stage i's design divisor: its lower rate over the lowest.
            static constexpr std::array<exact_ratio, k_stages> k_divisors = [] {
                std::array<exact_ratio, k_stages> d{};
                for (std::size_t i = 0; i < k_stages; ++i) {
                    const exact_ratio lower = ratio_less(k_rates[i], k_rates[i + 1]) ? k_rates[i] : k_rates[i + 1];
                    d[i]                    = design_divisor(ratio_over(lower, k_lowest));
                }
                return d;
            }();
        };
        // ANCHOR_END: rational_design_divisor

    } // namespace detail

    // ANCHOR: rational_chain
    /// A chain of stages at the ratios Rs, in order, over sample format S,
    /// every stage running Ch interleaved channels (a compile-time count);
    /// see the file header. Constructs each stage at the profile relaxed by
    /// the stage's design divisor (k_divisors); everything else is
    /// tap::dsp::chain's, which takes the count from the stages.
    template <tap::dsp::sample_type S, std::size_t Ch, rational_ratio... Rs>
    class basic_chain : public tap::dsp::chain<basic_stage<S, Ch, Rs>...> {
        using base = tap::dsp::chain<basic_stage<S, Ch, Rs>...>;

      public:
        static_assert(sizeof...(Rs) >= 1, "tap::sr::rational::basic_chain: a chain has at least one stage");

        static constexpr std::size_t k_stages = sizeof...(Rs);
        /// The design divisor of every stage (the file header; PLAN.md 3.1).
        static constexpr std::array<tap::dsp::exact_ratio, k_stages> k_divisors =
            detail::chain_rates<Rs...>::k_divisors;

        /// The reason a chain cannot be built from p, or nullptr when it
        /// can: each stage's basic_stage::validate at its design
        /// divisor (the profile relaxed as the constructor relaxes it), in
        /// stage order, the first reason wins. noexcept; allocation-free for
        /// the named profiles (every relaxation the matrix uses has its pin).
        static const char* validate(const profile& p = profile::economy()) noexcept {
            return validate(p, std::make_index_sequence<k_stages>{});
        }

        /// Allocates every stage's state and designs its table; rejects what
        /// validate() rejects, with its text (tap::dsp::raise: a build without
        /// exceptions terminates there and asks validate() first). Setup time
        /// only (R9).
        explicit basic_chain(const profile& p = profile::economy())
            : basic_chain(p, std::make_index_sequence<k_stages>{}) {}

        /// MACs per output frame per channel: the sum over the stages of
        /// macs_per_output() at the stage's output rate, scaled to the
        /// chain's output rate; exact, and its double.
        tap::dsp::exact_ratio macs_per_output_exact() const noexcept {
            using tap::dsp::exact_ratio;
            constexpr auto& rates = detail::chain_rates<Rs...>::k_rates;
            exact_ratio     total{0, 1};
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((total = total
                          + exact_ratio{this->template stage<I>().macs_per_superblock(),
                                        std::tuple_element_t<I, std::tuple<Rs...>>::k_up == 1
                                            ? 1
                                            : std::tuple_element_t<I, std::tuple<Rs...>>::k_up}
                                * detail::ratio_over(rates[I + 1], rates[k_stages])),
                 ...);
            }(std::make_index_sequence<k_stages>{});
            return total;
        }
        double macs_per_output() const noexcept { return macs_per_output_exact().value(); }

      private:
        template <std::size_t... I>
        basic_chain(const profile& p, std::index_sequence<I...>)
            : base(basic_stage<S, Ch, Rs>(p.relaxed(k_divisors[I]))...) {}

        template <std::size_t... I>
        static const char* validate(const profile& p, std::index_sequence<I...>) noexcept {
            const char* why = nullptr;
            // Left to right, stopping at the first reason.
            ((why == nullptr ? (why = basic_stage<S, Ch, Rs>::validate(p.relaxed(k_divisors[I]))) : why), ...);
            return why;
        }
    };
    // ANCHOR_END: rational_chain

    /// The float chain of Ch channels at the ratios Rs (the golden-model
    /// profile), and the Q15 / Q31 chains (R8).
    template <std::size_t Ch, rational_ratio... Rs>
    using chain = basic_chain<float, Ch, Rs...>;
    template <std::size_t Ch, rational_ratio... Rs>
    using chain_q15 = basic_chain<std::int16_t, Ch, Rs...>;
    template <std::size_t Ch, rational_ratio... Rs>
    using chain_q31 = basic_chain<std::int32_t, Ch, Rs...>;

    // ANCHOR: rational_named_chains
    /// The named chains of the coverage matrix (PLAN.md 3.3 / 3.4), one per
    /// distinct chain of two or more stages, named by its stages in order
    /// with an underscore between them (a one-stage chain is
    /// basic_converter<S, Ch, R>): twenty in all, the 48 kHz family's
    /// eighteen plus the two the 44.1 kHz family's lattice chooses for 8/1
    /// and 1/8 (its r_min * 3 intermediate, 33.075 kHz, where the 48 family
    /// has r_min * 8/3). Ratios in comments are the chain's L/M.
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up2 = basic_chain<S, Ch, up2, up2>; ///< 4/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up3 = basic_chain<S, Ch, up2, up3>; ///< 6/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_ratio4over3_up3 = basic_chain<S, Ch, up2, ratio4over3, up3>; ///< 8/1 (48 family)
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up3_ratio8over3 = basic_chain<S, Ch, up3, ratio8over3>; ///< 8/1 (44.1 family)
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up6 = basic_chain<S, Ch, up2, up6>; ///< 12/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up8 = basic_chain<S, Ch, up2, up8>; ///< 16/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up6_up2 = basic_chain<S, Ch, up2, up6, up2>; ///< 24/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up8_up2 = basic_chain<S, Ch, up2, up8, up2>; ///< 32/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_up8_up3 = basic_chain<S, Ch, up2, up8, up3>; ///< 48/1
    template <tap::dsp::sample_type S, std::size_t Ch>
    using up2_ratio4over3 = basic_chain<S, Ch, up2, ratio4over3>; ///< 8/3
    template <tap::dsp::sample_type S, std::size_t Ch>
    using ratio3over4_down2 = basic_chain<S, Ch, ratio3over4, down2>; ///< 3/8
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down2_down2 = basic_chain<S, Ch, down2, down2>; ///< 1/4
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down3_down2 = basic_chain<S, Ch, down3, down2>; ///< 1/6
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down3_ratio3over4_down2 = basic_chain<S, Ch, down3, ratio3over4, down2>; ///< 1/8 (48 family)
    template <tap::dsp::sample_type S, std::size_t Ch>
    using ratio3over8_down3 = basic_chain<S, Ch, ratio3over8, down3>; ///< 1/8 (44.1 family)
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down6_down2 = basic_chain<S, Ch, down6, down2>; ///< 1/12
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down8_down2 = basic_chain<S, Ch, down8, down2>; ///< 1/16
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down2_down6_down2 = basic_chain<S, Ch, down2, down6, down2>; ///< 1/24
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down2_down8_down2 = basic_chain<S, Ch, down2, down8, down2>; ///< 1/32
    template <tap::dsp::sample_type S, std::size_t Ch>
    using down3_down8_down2 = basic_chain<S, Ch, down3, down8, down2>; ///< 1/48
    // ANCHOR_END: rational_named_chains

} // namespace tap::sr::rational
