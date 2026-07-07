/// \file polyphase_filter.hpp
/// \brief Polyphase Kaiser-sinc filter bank and the fractional-delay resampler core.
#ifndef SRT_POLYPHASE_FILTER_HPP
#define SRT_POLYPHASE_FILTER_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "srt/detail/kaiser.hpp"
#include "srt/sample_traits.hpp"

// No-alias qualifier for the kernel hot loops: without it the compiler
// versions the blend loop behind a runtime aliasing check (verified with
// -fopt-info-vec; see docs/PERFORMANCE.md, hypothesis 2).
#if defined(_MSC_VER)
#define SRT_RESTRICT __restrict
#else
#define SRT_RESTRICT __restrict__
#endif

// ANCHOR: opt_smlald_gate
// Dual 16x16 MAC (SMLALD) for the Q15 dot product on Arm cores that have
// the DSP extension but no Helium — the Cortex-M33/M4/M7 class (e.g.
// Raspberry Pi Pico 2). Gated off when MVE is present: on M55 the compiler
// already auto-vectorizes the scalar loop with Helium and the intrinsic
// path would replace vectors with dual-MACs (see docs/PERFORMANCE.md,
// hypothesis 4). Bit-exactness: each 16x16 product is exact in int32 and
// the int64 accumulation is associative, so pairing changes no output bit.
#if defined(__ARM_FEATURE_DSP) && !defined(__ARM_FEATURE_MVE)
#include <arm_acle.h>
#define SRT_Q15_SMLALD 1
#else
#define SRT_Q15_SMLALD 0
#endif
// ANCHOR_END: opt_smlald_gate

// Channel-parallel dot product for high channel counts (hypothesis C6,
// docs/PERFORMANCE.md): history stored frame-major so the per-tap inner
// loop runs across channels — contiguous loads, one accumulator lane per
// channel, coefficient broadcast. Bit-exact because each channel's
// accumulation order over taps is unchanged (lanes are channels, not
// taps), which is what lets the FLOAT path vectorize at all: its strict
// per-channel double accumulation forbids tap-axis SIMD (PERFORMANCE.md
// hypothesis 5), but the channel axis is free. Float-only by measurement:
// fixed-point planar dots already auto-vectorize over taps on hosts
// (integer reduction is exactly reassociable) and measured ~1.5x FASTER
// than the channel-parallel form. Host-only: the embedded targets keep
// their proven planar codegen (Helium on M55, SMLALD on M33-class,
// Hexagon's measured scalar floor — see PERFORMANCE.md C4/C5).
#if !defined(__ARM_FEATURE_MVE) && !defined(__ARM_FEATURE_DSP) && !defined(__hexagon__)
#define SRT_CHANNEL_PARALLEL 1
#else
#define SRT_CHANNEL_PARALLEL 0
#endif
// Minimum channel count for the frame-major path (overridable for A/B
// measurements; the blend-share planar path stays better at low counts).
#ifndef SRT_CP_MIN_CHANNELS
#define SRT_CP_MIN_CHANNELS 4
#endif

namespace srt {

    // ANCHOR: bank_spec
    /// Specification of the interpolation prototype filter.
    ///
    /// numPhases (L) sets the polyphase table resolution: the residual images from
    /// linearly interpolating coefficients between adjacent phases fall roughly
    /// 12 dB for every doubling of L. tapsPerPhase (T) sets transition steepness
    /// and stopband depth; group delay is about T/2 input samples.
    struct filter_spec {
        std::size_t num_phases        = 256;     ///< L, rounded up to a power of two
        std::size_t taps_per_phase    = 48;      ///< T, window length in input samples
        double      passband_hz       = 20000.0; ///< edge of the flat passband
        double      stopband_hz       = 28000.0; ///< first image to suppress (fs - passband)
        double      stopband_atten_db = 120.0;   ///< prototype stopband attenuation target
        // ANCHOR: pw_image_zeros
        /// Places transmission zeros at every integer multiple of the sample
        /// rate (droop pre-compensated; see designPrototypeCompensated). Images
        /// of low-frequency program energy — where real audio concentrates —
        /// land on those zeros, deepening their rejection by 10-20 dB at no
        /// runtime cost: the rect that creates the zeros spends the last of the
        /// same tapsPerPhase budget (design uses T-1 taps + 1). Worst-case
        /// single-sine numbers near Nyquist are unchanged. Requires
        /// tapsPerPhase >= 8. On by default for every preset except fast().
        bool image_zeros = true;
        // ANCHOR_END: pw_image_zeros

        /// Small/cheap: ~96 dB prototype, ~0.33 ms group delay at 48 kHz.
        /// Plain windowed-sinc design (no k*fs zeros) — the legacy budget tier.
        static filter_spec fast() noexcept {
            return {.num_phases        = 128,
                    .taps_per_phase    = 32,
                    .passband_hz       = 18000.0,
                    .stopband_hz       = 30000.0,
                    .stopband_atten_db = 96.0,
                    .image_zeros       = false};
        }
        /// Default: flat to 20 kHz, >=120 dB images, ~0.5 ms group delay at 48 kHz.
        static filter_spec balanced() noexcept { return {}; }
        /// Maximum rejection: longer filter and denser phase table.
        static filter_spec transparent() noexcept {
            return {.num_phases        = 512,
                    .taps_per_phase    = 80,
                    .passband_hz       = 20000.0,
                    .stopband_hz       = 26000.0,
                    .stopband_atten_db = 140.0};
        }
        // ANCHOR: pw_economy
        /// Program-weighted economy: two-thirds the per-sample compute and
        /// ~0.16 ms less group delay than balanced(). The worst-case single-sine
        /// floor near Nyquist is 96 dB-class (this preset trades exactly that),
        /// but the k*fs zeros hold low/mid-band folded images at balanced-class
        /// depth where program energy actually lives, and L=512 keeps the
        /// inter-phase interpolation floor at the 120 dB tier. Measured by the
        /// program-weighted multitone metric in test_asrc_program.cpp; the whole
        /// trade is the book's epilogue chapter.
        static filter_spec economy() noexcept {
            return {.num_phases        = 512,
                    .taps_per_phase    = 32,
                    .passband_hz       = 18000.0,
                    .stopband_hz       = 30000.0,
                    .stopband_atten_db = 96.0};
        }
        // ANCHOR_END: pw_economy
        // ANCHOR_END: bank_spec

        /// This spec with the band edges rescaled from the 48 kHz design rate
        /// to sampleRateHz. The presets' passband/stopband are absolute Hz
        /// chosen for ~48 kHz operation; at other rates the same L/T with
        /// proportional band edges gives the identical normalized-frequency
        /// response (and group delay in samples — i.e. more milliseconds at
        /// lower rates). See also servo_config::scaledTo and
        /// Config::forSampleRate, which a 16 kHz deployment wants as a set.
        filter_spec scaled_to(double sample_rate_hz) const noexcept {
            constexpr double k_design_rate_hz = 48000.0;
            filter_spec      s                = *this;
            s.passband_hz *= sample_rate_hz / k_design_rate_hz;
            s.stopband_hz *= sample_rate_hz / k_design_rate_hz;
            return s;
        }
    };

    // ANCHOR: bank_layout
    /// Immutable polyphase coefficient table designed at construction.
    ///
    /// Storage layout: (L+1) rows of T coefficients. Row p in [0, L) is polyphase
    /// branch p; the extra row L equals branch 0 advanced by one input sample, so
    /// the mu-interpolation between rows p and p+1 is branch-free even at p = L-1
    /// and the mu wrap 1.0 -> 0.0 (window shifted by one sample) is exactly
    /// continuous. Rows are stored tap-reversed so the dot product runs forward
    /// over an oldest-first history window.
    // ANCHOR_END: bank_layout
    template <sample_type S>
    class polyphase_filter_bank {
      public:
        using coeff = typename sample_traits<S>::coeff;

        // ANCHOR: bank_build
        /// Designs the prototype (double precision) and builds the table.
        /// Allocates; may throw std::invalid_argument / std::bad_alloc. Do this at
        /// setup time, not on the audio path.
        polyphase_filter_bank(const filter_spec& spec, double sample_rate_hz)
            : m_phases(std::bit_ceil(spec.num_phases))
            , m_taps(spec.taps_per_phase) {
            if (sample_rate_hz <= 0.0 || m_taps < 4 || m_phases < 2) {
                throw std::invalid_argument("polyphase_filter_bank: bad filter_spec");
            }
            if (spec.image_zeros && m_taps < 8) {
                throw std::invalid_argument("polyphase_filter_bank: imageZeros needs tapsPerPhase >= 8");
            }
            if (spec.passband_hz <= 0.0 || spec.stopband_hz <= spec.passband_hz || spec.stopband_hz > sample_rate_hz) {
                throw std::invalid_argument("polyphase_filter_bank: bad band edges");
            }

            const std::size_t   n = m_phases * m_taps;
            std::vector<double> proto(n);
            const double        cutoff_norm = (spec.passband_hz + spec.stopband_hz) / sample_rate_hz;
            if (spec.image_zeros) {
                detail::design_prototype_compensated(proto, m_phases, cutoff_norm,
                                                     detail::kaiser_beta(spec.stopband_atten_db),
                                                     spec.passband_hz / sample_rate_hz);
            }
            else {
                detail::design_prototype(proto, m_phases, cutoff_norm, detail::kaiser_beta(spec.stopband_atten_db));
            }

            m_table.resize((m_phases + 1) * m_taps);
            for (std::size_t p = 0; p <= m_phases; ++p) {
                for (std::size_t t = 0; t < m_taps; ++t) {
                    const std::size_t m                    = t * m_phases + p; // prototype index of (branch p, tap t)
                    const double      v                    = (m < n) ? proto[m] : 0.0;
                    m_table[p * m_taps + (m_taps - 1 - t)] = sample_traits<S>::make_coeff(v);
                }
            }
        }
        // ANCHOR_END: bank_build

        // ANCHOR: bank_accessors
        /// Row pointer for phase p in [0, numPhases()]; T contiguous coefficients.
        const coeff* phase(std::size_t p) const noexcept { return m_table.data() + p * m_taps; }
        std::size_t  num_phases() const noexcept { return m_phases; } ///< L
        std::size_t  taps() const noexcept { return m_taps; }         ///< T

        /// Linear-phase group delay in input samples: (L*T - 1) / (2L), ~= T/2.
        double group_delay_samples() const noexcept {
            return static_cast<double>(m_phases * m_taps - 1) / (2.0 * static_cast<double>(m_phases));
        }
        // ANCHOR_END: bank_accessors

      private:
        std::size_t        m_phases;
        std::size_t        m_taps;
        std::vector<coeff> m_table; // (L+1) x T, rows tap-reversed
    };

    // ANCHOR: bank_interpolate
    /// Evaluates one output sample at fractional position mu in [0, 1).
    ///
    /// \param hist oldest-first window of the newest T input samples of one channel
    /// \param mu   fractional position between hist[T/2-1] (mu=0) and hist[T/2] (mu->1)
    ///
    /// Coefficients are linearly interpolated between the two adjacent phase rows;
    /// accumulation runs in sample_traits<S>::accum (double for float samples).
    template <sample_type S>
    inline S interpolate(const polyphase_filter_bank<S>& bank, const S* hist, double mu) noexcept {
        using tr         = sample_traits<S>;
        const double pos = mu * static_cast<double>(bank.num_phases());
        std::size_t  p   = static_cast<std::size_t>(pos);
        if (p >= bank.num_phases()) { // guards mu rounding up to exactly L
            p = bank.num_phases() - 1;
        }
        // Converted once per output sample so fixed-point datapaths keep an
        // integer-only inner loop.
        const auto         fr = tr::make_blend_factor(pos - static_cast<double>(p));
        const auto*        c0 = bank.phase(p);
        const auto*        c1 = bank.phase(p + 1);
        typename tr::accum acc{};
        const std::size_t  taps = bank.taps();
        for (std::size_t t = 0; t < taps; ++t) {
            acc = tr::mac(acc, hist[t], tr::blend(c0[t], c1[t], fr));
        }
        return tr::finalize(acc);
    }
    // ANCHOR_END: bank_interpolate

    /// Blends the two phase rows adjacent to mu into `row` (taps() entries).
    /// Multichannel datapaths do this once per output frame and then run
    /// dotRow() per channel, instead of re-blending inside interpolate() for
    /// every channel.
    template <sample_type S>
    inline void blend_row(const polyphase_filter_bank<S>& bank, typename sample_traits<S>::coeff* SRT_RESTRICT row,
                          double mu) noexcept {
        using tr         = sample_traits<S>;
        const double pos = mu * static_cast<double>(bank.numPhases());
        std::size_t  p   = static_cast<std::size_t>(pos);
        if (p >= bank.numPhases()) {
            p = bank.numPhases() - 1;
        }
        const auto        fr   = tr::make_blend_factor(pos - static_cast<double>(p));
        const auto*       c0   = bank.phase(p);
        const auto*       c1   = bank.phase(p + 1);
        const std::size_t taps = bank.taps();
        for (std::size_t t = 0; t < taps; ++t) {
            row[t] = tr::blend(c0[t], c1[t], fr);
        }
    }

    // ANCHOR: rs_blend_row_phase
    /// Phase-bit variants: the fractional position as an unsigned Q0.64
    /// fraction. The polyphase index is the top log2(L) bits and the intra-phase
    /// blend factor comes from the bits below — no double arithmetic per sample,
    /// which is what makes this path cheap on targets without a double-precision
    /// FPU. Resolution is 2^-64 samples (finer than the double-mu path's 2^-52).
    template <sample_type S>
    inline void blend_row_phase(const polyphase_filter_bank<S>&                bank,
                                typename sample_traits<S>::coeff* SRT_RESTRICT row, std::uint64_t phase) noexcept {
        using tr               = sample_traits<S>;
        const int         lg   = std::countr_zero(bank.num_phases()); // L is a power of two
        const std::size_t p    = static_cast<std::size_t>(phase >> (64 - lg));
        const auto        fr   = tr::blend_factor_from_q64(phase << lg);
        const auto*       c0   = bank.phase(p);
        const auto*       c1   = bank.phase(p + 1);
        const std::size_t taps = bank.taps();
        for (std::size_t t = 0; t < taps; ++t) {
            row[t] = tr::blend(c0[t], c1[t], fr);
        }
    }
    // ANCHOR_END: rs_blend_row_phase

    // ANCHOR: rs_interpolate_phase
    /// interpolate() over a Q0.64 phase; fused blend+mac (mono fast path).
    template <sample_type S>
    inline S interpolate_phase(const polyphase_filter_bank<S>& bank, const S* hist, std::uint64_t phase) noexcept {
        using tr              = sample_traits<S>;
        const int          lg = std::countr_zero(bank.num_phases());
        const std::size_t  p  = static_cast<std::size_t>(phase >> (64 - lg));
        const auto         fr = tr::blend_factor_from_q64(phase << lg);
        const auto*        c0 = bank.phase(p);
        const auto*        c1 = bank.phase(p + 1);
        typename tr::accum acc{};
        const std::size_t  taps = bank.taps();
        for (std::size_t t = 0; t < taps; ++t) {
            acc = tr::mac(acc, hist[t], tr::blend(c0[t], c1[t], fr));
        }
        return tr::finalize(acc);
    }
    // ANCHOR_END: rs_interpolate_phase

    // ANCHOR: rs_dot_row
    /// Dot product of a pre-blended coefficient row against a history window.
    /// Identical arithmetic to interpolate() given the same mu: blend then mac,
    /// per tap, in the same order — outputs are bit-exact either way.
    template <sample_type S>
    inline S dot_row(const typename sample_traits<S>::coeff* SRT_RESTRICT row, const S* SRT_RESTRICT hist,
                     std::size_t taps) noexcept {
        using tr = sample_traits<S>;
#if SRT_Q15_SMLALD
        if constexpr (std::is_same_v<S, std::int16_t>) {
            std::int64_t acc = 0;
            std::size_t  t   = 0;
            for (; t + 1 < taps; t += 2) {
                // memcpy keeps the 16-bit pair loads alignment-safe; both
                // compile to a single 32-bit load (little-endian packing
                // matches SMLALD's lo/hi lanes).
                std::uint32_t h;
                std::uint32_t r;
                std::memcpy(&h, hist + t, sizeof h);
                std::memcpy(&r, row + t, sizeof r);
                acc = __smlald(static_cast<int16x2_t>(h), static_cast<int16x2_t>(r), acc);
            }
            for (; t < taps; ++t) // odd-tap tail; every preset is even
                acc = Tr::mac(acc, hist[t], row[t]);
            return Tr::finalize(acc);
        }
#endif
        typename tr::accum acc{};
        for (std::size_t t = 0; t < taps; ++t) {
            acc = tr::mac(acc, hist[t], row[t]);
        }
        return tr::finalize(acc);
    }
    // ANCHOR_END: rs_dot_row

    // ANCHOR: opt_dot_tile
    /// One K-channel tile of the channel-parallel dot (hypothesis C6): K
    /// accumulators live in a constexpr-size local array — registers, not
    /// memory — while the tap loop walks the frame-major window with stride
    /// `stride` samples per frame. K is the register-blocking factor; a naive
    /// channels-inner loop with accumulators in memory measures ~2.8x SLOWER
    /// than planar (each mac round-trips its accumulator through the stack).
    template <sample_type S, std::size_t K>
    inline void dot_tile_frame_major(const typename sample_traits<S>::coeff* SRT_RESTRICT row, const S* SRT_RESTRICT x,
                                     std::size_t taps, std::size_t stride, S* SRT_RESTRICT out) noexcept {
        using tr = sample_traits<S>;
        typename tr::accum acc[K]{};
        for (std::size_t t = 0; t < taps; ++t) {
            const auto            coeff = row[t];
            const S* SRT_RESTRICT frame = x + t * stride;
            for (std::size_t k = 0; k < K; ++k) {
                acc[k] = tr::mac(acc[k], frame[k], coeff);
            }
        }
        for (std::size_t k = 0; k < K; ++k) {
            out[k] = tr::finalize(acc[k]);
        }
    }
    // ANCHOR_END: opt_dot_tile

    // ANCHOR: rs_dot_rows_frame_major
    // ANCHOR: opt_dot_rows
    /// Channel-parallel dot products over a frame-major history block: all
    /// channels' outputs for one frame in register-blocked tiles of 8/4/2/1.
    /// Per channel the accumulation order over taps equals dotRow's, so the
    /// outputs are bit-exact vs the planar path for every sample type — float
    /// included, since each channel's double accumulator still sums the taps
    /// in the same order (lanes are channels, not taps).
    template <sample_type S>
    inline void dot_rows_frame_major(const typename sample_traits<S>::coeff* SRT_RESTRICT row, const S* SRT_RESTRICT x,
                                     std::size_t taps, std::size_t channels, S* SRT_RESTRICT out) noexcept {
        std::size_t c = 0;
        for (; c + 8 <= channels; c += 8) {
            dot_tile_frame_major<S, 8>(row, x + c, taps, channels, out + c);
        }
        if (c + 4 <= channels) {
            dot_tile_frame_major<S, 4>(row, x + c, taps, channels, out + c);
            c += 4;
        }
        if (c + 2 <= channels) {
            dot_tile_frame_major<S, 2>(row, x + c, taps, channels, out + c);
            c += 2;
        }
        if (c < channels) {
            dot_tile_frame_major<S, 1>(row, x + c, taps, channels, out + c);
        }
    }
    // ANCHOR_END: rs_dot_rows_frame_major
    // ANCHOR_END: opt_dot_rows

    // ANCHOR: rs_class_doc
    /// Streaming fractional-delay engine for one converter instance.
    ///
    /// Owns the history delay lines (planar per-channel below the
    /// channel-parallel threshold, frame-major above it — see the hist_
    /// field) and the phase accumulator mu. Input frames are pulled
    /// through a caller-supplied PopFn in small bulk chunks and deinterleaved into
    /// the histories as the integer read position advances.
    ///
    /// Phase accumulator: the fractional position lives in an unsigned Q0.64
    /// integer and accumulates only the rate DEVIATION eps per output sample
    /// (converted from double once per process() call, at block rate); the
    /// unity part of the ratio is applied as the integer window advance. The
    /// per-sample path is therefore integer-only (plus one single-precision
    /// blend-factor conversion on the float datapath) — no doubles, which is
    /// what keeps it cheap on FPU-less DSP targets. Resolution is 2^-64 samples,
    /// far below the ~8 ps jitter budget for 120 dB transparency, and slips are
    /// detected by 64-bit wraparound instead of comparisons.
    template <sample_type S>
    class fractional_resampler {
        // ANCHOR_END: rs_class_doc
      public:
        /// Frame-major channel-parallel mode is compiled in only on CP targets
        /// and only for floating-point samples (see SRT_CHANNEL_PARALLEL).
        static constexpr bool k_k_channel_parallel = SRT_CHANNEL_PARALLEL != 0 && std::is_floating_point_v<S>;

        /// Allocates histories and the pop scratch buffer; setup time only.
        fractional_resampler(const polyphase_filter_bank<S>& bank, std::size_t channels, std::size_t chunk_frames = 64)
            : m_bank(&bank)
            , m_channels(channels)
            , m_chunk(chunk_frames)
            , m_hist_cap(bank.taps() + chunk_frames)
            , m_scratch(chunk_frames * channels)
            , m_frame_major(k_k_channel_parallel && channels >= SRT_CP_MIN_CHANNELS)
            , m_hist(m_frame_major ? 1 : channels)
            , m_row(bank.taps()) {
            if (m_channels == 0 || m_chunk == 0) {
                throw std::invalid_argument("fractional_resampler: bad config");
            }
            for (auto& h : m_hist) {
                h.assign(m_hist_cap * (m_frame_major ? m_channels : 1), sample_traits<S>::silence());
            }
            reset();
        }

        /// Clears history, scratch and mu. Frames already popped into the scratch
        /// are dropped (only used across discontinuities, where they are stale).
        void reset() noexcept {
            m_phase          = 0;
            m_end            = 0;
            m_primed         = false;
            m_scratch_frames = 0;
            m_scratch_pos    = 0;
        }

        // ANCHOR: rs_mu
        /// Fractional position in [0,1) as a double; used by the servo at block
        /// rate (one conversion per pull, not per sample).
        double mu() const noexcept { return static_cast<double>(m_phase) * 0x1p-64; }
        bool   primed() const noexcept { return m_primed; }

        /// Frames popped from the source but not yet consumed by the filter; part
        /// of the effective backlog the servo must observe.
        std::size_t buffered_frames() const noexcept { return m_scratch_frames - m_scratch_pos; }
        // ANCHOR_END: rs_mu

        /// Fills the history window with taps() frames from the source.
        /// Returns false (and stays unprimed) if the source ran dry.
        template <typename PopFn>
        bool prime(PopFn&& pop_frames) noexcept {
            const std::size_t need = m_bank->taps();
            for (std::size_t i = 0; i < need; ++i) {
                if (!append_one(pop_frames)) {
                    return false;
                }
            }
            m_primed = true;
            return true;
        }

        // ANCHOR: rs_process_doc
        /// Synthesizes up to maxFrames output frames (interleaved) advancing the
        /// read position by (1 + epsHat) input frames per output frame. Returns
        /// the number produced; fewer than maxFrames means the source ran dry
        /// (underrun). RT-safe: no allocation, locks or exceptions.
        ///
        /// Preconditions (the converter upholds both; direct users must too):
        /// a successful prime() before the first process() — the window math
        /// underflows otherwise — and reset()+reprime after any dry return, as
        /// a dry advance==2 slip leaves history and phase one frame apart.
        ///
        /// PopFn: std::size_t popFrames(S* dst, std::size_t maxFrames) — bulk-pops
        /// interleaved frames, returning the count actually delivered.
        template <typename PopFn>
        std::size_t process(S* out, std::size_t max_frames, double eps_hat, PopFn&& pop_frames) noexcept {
            // ANCHOR_END: rs_process_doc
            // ANCHOR: p0_phase_step
            // ANCHOR: rs_slip
            // eps in Q0.64, converted once per call (block rate). |eps| is
            // servo-clamped to ~1e-3, so eps * 2^64 fits int64 comfortably.
            const auto eps_fix = static_cast<std::int64_t>(eps_hat * 0x1p64);
            const auto eps_u   = static_cast<std::uint64_t>(eps_fix);
            for (std::size_t n = 0; n < max_frames; ++n) {
                const std::uint64_t m       = m_phase + eps_u; // mod 2^64
                std::size_t         advance = 1;
                if (eps_fix >= 0) {
                    if (m < m_phase) { // wrapped past 1.0: forward slip,
                        advance = 2;   // consume one extra input frame
                    }
                }
                else if (m > m_phase) { // wrapped below 0.0: backward slip,
                    advance = 0;        // re-use the current window
                }
                for (std::size_t a = 0; a < advance; ++a) {
                    if (!append_one(pop_frames)) {
                        return n; // dry: phase_ not advanced for this frame
                    }
                }
                m_phase = m;
                // ANCHOR_END: p0_phase_step
                // ANCHOR_END: rs_slip
                // ANCHOR: rs_dispatch
                // Q15 on SMLALD targets routes mono through blendRow+dotRow as
                // well: dotRow carries the dual-MAC loop, and the two paths are
                // bit-exact by construction (see dotRow).
                constexpr bool k_prefer_dot_row = SRT_Q15_SMLALD && std::is_same_v<S, std::int16_t>;
                if (m_channels == 1 && !k_prefer_dot_row) { // fused blend+mac; no scratch traffic
                    out[n] = interpolate_phase(*m_bank, window(0), m);
                }
                else if (k_k_channel_parallel && m_frame_major) { // constant-folds away off-host
                    // High channel counts: one blend, then all channels' dots in
                    // a single channel-parallel pass over the frame-major window.
                    blend_row_phase(*m_bank, m_row.data(), m);
                    const std::size_t taps = m_bank->taps();
                    const S*          base = m_hist[0].data() + (m_end - taps) * m_channels;
                    dot_rows_frame_major<S>(m_row.data(), base, taps, m_channels, out + n * m_channels);
                }
                else {
                    // Blend once per frame, dot per channel: the blend is the
                    // same for every channel, so this halves the inner-loop work
                    // for stereo and scales with channel count.
                    blend_row_phase(*m_bank, m_row.data(), m);
                    const std::size_t taps = m_bank->taps();
                    for (std::size_t c = 0; c < m_channels; ++c) {
                        out[n * m_channels + c] = dot_row<S>(m_row.data(), window(c), taps);
                    }
                }
                // ANCHOR_END: rs_dispatch
            }
            return max_frames;
        }

      private:
        const S* window(std::size_t c) const noexcept { return m_hist[c].data() + m_end - m_bank->taps(); }

        // ANCHOR: rs_append
        template <typename PopFn>
        bool append_one(PopFn&& pop_frames) noexcept {
            if (m_scratch_pos == m_scratch_frames) {
                m_scratch_frames = pop_frames(m_scratch.data(), m_chunk);
                m_scratch_pos    = 0;
                if (m_scratch_frames == 0) {
                    return false;
                }
            }
            if (m_end == m_hist_cap) { // compact: keep the newest T-1 frames at the front
                const std::size_t keep = m_bank->taps() - 1;
                // Samples per frame slot; the gate is compile-time so non-CP
                // targets keep their previous codegen exactly (the runtime form
                // measured +6-8% on the M55 ratchet from hot-loop branch bloat).
                const std::size_t w = (k_k_channel_parallel && m_frame_major) ? m_channels : 1;
                for (auto& h : m_hist) {
                    std::memmove(h.data(), h.data() + (m_end - keep) * w, keep * w * sizeof(S));
                }
                m_end = keep;
            }
            const S* frame = m_scratch.data() + m_scratch_pos * m_channels;
            if (k_k_channel_parallel && m_frame_major) { // frames stay interleaved: one contiguous copy
                std::memcpy(m_hist[0].data() + m_end * m_channels, frame, m_channels * sizeof(S));
            }
            else {
                for (std::size_t c = 0; c < m_channels; ++c) {
                    m_hist[c][m_end] = frame[c];
                }
            }
            ++m_end;
            ++m_scratch_pos;
            return true;
        }
        // ANCHOR_END: rs_append

        const polyphase_filter_bank<S>* m_bank;
        std::size_t                     m_channels;
        std::size_t                     m_chunk;
        std::size_t                     m_hist_cap;
        std::vector<S>                  m_scratch; // interleaved staging for bulk pops
        // ANCHOR: rs_members
        // History storage: planar (one delay line per channel, hist_[c]) below
        // SRT_CP_MIN_CHANNELS, frame-major (single interleaved line, hist_[0])
        // at or above it on SRT_CHANNEL_PARALLEL targets. end_/histCap_ count
        // frames in both modes.
        bool                                          m_frame_major;
        std::vector<std::vector<S>>                   m_hist;
        std::vector<typename sample_traits<S>::coeff> m_row;     // per-frame blended coefficients
        std::size_t                                   m_end = 0; // shared end index; all channels advance in lockstep
        std::size_t                                   m_scratch_frames = 0;
        std::size_t                                   m_scratch_pos    = 0;
        std::uint64_t                                 m_phase          = 0; // fractional position, unsigned Q0.64
        // ANCHOR_END: rs_members
        bool m_primed = false;
    };

} // namespace srt

#endif // SRT_POLYPHASE_FILTER_HPP
