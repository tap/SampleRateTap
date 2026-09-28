/// @file converter.h
/// @brief The synchronous 44.1 <-> 48 kHz converter: one direction, streamed.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/sample_traits.h"
#include "tap/ratio/design.h"
#include "tap/ratio/phase_table.h"
#include "tap/ratio/schedule.h"

// Out-of-lining attribute for the mirrored-phase dot (see dot_mirrored):
// measured per target, gated per target, the same pattern as the tap::dsp
// kernel gates. On Arm, two inlined dot expansions in the walk's loop body
// break the forward arm's unrolled codegen (M55 up_q15 +3.3%), so the
// mirrored arm is a call; hexagon-clang keeps both expansions tight inline
// and pays for the call instead (down_q31: +3.3% outlined, +2.7% inlined),
// so there the attribute is empty and the helper inlines away.
#if defined(__hexagon__)
#define TAP_RATIO_MIRRORED_DOT_ATTR
#elif defined(_MSC_VER)
#define TAP_RATIO_MIRRORED_DOT_ATTR __declspec(noinline)
#else
#define TAP_RATIO_MIRRORED_DOT_ATTR __attribute__((noinline))
#endif

namespace tap::ratio {

    // ANCHOR: rt_converter_doc
    /// Streaming fixed-ratio converter for one direction (compile-time D).
    ///
    /// The machine is the whole point of this library: one phase-major table
    /// row dot per output frame, driven by the compile-time (phase, advance)
    /// schedule — no coefficient interpolation, no fractional-delay state, no
    /// servo, and a phase sequence that repeats every L outputs exactly.
    ///
    /// Alignment contract: the converter is zero-primed and causal. Output n
    /// is y[n] = sum_k x[floor(nM/L) - k] * h[phase(n) + kL] with x[<0] = 0 —
    /// exactly scipy.signal.upfirdn's streaming prefix, sample for sample
    /// from n = 0, transient included (pinned against committed scipy vectors
    /// by test_converter.cpp). Latency is therefore the prototype's
    /// linear-phase group delay, latency_input_frames() ~= taps()/2.
    ///
    /// Real-time contract: the constructor performs all allocation and filter
    /// design and may throw; process(), pull(), flush() and reset() are
    /// noexcept, lock-free and allocation-free. One stream per instance;
    /// every channel of an instance shares the coefficient row per frame, so
    /// inter-channel phase coherence is exact by construction.
    // ANCHOR_END: rt_converter_doc
    template <tap::dsp::sample_type S, direction D>
    class basic_converter {
      public:
        using coeff = typename tap::dsp::sample_traits<S>::coeff;

        static constexpr std::size_t k_phases     = ratio_traits<D>::k_phases;     ///< L
        static constexpr std::size_t k_decimation = ratio_traits<D>::k_decimation; ///< M

        /// The canonical trip counts (taps per phase = MACs per output) the
        /// hot path hard-commits to at compile time; any other profile runs
        /// the runtime-length walk.
        static constexpr std::size_t k_taps_economy       = profile::economy().taps<D>();
        static constexpr std::size_t k_taps_super_economy = profile::super_economy().taps<D>();
        static constexpr std::size_t k_taps_balanced      = profile::balanced().taps<D>();
        static constexpr std::size_t k_taps_transparent   = profile::transparent().taps<D>();

        /// Allocates histories and designs the table; setup time only.
        explicit basic_converter(std::size_t channels = 1, const profile& p = profile::economy())
            : m_table(p)
            , m_channels(channels)
            , m_hist_cap(m_table.taps() + k_hist_slack)
            , m_hist(channels)
            , m_scratch(k_pop_chunk * channels) {
            if (channels == 0) {
                throw std::invalid_argument("tap::ratio::basic_converter: channels == 0");
            }
            for (auto& h : m_hist) {
                h.assign(m_hist_cap, tap::dsp::sample_traits<S>::silence());
            }
            reset();
        }

        // ANCHOR: rt_process
        /// Push-transform: consume ALL in_frames interleaved input frames,
        /// writing every output frame that becomes ready. Returns the number
        /// of output frames written.
        ///
        /// \pre out has room for outputs_for(in_frames) frames — the exact
        /// count this call will produce from the current position.
        ///
        /// The hot path is the superblock walk (M7 lever 1, PLAN section 7):
        /// the output count is settled by arithmetic up front, the schedule
        /// cursor / history end / pending gap live in locals the compiler
        /// keeps in registers, input and output move by pointer bumps
        /// instead of per-frame index multiplies, and the mono and stereo
        /// shapes are stamped out as their own specializations so the
        /// channel loops vanish. The dots themselves are the unchanged
        /// tap::dsp kernels, and the append/emit order is identical, so
        /// outputs stay bit-exact (pinned by the scipy-vector tests).
        ///
        /// M7 lever 2 commits the trip counts: the canonical profiles'
        /// taps-per-phase are compile-time facts (constexpr profile), so one
        /// dispatch per call hands the walk a constant dot length — the
        /// inlined kernels unroll and vectorize against it instead of a
        /// runtime bound. A custom-taps profile takes the runtime-length
        /// instantiation (pinned by the custom-profile parity test).
        std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            const std::size_t taps = m_table.taps();
            if (taps == k_taps_economy) {
                return process_taps<k_taps_economy>(in, in_frames, out);
            }
            if (taps == k_taps_super_economy) {
                return process_taps<k_taps_super_economy>(in, in_frames, out);
            }
            if (taps == k_taps_balanced) {
                return process_taps<k_taps_balanced>(in, in_frames, out);
            }
            if (taps == k_taps_transparent) {
                return process_taps<k_taps_transparent>(in, in_frames, out);
            }
            return process_taps<0>(in, in_frames, out);
        }
        // ANCHOR_END: rt_process

        // ANCHOR: rt_pull
        /// Pull: produce exactly out_frames interleaved output frames, drawing
        /// input as needed through pop(dst, max_frames) -> frames delivered
        /// (interleaved, may deliver fewer). Returns frames produced; fewer
        /// than out_frames means the source ran dry — the partial consumption
        /// is retained, so delivering more input later resumes exactly where
        /// the stream left off. Bit-identical to process() on the same input.
        /// PopFn must be noexcept (enforced at compile time: pull() is
        /// noexcept, so a throwing callback could only terminate).
        template <typename PopFn>
        std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
            static_assert(std::is_nothrow_invocable_r_v<std::size_t, PopFn&, S*, std::size_t>,
                          "pull() requires a noexcept PopFn: std::size_t(S*, std::size_t) noexcept");
            for (std::size_t n = 0; n < out_frames; ++n) {
                while (m_pending != 0) {
                    const std::size_t pending = m_pending;
                    const std::size_t want    = pending < k_pop_chunk ? pending : k_pop_chunk;
                    const std::size_t got     = pop(m_scratch.data(), want);
                    if (got == 0) {
                        return n; // dry
                    }
                    for (std::size_t i = 0; i < got && i < want; ++i) {
                        append_frame(m_scratch.data() + i * m_channels);
                        --m_pending;
                    }
                }
                emit(out + n * m_channels);
            }
            return out_frames;
        }
        // ANCHOR_END: rt_pull

        // ANCHOR: rt_frames_needed
        /// Exact input frames required to produce the next out_frames outputs
        /// from the CURRENT stream position — deterministic arithmetic the
        /// async engine can never offer. pull() with a source delivering
        /// exactly this many frames produces exactly out_frames outputs.
        std::uint64_t frames_needed(std::uint64_t out_frames) const noexcept {
            if (out_frames == 0) {
                return 0;
            }
            return m_pending + tap::ratio::frames_needed<D>(m_pos, out_frames - 1);
        }

        /// Exact output frames the next in_frames input frames will yield from
        /// the current position (the sizing companion to process()).
        std::uint64_t outputs_for(std::uint64_t in_frames) const noexcept {
            if (in_frames < m_pending) {
                return 0;
            }
            constexpr std::uint64_t l    = k_phases;
            constexpr std::uint64_t m    = k_decimation;
            const std::uint64_t     base = m_pos * m / l;
            const std::uint64_t     a    = in_frames - m_pending;
            // Largest N with needed(N) <= in_frames, i.e. with
            // floor((pos + N - 1) M / L) <= base + a; solved for N.
            return (l * (base + a + 1) - 1) / m - m_pos + 1;
        }
        // ANCHOR_END: rt_frames_needed

        /// End-of-stream: feed taps() zero frames and write the outputs that
        /// become ready — every output influenced by real input. Returns the
        /// frames written (== outputs_for(taps()) beforehand). The engine is
        /// left mid-stream in the zero-fed state; reset() before reuse.
        std::size_t flush(S* out) noexcept {
            const std::size_t zeros    = m_table.taps();
            std::size_t       fed      = 0;
            std::size_t       produced = 0;
            for (;;) {
                while (m_pending != 0 && fed < zeros) {
                    append_silence();
                    ++fed;
                    --m_pending;
                }
                if (m_pending != 0) {
                    return produced;
                }
                emit(out + produced * m_channels);
                ++produced;
            }
        }

        /// Frames flush() will write from the current position.
        std::uint64_t flush_output_frames() const noexcept { return outputs_for(m_table.taps()); }

        /// Return to the initial zero-primed state (position 0, silence).
        void reset() noexcept {
            for (auto& h : m_hist) {
                for (auto& v : h) {
                    v = tap::dsp::sample_traits<S>::silence();
                }
            }
            m_end     = m_table.taps();
            m_pos     = 0;
            m_pending = 1; // the pre-advance that delivers x[0] under output 0
        }

        /// Linear-phase group delay in input samples, (L*T - 1) / (2L) ~= T/2.
        double latency_input_frames() const noexcept { return m_table.group_delay_input_samples(); }

        std::size_t channels() const noexcept { return m_channels; }
        std::size_t taps() const noexcept { return m_table.taps(); } ///< MACs per output sample
        std::size_t position() const noexcept { return m_pos; }      ///< superblock position in [0, L)

        const basic_phase_table<S, D>& table() const noexcept { return m_table; }

      private:
        static constexpr std::size_t k_hist_slack = 64; ///< appends between compactions
        static constexpr std::size_t k_pop_chunk  = 16; ///< pull()'s bulk-pop granularity

        const S* window(std::size_t c) const noexcept { return m_hist[c].data() + m_end - m_table.taps(); }

        /// Channel-shape dispatch for one compile-time trip count T (0 =
        /// runtime taps): mono and stereo get stamped-out walks, anything
        /// else the generic.
        template <std::size_t T>
        std::size_t process_taps(const S* in, std::size_t in_frames, S* out) noexcept {
            if (m_channels == 1) {
                return process_walk<1, T>(in, in_frames, out);
            }
            if (m_channels == 2) {
                return process_walk<2, T>(in, in_frames, out);
            }
            return process_walk<0, T>(in, in_frames, out);
        }

        // ANCHOR: rt_superblock_walk
        /// The superblock walk behind process() (M7 levers 1 + 2). CH = 1 and
        /// 2 are stamped-out specializations — the deployment shapes — with
        /// the per-channel loops dissolved and the planar history pointers
        /// hoisted (compaction memmoves in place, so they stay valid for the
        /// whole call); CH = 0 is the any-channel-count generic. T is the
        /// compile-time dot length for the canonical profiles (0 = runtime).
        /// The schedule cursor, history end and pending gap run in locals so
        /// the state machine lives in registers, and the outputs_for()
        /// arithmetic settles the trip count before the first sample moves —
        /// the walk itself has no exhaustion checks. Same append/emit order
        /// and the same tap::dsp dot kernels as the naive loop: bit-exact by
        /// construction, pinned by the scipy-vector and pull-parity tests.
        template <std::size_t CH, std::size_t T>
        std::size_t process_walk(const S* in, std::size_t in_frames, S* out) noexcept {
            const std::size_t taps  = T != 0 ? T : m_table.taps();
            const std::size_t total = static_cast<std::size_t>(outputs_for(in_frames));
            const std::size_t ch    = CH != 0 ? CH : m_channels;

            S* const h0 = m_hist[0].data();
            S* const h1 = CH == 2 ? m_hist[1].data() : nullptr;

            const S*    src     = in;
            std::size_t remain  = in_frames;
            std::size_t end     = m_end;
            std::size_t pos     = m_pos;
            std::size_t pending = m_pending;

            const auto append = [&]() noexcept {
                if (end == m_hist_cap) {
                    end = compact(end);
                }
                if constexpr (CH == 1) {
                    h0[end] = src[0];
                }
                else if constexpr (CH == 2) {
                    h0[end] = src[0];
                    h1[end] = src[1];
                }
                else {
                    for (std::size_t c = 0; c < ch; ++c) {
                        m_hist[c][end] = src[c];
                    }
                }
                src += ch;
                --remain;
                ++end;
            };

            for (std::size_t produced = 0; produced < total; ++produced) {
                while (pending != 0) { // never outruns in_frames: total is exact
                    append();
                    --pending;
                }
                // Symmetry-halved table (M7 lever 3): a mirrored phase dots
                // its partner's stored row tap-reversed — same products,
                // same accumulation order, bit-identical to a full table.
                // The mirrored arm is a CALL (dot_mirrored), not an inline
                // expansion: two inlined dot bodies in this loop measurably
                // break the forward arm's unrolled codegen (M55 up_q15,
                // +3.3%), while a call against a ~300-insn dot is noise.
                const schedule_entry step = k_schedule<D>[pos];
                const coeff*         row  = m_table.stored_row(step.phase);
                const bool           mir  = basic_phase_table<S, D>::is_mirrored(step.phase);
                if constexpr (CH == 1) {
                    out[0] =
                        mir ? dot_mirrored<T>(row, h0 + end - taps) : tap::dsp::dot_row<S>(row, h0 + end - taps, taps);
                }
                else if constexpr (CH == 2) {
                    if (mir) {
                        out[0] = dot_mirrored<T>(row, h0 + end - taps);
                        out[1] = dot_mirrored<T>(row, h1 + end - taps);
                    }
                    else {
                        out[0] = tap::dsp::dot_row<S>(row, h0 + end - taps, taps);
                        out[1] = tap::dsp::dot_row<S>(row, h1 + end - taps, taps);
                    }
                }
                else {
                    for (std::size_t c = 0; c < ch; ++c) {
                        out[c] = mir ? dot_mirrored<T>(row, m_hist[c].data() + end - taps)
                                     : tap::dsp::dot_row<S>(row, m_hist[c].data() + end - taps, taps);
                    }
                }
                out += ch;
                pending = step.advance;
                pos     = pos + 1 == k_phases ? 0 : pos + 1;
            }
            while (remain != 0) { // leftover input smaller than the next gap
                append();
                --pending;
            }

            m_end     = end;
            m_pos     = static_cast<std::uint32_t>(pos);
            m_pending = static_cast<std::uint32_t>(pending);
            return total;
        }
        // ANCHOR_END: rt_superblock_walk

        /// The mirrored-phase dot, forced out of line (T = compile-time trip
        /// count, 0 = runtime): the walk's loop body keeps the forward
        /// tap::dsp::dot_row as its only inlined dot expansion — inlining
        /// both arms measurably regressed the forward arm's unrolled codegen
        /// — while this instantiation gets the same compile-time trip count
        /// on its own. Bit-exactness is the reversed kernel's contract:
        /// identical bits to dotting the materialized mirrored row.
        template <std::size_t T>
        TAP_RATIO_MIRRORED_DOT_ATTR S dot_mirrored(const coeff* row, const S* hist) const noexcept {
            return tap::dsp::dot_row_reversed<S>(row, hist, T != 0 ? T : m_table.taps());
        }

        /// One output frame at the current schedule position; advances state.
        void emit(S* out) noexcept {
            const schedule_entry step = k_schedule<D>[m_pos];
            const coeff*         row  = m_table.stored_row(step.phase);
            const bool           mir  = basic_phase_table<S, D>::is_mirrored(step.phase);
            const std::size_t    taps = m_table.taps();
            for (std::size_t c = 0; c < m_channels; ++c) {
                out[c] = mir ? tap::dsp::dot_row_reversed<S>(row, window(c), taps)
                             : tap::dsp::dot_row<S>(row, window(c), taps);
            }
            m_pending = step.advance;
            m_pos     = m_pos + 1 == k_phases ? 0 : m_pos + 1;
        }

        void append_frame(const S* frame) noexcept {
            compact_if_full();
            for (std::size_t c = 0; c < m_channels; ++c) {
                m_hist[c][m_end] = frame[c];
            }
            ++m_end;
        }

        void append_silence() noexcept {
            compact_if_full();
            for (std::size_t c = 0; c < m_channels; ++c) {
                m_hist[c][m_end] = tap::dsp::sample_traits<S>::silence();
            }
            ++m_end;
        }

        void compact_if_full() noexcept {
            if (m_end == m_hist_cap) {
                m_end = compact(m_end);
            }
        }

        /// Slide the newest T-1 frames to the front of every channel's
        /// history (in place — data() pointers stay valid); returns the new
        /// end index.
        std::size_t compact(std::size_t end) noexcept {
            const std::size_t keep = m_table.taps() - 1;
            for (auto& h : m_hist) {
                std::memmove(h.data(), h.data() + (end - keep), keep * sizeof(S));
            }
            return keep;
        }

        basic_phase_table<S, D>     m_table;
        std::size_t                 m_channels;
        std::size_t                 m_hist_cap;
        std::vector<std::vector<S>> m_hist;    // planar delay line per channel
        std::vector<S>              m_scratch; // interleaved staging for pull()
        std::size_t                 m_end     = 0;
        std::uint32_t               m_pos     = 0; // superblock position in [0, L)
        std::uint32_t               m_pending = 1; // inputs to consume before the next output
    };

    /// The float converters, one per direction — the golden-model profile.
    using converter_to_48k  = basic_converter<float, direction::up_to_48k>;
    using converter_to_44k1 = basic_converter<float, direction::down_to_44k1>;

    /// Q15 fixed-point converters (int16_t samples; the flagship embedded
    /// profile — Bluetooth-adjacent M33/M55 deployments). Integer-only hot
    /// loop via the tap::dsp Q15 core; the floor is the 16-bit format itself
    /// (see test_converter_fixed_point.cpp for the measured numbers).
    using converter_to_48k_q15  = basic_converter<std::int16_t, direction::up_to_48k>;
    using converter_to_44k1_q15 = basic_converter<std::int16_t, direction::down_to_44k1>;

    /// Q31 fixed-point converters (int32_t samples): matches the float
    /// datapath at the format-negligible level (parity pinned by test).
    using converter_to_48k_q31  = basic_converter<std::int32_t, direction::up_to_48k>;
    using converter_to_44k1_q31 = basic_converter<std::int32_t, direction::down_to_44k1>;

} // namespace tap::ratio
