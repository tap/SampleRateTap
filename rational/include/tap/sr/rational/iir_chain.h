/// @file iir_chain.h
/// @brief iir_down_by / iir_up_by<S, Ch, P, D>: a power-of-2 cascade of recursive half-bands, each relaxed to its
/// divisor.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A conversion by 4, 8 or 16 through recursive stages is a cascade of
// basic_iir_stage by 2 (iir_stage.h), and like an FIR chain (chain.h,
// PLAN.md 3.1) each stage is designed at its own rate: the cascade's profile
// declares f_pass = p * r_min at its lowest rate, so a stage whose lower rate
// is d r_min runs profile::relaxed(d), its passband p / d of its own lower
// rate and its stopband mirrored about its own low Nyquist (docs/IIR_PLAN.md
// I13). tap::dsp::chain does not relax its stages; these helpers do, as
// basic_chain does for the FIR stages. Down, the stages run from the high
// rate at divisors D/2, ..., 2, 1; up, from the low rate at 1, 2, ..., D/2.
// The named profiles have compile-time designs at every divisor a cascade up
// to 16 uses (1, 2, 4 and 8), both variants.
//
// What relaxing buys, in multiplies per output frame per channel
// (macs_per_output(), pinned by rational.IirChain.CascadesCostTheRelaxedRow
// against docs/IIR_PLAN.md 2.4), going down by 2 / 4 / 8 / 16:
//   - nonlinear, economy: 4 / 8 / 16 / 24 (every stage at the profile's own
//     design: 4 / 12 / 28 / 60); transparent: 8 / 16 / 28 / 44;
//   - near-linear, economy: 7 / 13 / 21 / 29 (unrelaxed 7 / 21 / 49 / 105);
// going up, the same over the factor. Against the FIR chains by the same
// factor at economy (chain.h's down_by4 ... down_by16: 43, 71 and 143 MACs
// per output), and the IIR stages' phase: the nonlinear cascade's delay
// rises toward the passband edge, the near-linear one's is its delay path
// within a stated deviation.
//
// Contract: tap::dsp::chain's (process in chunks, outputs_for and
// frames_needed exact, flush equal to zero padding, reset), over the stages'
// own; the latency is the chain's nominal (nominal_latency_output_frames(),
// latency_deviation_output_frames(), group_delay_output_frames(f), composed
// over the stages), never an exact latency_output_frames(). A near-linear
// cascade is an exact_delay_stage for tap::dsp::subband_delta (its delay
// path composed, exact); a nonlinear one is not, and the round-trip scaffold
// refuses it at compile time (X10). validate() states each stage's rejection
// at its divisor, in order; the constructor rejects with the same text.
//
// Every cascade is half-bands. The nonlinear four-path stage by 4 was
// measured in place of the two half-bands at the top of the cascade by 8 and
// the middle of the one by 16 (I12), on this machine over DspTap's kernels:
// it beat them by more than R3's 10 % on Hexagon going down, and here and
// there in Q15, but cost more on the M33 and the M55 in float going down
// (+10.7 and +9.2 %) and on the M33 in Q15 (+14.6 %); no format and direction
// cleared the bar on all three targets, so it does not ship
// (docs/IIR_PLAN.md I12, the IIR-b record).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "tap/dsp/chain.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/rational/design.h"
#include "tap/sr/rational/iir_stage.h"
#include "tap/sr/rational/ratio.h"

namespace tap::sr::rational {

    namespace detail {

        /// log2(D) for a power-of-2 cascade factor from 2 to 16, else 0.
        constexpr std::size_t iir_cascade_stages(std::size_t d) noexcept {
            switch (d) {
            case 2:
                return 1;
            case 4:
                return 2;
            case 8:
                return 3;
            case 16:
                return 4;
            default:
                return 0;
            }
        }

        /// The stage types of a cascade: K half-bands in one direction.
        template <typename Stage, typename Seq>
        struct iir_cascade_chain;
        template <typename Stage, std::size_t... I>
        struct iir_cascade_chain<Stage, std::index_sequence<I...>> {
            template <std::size_t>
            using stage_at = Stage;
            using type     = tap::dsp::chain<stage_at<I>...>;
        };

    } // namespace detail

    // ANCHOR: rational_iir_cascade
    /// A power-of-2 cascade by D (2, 4, 8 or 16) of recursive half-bands in
    /// variant P over sample format S, Ch interleaved channels, going down
    /// (Up false) or up; see the file header. The public spellings are
    /// iir_down_by and iir_up_by.
    template <tap::dsp::sample_type S, std::size_t Ch, iir_phase P, std::size_t D, bool Up>
    class basic_iir_cascade
        : public detail::iir_cascade_chain<basic_iir_stage<S, Ch, std::conditional_t<Up, up2, down2>, P>,
                                           std::make_index_sequence<detail::iir_cascade_stages(D)>>::type {
        static_assert(detail::iir_cascade_stages(D) != 0,
                      "tap::sr::rational: a recursive cascade runs by 2, 4, 8 or 16 (iir_down_by / iir_up_by), the "
                      "divisors the named profiles' designs cover");

      public:
        using stage_type = basic_iir_stage<S, Ch, std::conditional_t<Up, up2, down2>, P>;
        using base       = typename detail::iir_cascade_chain<stage_type,
                                                              std::make_index_sequence<detail::iir_cascade_stages(D)>>::type;

        static constexpr std::size_t k_stages = detail::iir_cascade_stages(D);
        static constexpr std::size_t k_factor = D;

        /// Stage i's design divisor: D/2, ..., 1 going down, 1, ..., D/2 up.
        static constexpr std::array<std::size_t, k_stages> k_divisors = [] {
            std::array<std::size_t, k_stages> d{};
            for (std::size_t i = 0; i < k_stages; ++i) {
                d[i] = Up ? std::size_t{1} << i : (D / 2) >> i;
            }
            return d;
        }();

        /// The reason the cascade cannot be built from p, or nullptr: each
        /// stage's validate at its divisor, in stage order (D17).
        static const char* validate(const profile& p = profile::economy()) noexcept {
            for (const std::size_t d : k_divisors) {
                if (const char* why = stage_type::validate(p.relaxed({d, 1}))) {
                    return why;
                }
            }
            return nullptr;
        }

        /// Builds every stage at the profile relaxed by its divisor; rejects
        /// what validate() rejects, with its text. Setup time only.
        explicit basic_iir_cascade(const profile& p = profile::economy())
            : basic_iir_cascade(p, std::make_index_sequence<k_stages>{}) {}

        /// Multiplies per output frame per channel: each stage's per output
        /// at its own rate, scaled to the cascade's output rate.
        double macs_per_output() const noexcept {
            double total = 0.0;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((total += this->template stage<I>().macs_per_output() * output_rate_over_last(I)), ...);
            }(std::make_index_sequence<k_stages>{});
            return total;
        }

        /// A recursive cascade has no exact latency (I5): its nominal is
        /// tap::dsp::chain's nominal_latency_output_frames(). Deleted, so
        /// tap::dsp::reports_latency_output and the round-trip scaffold see
        /// none (a cascade is a class of its own, not a tap::dsp::chain that
        /// subband.h would walk stage by stage).
        tap::dsp::exact_ratio latency_output_frames() const = delete;
        double                latency_seconds(double) const = delete;

        /// The near-linear cascade's delay path in output frames, exact: each
        /// stage's converted to the cascade's output rate and summed (the
        /// round-trip scaffold's T, tap::dsp::reports_delay_path).
        tap::dsp::exact_ratio delay_path_output_frames() const noexcept
            requires(P == iir_phase::near_linear)
        {
            tap::dsp::exact_ratio total{0, 1};
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((total = total + this->template stage<I>().delay_path_output_frames() * ratio_after(I)), ...);
            }(std::make_index_sequence<k_stages>{});
            return total;
        }

        /// Zero input frames after which the cascade's output is silence and
        /// every state zero, from any state: the first stage's window plus
        /// the inputs it needs to write the rest's, composed backward
        /// (ceil(k M / L) + M inputs for k outputs of a stage, from any
        /// phase; tap/dsp/subband.h's drain bound). tap::dsp::chain's own
        /// flush() feeds each stage its window in turn instead.
        std::size_t window_frames() const noexcept {
            std::size_t w = 0;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((w = this->template stage<k_stages - 1 - I>().window_frames() + inputs_for(w)), ...);
            }(std::make_index_sequence<k_stages>{});
            return w;
        }

      private:
        /// Inputs a stage needs, at most, to write k outputs from any phase.
        static constexpr std::size_t inputs_for(std::size_t k) noexcept {
            if (k == 0) {
                return 0;
            }
            constexpr std::size_t l = stage_type::k_up;
            constexpr std::size_t m = stage_type::k_down;
            return (k * m + l - 1) / l + m;
        }

        /// The ratio of the stages after stage i: converts its output frames
        /// to the cascade's (1 / 2^(K - 1 - i) down, 2^(K - 1 - i) up).
        static constexpr tap::dsp::exact_ratio ratio_after(std::size_t i) noexcept {
            const auto r = static_cast<std::uint64_t>(std::size_t{1} << (k_stages - 1 - i));
            return Up ? tap::dsp::exact_ratio{r, 1} : tap::dsp::exact_ratio{1, r};
        }

        /// Stage i's output rate over the cascade's: 2^(K - 1 - i) down,
        /// 2^-(K - 1 - i) up.
        static constexpr double output_rate_over_last(std::size_t i) noexcept {
            const auto r = static_cast<double>(std::size_t{1} << (k_stages - 1 - i));
            return Up ? 1.0 / r : r;
        }

        template <std::size_t... I>
        basic_iir_cascade(const profile& p, std::index_sequence<I...>)
            : base(stage_type(p.relaxed({k_divisors[I], 1}))...) {}
    };
    // ANCHOR_END: rational_iir_cascade

    // ANCHOR: rational_iir_by
    /// The cascades by D, named by intent beside chain.h's down_by4 ... (X1):
    /// down (1/D) and up (D/1), D = 2, 4, 8 or 16, in variant P.
    template <tap::dsp::sample_type S, std::size_t Ch, iir_phase P, std::size_t D>
    using iir_down_by = basic_iir_cascade<S, Ch, P, D, false>;
    template <tap::dsp::sample_type S, std::size_t Ch, iir_phase P, std::size_t D>
    using iir_up_by = basic_iir_cascade<S, Ch, P, D, true>;
    // ANCHOR_END: rational_iir_by

} // namespace tap::sr::rational
