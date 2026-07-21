/// @file asrc.h
/// @brief Top-level push/pull asynchronous sample rate converter.
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "srt/pi_servo.h"
#include "srt/polyphase_filter.h"
#include "srt/sample_traits.h"
#include "srt/spsc_ring.h"

namespace tap::samplerate {

    // ANCHOR: p0_config
    /// Converter configuration. The defaults give ~1.5 ms designed latency at
    /// 48 kHz (FIFO setpoint 48 frames + ~24 frames filter group delay; see
    /// the README latency section), transparent for clocks within +/-1000 ppm.
    struct config {
        double       sample_rate_hz        = 48000.0; ///< nominal rate of BOTH clock domains
        std::size_t  channels              = 2;
        std::size_t  target_latency_frames = 48; ///< FIFO occupancy setpoint (~1 ms at 48 kHz)
        std::size_t  fifo_frames           = 0;  ///< ring capacity; 0 => automatic
        filter_spec  filter{};
        servo_config servo{};
        // ANCHOR_END: p0_config

        /// Defaults adapted to a nominal rate other than 48 kHz. The filter
        /// band edges and servo bandwidths are absolute Hz designed for 48 kHz;
        /// running another rate with unscaled defaults silently costs quality
        /// (measured: ~32 dB at 16 kHz, because the slip beat ppm * fs drops
        /// below the servo smoothers' rejection). This factory rescales both —
        /// see filter_spec::scaledTo and servo_config::scaledTo — and is the
        /// recommended starting point for any non-48 kHz deployment:
        ///
        ///   tap::samplerate::Config cfg = tap::samplerate::Config::forSampleRate(16000.0);
        ///   cfg.channels = ...;            // then adjust as usual
        ///
        /// Frame-denominated fields (targetLatencyFrames, fifoFrames) are
        /// rate-invariant in frames and left alone; note their duration in
        /// milliseconds scales inversely with the rate.
        static config for_sample_rate(double sample_rate_hz) noexcept {
            config c;
            c.sample_rate_hz = sample_rate_hz;
            c.filter         = c.filter.scaled_to(sample_rate_hz);
            c.servo          = c.servo.scaled_to(sample_rate_hz);
            return c;
        }
    };

    /// Converter state as seen by status().
    enum class converter_state : int {
        filling,   ///< buffering input until the FIFO reaches its setpoint
        acquiring, ///< servo running at the wide acquisition bandwidth
        locked     ///< servo narrowed; steady-state tracking
    };

    /// Snapshot of converter telemetry; safe to call from any thread.
    ///
    /// Counters are kept in 32-bit atomics internally (so the hot path stays
    /// genuinely lock-free on 32-bit targets) and therefore wrap at 2^32 —
    /// far beyond any plausible event count, but treat them as modular if you
    /// difference them over very long horizons.
    struct converter_status {
        converter_state state            = converter_state::filling;
        double          ratio_estimate   = 1.0; ///< estimated f_in / f_out = 1 + epsHat
        double          ppm              = 0.0; ///< epsHat * 1e6
        double          fifo_fill_frames = 0.0; ///< smoothed occupancy observable
        std::uint64_t   underruns        = 0;   ///< consumer ran dry (output zero-padded)
        std::uint64_t   overruns         = 0;   ///< push() calls that could not accept every
                                                ///< offered frame (FIFO full; excess dropped)
        std::uint64_t resyncs = 0;              ///< hard occupancy resyncs (high watermark)
        /// The setpoint actually in force. Starts at Config::targetLatencyFrames
        /// and is raised automatically when pull() blocks larger than the
        /// setpoint are observed (see pull()); differs from the configured value
        /// exactly when that adaptation has occurred.
        std::uint64_t effective_target_latency_frames = 0;
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
    template <sample_type S>
    class basic_async_sample_rate_converter {
      public:
        explicit basic_async_sample_rate_converter(const config& cfg)
            : m_cfg(validated(cfg))
            , m_bank(m_cfg.filter, m_cfg.sample_rate_hz)
            , m_resampler(m_bank, m_cfg.channels, k_pop_chunk_frames)
            , m_ring(ring_capacity_elems(m_cfg, m_bank.taps()))
            , m_servo(m_cfg.servo, m_cfg.sample_rate_hz, static_cast<double>(m_cfg.target_latency_frames))
            , m_target_frames(m_cfg.target_latency_frames)
            , m_fill_threshold_frames(m_cfg.target_latency_frames + m_bank.taps())
            , m_high_water_frames(
                  std::max(3 * m_cfg.target_latency_frames, m_fill_threshold_frames + m_cfg.target_latency_frames)) {
            if (m_ring.capacity() / m_cfg.channels <= m_high_water_frames) {
                throw std::invalid_argument("async_sample_rate_converter: fifoFrames too small");
            }
            // Largest setpoint the FIFO capacity supports while keeping the
            // high-watermark relation; bounds the adaptive raise in pull().
            const std::size_t cap_frames = m_ring.capacity() / m_cfg.channels;
            const std::size_t taps       = m_bank.taps();
            m_max_target_frames =
                std::max(m_cfg.target_latency_frames,
                         std::min((cap_frames - 1) / 3,
                                  cap_frames > taps + 1 ? (cap_frames - taps - 1) / 2 : m_cfg.target_latency_frames));
            m_effective_target.store(static_cast<std::uint32_t>(m_target_frames), std::memory_order_relaxed);
        }

        basic_async_sample_rate_converter(const basic_async_sample_rate_converter&)            = delete;
        basic_async_sample_rate_converter& operator=(const basic_async_sample_rate_converter&) = delete;

        /// Producer thread: offer `frames` interleaved input frames at the input
        /// clock. Returns frames accepted; fewer than `frames` means the FIFO was
        /// full (newest data dropped, overrun counted).
        std::size_t push(const S* interleaved, std::size_t frames) noexcept {
            const std::size_t accept_frames = std::min(frames, m_ring.write_available() / m_cfg.channels);
            m_ring.write(interleaved, accept_frames * m_cfg.channels);
            if (accept_frames < frames) {
                m_overruns.fetch_add(1, std::memory_order_relaxed);
            }
            return accept_frames;
        }

        /// Consumer thread: produce exactly `frames` interleaved output frames at
        /// the output clock. Silence-pads while filling and on underrun, and
        /// fades the first kFadeFrames frames in after every (re)fill so dropout
        /// recovery does not click. (The dropout onset itself and a hard-resync
        /// splice are unfaded cuts: there is nothing valid to fade to at the
        /// moment they occur.) Returns the number of frames synthesized from
        /// real input.
        std::size_t pull(S* interleaved, std::size_t frames) noexcept {
            const std::size_t ch     = m_cfg.channels;
            const auto        pop_fn = [this](S* dst, std::size_t max_frames) noexcept {
                return m_ring.read(dst, max_frames * m_cfg.channels) / m_cfg.channels;
            };

            // ANCHOR: asrc_feasibility
            // Feasibility: a pull must synthesize from frames already buffered,
            // so the occupancy setpoint must exceed the pull block size or the
            // loop drains into a permanent underrun limit cycle (dropouts every
            // few hundred ms, never locking). Raise the effective setpoint to
            // the largest observed block plus slew/sawtooth margin, bounded by
            // FIFO capacity; the servo slews to the new setpoint glitch-free
            // (integrator kept, occupancy only grows). Cost: latency follows
            // the raised setpoint — see converter_status::effectiveTargetLatencyFrames.
            if (frames > m_observed_max_pull) {
                m_observed_max_pull = frames;
                // Margin sized to the block-beat sawtooth (~half the block) so
                // the entry occupancy never grazes the pull size; configs that
                // already satisfy it (e.g. the 32-frame default transfer against
                // the 48-frame default setpoint) are left exactly as configured.
                const std::size_t needed     = frames + std::max<std::size_t>(frames / 2, k_pop_chunk_frames);
                const std::size_t new_target = std::clamp(needed, m_cfg.target_latency_frames, m_max_target_frames);
                if (new_target > m_target_frames) {
                    m_target_frames         = new_target;
                    m_fill_threshold_frames = new_target + m_bank.taps();
                    m_high_water_frames     = std::max(3 * new_target, m_fill_threshold_frames + new_target);
                    m_servo.set_target(static_cast<double>(new_target));
                    m_effective_target.store(static_cast<std::uint32_t>(new_target), std::memory_order_relaxed);
                }
            }

            // ANCHOR_END: asrc_feasibility
            double occ = backlog_frames();

            // ANCHOR: asrc_filling
            if (m_filling) {
                if (occ < static_cast<double>(m_fill_threshold_frames)) {
                    fill_silence(interleaved, frames * ch);
                    publish_status();
                    return 0;
                }
                m_resampler.reset();
                m_resampler.prime(pop_fn); // guaranteed: occ >= target + taps
                m_servo.reset(true);       // keep ppm estimate across dropouts
                occ = backlog_frames();
                m_servo.seed(occ);
                m_filling          = false;
                m_fade_frames_left = k_fade_frames;
            }

            // ANCHOR_END: asrc_filling
            // ANCHOR: asrc_resync
            if (occ > static_cast<double>(m_high_water_frames)) { // hard resync
                const double target = static_cast<double>(m_target_frames);
                // The discard can only come from the ring; frames staged in the
                // resampler scratch are part of occ but not discardable. Clamp,
                // or a setpoint below the staged count drains the ring entirely
                // and cascades straight back into Filling.
                const std::size_t ring_frames = m_ring.read_available() / ch;
                const double      excess      = occ - target;
                const std::size_t drop_frames =
                    std::min(ring_frames, excess > 0.0 ? static_cast<std::size_t>(excess) : 0);
                m_ring.discard(drop_frames * ch);
                m_resyncs.fetch_add(1, std::memory_order_relaxed);
                occ = backlog_frames();
                m_servo.seed(occ + m_resampler.mu());
            }

            // ANCHOR_END: asrc_resync
            const double dt      = static_cast<double>(frames) / m_cfg.sample_rate_hz;
            const double eps_hat = m_servo.update(occ, m_resampler.mu(), dt);

            // ANCHOR: asrc_underrun
            const std::size_t made = m_resampler.process(interleaved, frames, eps_hat, pop_fn);
            if (m_fade_frames_left != 0 && made != 0) {
                apply_fade_in(interleaved, made);
            }
            if (made < frames) { // underrun: pad and refill
                fill_silence(interleaved + made * ch, (frames - made) * ch);
                m_underruns.fetch_add(1, std::memory_order_relaxed);
                m_filling = true;
                m_servo.reset(true);
            }
            publish_status();
            return made;
            // ANCHOR_END: asrc_underrun
        }

        /// Any thread: telemetry snapshot (relaxed atomics; fields are individually
        /// coherent, not mutually).
        converter_status status() const noexcept {
            converter_status s;
            s.state                           = static_cast<converter_state>(m_state.load(std::memory_order_relaxed));
            s.ppm                             = m_ppm.load(std::memory_order_relaxed);
            s.ratio_estimate                  = 1.0 + s.ppm * 1e-6;
            s.fifo_fill_frames                = m_fill.load(std::memory_order_relaxed);
            s.underruns                       = m_underruns.load(std::memory_order_relaxed);
            s.overruns                        = m_overruns.load(std::memory_order_relaxed);
            s.resyncs                         = m_resyncs.load(std::memory_order_relaxed);
            s.effective_target_latency_frames = m_effective_target.load(std::memory_order_relaxed);
            return s;
        }

        /// Consumer thread: full restart — discard all buffered input, forget the
        /// ppm estimate, return to Filling.
        void reset_from_consumer() noexcept {
            m_ring.discard(m_ring.read_available());
            m_resampler.reset();
            m_servo.reset(false);
            m_filling = true;
            publish_status();
        }

        /// Nominal design latency: FIFO setpoint + filter group delay. Uses the
        /// effective (possibly adaptively raised) setpoint; the actual figure
        /// breathes by a fraction of a frame as the servo tracks drift.
        double designed_latency_seconds() const noexcept {
            return (static_cast<double>(m_effective_target.load(std::memory_order_relaxed))
                    + m_bank.group_delay_samples())
                   / m_cfg.sample_rate_hz;
        }

        const polyphase_filter_bank<S>& filter_bank() const noexcept { return m_bank; }

      private:
        static constexpr std::size_t k_pop_chunk_frames = 16;

        static std::size_t ring_capacity_elems(const config& cfg, std::size_t taps) {
            const std::size_t fill_threshold = cfg.target_latency_frames + taps;
            // The 1024-frame floor (21 ms at 48 kHz) leaves the adaptive
            // setpoint raise enough capacity for pull blocks up to ~340 frames
            // without explicit fifoFrames sizing; larger callbacks need
            // fifoFrames set by the caller (the raise clamps to capacity).
            const std::size_t frames =
                cfg.fifo_frames != 0 ? cfg.fifo_frames : std::max<std::size_t>(1024, 4 * fill_threshold);
            return std::bit_ceil(frames * cfg.channels);
        }

        /// Effective backlog: FIFO occupancy plus frames staged in the resampler's
        /// pop scratch (already off the ring but not yet through the filter).
        double backlog_frames() noexcept {
            return static_cast<double>(m_ring.read_available() / m_cfg.channels + m_resampler.buffered_frames());
        }

        void fill_silence(S* dst, std::size_t count) noexcept {
            for (std::size_t i = 0; i < count; ++i) {
                dst[i] = sample_traits<S>::silence();
            }
        }

        static S scale_sample(S x, double g) noexcept {
            if constexpr (std::is_floating_point_v<S>) {
                return static_cast<S>(static_cast<double>(x) * g);
            }
            else {
                return detail::round_sat<S>(static_cast<double>(x) * g);
            }
        }

        /// Linear gain ramp over the first kFadeFrames frames after a (re)fill.
        /// Rare event and at most 64 frames, so the double math is acceptable
        /// even on FPU-less targets.
        void apply_fade_in(S* interleaved, std::size_t made_frames) noexcept {
            const std::size_t n    = std::min(made_frames, m_fade_frames_left);
            const std::size_t done = k_fade_frames - m_fade_frames_left;
            for (std::size_t f = 0; f < n; ++f) {
                const double g = static_cast<double>(done + f + 1) / static_cast<double>(k_fade_frames);
                for (std::size_t c = 0; c < m_cfg.channels; ++c) {
                    S& x = interleaved[f * m_cfg.channels + c];
                    x    = scale_sample(x, g);
                }
            }
            m_fade_frames_left -= n;
        }

        void publish_status() noexcept {
            const converter_state st = m_filling          ? converter_state::filling
                                       : m_servo.locked() ? converter_state::locked
                                                          : converter_state::acquiring;
            m_state.store(static_cast<int>(st), std::memory_order_relaxed);
            m_ppm.store(static_cast<float>(m_servo.eps_hat() * 1e6), std::memory_order_relaxed);
            m_fill.store(static_cast<float>(m_servo.smoothed_occupancy()), std::memory_order_relaxed);
        }

        /// Rejects configurations that would otherwise construct successfully
        /// and misbehave silently: NaN/Inf anywhere (a NaN sample rate designs
        /// an all-NaN coefficient table), band edges whose sum exceeds the rate
        /// (anti-image cutoff above input Nyquist passes images wholesale), a
        /// deviation clamp large enough to overflow the Q0.64 eps conversion
        /// (UB), and size products that overflow 32-bit size_t targets.
        static config validated(config cfg) {
            const auto finite = [](double v) { return std::isfinite(v); };
            if (cfg.channels == 0 || cfg.target_latency_frames == 0 || !finite(cfg.sample_rate_hz)
                || cfg.sample_rate_hz <= 0.0) {
                throw std::invalid_argument("async_sample_rate_converter: bad Config");
            }
            const filter_spec& f = cfg.filter;
            if (!finite(f.passband_hz) || !finite(f.stopband_hz) || !finite(f.stopband_atten_db)
                || f.passband_hz + f.stopband_hz > cfg.sample_rate_hz) {
                throw std::invalid_argument("async_sample_rate_converter: bad filter_spec "
                                            "(need passbandHz + stopbandHz <= sampleRateHz)");
            }
            const servo_config& sv = cfg.servo;
            if (!finite(sv.acquire_bandwidth_hz) || !finite(sv.track_bandwidth_hz) || !finite(sv.quiet_bandwidth_hz)
                || !finite(sv.damping) || !finite(sv.acquire_smoother_hz) || !finite(sv.track_smoother_hz)
                || !finite(sv.quiet_smoother_hz) || !finite(sv.lock_threshold_frames) || !finite(sv.lock_hold_seconds)
                || !finite(sv.quiet_hold_seconds) || !finite(sv.unlock_threshold_frames)
                || !finite(sv.max_deviation_ppm) || sv.max_deviation_ppm <= 0.0
                || sv.max_deviation_ppm > 100000.0) { // |eps| stays far from the Q0.64 int64 limit
                throw std::invalid_argument("async_sample_rate_converter: bad servo_config");
            }
            // Size products evaluated later must not wrap on 32-bit size_t.
            const auto mul_ok = [](std::size_t a, std::size_t b) {
                return b == 0 || a <= std::numeric_limits<std::size_t>::max() / b;
            };
            const std::size_t phases = std::bit_ceil(f.num_phases);
            if (!mul_ok(phases + 1, f.taps_per_phase)
                || !mul_ok(cfg.target_latency_frames + f.taps_per_phase, 8 * cfg.channels)
                || !mul_ok(cfg.fifo_frames, 2 * cfg.channels)) {
                throw std::invalid_argument("async_sample_rate_converter: Config sizes overflow");
            }
            return cfg;
        }

        static constexpr std::size_t k_fade_frames = 64;

        config                   m_cfg;
        polyphase_filter_bank<S> m_bank;
        fractional_resampler<S>  m_resampler;
        spsc_ring<S>             m_ring;
        pi_servo                 m_servo;
        // Consumer-thread setpoint state (see the adaptive raise in pull()).
        std::size_t m_target_frames;
        std::size_t m_fill_threshold_frames;
        std::size_t m_high_water_frames;
        std::size_t m_max_target_frames = 0;
        std::size_t m_observed_max_pull = 0;
        bool        m_filling           = true; // consumer-thread state; mirrored into state_
        std::size_t m_fade_frames_left  = 0;    // consumer-thread state

        // Telemetry is 32-bit on purpose: 64-bit atomics fall back to lock-based
        // libatomic on 32-bit targets (e.g. Hexagon), which would break the
        // lock-free contract of the hot path. float carries ~7 significant
        // digits — ample for ppm/fill observability; counters wrap at 2^32.
        std::atomic<int>   m_state{static_cast<int>(converter_state::filling)};
        std::atomic<float> m_ppm{0.0f};
        std::atomic<float> m_fill{0.0f};
        // Effective setpoint mirror for status()/designedLatencySeconds() from
        // any thread; written only by the consumer (32-bit: lock-free everywhere).
        std::atomic<std::uint32_t> m_effective_target{0};
        std::atomic<std::uint32_t> m_underruns{0};
        std::atomic<std::uint32_t> m_overruns{0};
        std::atomic<std::uint32_t> m_resyncs{0};

        static_assert(std::atomic<int>::is_always_lock_free && std::atomic<float>::is_always_lock_free
                          && std::atomic<std::uint32_t>::is_always_lock_free,
                      "telemetry atomics must be lock-free for the RT contract");
    };

    /// The float converter.
    using async_sample_rate_converter = basic_async_sample_rate_converter<float>;
    /// Q15 fixed-point converter (int16_t samples; see sample_traits<int16_t>).
    using async_sample_rate_converter_q15 = basic_async_sample_rate_converter<std::int16_t>;
    /// Q31 fixed-point converter (int32_t samples; see sample_traits<int32_t>).
    using async_sample_rate_converter_q31 = basic_async_sample_rate_converter<std::int32_t>;

} // namespace tap::samplerate
