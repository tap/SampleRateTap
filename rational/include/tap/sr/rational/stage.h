/// @file stage.h
/// @brief basic_stage<S, R>: one Nyquist stage at ratio L/M — table, schedule, process / pull / flush.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// One stage converts by one ratio L/M of the charter with ONE L-th-band
// (Nyquist) filter (PLAN.md R2, R4), designed by design.h at the stage's
// band B = max(L, M) and driven by bridge's compile-time (phase, advance)
// schedule. The three shapes of the vocabulary run on two machines, chosen
// at compile time by the ratio:
//
//   - ratio<L, M> with L >= 2 (an interpolator, or a mixed ratio): the
//     L-phase polyphase machine of bridge's converter. Output n dots phase
//     (n M) mod L of the table against the T newest inputs, T = ceil(N / L),
//     and consumes floor((n + 1) M / L) - floor(n M / L) inputs after it:
//     y[n] = sum_k x[floor(n M / L) - k] * h[phase(n) + k L], x[< 0] = 0 —
//     scipy's upfirdn(h, x, up = L, down = M), sample for sample from n = 0.
//     Each phase row is stored tap-reversed and TRIMMED to its nonzero span:
//     an interpolator's centre phase is the single tap 1 (a copy, one MAC),
//     the zero-padded last phase of a mixed ratio costs its real taps only.
//     Skipping an exact-zero product changes no bit (x * 0 adds 0 to the
//     accumulator in every format), so the trimmed dot is the full formula's.
//   - ratio<1, M> (a decimator): the M-branch commutator. Input n goes to
//     sub-line n mod M; output k, produced as x[k M] arrives, is
//     y[k] = sum_j dot(branch j, sub-line (M - j) mod M) with branch j the
//     taps h[j + s M], s ascending = older samples: scipy's
//     upfirdn(h, x, down = M), x[< 0] = 0, n inputs give ceil(n / M) outputs
//     (decimate.h's alignment). The branches are summed in j order through
//     ONE accumulator (tap::dsp::accumulate_row) and finalized once, so the
//     rounding point stays single; the centre branch (j = M - 1) is the
//     structural zeros around the centre tap, trimmed to that one tap. MACs
//     per output are therefore the design's nonzero count, 2 m (M - 1) + 1:
//     a half-band decimator costs 2 m + 1 of its 4 m - 1 taps (R4).
//
// Table gain (DC gain 1 at every output): the design's B branches each sum
// to 1 (nyquist.h). Going up (L = B) the L phases ARE those branches and the
// table is design_stage<R>'s vector bit for bit. Going down the table is
// scaled so that each of its L phases sums to 1 — for a decimator h / M
// (the single phase sums to 1), for a mixed ratio each L-phase normalized at
// the branch-spread level, bridge's design.h argument — and the structural
// zeros stay exactly 0 in every format. Fixed-point rows are quantized with
// quantize.h's row-sum preservation per phase (per whole filter for a Q31
// decimator), so unity DC survives the format exactly. A Q15 decimator is the
// exception: Q1.14 cannot hold h / M at the precision h needs (each
// coefficient M times smaller on the same LSB costs about 20 log10 M dB of
// stopband), so its table is the unscaled design, each of its M branches
// quantized at its own unity sum — bit for bit the Q15 interpolator's table
// of the same band — and the 1 / M is applied in the single rounding
// (tap::dsp::finalize_divided: a shift for M = 2, 8, an exact-at-DC
// multiply-back for M = 3, 6). A Q15 mixed ratio going down has the same
// shrinkage at a smaller factor (its unity rows' centre tap is about L / M),
// so its rows are held at a power-of-two gain G (G L / M < 2: 2/3 and 3/4
// at 2, 3/8 at 4), shifted back out exactly in the rounding. k_table_gain
// states what the rows sum to; finalize_output() is the stage's rounding
// point.
//
// Contract, as numbers (PLAN.md 2.5, R7, R9, R10):
//   - zero-primed and causal; outputs_for(n) and frames_needed(k) are exact
//     arithmetic from the current position (schedule or decimation phase)
//     for every count the address space can hold (n L and k M below
//     SIZE_MAX; beyond that the arithmetic wraps, as the C ABI's uint64
//     counts do);
//     pull() with a source delivering exactly frames_needed(k) frames yields
//     exactly k outputs, and is bit-identical to process() on the same
//     stream, for any chunking; from any state process() or flush() leaves
//     the stage in, outputs_for(n) <= floor(n L / M) + 2, the bound
//     tap::dsp::chain sizes its scratch by (pull() can stop with up to
//     L - 1 outputs banked and exceed it, which is why a chain resets the
//     stages it is given and hands them out read-only);
//   - latency_output_frames() = (N - 1) / 2 samples at the composite rate
//     L f_in = (N - 1) / (2 M) output frames, an exact rational
//     (tap::dsp::exact_ratio); latency_seconds(out_rate_hz) is its double;
//   - flush() feeds window_frames() zeros and writes every output that
//     becomes ready, flush_output_frames() of them; reset() returns to the
//     zero-primed state;
//   - the constructor designs and allocates (may throw); process(), pull(),
//     flush() and reset() are noexcept and allocation-free; one stream per
//     instance, channels planar inside and interleaved at the API, every
//     channel sharing the coefficient row per frame;
//   - the stage satisfies tap::dsp::sync_stage, so chains compose it;
//   - fixed point (PLAN.md section 6, M5; pinned by test_fixed_point.cpp):
//     every row sums to exact unity in the format, so full-scale DC of
//     either sign comes out at exactly full scale; full-scale drive
//     saturates in the trait's finalize and never wraps; Q31 tracks double
//     within 3.4e-9 of full scale on the reference noise (480 frames at
//     peak 0.9; 1.6e-8 under full-scale drive); Q15 is format-limited — RMS -90.2 to
//     -99.8 dBFS from double, attained stopband -70.0 to -83.1 dB (every
//     stage the 70 dB tier; transparent's 120 dB is float's), a decimator's
//     equal to the interpolator's of its band, stated per stage in PLAN.md;
//   - MACs: no structural zero enters the dot of an interpolator, a
//     decimator or a mixed ratio going up; a mixed ratio going down (band
//     M, rows of stride L) multiplies the band's zeros its rows cross.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "tap/dsp/chain.h"
#include "tap/dsp/detail/expects.h"
#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/quantize.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/rational/design.h"
#include "tap/sr/rational/ratio.h"

// The per-block entry points (process, pull, flush) stay out of line: the
// hot loop is then register-allocated in the engine's own frame, so its
// instruction count is a property of the engine and not of whatever the
// caller happens to have live around it (measured both ways on the M33, M55
// and Hexagon, the ratchet's record in PLAN.md section 6: inlined into a
// large caller the inner MAC loop lost its two pointers to the stack).
#if defined(_MSC_VER)
#define TAP_SR_RATIONAL_NOINLINE __declspec(noinline)
#else
#define TAP_SR_RATIONAL_NOINLINE __attribute__((noinline))
#endif

namespace tap::sr::rational {

    // ANCHOR: rational_schedule
    /// One output's step of the L-phase machine: the phase to dot and the
    /// inputs to consume after it (bridge's schedule_entry). The advance is
    /// a byte wherever every advance of the ratio fits one (ceil(M / L) <=
    /// 255: the whole vocabulary, the 4-byte entry the ratchet measured) and
    /// 32 bits for the wide ratios of the charter (ratio<3, 1024>: 342), so
    /// no charter ratio is truncated and no pinned count moves (the 8-byte
    /// entry cost the M55 Q15 interpolators 8.7 %, the 2026-10 audit's S1).
    template <rational_ratio R>
    struct schedule_entry {
        using advance_type =
            std::conditional_t<((R::k_down + R::k_up - 1) / R::k_up) <= 255, std::uint8_t, std::uint32_t>;
        std::uint16_t phase;   ///< polyphase branch index in [0, L)
        advance_type  advance; ///< input frames consumed after this output, up to ceil(M / L)
    };

    /// The superblock of ratio R: entry n serves output k L + n; phase(n) =
    /// (n M) mod L visits every phase once per L outputs; the advances sum
    /// to M. For an interpolator the advances are L - 1 zeros then a 1; for
    /// a decimator (L = 1) the single entry advances by M.
    template <rational_ratio R>
    constexpr std::array<schedule_entry<R>, R::k_up> make_schedule() noexcept {
        constexpr std::size_t                  l = R::k_up;
        constexpr std::size_t                  m = R::k_down;
        std::array<schedule_entry<R>, R::k_up> s{};
        for (std::size_t n = 0; n < l; ++n) {
            s[n].phase   = static_cast<std::uint16_t>((n * m) % l);
            s[n].advance = static_cast<typename schedule_entry<R>::advance_type>(((n + 1) * m) / l - (n * m) / l);
        }
        return s;
    }

    template <rational_ratio R>
    inline constexpr std::array<schedule_entry<R>, R::k_up> k_schedule = make_schedule<R>();

    /// Inputs the next out_frames outputs need from superblock position pos:
    /// floor((pos + out) M / L) - floor(pos M / L), pure arithmetic.
    template <rational_ratio R>
    constexpr std::uint64_t schedule_frames_needed(std::size_t pos, std::uint64_t out_frames) noexcept {
        constexpr std::uint64_t l = R::k_up;
        constexpr std::uint64_t m = R::k_down;
        const std::uint64_t     p = pos % l;
        return (p + out_frames) * m / l - p * m / l;
    }
    // ANCHOR_END: rational_schedule

    // ANCHOR: rational_stage
    /// One Nyquist stage at ratio R over sample format S; see the file header.
    template <tap::dsp::sample_type S, rational_ratio R>
    class basic_stage {
      public:
        using sample = S;
        using coeff  = typename tap::dsp::sample_traits<S>::coeff;
        using traits = ratio_traits<R>;

        static constexpr std::size_t k_up           = traits::k_up;   ///< L
        static constexpr std::size_t k_down         = traits::k_down; ///< M
        static constexpr std::size_t k_band         = traits::k_band; ///< B = max(L, M)
        static constexpr bool        k_is_decimator = traits::k_is_decimator;
        // ANCHOR: rational_gain
        /// A Q15 decimator quantizes each of its M branches at the branch's
        /// own unity sum and divides the summed branches by M in the single
        /// rounding (tap::dsp::finalize_divided): Q1.14 cannot hold h / M at
        /// the precision h needs (see the file header).
        static constexpr bool k_branch_quantized = k_is_decimator && std::is_same_v<S, std::int16_t>;
        /// A Q15 mixed ratio going down holds each L-phase row at a power-of-
        /// two gain G instead of unity: normalized to unity its centre tap is
        /// about L / M (2/3: 0.67, 3/8: 0.38), so the Q1.14 range is part
        /// unused; G is the largest power of two with G L / M < 2, which keeps
        /// every tap inside Q1.14 (2/3 and 3/4: 2, 3/8: 4), and the single
        /// rounding shifts it back out exactly.
        static constexpr std::size_t k_mixed_down_gain = [] {
            std::size_t g = 1;
            while (2 * g * k_up < 2 * k_down) {
                g *= 2;
            }
            return g;
        }();
        static constexpr bool k_row_scaled = !traits::k_is_up && !k_is_decimator && std::is_same_v<S, std::int16_t>;
        /// What the table's rows sum to, in units of the format's unity: 1,
        /// M for a Q15 decimator (M branches each at unity), the power-of-two
        /// G above for a Q15 mixed ratio going down; the single rounding
        /// divides it back out.
        static constexpr std::size_t k_table_gain =
            k_branch_quantized ? k_down : (k_row_scaled ? k_mixed_down_gain : 1);

        /// The stage's single rounding point, accumulator -> sample: the
        /// trait's finalize, or finalize_divided by k_table_gain.
        static S finalize_output(typename tap::dsp::sample_traits<S>::accum acc) noexcept {
            if constexpr (k_table_gain != 1) {
                return tap::dsp::finalize_divided<S, static_cast<std::uint32_t>(k_table_gain)>(acc);
            }
            else {
                return tap::dsp::sample_traits<S>::finalize(acc);
            }
        }
        // ANCHOR_END: rational_gain
        /// Rows of the table: L phases, or the M branches of a decimator.
        static constexpr std::size_t k_rows = k_is_decimator ? k_down : k_up;

        // ANCHOR: rational_validate
        /// The reason a stage cannot be built from (channels, p), or nullptr
        /// when it can: the family's error model. noexcept; it states every
        /// rejection the constructor makes other than a refused allocation —
        /// zero channels, a profile the designer rejects (validate(p)), a
        /// spec no length up to the search bound meets. Allocation-free for a
        /// profile with a pin for this stage's band (the four named profiles
        /// and their relaxations); a custom profile runs the design search,
        /// as construction does. The constructor rejects with the same text
        /// (std::invalid_argument, or std::runtime_error for the unmeetable
        /// spec, through tap::dsp::raise), so a build with exceptions may
        /// construct and catch; a build without them (-fno-exceptions, where
        /// the rejection terminates) calls this first and constructs only on
        /// nullptr.
        static const char* validate(std::size_t channels, const profile& p = profile::economy()) noexcept {
            if (channels == 0) {
                return "tap::sr::rational::basic_stage: channels == 0";
            }
            if (const char* why = tap::sr::rational::validate(p)) {
                return why;
            }
            if (taps_per_branch_for(ratio_traits<R>::k_band, p) == 0) {
                return k_unmeetable_spec_reason;
            }
            return nullptr;
        }
        // ANCHOR_END: rational_validate

        /// Designs the table and allocates the histories; setup time only.
        /// Rejects what validate() rejects, with its text.
        explicit basic_stage(std::size_t channels = 1, const profile& p = profile::economy())
            : m_channels(channels) {
            if (channels == 0) {
                tap::dsp::raise<std::invalid_argument>("tap::sr::rational::basic_stage: channels == 0");
            }
            const std::vector<double> h = design_stage<R>(p);
            m_taps                      = h.size();
            build_table(h);
            m_hist_cap = m_row_len + k_hist_slack;
            m_hist.assign(channels * k_lines * m_hist_cap, tap::dsp::sample_traits<S>::silence());
            m_scratch.resize(k_pop_chunk * channels);
            reset();
        }

        /// Push-transform: consumes all in_frames interleaved frames, writes
        /// outputs_for(in_frames) frames (out must hold them); returns that.
        TAP_SR_RATIONAL_NOINLINE std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            // The channel count as a compile-time constant for the common
            // counts (bridge's process() does the same for its tap count): the
            // channel loops unroll, and the block runs with two fewer live
            // values in the engine's own frame.
            switch (m_channels) {
            case 1:
                return process_block<1>(in, in_frames, out);
            case 2:
                return process_block<2>(in, in_frames, out);
            default:
                return process_block<0>(in, in_frames, out);
            }
        }

        /// process() for a channel count known at compile time (Ch), or at
        /// run time (Ch == 0).
        template <std::size_t Ch>
        std::size_t process_block(const S* in, std::size_t in_frames, S* out) noexcept {
            std::size_t produced = 0;
            cursor      b        = open();
            if constexpr (Ch != 0) {
                b.channels = Ch;
            }
            if constexpr (k_is_decimator) {
                for (std::size_t i = 0; i < in_frames; ++i) {
                    append_frame(b, in + i * b.channels);
                    if (b.dphase == 0) {
                        emit(b, out + produced * b.channels);
                        ++produced;
                    }
                    b.dphase = b.dphase + 1 == k_down ? 0 : b.dphase + 1;
                }
            }
            else {
                const std::size_t total = outputs_for(in_frames);
                std::size_t       fed   = 0;
                for (; produced < total; ++produced) {
                    while (b.pending != 0) { // never outruns in_frames: total is exact
                        append_frame(b, in + fed * b.channels);
                        ++fed;
                        --b.pending;
                    }
                    emit(b, out + produced * b.channels);
                }
                while (fed < in_frames) { // leftover input smaller than the next gap
                    append_frame(b, in + fed * b.channels);
                    ++fed;
                    --b.pending;
                }
            }
            close(b);
            return produced;
        }

        /// Pull: produce exactly out_frames frames, drawing input through
        /// pop(dst, max_frames) -> frames delivered (interleaved; fewer means
        /// the source ran dry and the call returns short, resumable). Bit-
        /// identical to process() on the same stream. PopFn must be noexcept.
        template <typename PopFn>
        TAP_SR_RATIONAL_NOINLINE std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
            static_assert(std::is_nothrow_invocable_r_v<std::size_t, PopFn&, S*, std::size_t>,
                          "pull() requires a noexcept PopFn: std::size_t(S*, std::size_t) noexcept");
            switch (m_channels) { // the channel count as a constant, as process()
            case 1:
                return pull_block<1>(out, out_frames, pop);
            case 2:
                return pull_block<2>(out, out_frames, pop);
            default:
                return pull_block<0>(out, out_frames, pop);
            }
        }

        template <std::size_t Ch, typename PopFn>
        std::size_t pull_block(S* out, std::size_t out_frames, PopFn& pop) noexcept {
            cursor b = open();
            if constexpr (Ch != 0) {
                b.channels = Ch;
            }
            for (std::size_t n = 0; n < out_frames; ++n) {
                // The inputs the next output needs: the schedule's pending
                // gap, or for a decimator the pushes up to and including the
                // one at phase 0 (whose frame emits). Consumed one by one,
                // so a dry source leaves the exact partial state behind.
                std::size_t need = pending_inputs(b);
                while (need != 0) {
                    const std::size_t want = need < k_pop_chunk ? need : k_pop_chunk;
                    const std::size_t got  = pop(m_scratch.data(), want);
                    if (got == 0) {
                        close(b);
                        return n; // dry
                    }
                    for (std::size_t i = 0; i < got && i < want; ++i) {
                        append_frame(b, m_scratch.data() + i * b.channels);
                        consume_one(b);
                        --need;
                    }
                }
                emit(b, out + n * b.channels);
            }
            close(b);
            return out_frames;
        }

        /// Outputs the next in_frames inputs yield from the current position.
        std::size_t outputs_for(std::size_t in_frames) const noexcept {
            if constexpr (k_is_decimator) {
                const std::size_t first = 1 + (k_down - m_dphase) % k_down;
                return in_frames < first ? 0 : 1 + (in_frames - first) / k_down;
            }
            else {
                if (in_frames < m_pending) {
                    return 0;
                }
                constexpr std::size_t l    = k_up;
                constexpr std::size_t m    = k_down;
                const std::size_t     base = m_pos * m / l;
                const std::size_t     a    = in_frames - m_pending;
                return (l * (base + a + 1) - 1) / m - m_pos + 1;
            }
        }

        /// Inputs the next out_frames outputs need from the current position:
        /// the smallest n with outputs_for(n) >= out_frames.
        std::size_t frames_needed(std::size_t out_frames) const noexcept {
            if (out_frames == 0) {
                return 0;
            }
            if constexpr (k_is_decimator) {
                return 1 + (k_down - m_dphase) % k_down + (out_frames - 1) * k_down;
            }
            else {
                return m_pending + static_cast<std::size_t>(schedule_frames_needed<R>(m_pos, out_frames - 1));
            }
        }

        /// End of stream: feeds window_frames() zeros and writes the outputs
        /// that become ready — flush_output_frames() of them. The stage is
        /// left mid-stream in the zero-fed state; reset() before reuse.
        TAP_SR_RATIONAL_NOINLINE std::size_t flush(S* out) noexcept {
            switch (m_channels) { // the channel count as a constant, as process()
            case 1:
                return flush_block<1>(out);
            case 2:
                return flush_block<2>(out);
            default:
                return flush_block<0>(out);
            }
        }

        template <std::size_t Ch>
        std::size_t flush_block(S* out) noexcept {
            const std::size_t zeros = window_frames();
            std::size_t       fed   = 0;
            std::size_t       made  = 0;
            cursor            b     = open();
            if constexpr (Ch != 0) {
                b.channels = Ch;
            }
            if constexpr (k_is_decimator) {
                for (; fed < zeros; ++fed) {
                    append_silence(b);
                    if (b.dphase == 0) {
                        emit(b, out + made * b.channels);
                        ++made;
                    }
                    b.dphase = b.dphase + 1 == k_down ? 0 : b.dphase + 1;
                }
            }
            else {
                for (;;) {
                    while (b.pending != 0 && fed < zeros) {
                        append_silence(b);
                        ++fed;
                        --b.pending;
                    }
                    if (b.pending != 0) {
                        break;
                    }
                    emit(b, out + made * b.channels);
                    ++made;
                }
            }
            close(b);
            return made;
        }

        /// Frames flush() will write from the current position.
        std::size_t flush_output_frames() const noexcept { return outputs_for(window_frames()); }

        /// Return to the zero-primed initial state.
        void reset() noexcept {
            for (auto& v : m_hist) {
                v = tap::dsp::sample_traits<S>::silence();
            }
            for (auto& e : m_end) {
                e = m_row_len;
            }
            m_pos     = 0;
            m_pending = 1; // the pre-advance that delivers x[0] under output 0
            m_dphase  = 0;
        }

        /// Group delay at the output rate: (N - 1) / (2 M), exact (R7).
        tap::dsp::exact_ratio latency_output_frames() const noexcept {
            return tap::dsp::exact_ratio{static_cast<std::uint64_t>((m_taps - 1) / 2),
                                         static_cast<std::uint64_t>(k_down)};
        }
        /// latency_output_frames() in seconds at the stage's output rate.
        double latency_seconds(double out_rate_hz) const noexcept {
            return latency_output_frames().value() / out_rate_hz;
        }

        std::size_t channels() const noexcept { return m_channels; }
        std::size_t taps() const noexcept { return m_taps; }          ///< N, the Nyquist design's length
        std::size_t row_length() const noexcept { return m_row_len; } ///< T: taps per phase (per branch, decimating)
        std::size_t position() const noexcept { return m_pos; } ///< superblock position in [0, L) (L-phase machine)
        std::size_t decimation_phase() const noexcept { return m_dphase; } ///< inputs since the last output (decimator)

        /// Input frames whose window every output depends on: the longest
        /// history any row reads, in input frames.
        std::size_t window_frames() const noexcept { return k_is_decimator ? m_taps - 1 : m_row_len; }

        /// Multiply-accumulates one output costs, summed over a superblock
        /// of L outputs (the trimmed rows' lengths): the nonzero count for a
        /// decimator, (L - 1) 2 m + 1 for an interpolator.
        std::size_t macs_per_superblock() const noexcept {
            std::size_t total = 0;
            for (std::size_t r = 0; r < k_rows; ++r) {
                total += m_count[r];
            }
            return total;
        }
        /// MACs per output frame per channel, averaged over the superblock.
        double macs_per_output() const noexcept {
            return static_cast<double>(macs_per_superblock()) / static_cast<double>(k_is_decimator ? 1 : k_up);
        }

        /// Logical coefficient of row r (phase, or decimating branch) at tap
        /// t in storage order (tap-reversed: t = 0 is the oldest sample's);
        /// the cold-path accessor for tests. Trimmed-away taps read as 0.
        coeff coefficient(std::size_t r, std::size_t t) const noexcept {
            return (t >= m_first[r] && t < m_first[r] + m_count[r]) ? m_rows[r * m_row_len + t] : static_cast<coeff>(0);
        }

      private:
        static constexpr std::size_t k_hist_slack = 64; ///< appends between compactions
        static constexpr std::size_t k_pop_chunk  = 16; ///< pull()'s bulk-pop granularity
        /// Delay lines per channel: the M sub-lines of a decimator, else one.
        static constexpr std::size_t k_lines = k_is_decimator ? k_down : 1;

        // ANCHOR: rational_build_table
        /// Builds the rows from the design: scaling (file header), tap
        /// reversal, trimming to the nonzero span, quantization.
        void build_table(const std::vector<double>& h) {
            const std::size_t n = h.size();
            if constexpr (k_is_decimator) {
                // The whole filter at sum 1, quantized as ONE row so the
                // fixed-point sum lands on unity exactly, then scattered to
                // the M branches (branch j: taps j + s M, s ascending =
                // older, so the row is stored reversed: oldest first). In
                // Q15 each branch of the unscaled design (every branch of a
                // Nyquist filter sums to 1) is quantized on its own at unity
                // instead, and the 1 / M is applied in the finalize.
                std::vector<coeff> q(n);
                if constexpr (k_branch_quantized) {
                    std::vector<double> branch((n + k_down - 1) / k_down);
                    std::vector<coeff>  qb(branch.size());
                    for (std::size_t j = 0; j < k_down; ++j) {
                        const std::size_t len = (n - j + k_down - 1) / k_down; // taps j, j + M, ...
                        for (std::size_t s = 0; s < len; ++s) {
                            branch[s] = h[j + s * k_down];
                        }
                        tap::dsp::quantize_row_preserving_sum<S>(std::span<const double>(branch.data(), len),
                                                                 std::span<coeff>(qb.data(), len));
                        for (std::size_t s = 0; s < len; ++s) {
                            q[j + s * k_down] = qb[s];
                        }
                    }
                }
                else {
                    std::vector<double> scaled(n);
                    for (std::size_t i = 0; i < n; ++i) {
                        scaled[i] = h[i] / static_cast<double>(k_down);
                    }
                    tap::dsp::quantize_row_preserving_sum<S>(scaled, q);
                }
                m_row_len = (n + k_down - 1) / k_down; // 2 m
                m_rows.assign(k_rows * m_row_len, static_cast<coeff>(0));
                for (std::size_t j = 0; j < k_rows; ++j) {
                    const std::size_t len = (n - j + k_down - 1) / k_down; // taps j, j + M, ...
                    // Right-aligned to the newest sample (s = 0 at the row's
                    // end): a branch one tap short pads at its OLD end.
                    for (std::size_t s = 0; s < len; ++s) {
                        m_rows[j * m_row_len + (m_row_len - 1 - s)] = q[j + s * k_down];
                    }
                    trim_row(j);
                }
            }
            else {
                m_row_len = (n + k_up - 1) / k_up; // T = ceil(N / L)
                m_rows.assign(k_rows * m_row_len, static_cast<coeff>(0));
                std::vector<double> row(m_row_len);
                std::vector<coeff>  q(m_row_len);
                for (std::size_t p = 0; p < k_rows; ++p) {
                    double sum = 0.0;
                    for (std::size_t t = 0; t < m_row_len; ++t) {
                        const std::size_t i    = p + t * k_up;
                        const double      v    = i < n ? h[i] : 0.0;
                        row[m_row_len - 1 - t] = v;
                        sum += v;
                    }
                    if constexpr (!traits::k_is_up) {
                        // A mixed ratio going down: the L phases are not the
                        // design's M branches; normalize each to DC gain 1
                        // (k_table_gain in Q15, divided out in the rounding).
                        for (auto& v : row) {
                            v = v / sum * static_cast<double>(k_table_gain);
                        }
                    }
                    tap::dsp::quantize_row_preserving_sum<S>(row, q);
                    for (std::size_t t = 0; t < m_row_len; ++t) {
                        m_rows[p * m_row_len + t] = q[t];
                    }
                    trim_row(p);
                }
            }
        }
        // ANCHOR_END: rational_build_table

        /// Records the nonzero span [first, first + count) of row r.
        void trim_row(std::size_t r) noexcept {
            const coeff* row   = m_rows.data() + r * m_row_len;
            std::size_t  first = 0;
            while (first < m_row_len && row[first] == static_cast<coeff>(0)) {
                ++first;
            }
            std::size_t last = m_row_len;
            while (last > first && row[last - 1] == static_cast<coeff>(0)) {
                --last;
            }
            m_first[r] = first;
            m_count[r] = last - first;
        }

        // ANCHOR: rational_cursor
        /// The hot loops' view of the stage for one call: the stream state
        /// (where each sub-line ends, the schedule position, the pending gap,
        /// the decimator's phase) and the constants they index with, as
        /// locals. open() loads it, the block runs against it, close() stores
        /// the state back. The members stay authoritative between calls;
        /// inside one, this is what lets the compiler keep the loop's state
        /// in registers: with the entry out of line, `this` is reachable from
        /// compact()'s memmove, so every field the loop carried would be
        /// written back and reloaded around each frame.
        struct cursor {
            S*           hist;
            const coeff* rows;
            std::size_t  channels;
            std::size_t  row_len;
            std::size_t  cap;
            /// The decimator's rows' nonzero spans (m_first / m_count), which
            /// its unrolled emit reads as scalars; the interpolator indexes
            /// them by a runtime phase and reads the members (measured: the
            /// copy cost its Q15 rows 4 % on the M55, and saved the
            /// decimator's float rows 3 %).
            std::array<std::size_t, k_is_decimator ? k_rows : 0> first;
            std::array<std::size_t, k_is_decimator ? k_rows : 0> count;
            std::array<std::size_t, k_lines>                     end;
            std::uint32_t                                        pos;
            std::uint32_t                                        pending;
            std::uint32_t                                        dphase;
        };

        cursor open() noexcept {
            cursor b{};
            b.hist     = m_hist.data();
            b.rows     = m_rows.data();
            b.channels = m_channels;
            b.row_len  = m_row_len;
            b.cap      = m_hist_cap;
            if constexpr (k_is_decimator) {
                b.first = m_first;
                b.count = m_count;
            }
            b.end     = m_end;
            b.pos     = m_pos;
            b.pending = m_pending;
            b.dphase  = m_dphase;
            return b;
        }

        void close(const cursor& b) noexcept {
            m_end     = b.end;
            m_pos     = b.pos;
            m_pending = b.pending;
            m_dphase  = b.dphase;
        }
        // ANCHOR_END: rational_cursor

        /// Inputs to push before the next output: the schedule's pending gap,
        /// or for a decimator the pushes through the one at phase 0 (which
        /// emits): 1 + (M - phase) mod M.
        std::size_t pending_inputs() const noexcept {
            if constexpr (k_is_decimator) {
                return 1 + (k_down - m_dphase) % k_down;
            }
            else {
                return m_pending;
            }
        }

        static std::size_t pending_inputs(const cursor& b) noexcept {
            if constexpr (k_is_decimator) {
                return 1 + (k_down - b.dphase) % k_down;
            }
            else {
                return b.pending;
            }
        }

        static void consume_one(cursor& b) noexcept {
            if constexpr (k_is_decimator) {
                b.dphase = b.dphase + 1 == k_down ? 0 : b.dphase + 1;
            }
            else {
                --b.pending;
            }
        }

        /// Channel c's sub-line sub: the history is one planar buffer,
        /// channels x k_lines lines of cap samples each.
        static S* line(const cursor& b, std::size_t c, std::size_t sub) noexcept {
            return b.hist + (c * k_lines + sub) * b.cap;
        }

        /// Appends one interleaved frame to the history (the decimator's
        /// current sub-line); compacts a full line first.
        static void append_frame(cursor& b, const S* frame) noexcept {
            const std::size_t sub = k_is_decimator ? b.dphase : 0;
            if (b.end[sub] == b.cap) {
                compact(b, sub);
            }
            for (std::size_t c = 0; c < b.channels; ++c) {
                line(b, c, sub)[b.end[sub]] = frame[c];
            }
            ++b.end[sub];
        }

        static void append_silence(cursor& b) noexcept {
            const std::size_t sub = k_is_decimator ? b.dphase : 0;
            if (b.end[sub] == b.cap) {
                compact(b, sub);
            }
            for (std::size_t c = 0; c < b.channels; ++c) {
                line(b, c, sub)[b.end[sub]] = tap::dsp::sample_traits<S>::silence();
            }
            ++b.end[sub];
        }

        /// Slides the newest row_len - 1 samples of sub-line sub to the front
        /// of every channel's line (in place).
        static void compact(cursor& b, std::size_t sub) noexcept {
            const std::size_t keep = b.row_len - 1;
            for (std::size_t c = 0; c < b.channels; ++c) {
                S* l = line(b, c, sub);
                std::memmove(l, l + (b.end[sub] - keep), keep * sizeof(S));
            }
            b.end[sub] = keep;
        }

        // ANCHOR: rational_emit
        /// One output frame at the current position; advances the schedule
        /// (the decimator's phase advances in process / pull / flush).
        void emit(cursor& b, S* out) const noexcept {
            using tr = tap::dsp::sample_traits<S>;
            if constexpr (k_is_decimator) {
                for (std::size_t c = 0; c < b.channels; ++c) {
                    typename tr::accum acc{};
                    for (std::size_t j = 0; j < k_rows; ++j) {
                        const std::size_t sub    = (k_down - j) % k_down;
                        const S*          window = line(b, c, sub) + b.end[sub] - b.row_len + b.first[j];
                        acc = tap::dsp::accumulate_row<S>(acc, b.rows + j * b.row_len + b.first[j], window, b.count[j]);
                    }
                    out[c] = finalize_output(acc);
                }
            }
            else {
                const schedule_entry<R> step   = k_schedule<R>[b.pos];
                const coeff*            row    = b.rows + step.phase * b.row_len + m_first[step.phase];
                const std::size_t       count  = m_count[step.phase];
                const S*                window = line(b, 0, 0) + b.end[0] - b.row_len + m_first[step.phase];
                for (std::size_t c = 0; c < b.channels; ++c, window += b.cap) {
                    if constexpr (k_table_gain != 1) {
                        out[c] = finalize_output(tap::dsp::accumulate_row<S>(typename tr::accum{}, row, window, count));
                    }
                    else {
                        out[c] = tap::dsp::dot_row<S>(row, window, count);
                    }
                }
                b.pending = step.advance;
                b.pos     = b.pos + 1 == k_up ? 0 : b.pos + 1;
            }
        }
        // ANCHOR_END: rational_emit

        std::size_t                      m_channels;
        std::size_t                      m_taps     = 0; ///< N
        std::size_t                      m_row_len  = 0; ///< T (per phase or per branch)
        std::size_t                      m_hist_cap = 0;
        std::vector<coeff>               m_rows;        ///< k_rows x T, tap-reversed
        std::array<std::size_t, k_rows>  m_first{};     ///< nonzero span start per row
        std::array<std::size_t, k_rows>  m_count{};     ///< nonzero span length per row
        std::vector<S>                   m_hist;        ///< channels x k_lines planar lines of m_hist_cap each
        std::array<std::size_t, k_lines> m_end{};       ///< newest sample index + 1 per sub-line
        std::vector<S>                   m_scratch;     ///< interleaved staging for pull()
        std::uint32_t                    m_pos     = 0; ///< superblock position in [0, L)
        std::uint32_t                    m_pending = 1; ///< inputs before the next output (L-phase machine)
        std::uint32_t                    m_dphase  = 0; ///< inputs since the last output (decimator)
    };
    // ANCHOR_END: rational_stage

    static_assert(tap::dsp::sync_stage<basic_stage<float, ratio<2, 1>>>);
    static_assert(tap::dsp::sync_stage<basic_stage<double, ratio<1, 3>>>);
    static_assert(tap::dsp::sync_stage<basic_stage<std::int16_t, ratio<2, 3>>>);

} // namespace tap::sr::rational
