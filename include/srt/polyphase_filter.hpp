/// \file polyphase_filter.hpp
/// \brief Polyphase Kaiser-sinc filter bank and the fractional-delay resampler core.
#ifndef SRT_POLYPHASE_FILTER_HPP
#define SRT_POLYPHASE_FILTER_HPP

#include <bit>
#include <cstddef>
#include <cstring>
#include <stdexcept>
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

/// Dot product of a pre-blended coefficient row against a history window.
/// Identical arithmetic to interpolate() given the same mu: blend then mac,
/// per tap, in the same order — outputs are bit-exact either way.
template <SampleType S>
inline S dotRow(const typename SampleTraits<S>::Coeff* SRT_RESTRICT row, const S* SRT_RESTRICT hist,
                std::size_t taps) noexcept {
    using Tr = SampleTraits<S>;
    typename Tr::Accum acc{};
    for (std::size_t t = 0; t < taps; ++t)
        acc = Tr::mac(acc, hist[t], row[t]);
    return Tr::finalize(acc);
}

/// Streaming fractional-delay engine for one converter instance.
///
/// Owns the per-channel history delay lines (planar, contiguous windows with
/// periodic compaction) and the phase accumulator mu. Input frames are pulled
/// through a caller-supplied PopFn in small bulk chunks and deinterleaved into
/// the histories as the integer read position advances.
///
/// Phase accumulator: mu is a double in [0, 1) and accumulates only the rate
/// DEVIATION eps per output sample; the unity part of the ratio is applied as
/// the integer window advance. Adding eps ~ 1e-4 to a [0,1) double keeps full
/// 52-bit fractional precision (2^-52 samples ~ 5 attoseconds at 48 kHz), far
/// below the ~8 ps jitter budget for 120 dB transparency.
template <SampleType S>
class FractionalResampler {
public:
    /// Allocates histories and the pop scratch buffer; setup time only.
    FractionalResampler(const PolyphaseFilterBank<S>& bank, std::size_t channels,
                        std::size_t chunkFrames = 64)
        : bank_(&bank), channels_(channels), chunk_(chunkFrames),
          histCap_(bank.taps() + chunkFrames), scratch_(chunkFrames * channels), hist_(channels),
          row_(bank.taps()) {
        if (channels_ == 0 || chunk_ == 0)
            throw std::invalid_argument("FractionalResampler: bad config");
        for (auto& h : hist_)
            h.assign(histCap_, SampleTraits<S>::silence());
        reset();
    }

    /// Clears history, scratch and mu. Frames already popped into the scratch
    /// are dropped (only used across discontinuities, where they are stale).
    void reset() noexcept {
        mu_ = 0.0;
        end_ = 0;
        primed_ = false;
        scratchFrames_ = 0;
        scratchPos_ = 0;
    }

    double mu() const noexcept { return mu_; }
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
    /// PopFn: std::size_t popFrames(S* dst, std::size_t maxFrames) — bulk-pops
    /// interleaved frames, returning the count actually delivered.
    template <typename PopFn>
    std::size_t process(S* out, std::size_t maxFrames, double epsHat, PopFn&& popFrames) noexcept {
        for (std::size_t n = 0; n < maxFrames; ++n) {
            double m = mu_ + epsHat;
            std::size_t advance = 1;
            if (m >= 1.0) { // forward slip: consume one extra input frame
                m -= 1.0;
                advance = 2;
            } else if (m < 0.0) { // backward slip: re-use the current window
                m += 1.0;
                advance = 0;
            }
            for (std::size_t a = 0; a < advance; ++a) {
                if (!appendOne(popFrames))
                    return n; // dry: mu_ not advanced for this frame
            }
            mu_ = m;
            if (channels_ == 1) { // fused blend+mac; no scratch traffic
                out[n] = interpolate(*bank_, window(0), m);
            } else {
                // Blend once per frame, dot per channel: the blend is the
                // same for every channel, so this halves the inner-loop work
                // for stereo and scales with channel count.
                blendRow(*bank_, row_.data(), m);
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
        if (end_ == histCap_) { // compact: keep the newest T-1 samples at the front
            const std::size_t keep = bank_->taps() - 1;
            for (auto& h : hist_)
                std::memmove(h.data(), h.data() + end_ - keep, keep * sizeof(S));
            end_ = keep;
        }
        const S* frame = scratch_.data() + scratchPos_ * channels_;
        for (std::size_t c = 0; c < channels_; ++c)
            hist_[c][end_] = frame[c];
        ++end_;
        ++scratchPos_;
        return true;
    }

    const PolyphaseFilterBank<S>* bank_;
    std::size_t channels_;
    std::size_t chunk_;
    std::size_t histCap_;
    std::vector<S> scratch_; // interleaved staging for bulk pops
    std::vector<std::vector<S>> hist_;
    std::vector<typename SampleTraits<S>::Coeff> row_; // per-frame blended coefficients
    std::size_t end_ = 0; // shared end index; all channels advance in lockstep
    std::size_t scratchFrames_ = 0;
    std::size_t scratchPos_ = 0;
    double mu_ = 0.0;
    bool primed_ = false;
};

} // namespace srt

#endif // SRT_POLYPHASE_FILTER_HPP
