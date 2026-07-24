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
    /// Phase-major layout, symmetry-halved (M7 lever 3): the linear-phase
    /// prototype satisfies h[n] = h[LT-1-n], which per branch reads
    /// b_p[t] = b_{L-1-p}[T-1-t] — branch p is branch L-1-p tap-reversed. So
    /// only the low half of the branches is stored (k_stored = ceil(L/2)
    /// rows, each taps() contiguous coefficients, tap-reversed for the
    /// forward tap::dsp::dot_row convention), and a mirrored phase dots its
    /// partner's stored row backward via tap::dsp::dot_row_reversed — same
    /// products, same accumulation order, bit-identical outputs to a full
    /// table, at half the bytes. L odd (down: 147) has a self-symmetric
    /// middle branch, stored normally.
    ///
    /// Quantization is canonical over the stored half only: each stored row
    /// is quantized with the shared row-sum-preserving utility, and a
    /// mirrored row IS its partner's reversal by construction — reversal
    /// preserves the coefficient multiset, so every branch's DC gain (and
    /// the fixed-point exact-unity row sum) carries over unchanged.
    template <tap::dsp::sample_type S, direction D>
    class basic_phase_table {
      public:
        using coeff = typename tap::dsp::sample_traits<S>::coeff;

        static constexpr std::size_t k_phases = ratio_traits<D>::k_phases;
        static constexpr std::size_t k_stored = (k_phases + 1) / 2; ///< rows actually held

        /// True when phase ph reads its partner's stored row tap-reversed.
        static constexpr bool is_mirrored(std::size_t ph) noexcept { return ph >= k_stored; }

        /// The stored row index serving phase ph (identity for the low half).
        static constexpr std::size_t stored_index(std::size_t ph) noexcept {
            return ph < k_stored ? ph : k_phases - 1 - ph;
        }

        /// Designs the prototype (double precision, via the shared tap::dsp
        /// designer) and quantizes the stored half. Allocates; may throw.
        /// Setup time only, off the audio path.
        explicit basic_phase_table(const profile& p = profile::economy())
            : m_taps(p.taps<D>())
            , m_table(k_stored * m_taps) {
            const std::vector<double> proto = design_prototype<D>(p);
            std::vector<double>       row_d(m_taps);
            for (std::size_t ph = 0; ph < k_stored; ++ph) {
                for (std::size_t t = 0; t < m_taps; ++t) {
                    row_d[m_taps - 1 - t] = proto[t * k_phases + ph];
                }
                tap::dsp::quantize_row_preserving_sum<S>(row_d, std::span<coeff>(m_table.data() + ph * m_taps, m_taps));
            }
        }

        /// Stored-row pointer serving phase ph in [0, k_phases): taps()
        /// contiguous coefficients for tap::dsp::dot_row when
        /// !is_mirrored(ph), or for tap::dsp::dot_row_reversed when it is.
        const coeff* stored_row(std::size_t ph) const noexcept { return m_table.data() + stored_index(ph) * m_taps; }

        /// Logical coefficient (ph, t) — the cold-path accessor for tests and
        /// tools; the hot path pairs stored_row() with is_mirrored().
        coeff at(std::size_t ph, std::size_t t) const noexcept {
            const coeff* r = stored_row(ph);
            return is_mirrored(ph) ? r[m_taps - 1 - t] : r[t];
        }

        std::size_t taps() const noexcept { return m_taps; } ///< T: MACs per output sample

        /// Linear-phase group delay in input samples: (L*T - 1) / (2L) ~= T/2.
        double group_delay_input_samples() const noexcept {
            return static_cast<double>(k_phases * m_taps - 1) / (2.0 * static_cast<double>(k_phases));
        }

        std::size_t storage_bytes() const noexcept { return m_table.size() * sizeof(coeff); }

      private:
        std::size_t        m_taps;
        std::vector<coeff> m_table; // ceil(L/2) x T, rows tap-reversed
    };
    // ANCHOR_END: rt_phase_table

} // namespace tap::ratio
