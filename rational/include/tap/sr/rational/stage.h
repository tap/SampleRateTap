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
// multiply-back for M = 3, 6). k_table_gain states what the rows sum to;
// finalize_output() is the stage's rounding point.
//
// Contract, as numbers (PLAN.md 2.5, R7, R9, R10):
//   - zero-primed and causal; outputs_for(n) and frames_needed(k) are exact
//     arithmetic from the current position (schedule or decimation phase);
//     pull() with a source delivering exactly frames_needed(k) frames yields
//     exactly k outputs, and is bit-identical to process() on the same
//     stream, for any chunking;
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
//     within 3.4e-9 of full scale; Q15 is format-limited — RMS -87.1 to
//     -99.8 dBFS from double, attained stopband -68.2 to -78.5 dB, a
//     decimator's equal to the interpolator's of its band (by 8 at economy
//     -71.7 dB), stated per stage in PLAN.md;
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
#include "tap/dsp/fir_kernels.h"
#include "tap/dsp/quantize.h"
#include "tap/dsp/sample_traits.h"
#include "tap/sr/rational/design.h"
#include "tap/sr/rational/ratio.h"

namespace tap::sr::rational {

    // ANCHOR: rational_schedule
    /// One output's step of the L-phase machine: the phase to dot and the
    /// inputs to consume after it (bridge's schedule_entry).
    struct schedule_entry {
        std::uint16_t phase;   ///< polyphase branch index in [0, L)
        std::uint8_t  advance; ///< input frames consumed after this output
    };

    /// The superblock of ratio R: entry n serves output k L + n; phase(n) =
    /// (n M) mod L visits every phase once per L outputs; the advances sum
    /// to M. For an interpolator the advances are L - 1 zeros then a 1; for
    /// a decimator (L = 1) the single entry advances by M.
    template <rational_ratio R>
    constexpr std::array<schedule_entry, R::k_up> make_schedule() noexcept {
        constexpr std::size_t               l = R::k_up;
        constexpr std::size_t               m = R::k_down;
        std::array<schedule_entry, R::k_up> s{};
        for (std::size_t n = 0; n < l; ++n) {
            s[n].phase   = static_cast<std::uint16_t>((n * m) % l);
            s[n].advance = static_cast<std::uint8_t>(((n + 1) * m) / l - (n * m) / l);
        }
        return s;
    }

    template <rational_ratio R>
    inline constexpr std::array<schedule_entry, R::k_up> k_schedule = make_schedule<R>();

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
        /// A Q15 decimator quantizes each of its M branches at the branch's
        /// own unity sum and divides the summed branches by M in the single
        /// rounding (tap::dsp::finalize_divided): Q1.14 cannot hold h / M at
        /// the precision h needs (see the file header).
        static constexpr bool k_branch_quantized = k_is_decimator && std::is_same_v<S, std::int16_t>;
        /// What the table's rows sum to, in units of the format's unity: 1,
        /// or M for a Q15 decimator (M branches each at unity), which the
        /// single rounding divides back out.
        static constexpr std::size_t k_table_gain = k_branch_quantized ? k_down : 1;

        /// The stage's single rounding point, accumulator -> sample: the
        /// trait's finalize, or finalize_divided by M for a Q15 decimator.
        static S finalize_output(typename tap::dsp::sample_traits<S>::accum acc) noexcept {
            if constexpr (k_branch_quantized) {
                return tap::dsp::finalize_divided<S, static_cast<std::uint32_t>(k_down)>(acc);
            }
            else {
                return tap::dsp::sample_traits<S>::finalize(acc);
            }
        }
        /// Rows of the table: L phases, or the M branches of a decimator.
        static constexpr std::size_t k_rows = k_is_decimator ? k_down : k_up;

        /// Designs the table and allocates the histories; setup time only.
        explicit basic_stage(std::size_t channels = 1, const profile& p = profile::economy())
            : m_channels(channels) {
            if (channels == 0) {
                throw std::invalid_argument("tap::sr::rational::basic_stage: channels == 0");
            }
            const std::vector<double> h = design_stage<R>(p);
            m_taps                      = h.size();
            build_table(h);
            m_hist_cap = m_row_len + k_hist_slack;
            m_hist.resize(channels * k_lines);
            for (auto& line : m_hist) {
                line.assign(m_hist_cap, tap::dsp::sample_traits<S>::silence());
            }
            m_scratch.resize(k_pop_chunk * channels);
            reset();
        }

        /// Push-transform: consumes all in_frames interleaved frames, writes
        /// outputs_for(in_frames) frames (out must hold them); returns that.
        std::size_t process(const S* in, std::size_t in_frames, S* out) noexcept {
            std::size_t produced = 0;
            if constexpr (k_is_decimator) {
                for (std::size_t i = 0; i < in_frames; ++i) {
                    append_frame(in + i * m_channels);
                    if (m_dphase == 0) {
                        emit(out + produced * m_channels);
                        ++produced;
                    }
                    m_dphase = m_dphase + 1 == k_down ? 0 : m_dphase + 1;
                }
            }
            else {
                const std::size_t total = outputs_for(in_frames);
                std::size_t       fed   = 0;
                for (; produced < total; ++produced) {
                    while (m_pending != 0) { // never outruns in_frames: total is exact
                        append_frame(in + fed * m_channels);
                        ++fed;
                        --m_pending;
                    }
                    emit(out + produced * m_channels);
                }
                while (fed < in_frames) { // leftover input smaller than the next gap
                    append_frame(in + fed * m_channels);
                    ++fed;
                    --m_pending;
                }
            }
            return produced;
        }

        /// Pull: produce exactly out_frames frames, drawing input through
        /// pop(dst, max_frames) -> frames delivered (interleaved; fewer means
        /// the source ran dry and the call returns short, resumable). Bit-
        /// identical to process() on the same stream. PopFn must be noexcept.
        template <typename PopFn>
        std::size_t pull(S* out, std::size_t out_frames, PopFn&& pop) noexcept {
            static_assert(std::is_nothrow_invocable_r_v<std::size_t, PopFn&, S*, std::size_t>,
                          "pull() requires a noexcept PopFn: std::size_t(S*, std::size_t) noexcept");
            for (std::size_t n = 0; n < out_frames; ++n) {
                // The inputs the next output needs: the schedule's pending
                // gap, or for a decimator the pushes up to and including the
                // one at phase 0 (whose frame emits). Consumed one by one,
                // so a dry source leaves the exact partial state behind.
                std::size_t need = pending_inputs();
                while (need != 0) {
                    const std::size_t want = need < k_pop_chunk ? need : k_pop_chunk;
                    const std::size_t got  = pop(m_scratch.data(), want);
                    if (got == 0) {
                        return n; // dry
                    }
                    for (std::size_t i = 0; i < got && i < want; ++i) {
                        append_frame(m_scratch.data() + i * m_channels);
                        consume_one();
                        --need;
                    }
                }
                emit(out + n * m_channels);
            }
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
        std::size_t flush(S* out) noexcept {
            const std::size_t zeros = window_frames();
            std::size_t       fed   = 0;
            std::size_t       made  = 0;
            if constexpr (k_is_decimator) {
                for (; fed < zeros; ++fed) {
                    append_silence();
                    if (m_dphase == 0) {
                        emit(out + made * m_channels);
                        ++made;
                    }
                    m_dphase = m_dphase + 1 == k_down ? 0 : m_dphase + 1;
                }
                return made;
            }
            else {
                for (;;) {
                    while (m_pending != 0 && fed < zeros) {
                        append_silence();
                        ++fed;
                        --m_pending;
                    }
                    if (m_pending != 0) {
                        return made;
                    }
                    emit(out + made * m_channels);
                    ++made;
                }
            }
        }

        /// Frames flush() will write from the current position.
        std::size_t flush_output_frames() const noexcept { return outputs_for(window_frames()); }

        /// Return to the zero-primed initial state.
        void reset() noexcept {
            for (auto& line : m_hist) {
                for (auto& v : line) {
                    v = tap::dsp::sample_traits<S>::silence();
                }
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
                        // design's M branches; normalize each to DC gain 1.
                        for (auto& v : row) {
                            v /= sum;
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

        void consume_one() noexcept {
            if constexpr (k_is_decimator) {
                m_dphase = m_dphase + 1 == k_down ? 0 : m_dphase + 1;
            }
            else {
                --m_pending;
            }
        }

        S*       line(std::size_t c, std::size_t sub) noexcept { return m_hist[c * k_lines + sub].data(); }
        const S* line(std::size_t c, std::size_t sub) const noexcept { return m_hist[c * k_lines + sub].data(); }

        /// Appends one interleaved frame to the history (the decimator's
        /// current sub-line); compacts a full line first.
        void append_frame(const S* frame) noexcept {
            const std::size_t sub = k_is_decimator ? m_dphase : 0;
            if (m_end[sub] == m_hist_cap) {
                compact(sub);
            }
            for (std::size_t c = 0; c < m_channels; ++c) {
                line(c, sub)[m_end[sub]] = frame[c];
            }
            ++m_end[sub];
        }

        void append_silence() noexcept {
            const std::size_t sub = k_is_decimator ? m_dphase : 0;
            if (m_end[sub] == m_hist_cap) {
                compact(sub);
            }
            for (std::size_t c = 0; c < m_channels; ++c) {
                line(c, sub)[m_end[sub]] = tap::dsp::sample_traits<S>::silence();
            }
            ++m_end[sub];
        }

        /// Slides the newest row_len - 1 samples of sub-line sub to the front
        /// of every channel's line (in place).
        void compact(std::size_t sub) noexcept {
            const std::size_t keep = m_row_len - 1;
            for (std::size_t c = 0; c < m_channels; ++c) {
                S* l = line(c, sub);
                std::memmove(l, l + (m_end[sub] - keep), keep * sizeof(S));
            }
            m_end[sub] = keep;
        }

        /// One output frame at the current position; advances the schedule
        /// (the decimator's phase advances in process / pull / flush).
        void emit(S* out) noexcept {
            using tr = tap::dsp::sample_traits<S>;
            if constexpr (k_is_decimator) {
                for (std::size_t c = 0; c < m_channels; ++c) {
                    typename tr::accum acc{};
                    for (std::size_t j = 0; j < k_rows; ++j) {
                        const std::size_t sub    = (k_down - j) % k_down;
                        const S*          window = line(c, sub) + m_end[sub] - m_row_len + m_first[j];
                        acc = tap::dsp::accumulate_row<S>(acc, m_rows.data() + j * m_row_len + m_first[j], window,
                                                          m_count[j]);
                    }
                    out[c] = finalize_output(acc);
                }
            }
            else {
                const schedule_entry step = k_schedule<R>[m_pos];
                const coeff*         row  = m_rows.data() + step.phase * m_row_len + m_first[step.phase];
                for (std::size_t c = 0; c < m_channels; ++c) {
                    const S* window = line(c, 0) + m_end[0] - m_row_len + m_first[step.phase];
                    out[c]          = tap::dsp::dot_row<S>(row, window, m_count[step.phase]);
                }
                m_pending = step.advance;
                m_pos     = m_pos + 1 == k_up ? 0 : m_pos + 1;
            }
        }

        std::size_t                      m_channels;
        std::size_t                      m_taps     = 0; ///< N
        std::size_t                      m_row_len  = 0; ///< T (per phase or per branch)
        std::size_t                      m_hist_cap = 0;
        std::vector<coeff>               m_rows;        ///< k_rows x T, tap-reversed
        std::array<std::size_t, k_rows>  m_first{};     ///< nonzero span start per row
        std::array<std::size_t, k_rows>  m_count{};     ///< nonzero span length per row
        std::vector<std::vector<S>>      m_hist;        ///< channels x k_lines planar delay lines
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
