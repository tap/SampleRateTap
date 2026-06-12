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

/// Specification of the interpolation prototype filter.
///
/// numPhases (L) sets the polyphase table resolution: the residual images from
/// linearly interpolating coefficients between adjacent phases fall roughly
/// 12 dB for every doubling of L. tapsPerPhase (T) sets transition steepness
/// and stopband depth; group delay is about T/2 input samples.
struct FilterSpec {
    std::size_t numPhases = 256;    ///< L, rounded up to a power of two
    std::size_t tapsPerPhase = 48;  ///< T, window length in input samples
    double passbandHz = 20000.0;    ///< edge of the flat passband
    double stopbandHz = 28000.0;    ///< first image to suppress (fs - passband)
    double stopbandAttenDb = 120.0; ///< prototype stopband attenuation target

    /// Small/cheap: ~96 dB prototype, ~0.33 ms group delay at 48 kHz.
    static FilterSpec fast() noexcept {
        return {.numPhases = 128,
                .tapsPerPhase = 32,
                .passbandHz = 18000.0,
                .stopbandHz = 30000.0,
                .stopbandAttenDb = 96.0};
    }
    /// Default: flat to 20 kHz, >=120 dB images, ~0.5 ms group delay at 48 kHz.
    static FilterSpec balanced() noexcept { return {}; }
    /// Maximum rejection: longer filter and denser phase table.
    static FilterSpec transparent() noexcept {
        return {.numPhases = 512,
                .tapsPerPhase = 80,
                .passbandHz = 20000.0,
                .stopbandHz = 26000.0,
                .stopbandAttenDb = 140.0};
    }

    /// This spec with the band edges rescaled from the 48 kHz design rate
    /// to sampleRateHz. The presets' passband/stopband are absolute Hz
    /// chosen for ~48 kHz operation; at other rates the same L/T with
    /// proportional band edges gives the identical normalized-frequency
    /// response (and group delay in samples — i.e. more milliseconds at
    /// lower rates). See also ServoConfig::scaledTo and
    /// Config::forSampleRate, which a 16 kHz deployment wants as a set.
    FilterSpec scaledTo(double sampleRateHz) const noexcept {
        constexpr double kDesignRateHz = 48000.0;
        FilterSpec s = *this;
        s.passbandHz *= sampleRateHz / kDesignRateHz;
        s.stopbandHz *= sampleRateHz / kDesignRateHz;
        return s;
    }
};

/// Immutable polyphase coefficient table designed at construction.
///
/// Storage layout: (L+1) rows of T coefficients. Row p in [0, L) is polyphase
/// branch p; the extra row L equals branch 0 advanced by one input sample, so
/// the mu-interpolation between rows p and p+1 is branch-free even at p = L-1
/// and the mu wrap 1.0 -> 0.0 (window shifted by one sample) is exactly
/// continuous. Rows are stored tap-reversed so the dot product runs forward
/// over an oldest-first history window.
template <SampleType S>
class PolyphaseFilterBank {
public:
    using Coeff = typename SampleTraits<S>::Coeff;

    /// Designs the prototype (double precision) and builds the table.
    /// Allocates; may throw std::invalid_argument / std::bad_alloc. Do this at
    /// setup time, not on the audio path.
    PolyphaseFilterBank(const FilterSpec& spec, double sampleRateHz)
        : phases_(std::bit_ceil(spec.numPhases)), taps_(spec.tapsPerPhase) {
        if (sampleRateHz <= 0.0 || taps_ < 4 || phases_ < 2)
            throw std::invalid_argument("PolyphaseFilterBank: bad FilterSpec");
        if (spec.passbandHz <= 0.0 || spec.stopbandHz <= spec.passbandHz ||
            spec.stopbandHz > sampleRateHz)
            throw std::invalid_argument("PolyphaseFilterBank: bad band edges");

        const std::size_t n = phases_ * taps_;
        std::vector<double> proto(n);
        const double cutoffNorm = (spec.passbandHz + spec.stopbandHz) / sampleRateHz;
        detail::designPrototype(proto, phases_, cutoffNorm,
                                detail::kaiserBeta(spec.stopbandAttenDb));

        table_.resize((phases_ + 1) * taps_);
        for (std::size_t p = 0; p <= phases_; ++p) {
            for (std::size_t t = 0; t < taps_; ++t) {
                const std::size_t m = t * phases_ + p; // prototype index of (branch p, tap t)
                const double v = (m < n) ? proto[m] : 0.0;
                table_[p * taps_ + (taps_ - 1 - t)] = SampleTraits<S>::makeCoeff(v);
            }
        }
    }

    /// Row pointer for phase p in [0, numPhases()]; T contiguous coefficients.
    const Coeff* phase(std::size_t p) const noexcept { return table_.data() + p * taps_; }
    std::size_t numPhases() const noexcept { return phases_; } ///< L
    std::size_t taps() const noexcept { return taps_; }        ///< T

    /// Linear-phase group delay in input samples: (L*T - 1) / (2L), ~= T/2.
    double groupDelaySamples() const noexcept {
        return static_cast<double>(phases_ * taps_ - 1) / (2.0 * static_cast<double>(phases_));
    }

private:
    std::size_t phases_;
    std::size_t taps_;
    std::vector<Coeff> table_; // (L+1) x T, rows tap-reversed
};

/// Evaluates one output sample at fractional position mu in [0, 1).
///
/// \param hist oldest-first window of the newest T input samples of one channel
/// \param mu   fractional position between hist[T/2-1] (mu=0) and hist[T/2] (mu->1)
///
/// Coefficients are linearly interpolated between the two adjacent phase rows;
/// accumulation runs in SampleTraits<S>::Accum (double for float samples).
template <SampleType S>
inline S interpolate(const PolyphaseFilterBank<S>& bank, const S* hist, double mu) noexcept {
    using Tr = SampleTraits<S>;
    const double pos = mu * static_cast<double>(bank.numPhases());
    std::size_t p = static_cast<std::size_t>(pos);
    if (p >= bank.numPhases()) // guards mu rounding up to exactly L
        p = bank.numPhases() - 1;
    // Converted once per output sample so fixed-point datapaths keep an
    // integer-only inner loop.
    const auto fr = Tr::makeBlendFactor(pos - static_cast<double>(p));
    const auto* c0 = bank.phase(p);
    const auto* c1 = bank.phase(p + 1);
    typename Tr::Accum acc{};
    const std::size_t taps = bank.taps();
    for (std::size_t t = 0; t < taps; ++t)
        acc = Tr::mac(acc, hist[t], Tr::blend(c0[t], c1[t], fr));
    return Tr::finalize(acc);
}

/// Blends the two phase rows adjacent to mu into `row` (taps() entries).
/// Multichannel datapaths do this once per output frame and then run
/// dotRow() per channel, instead of re-blending inside interpolate() for
/// every channel.
template <SampleType S>
inline void blendRow(const PolyphaseFilterBank<S>& bank,
                     typename SampleTraits<S>::Coeff* SRT_RESTRICT row, double mu) noexcept {
    using Tr = SampleTraits<S>;
    const double pos = mu * static_cast<double>(bank.numPhases());
    std::size_t p = static_cast<std::size_t>(pos);
    if (p >= bank.numPhases())
        p = bank.numPhases() - 1;
    const auto fr = Tr::makeBlendFactor(pos - static_cast<double>(p));
    const auto* c0 = bank.phase(p);
    const auto* c1 = bank.phase(p + 1);
    const std::size_t taps = bank.taps();
    for (std::size_t t = 0; t < taps; ++t)
        row[t] = Tr::blend(c0[t], c1[t], fr);
}

/// Phase-bit variants: the fractional position as an unsigned Q0.64
/// fraction. The polyphase index is the top log2(L) bits and the intra-phase
/// blend factor comes from the bits below — no double arithmetic per sample,
/// which is what makes this path cheap on targets without a double-precision
/// FPU. Resolution is 2^-64 samples (finer than the double-mu path's 2^-52).
template <SampleType S>
inline void blendRowPhase(const PolyphaseFilterBank<S>& bank,
                          typename SampleTraits<S>::Coeff* SRT_RESTRICT row,
                          std::uint64_t phase) noexcept {
    using Tr = SampleTraits<S>;
    const int lg = std::countr_zero(bank.numPhases()); // L is a power of two
    const std::size_t p = static_cast<std::size_t>(phase >> (64 - lg));
    const auto fr = Tr::blendFactorFromQ64(phase << lg);
    const auto* c0 = bank.phase(p);
    const auto* c1 = bank.phase(p + 1);
    const std::size_t taps = bank.taps();
    for (std::size_t t = 0; t < taps; ++t)
        row[t] = Tr::blend(c0[t], c1[t], fr);
}

/// interpolate() over a Q0.64 phase; fused blend+mac (mono fast path).
template <SampleType S>
inline S interpolatePhase(const PolyphaseFilterBank<S>& bank, const S* hist,
                          std::uint64_t phase) noexcept {
    using Tr = SampleTraits<S>;
    const int lg = std::countr_zero(bank.numPhases());
    const std::size_t p = static_cast<std::size_t>(phase >> (64 - lg));
    const auto fr = Tr::blendFactorFromQ64(phase << lg);
    const auto* c0 = bank.phase(p);
    const auto* c1 = bank.phase(p + 1);
    typename Tr::Accum acc{};
    const std::size_t taps = bank.taps();
    for (std::size_t t = 0; t < taps; ++t)
        acc = Tr::mac(acc, hist[t], Tr::blend(c0[t], c1[t], fr));
    return Tr::finalize(acc);
}

/// Dot product of a pre-blended coefficient row against a history window.
/// Identical arithmetic to interpolate() given the same mu: blend then mac,
/// per tap, in the same order — outputs are bit-exact either way.
template <SampleType S>
inline S dotRow(const typename SampleTraits<S>::Coeff* SRT_RESTRICT row, const S* SRT_RESTRICT hist,
                std::size_t taps) noexcept {
    using Tr = SampleTraits<S>;
#if SRT_Q15_SMLALD
    if constexpr (std::is_same_v<S, std::int16_t>) {
        std::int64_t acc = 0;
        std::size_t t = 0;
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
    typename Tr::Accum acc{};
    for (std::size_t t = 0; t < taps; ++t)
        acc = Tr::mac(acc, hist[t], row[t]);
    return Tr::finalize(acc);
}

/// One K-channel tile of the channel-parallel dot (hypothesis C6): K
/// accumulators live in a constexpr-size local array — registers, not
/// memory — while the tap loop walks the frame-major window with stride
/// `stride` samples per frame. K is the register-blocking factor; a naive
/// channels-inner loop with accumulators in memory measures ~2.8x SLOWER
/// than planar (each mac round-trips its accumulator through the stack).
template <SampleType S, std::size_t K>
inline void dotTileFrameMajor(const typename SampleTraits<S>::Coeff* SRT_RESTRICT row,
                              const S* SRT_RESTRICT x, std::size_t taps, std::size_t stride,
                              S* SRT_RESTRICT out) noexcept {
    using Tr = SampleTraits<S>;
    typename Tr::Accum acc[K]{};
    for (std::size_t t = 0; t < taps; ++t) {
        const auto coeff = row[t];
        const S* SRT_RESTRICT frame = x + t * stride;
        for (std::size_t k = 0; k < K; ++k)
            acc[k] = Tr::mac(acc[k], frame[k], coeff);
    }
    for (std::size_t k = 0; k < K; ++k)
        out[k] = Tr::finalize(acc[k]);
}

/// Channel-parallel dot products over a frame-major history block: all
/// channels' outputs for one frame in register-blocked tiles of 8/4/2/1.
/// Per channel the accumulation order over taps equals dotRow's, so the
/// outputs are bit-exact vs the planar path for every sample type — float
/// included, since each channel's double accumulator still sums the taps
/// in the same order (lanes are channels, not taps).
template <SampleType S>
inline void dotRowsFrameMajor(const typename SampleTraits<S>::Coeff* SRT_RESTRICT row,
                              const S* SRT_RESTRICT x, std::size_t taps, std::size_t channels,
                              S* SRT_RESTRICT out) noexcept {
    std::size_t c = 0;
    for (; c + 8 <= channels; c += 8)
        dotTileFrameMajor<S, 8>(row, x + c, taps, channels, out + c);
    if (c + 4 <= channels) {
        dotTileFrameMajor<S, 4>(row, x + c, taps, channels, out + c);
        c += 4;
    }
    if (c + 2 <= channels) {
        dotTileFrameMajor<S, 2>(row, x + c, taps, channels, out + c);
        c += 2;
    }
    if (c < channels)
        dotTileFrameMajor<S, 1>(row, x + c, taps, channels, out + c);
}

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
template <SampleType S>
class FractionalResampler {
public:
    /// Frame-major channel-parallel mode is compiled in only on CP targets
    /// and only for floating-point samples (see SRT_CHANNEL_PARALLEL).
    static constexpr bool kChannelParallel =
        SRT_CHANNEL_PARALLEL != 0 && std::is_floating_point_v<S>;

    /// Allocates histories and the pop scratch buffer; setup time only.
    FractionalResampler(const PolyphaseFilterBank<S>& bank, std::size_t channels,
                        std::size_t chunkFrames = 64)
        : bank_(&bank), channels_(channels), chunk_(chunkFrames),
          histCap_(bank.taps() + chunkFrames), scratch_(chunkFrames * channels),
          frameMajor_(kChannelParallel && channels >= SRT_CP_MIN_CHANNELS),
          hist_(frameMajor_ ? 1 : channels), row_(bank.taps()) {
        if (channels_ == 0 || chunk_ == 0)
            throw std::invalid_argument("FractionalResampler: bad config");
        for (auto& h : hist_)
            h.assign(histCap_ * (frameMajor_ ? channels_ : 1), SampleTraits<S>::silence());
        reset();
    }

    /// Clears history, scratch and mu. Frames already popped into the scratch
    /// are dropped (only used across discontinuities, where they are stale).
    void reset() noexcept {
        phase_ = 0;
        end_ = 0;
        primed_ = false;
        scratchFrames_ = 0;
        scratchPos_ = 0;
    }

    /// Fractional position in [0,1) as a double; used by the servo at block
    /// rate (one conversion per pull, not per sample).
    double mu() const noexcept { return static_cast<double>(phase_) * 0x1p-64; }
    bool primed() const noexcept { return primed_; }

    /// Frames popped from the source but not yet consumed by the filter; part
    /// of the effective backlog the servo must observe.
    std::size_t bufferedFrames() const noexcept { return scratchFrames_ - scratchPos_; }

    /// Fills the history window with taps() frames from the source.
    /// Returns false (and stays unprimed) if the source ran dry.
    template <typename PopFn>
    bool prime(PopFn&& popFrames) noexcept {
        const std::size_t need = bank_->taps();
        for (std::size_t i = 0; i < need; ++i) {
            if (!appendOne(popFrames))
                return false;
        }
        primed_ = true;
        return true;
    }

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
    std::size_t process(S* out, std::size_t maxFrames, double epsHat, PopFn&& popFrames) noexcept {
        // eps in Q0.64, converted once per call (block rate). |eps| is
        // servo-clamped to ~1e-3, so eps * 2^64 fits int64 comfortably.
        const auto epsFix = static_cast<std::int64_t>(epsHat * 0x1p64);
        const auto epsU = static_cast<std::uint64_t>(epsFix);
        for (std::size_t n = 0; n < maxFrames; ++n) {
            const std::uint64_t m = phase_ + epsU; // mod 2^64
            std::size_t advance = 1;
            if (epsFix >= 0) {
                if (m < phase_)      // wrapped past 1.0: forward slip,
                    advance = 2;     // consume one extra input frame
            } else if (m > phase_) { // wrapped below 0.0: backward slip,
                advance = 0;         // re-use the current window
            }
            for (std::size_t a = 0; a < advance; ++a) {
                if (!appendOne(popFrames))
                    return n; // dry: phase_ not advanced for this frame
            }
            phase_ = m;
            // Q15 on SMLALD targets routes mono through blendRow+dotRow as
            // well: dotRow carries the dual-MAC loop, and the two paths are
            // bit-exact by construction (see dotRow).
            constexpr bool kPreferDotRow = SRT_Q15_SMLALD && std::is_same_v<S, std::int16_t>;
            if (channels_ == 1 && !kPreferDotRow) { // fused blend+mac; no scratch traffic
                out[n] = interpolatePhase(*bank_, window(0), m);
            } else if (kChannelParallel && frameMajor_) { // constant-folds away off-host
                // High channel counts: one blend, then all channels' dots in
                // a single channel-parallel pass over the frame-major window.
                blendRowPhase(*bank_, row_.data(), m);
                const std::size_t taps = bank_->taps();
                const S* base = hist_[0].data() + (end_ - taps) * channels_;
                dotRowsFrameMajor<S>(row_.data(), base, taps, channels_, out + n * channels_);
            } else {
                // Blend once per frame, dot per channel: the blend is the
                // same for every channel, so this halves the inner-loop work
                // for stereo and scales with channel count.
                blendRowPhase(*bank_, row_.data(), m);
                const std::size_t taps = bank_->taps();
                for (std::size_t c = 0; c < channels_; ++c)
                    out[n * channels_ + c] = dotRow<S>(row_.data(), window(c), taps);
            }
        }
        return maxFrames;
    }

private:
    const S* window(std::size_t c) const noexcept { return hist_[c].data() + end_ - bank_->taps(); }

    template <typename PopFn>
    bool appendOne(PopFn&& popFrames) noexcept {
        if (scratchPos_ == scratchFrames_) {
            scratchFrames_ = popFrames(scratch_.data(), chunk_);
            scratchPos_ = 0;
            if (scratchFrames_ == 0)
                return false;
        }
        if (end_ == histCap_) { // compact: keep the newest T-1 frames at the front
            const std::size_t keep = bank_->taps() - 1;
            // Samples per frame slot; the gate is compile-time so non-CP
            // targets keep their previous codegen exactly (the runtime form
            // measured +6-8% on the M55 ratchet from hot-loop branch bloat).
            const std::size_t w = (kChannelParallel && frameMajor_) ? channels_ : 1;
            for (auto& h : hist_)
                std::memmove(h.data(), h.data() + (end_ - keep) * w, keep * w * sizeof(S));
            end_ = keep;
        }
        const S* frame = scratch_.data() + scratchPos_ * channels_;
        if (kChannelParallel && frameMajor_) { // frames stay interleaved: one contiguous copy
            std::memcpy(hist_[0].data() + end_ * channels_, frame, channels_ * sizeof(S));
        } else {
            for (std::size_t c = 0; c < channels_; ++c)
                hist_[c][end_] = frame[c];
        }
        ++end_;
        ++scratchPos_;
        return true;
    }

    const PolyphaseFilterBank<S>* bank_;
    std::size_t channels_;
    std::size_t chunk_;
    std::size_t histCap_;
    std::vector<S> scratch_; // interleaved staging for bulk pops
    // History storage: planar (one delay line per channel, hist_[c]) below
    // SRT_CP_MIN_CHANNELS, frame-major (single interleaved line, hist_[0])
    // at or above it on SRT_CHANNEL_PARALLEL targets. end_/histCap_ count
    // frames in both modes.
    bool frameMajor_;
    std::vector<std::vector<S>> hist_;
    std::vector<typename SampleTraits<S>::Coeff> row_; // per-frame blended coefficients
    std::size_t end_ = 0; // shared end index; all channels advance in lockstep
    std::size_t scratchFrames_ = 0;
    std::size_t scratchPos_ = 0;
    std::uint64_t phase_ = 0; // fractional position, unsigned Q0.64
    bool primed_ = false;
};

} // namespace srt

#endif // SRT_POLYPHASE_FILTER_HPP
