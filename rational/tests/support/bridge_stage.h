// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// bridge's converter as a tap::dsp::sync_stage, for the cross-family rows
// of the coverage matrix (PLAN.md 3.5 / 3.6): a test-side adapter, since a
// test may name a sibling engine (4.2) and the engines themselves never do.
// It presents the converter's own machine unchanged: k_up / k_down are its
// L / M, process and outputs_for are its (outputs_for narrowed to size_t),
// window_frames() is its taps() (what flush() feeds), the latency is the
// prototype's (L T - 1) / 2 at the composite rate as an exact rational in
// output frames (phase_table.h's group_delay_input_samples times L / M),
// and macs_per_output() is T, its taps per phase.
#pragma once

#include <cstddef>
#include <cstdint>

#include "tap/dsp/chain.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/bridge/converter.h"

namespace rational_test {

    template <tap::dsp::sample_type S, tap::sr::bridge::direction D, unsigned K>
    class bridge_stage {
      public:
        using sample = S;
        using traits = tap::sr::bridge::ratio_traits<D, K>;

        static constexpr std::size_t k_up         = traits::k_phases;     ///< L
        static constexpr std::size_t k_down       = traits::k_decimation; ///< M
        static constexpr unsigned    k_rate_scale = K;

        explicit bridge_stage(std::size_t                     channels = 1,
                              const tap::sr::bridge::profile& p        = tap::sr::bridge::profile::economy())
            : m_converter(channels, p) {}

        std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            return m_converter.process(in, in_frames, out);
        }
        std::size_t outputs_for(std::size_t in_frames) const noexcept {
            return static_cast<std::size_t>(m_converter.outputs_for(in_frames));
        }
        void                  reset() noexcept { m_converter.reset(); }
        std::size_t           window_frames() const noexcept { return m_converter.taps(); }
        tap::dsp::exact_ratio latency_output_frames() const noexcept {
            return tap::dsp::exact_ratio{static_cast<std::uint64_t>(k_up * m_converter.taps() - 1),
                                         static_cast<std::uint64_t>(2 * k_down)};
        }
        /// T per output, L T per superblock of L outputs (the chain's
        /// accounting shape).
        std::size_t macs_per_superblock() const noexcept { return k_up * m_converter.taps(); }
        double      macs_per_output() const noexcept { return static_cast<double>(m_converter.taps()); }
        std::size_t taps() const noexcept { return m_converter.taps(); }

      private:
        tap::sr::bridge::basic_converter<S, D, K> m_converter;
    };

    static_assert(tap::dsp::sync_stage<bridge_stage<float, tap::sr::bridge::direction::down_to_44k1, 0>>);
    static_assert(tap::dsp::sync_stage<bridge_stage<double, tap::sr::bridge::direction::up_to_48k, 2>>);

} // namespace rational_test
