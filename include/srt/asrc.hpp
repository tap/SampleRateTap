/// \file asrc.hpp
/// \brief Top-level push/pull asynchronous sample rate converter.
#ifndef SRT_ASRC_HPP
#define SRT_ASRC_HPP

#include <atomic>
#include <bit>
#include <cstdint>
#include <stdexcept>

#include "srt/pi_servo.hpp"
#include "srt/polyphase_filter.hpp"
#include "srt/sample_traits.hpp"
#include "srt/spsc_ring.hpp"

namespace srt {

/// Converter configuration. The defaults realize the whitepaper's worked
/// budget: ~1 ms core latency (FIFO setpoint 48 frames + ~24 frames filter
/// group delay) at 48 kHz, transparent for clocks within +/-1000 ppm.
struct Config {
    double sampleRateHz = 48000.0; ///< nominal rate of BOTH clock domains
    std::size_t channels = 2;
    std::size_t targetLatencyFrames = 48; ///< FIFO occupancy setpoint (~1 ms at 48 kHz)
    std::size_t fifoFrames = 0;           ///< ring capacity; 0 => automatic
    FilterSpec filter{};
    ServoConfig servo{};
};

/// Converter state as seen by status().
enum class State : int {
    Filling,   ///< buffering input until the FIFO reaches its setpoint
    Acquiring, ///< servo running at the wide acquisition bandwidth
    Locked     ///< servo narrowed; steady-state tracking
};

/// Snapshot of converter telemetry; safe to call from any thread.
struct Status {
    State state = State::Filling;
    double ratioEstimate = 1.0;  ///< estimated f_in / f_out = 1 + epsHat
    double ppm = 0.0;            ///< epsHat * 1e6
    double fifoFillFrames = 0.0; ///< smoothed occupancy observable
    std::uint64_t underruns = 0; ///< consumer ran dry (output zero-padded)
    std::uint64_t overruns = 0;  ///< producer pushes dropped (FIFO full)
    std::uint64_t resyncs = 0;   ///< hard occupancy resyncs (high watermark)
};

/// Near-unity asynchronous sample rate converter between two clock domains.
///
/// One producer thread calls push() at the input clock; one consumer thread
/// calls pull() at the output clock. A lock-free FIFO sits between the
/// domains; its occupancy drives a type-2 PI servo whose output is the rate
/// deviation estimate epsHat, applied as a creeping fractional delay by the
/// polyphase interpolator.
///
/// Real-time contract: the constructor performs all allocation and filter
/// design and may throw; push(), pull(), status() and resetFromConsumer() are
/// noexcept, lock-free and allocation-free.
template <SampleType S>
class BasicAsyncSampleRateConverter {
public:
    explicit BasicAsyncSampleRateConverter(const Config& cfg)
        : cfg_(cfg), bank_(cfg.filter, cfg.sampleRateHz),
          resampler_(bank_, cfg.channels, kPopChunkFrames),
          ring_(ringCapacityElems(cfg, bank_.taps())),
          servo_(cfg.servo, cfg.sampleRateHz, static_cast<double>(cfg.targetLatencyFrames)),
          fillThresholdFrames_(cfg.targetLatencyFrames + bank_.taps()),
          highWaterFrames_(std::max(3 * cfg.targetLatencyFrames,
                                    fillThresholdFrames_ + cfg.targetLatencyFrames)) {
        if (cfg.channels == 0 || cfg.sampleRateHz <= 0.0 || cfg.targetLatencyFrames == 0)
            throw std::invalid_argument("AsyncSampleRateConverter: bad Config");
        if (ring_.capacity() / cfg.channels <= highWaterFrames_)
            throw std::invalid_argument("AsyncSampleRateConverter: fifoFrames too small");
    }

    BasicAsyncSampleRateConverter(const BasicAsyncSampleRateConverter&) = delete;
    BasicAsyncSampleRateConverter& operator=(const BasicAsyncSampleRateConverter&) = delete;

    /// Producer thread: offer `frames` interleaved input frames at the input
    /// clock. Returns frames accepted; fewer than `frames` means the FIFO was
    /// full (newest data dropped, overrun counted).
    std::size_t push(const S* interleaved, std::size_t frames) noexcept {
        const std::size_t acceptFrames = std::min(frames, ring_.writeAvailable() / cfg_.channels);
        ring_.write(interleaved, acceptFrames * cfg_.channels);
        if (acceptFrames < frames)
            overruns_.fetch_add(1, std::memory_order_relaxed);
        return acceptFrames;
    }

    /// Consumer thread: produce exactly `frames` interleaved output frames at
    /// the output clock. Silence-pads while filling and on underrun. Returns
    /// the number of frames synthesized from real input.
    std::size_t pull(S* interleaved, std::size_t frames) noexcept {
        const std::size_t ch = cfg_.channels;
        const auto popFn = [this](S* dst, std::size_t maxFrames) noexcept {
            return ring_.read(dst, maxFrames * cfg_.channels) / cfg_.channels;
        };

        double occ = backlogFrames();

        if (filling_) {
            if (occ < static_cast<double>(fillThresholdFrames_)) {
                fillSilence(interleaved, frames * ch);
                publishStatus();
                return 0;
            }
            resampler_.reset();
            resampler_.prime(popFn); // guaranteed: occ >= target + taps
            servo_.reset(true);      // keep ppm estimate across dropouts
            occ = backlogFrames();
            servo_.seed(occ);
            filling_ = false;
        }

        if (occ > static_cast<double>(highWaterFrames_)) { // hard resync
            const double target = static_cast<double>(cfg_.targetLatencyFrames);
            const auto dropFrames = static_cast<std::size_t>(occ - target);
            ring_.discard(dropFrames * ch);
            resyncs_.fetch_add(1, std::memory_order_relaxed);
            occ = backlogFrames();
            servo_.seed(occ + resampler_.mu());
        }

        const double dt = static_cast<double>(frames) / cfg_.sampleRateHz;
        const double epsHat = servo_.update(occ, resampler_.mu(), dt);

        const std::size_t made = resampler_.process(interleaved, frames, epsHat, popFn);
        if (made < frames) { // underrun: pad and refill
            fillSilence(interleaved + made * ch, (frames - made) * ch);
            underruns_.fetch_add(1, std::memory_order_relaxed);
            filling_ = true;
            servo_.reset(true);
        }
        publishStatus();
        return made;
    }

    /// Any thread: telemetry snapshot (relaxed atomics; fields are individually
    /// coherent, not mutually).
    Status status() const noexcept {
        Status s;
        s.state = static_cast<State>(state_.load(std::memory_order_relaxed));
        s.ppm = ppm_.load(std::memory_order_relaxed);
        s.ratioEstimate = 1.0 + s.ppm * 1e-6;
        s.fifoFillFrames = fill_.load(std::memory_order_relaxed);
        s.underruns = underruns_.load(std::memory_order_relaxed);
        s.overruns = overruns_.load(std::memory_order_relaxed);
        s.resyncs = resyncs_.load(std::memory_order_relaxed);
        return s;
    }

    /// Consumer thread: full restart — discard all buffered input, forget the
    /// ppm estimate, return to Filling.
    void resetFromConsumer() noexcept {
        ring_.discard(ring_.readAvailable());
        resampler_.reset();
        servo_.reset(false);
        filling_ = true;
        publishStatus();
    }

    /// Nominal design latency: FIFO setpoint + filter group delay. The actual
    /// figure breathes by a fraction of a frame as the servo tracks drift.
    double designedLatencySeconds() const noexcept {
        return (static_cast<double>(cfg_.targetLatencyFrames) + bank_.groupDelaySamples()) /
               cfg_.sampleRateHz;
    }

    const PolyphaseFilterBank<S>& filterBank() const noexcept { return bank_; }

private:
    static constexpr std::size_t kPopChunkFrames = 16;

    static std::size_t ringCapacityElems(const Config& cfg, std::size_t taps) {
        const std::size_t fillThreshold = cfg.targetLatencyFrames + taps;
        const std::size_t frames =
            cfg.fifoFrames != 0 ? cfg.fifoFrames : std::max<std::size_t>(256, 4 * fillThreshold);
        return std::bit_ceil(frames * cfg.channels);
    }

    /// Effective backlog: FIFO occupancy plus frames staged in the resampler's
    /// pop scratch (already off the ring but not yet through the filter).
    double backlogFrames() noexcept {
        return static_cast<double>(ring_.readAvailable() / cfg_.channels +
                                   resampler_.bufferedFrames());
    }

    void fillSilence(S* dst, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i)
            dst[i] = SampleTraits<S>::silence();
    }

    void publishStatus() noexcept {
        const State st = filling_          ? State::Filling
                         : servo_.locked() ? State::Locked
                                           : State::Acquiring;
        state_.store(static_cast<int>(st), std::memory_order_relaxed);
        ppm_.store(servo_.epsHat() * 1e6, std::memory_order_relaxed);
        fill_.store(servo_.smoothedOccupancy(), std::memory_order_relaxed);
    }

    Config cfg_;
    PolyphaseFilterBank<S> bank_;
    FractionalResampler<S> resampler_;
    SpscRing<S> ring_;
    PiServo servo_;
    std::size_t fillThresholdFrames_;
    std::size_t highWaterFrames_;
    bool filling_ = true; // consumer-thread state; mirrored into state_

    std::atomic<int> state_{static_cast<int>(State::Filling)};
    std::atomic<double> ppm_{0.0};
    std::atomic<double> fill_{0.0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<std::uint64_t> resyncs_{0};
};

/// The float converter.
using AsyncSampleRateConverter = BasicAsyncSampleRateConverter<float>;
/// Q15 fixed-point converter (int16_t samples; see SampleTraits<int16_t>).
using AsyncSampleRateConverterQ15 = BasicAsyncSampleRateConverter<std::int16_t>;
/// Q31 fixed-point converter (int32_t samples; see SampleTraits<int32_t>).
using AsyncSampleRateConverterQ31 = BasicAsyncSampleRateConverter<std::int32_t>;

} // namespace srt

#endif // SRT_ASRC_HPP
