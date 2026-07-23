/// @file phase_table.h
/// @brief Phase-major quantized coefficient table for one direction.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "tap/dsp/quantize.h"
#include "tap/dsp/sample_traits.h"
#include "tap/ratio/design.h"

namespace tap::ratio {

    // ANCHOR: rt_phase_table
    /// Immutable polyphase coefficient table, designed at construction.
    ///
    /// Phase-major layout: exactly L rows (no interpolation between phases,
    /// so no extra wrap row and no power-of-two rounding — the schedule
    /// indexes branches exactly), each row taps() contiguous coefficients,
    /// stored tap-reversed so the dot product runs forward over an
    /// oldest-first history window (the tap::dsp::dot_row convention).
    /// Branch p holds prototype taps h[p + t*L], quantized per row with the
    /// shared row-sum-preserving utility, so every branch's DC gain survives
    /// fixed point within one coefficient LSB.
    template <tap::dsp::sample_type S, direction D>
    class basic_phase_table {
      public:
        using coeff = typename tap::dsp::sample_traits<S>::coeff;

        static constexpr std::size_t k_phases = ratio_traits<D>::k_phases;

        /// Designs the prototype (double precision, via the shared tap::dsp
        /// designer) and quantizes the table. Allocates; may throw. Setup
        /// time only, off the audio path.
        explicit basic_phase_table(const profile& p = profile::economy())
            : m_taps(p.taps<D>())
            , m_table(k_phases * m_taps) {
            const std::vector<double> proto = design_prototype<D>(p);
            std::vector<double>       row_d(m_taps);
            for (std::size_t ph = 0; ph < k_phases; ++ph) {
                for (std::size_t t = 0; t < m_taps; ++t) {
                    row_d[m_taps - 1 - t] = proto[t * k_phases + ph];
                }
                tap::dsp::quantize_row_preserving_sum<S>(row_d, std::span<coeff>(m_table.data() + ph * m_taps, m_taps));
            }
        }

        /// Row pointer for branch ph in [0, k_phases); taps() contiguous
        /// coefficients, ready for tap::dsp::dot_row.
        const coeff* row(std::size_t ph) const noexcept { return m_table.data() + ph * m_taps; }

        std::size_t taps() const noexcept { return m_taps; } ///< T: MACs per output sample

        /// Linear-phase group delay in input samples: (L*T - 1) / (2L) ~= T/2.
        double group_delay_input_samples() const noexcept {
            return static_cast<double>(k_phases * m_taps - 1) / (2.0 * static_cast<double>(k_phases));
        }

        std::size_t storage_bytes() const noexcept { return m_table.size() * sizeof(coeff); }

      private:
        std::size_t        m_taps;
        std::vector<coeff> m_table; // L x T, rows tap-reversed
    };
    // ANCHOR_END: rt_phase_table

} // namespace tap::ratio
