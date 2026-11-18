/// @file converter.h
/// @brief The synchronous 44.1 <-> 48 kHz converter (and the pair at 2x, 4x): one direction, streamed.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "tap/dsp/detail/expects.h"
#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/bridge/design.h"
#include "tap/sr/bridge/phase_table.h"
#include "tap/sr/bridge/schedule.h"

// Out-of-lining attribute for the mirrored-phase dot (see dot_mirrored):
// measured per target, gated per target, the same pattern as the tap::dsp
// kernel gates. On Arm, two inlined dot expansions in the walk's loop body
// break the forward arm's unrolled codegen (M55 up_q15 +3.3%), so the
// mirrored arm is a call; hexagon-clang keeps both expansions tight inline
// and pays for the call instead (down_q31: +3.3% outlined, +2.7% inlined),
// so there the attribute is empty and the helper inlines away.
#if defined(__hexagon__)
#define TAP_SR_BRIDGE_MIRRORED_DOT_ATTR
#elif defined(_MSC_VER)
#define TAP_SR_BRIDGE_MIRRORED_DOT_ATTR __declspec(noinline)
#else
#define TAP_SR_BRIDGE_MIRRORED_DOT_ATTR __attribute__((noinline))
#endif
// The per-block entry points (process, pull, flush) stay out of line on every
// target: the walk is then register-allocated in the engine's own frame, so
// its instruction count is a property of the engine, not of what the caller
// has live around it (measured both ways on the M33, M55 and Hexagon; the
// family roadmap's A12, PLAN.md section 6).
#if defined(_MSC_VER)
#define TAP_SR_BRIDGE_NOINLINE __declspec(noinline)
#else
#define TAP_SR_BRIDGE_NOINLINE __attribute__((noinline))
#endif

namespace tap::sr::bridge {

    /// The count-independent half of basic_converter (A13's K12): the table,
    /// the stream position and every accessor that reads no history, for one
    /// format, direction and rate scale. Its constructor designs the table
    /// out of line, so an image that runs one converter type at several
    /// channel counts carries the design once; basic_converter<S, Ch, D, K>
    /// adds the per-count histories and the blocks.
    template <tap::dsp::sample_type S, direction D, unsigned K = 0>
    class converter_base {
      public:
        using coeff = typename tap::dsp::sample_traits<S>::coeff;

        static constexpr std::size_t k_phases     = ratio_traits<D, K>::k_phases;     ///< L
        static constexpr std::size_t k_decimation = ratio_traits<D, K>::k_decimation; ///< M
        static constexpr unsigned    k_rate_scale = K;                                ///< the pair times 2^K

        /// The canonical trip counts (taps per phase = MACs per output) the
        /// hot path hard-commits to at compile time; any other profile runs
        /// the runtime-length walk.
        static constexpr std::size_t k_taps_economy       = profile::economy().taps<D>();
        static constexpr std::size_t k_taps_super_economy = profile::super_economy().taps<D>();
        static constexpr std::size_t k_taps_balanced      = profile::balanced().taps<D>();
        static constexpr std::size_t k_taps_transparent   = profile::transparent().taps<D>();

        // ANCHOR: rt_validate
        /// The reason a converter cannot be built from p, or nullptr when it
        /// can: the family's error model. noexcept and allocation-free; it
        /// states every rejection the constructor makes other than a refused
        /// allocation (a profile the designer rejects, validate_profile<D, K>;
        /// a channel count of zero fails to compile). The constructor rejects
        /// with the same text (std::invalid_argument through tap::dsp::raise),
        /// so a build with exceptions may construct and catch; a build without
        /// them (-fno-exceptions, where the rejection terminates) calls this
        /// first and constructs only on nullptr.
        static const char* validate(const profile& p = profile::economy()) noexcept {
            return validate_profile<D, K>(p);
        }
        // ANCHOR_END: rt_validate

        // ANCHOR: rt_frames_needed
        /// Exact input frames required to produce the next out_frames outputs
        /// from the CURRENT stream position — deterministic arithmetic the
        /// async engine can never offer. pull() with a source delivering
        /// exactly this many frames produces exactly out_frames outputs.
        std::uint64_t frames_needed(std::uint64_t out_frames) const noexcept {
            if (out_frames == 0) {
                return 0;
            }
            return m_pending + tap::sr::bridge::frames_needed<D>(m_pos, out_frames - 1);
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

        /// Frames flush() will write from the current position.
        std::uint64_t flush_output_frames() const noexcept { return outputs_for(m_table.taps()); }

        /// Linear-phase group delay in input samples, (L*T - 1) / (2L) ~= T/2.
        double latency_input_frames() const noexcept { return m_table.group_delay_input_samples(); }

        std::size_t taps() const noexcept { return m_table.taps(); } ///< MACs per output sample
        std::size_t position() const noexcept { return m_pos; }      ///< superblock position in [0, L)

        const basic_phase_table<S, D, K>& table() const noexcept { return m_table; }

      protected:
        static constexpr std::size_t k_hist_slack = 64; ///< appends between compactions
        static constexpr std::size_t k_pop_chunk  = 16; ///< pull()'s bulk-pop granularity

        /// Designs the table; setup time only, out of line (one copy per
        /// format, direction and rate scale). Rejects what validate()
        /// rejects, with its text.
        TAP_SR_BRIDGE_NOINLINE explicit converter_base(const profile& p)
            : m_table(p)
            , m_hist_cap(m_table.taps() + k_hist_slack) {
            restart();
        }

        /// The stream position's initial state (the histories are the
        /// derived class's).
        void restart() noexcept {
            m_end     = m_table.taps();
            m_pos     = 0;
            m_pending = 1; // the pre-advance that delivers x[0] under output 0
        }

        basic_phase_table<S, D, K> m_table;
        std::size_t                m_hist_cap;
        std::size_t                m_end     = 0;
        std::uint32_t              m_pos     = 0; // superblock position in [0, L)
        std::uint32_t              m_pending = 1; // inputs to consume before the next output
    };

    // ANCHOR: rt_converter_doc
    /// Streaming fixed-ratio converter for Ch interleaved channels (a
    /// compile-time constant of at least 1, the family's A13) in one direction
    /// (compile-time D) at one rate scale (compile-time K: 0 is
    /// 44.1 <-> 48 kHz, 1 is 88.2 <-> 96, 2 is 176.4 <-> 192; ratio_traits).
    /// The machine is the same at every K — the ratio, the schedule and the
    /// table are K-independent, bit for bit — so K is a statement of which
    /// rates the frames carry, kept in the type per the family's rule that
    /// the caller declares the topology and nothing is inferred from a rate.
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
    template <tap::dsp::sample_type S, std::size_t Ch, direction D, unsigned K = 0>
    class basic_converter : public converter_base<S, D, K> {
        static_assert(Ch >= 1, "tap::sr::bridge: a converter's channel count is at least 1");
        using base = converter_base<S, D, K>;

      public:
        using base::k_phases;
        using typename base::coeff;

        /// The channel count, the type's (A13).
        static constexpr std::size_t channels() noexcept { return Ch; }

        /// Allocates the histories and designs the table; setup time only.
        /// Rejects what validate() rejects, with its text.
        explicit basic_converter(const profile& p = profile::economy())
            : base(p)
            , m_scratch(k_pop_chunk * Ch) {
            for (auto& h : m_hist) {
                h.assign(m_hist_cap, tap::dsp::sample_traits<S>::silence());
            }
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
        /// instead of per-frame index multiplies, and the channel count is
        /// the type's, so the mono and stereo shapes are their own bodies
        /// and every other count a loop of constant length. The dots
        /// themselves are the unchanged tap::dsp kernels, and the
        /// append/emit order is identical, so outputs stay bit-exact
        /// (pinned by the scipy-vector tests).
        ///
        /// M7 lever 2 commits the trip counts: the canonical profiles'
        /// taps-per-phase are compile-time facts (constexpr profile), so one
        /// dispatch per call hands the walk a constant dot length — the
        /// inlined kernels unroll and vectorize against it instead of a
        /// runtime bound. A custom-taps profile takes the runtime-length
        /// instantiation (pinned by the custom-profile parity test).
        TAP_SR_BRIDGE_NOINLINE std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            const std::size_t taps = m_table.taps();
            if (taps == base::k_taps_economy) {
                return process_walk<base::k_taps_economy>(in, in_frames, out);
            }
            if (taps == base::k_taps_super_economy) {
                return process_walk<base::k_taps_super_economy>(in, in_frames, out);
            }
            if (taps == base::k_taps_balanced) {
                return process_walk<base::k_taps_balanced>(in, in_frames, out);
            }
            if (taps == base::k_taps_transparent) {
                return process_walk<base::k_taps_transparent>(in, in_frames, out);
            }
            return process_walk<0>(in, in_frames, out);
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
        TAP_SR_BRIDGE_NOINLINE std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
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
                        append_frame(m_scratch.data() + i * Ch);
                        --m_pending;
                    }
                }
                emit(out + n * Ch);
            }
            return out_frames;
        }
        // ANCHOR_END: rt_pull

        /// End-of-stream: feed taps() zero frames and write the outputs that
        /// become ready — every output influenced by real input. Returns the
        /// frames written (== outputs_for(taps()) beforehand). The engine is
        /// left mid-stream in the zero-fed state; reset() before reuse.
        TAP_SR_BRIDGE_NOINLINE std::size_t flush(S* out) noexcept {
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
                emit(out + produced * Ch);
                ++produced;
            }
        }

        /// Return to the initial zero-primed state (position 0, silence).
        void reset() noexcept {
            for (auto& h : m_hist) {
                for (auto& v : h) {
                    v = tap::dsp::sample_traits<S>::silence();
                }
            }
            base::restart();
        }

      private:
        using base::k_hist_slack;
        using base::k_pop_chunk;
        using base::m_end;
        using base::m_hist_cap;
        using base::m_pending;
        using base::m_pos;
        using base::m_table;

        const S* window(std::size_t c) const noexcept { return m_hist[c].data() + m_end - m_table.taps(); }

        // ANCHOR: rt_superblock_walk
        /// The superblock walk behind process() (M7 levers 1 + 2), out of line
        /// per trip count (A13's gate 0: the block's codegen is then its own).
        /// Ch = 1 and 2 — the deployment shapes — have their per-channel loops
        /// dissolved and the planar history pointers hoisted (compaction
        /// memmoves in place, so they stay valid for the whole call); any
        /// other count is a loop of constant length. T is the compile-time
        /// dot length for the canonical profiles (0 = runtime). The schedule
        /// cursor, history end and pending gap run in locals so the state
        /// machine lives in registers, and the outputs_for() arithmetic
        /// settles the trip count before the first sample moves — the walk
        /// itself has no exhaustion checks. Same append/emit order and the
        /// same tap::dsp dot kernels as the naive loop: bit-exact by
        /// construction, pinned by the scipy-vector and pull-parity tests.
        template <std::size_t T>
        TAP_SR_BRIDGE_NOINLINE std::size_t process_walk(const S* in, std::size_t in_frames, S* out) noexcept {
            const std::size_t taps  = T != 0 ? T : m_table.taps();
            const std::size_t total = static_cast<std::size_t>(this->outputs_for(in_frames));

            S* const h0 = m_hist[0].data();
            S*       h1 = nullptr;
            if constexpr (Ch == 2) {
                h1 = m_hist[1].data();
            }

            const S*    src     = in;
            std::size_t remain  = in_frames;
            std::size_t end     = m_end;
            std::size_t pos     = m_pos;
            std::size_t pending = m_pending;

            const auto append = [&]() noexcept {
                if (end == m_hist_cap) {
                    end = compact(end);
                }
                if constexpr (Ch == 1) {
                    h0[end] = src[0];
                }
                else if constexpr (Ch == 2) {
                    h0[end] = src[0];
                    h1[end] = src[1];
                }
                else {
                    for (std::size_t c = 0; c < Ch; ++c) {
                        m_hist[c][end] = src[c];
                    }
                }
                src += Ch;
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
                if constexpr (Ch == 1) {
                    out[0] =
                        mir ? dot_mirrored<T>(row, h0 + end - taps) : tap::dsp::dot_row<S>(row, h0 + end - taps, taps);
                }
                else if constexpr (Ch == 2) {
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
                    for (std::size_t c = 0; c < Ch; ++c) {
                        out[c] = mir ? dot_mirrored<T>(row, m_hist[c].data() + end - taps)
                                     : tap::dsp::dot_row<S>(row, m_hist[c].data() + end - taps, taps);
                    }
                }
                out += Ch;
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
        TAP_SR_BRIDGE_MIRRORED_DOT_ATTR S dot_mirrored(const coeff* row, const S* hist) const noexcept {
            return tap::dsp::dot_row_reversed<S>(row, hist, T != 0 ? T : m_table.taps());
        }

        /// One output frame at the current schedule position; advances state.
        void emit(S* out) noexcept {
            const schedule_entry step = k_schedule<D>[m_pos];
            const coeff*         row  = m_table.stored_row(step.phase);
            const bool           mir  = basic_phase_table<S, D>::is_mirrored(step.phase);
            const std::size_t    taps = m_table.taps();
            for (std::size_t c = 0; c < Ch; ++c) {
                out[c] = mir ? tap::dsp::dot_row_reversed<S>(row, window(c), taps)
                             : tap::dsp::dot_row<S>(row, window(c), taps);
            }
            m_pending = step.advance;
            m_pos     = m_pos + 1 == k_phases ? 0 : m_pos + 1;
        }

        void append_frame(const S* frame) noexcept {
            compact_if_full();
            for (std::size_t c = 0; c < Ch; ++c) {
                m_hist[c][m_end] = frame[c];
            }
            ++m_end;
        }

        void append_silence() noexcept {
            compact_if_full();
            for (std::size_t c = 0; c < Ch; ++c) {
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

        std::array<std::vector<S>, Ch> m_hist;    // planar delay line per channel (K6)
        std::vector<S>                 m_scratch; // interleaved staging for pull()
    };

    /// The float converters, one per direction — the golden-model profile.
    template <std::size_t Ch>
    using converter_to_48k = basic_converter<float, Ch, direction::up_to_48k>;
    template <std::size_t Ch>
    using converter_to_44k1 = basic_converter<float, Ch, direction::down_to_44k1>;

    /// Q15 fixed-point converters (int16_t samples; the flagship embedded
    /// profile — Bluetooth-adjacent M33/M55 deployments). Integer-only hot
    /// loop via the tap::dsp Q15 core; the floor is the 16-bit format itself
    /// (see test_converter_fixed_point.cpp for the measured numbers).
    template <std::size_t Ch>
    using converter_to_48k_q15 = basic_converter<std::int16_t, Ch, direction::up_to_48k>;
    template <std::size_t Ch>
    using converter_to_44k1_q15 = basic_converter<std::int16_t, Ch, direction::down_to_44k1>;

    /// Q31 fixed-point converters (int32_t samples): matches the float
    /// datapath at the format-negligible level (parity pinned by test).
    template <std::size_t Ch>
    using converter_to_48k_q31 = basic_converter<std::int32_t, Ch, direction::up_to_48k>;
    template <std::size_t Ch>
    using converter_to_44k1_q31 = basic_converter<std::int32_t, Ch, direction::down_to_44k1>;

    /// The pair at 2x (K = 1): 88.2 <-> 96 kHz. The same machine as the base
    /// pair, bit for bit (ratio_traits); the name states which rates the
    /// frames carry. Images and aliases scale with the pair: going up, images
    /// land at >= 44.1 kHz; going down, aliases fold above 40.2 kHz.
    template <std::size_t Ch>
    using converter_to_96k = basic_converter<float, Ch, direction::up_to_48k, 1>;
    template <std::size_t Ch>
    using converter_to_88k2 = basic_converter<float, Ch, direction::down_to_44k1, 1>;
    template <std::size_t Ch>
    using converter_to_96k_q15 = basic_converter<std::int16_t, Ch, direction::up_to_48k, 1>;
    template <std::size_t Ch>
    using converter_to_88k2_q15 = basic_converter<std::int16_t, Ch, direction::down_to_44k1, 1>;
    template <std::size_t Ch>
    using converter_to_96k_q31 = basic_converter<std::int32_t, Ch, direction::up_to_48k, 1>;
    template <std::size_t Ch>
    using converter_to_88k2_q31 = basic_converter<std::int32_t, Ch, direction::down_to_44k1, 1>;

    /// The pair at 4x (K = 2): 176.4 <-> 192 kHz.
    template <std::size_t Ch>
    using converter_to_192k = basic_converter<float, Ch, direction::up_to_48k, 2>;
    template <std::size_t Ch>
    using converter_to_176k4 = basic_converter<float, Ch, direction::down_to_44k1, 2>;
    template <std::size_t Ch>
    using converter_to_192k_q15 = basic_converter<std::int16_t, Ch, direction::up_to_48k, 2>;
    template <std::size_t Ch>
    using converter_to_176k4_q15 = basic_converter<std::int16_t, Ch, direction::down_to_44k1, 2>;
    template <std::size_t Ch>
    using converter_to_192k_q31 = basic_converter<std::int32_t, Ch, direction::up_to_48k, 2>;
    template <std::size_t Ch>
    using converter_to_176k4_q31 = basic_converter<std::int32_t, Ch, direction::down_to_44k1, 2>;

} // namespace tap::sr::bridge
