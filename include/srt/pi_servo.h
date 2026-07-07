/// @file pi_servo.h
/// @brief Type-2 (PI) clock-tracking servo driven by FIFO occupancy.
///
/// Loop design. The plant is the elastic FIFO: with a true rate deviation
/// eps_true and the converter consuming (1 + epsHat) input frames per output
/// frame, the occupancy obeys d(occ)/dt = fs * (eps_true - epsHat) — a pure
/// integrator of gain fs. Closing a PI controller
///     epsHat = Kp * e + Ki * Integral(e dt),   e in frames,
/// around it gives the characteristic equation s^2 + fs*Kp*s + fs*Ki = 0;
/// matching the standard 2nd-order form s^2 + 2*zeta*wn*s + wn^2 yields
///     wn = 2*pi*f_L,   Kp = 2*zeta*wn / fs,   Ki = wn^2 / fs.
/// The integrator nulls a constant ppm offset (type-2 loop: zero standing
/// occupancy error), and the loop bandwidth f_L partitions input timing
/// jitter: components above f_L are absorbed by the buffer, components below
/// f_L pass into the resampling phase and frequency-modulate the audio.
///
/// The count-quantization sawtooth. The occupancy observable is quantized to
/// whole frames on the producer side, so at deviation eps it carries a
/// deterministic sawtooth of one push-block peak to peak at the beat
/// frequency eps*fs/pushBlock. Whatever the loop passes into epsHat becomes
/// FM sidebands on the audio, and a PI's proportional path leaks measurement
/// noise at gain Kp at all frequencies above f_L. This drives a three-stage
/// design:
///
///  - ACQUIRE (10 Hz, single 50 Hz smoother): fast initial pull-in.
///  - TRACK (1 Hz, single 5 Hz smoother): solid lock; with coarse-block
///    transfer (e.g. 32-frame callbacks) the loop deliberately stays here,
///    phase-tracking the block beat as benign latency breathing.
///  - QUIET (0.05 Hz, three-pole 0.5 Hz error cascade): steady-state mode for
///    fine-grained transfer; rejects a one-frame sawtooth to roughly
///    -120 dBc equivalent at 20 kHz while still following a 1 ppm/s drift
///    ramp with under half a frame of standing error.
///
/// Promotion TRACK -> QUIET is gated on the cascade-smoothed error staying
/// small, which is naturally false while a large block beat dominates the
/// observable — the discriminator that keeps coarse-block operation out of
/// the mode that could not handle it. At each promotion the integrator is
/// loaded with a hold-window average of epsHat (the wide stages phase-track
/// the sawtooth, so their instantaneous estimate wobbles; the average is the
/// clean central value), making handoffs transient-free to first order.
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace srt {

    // ANCHOR: sv_config
    /// Servo tuning. Defaults suit a 48 kHz near-unity converter.
    /// unlockThresholdFrames should stay comfortably above half the push/pull
    /// block size, since block-quantized occupancy legitimately excursions by
    /// that much without the clocks having moved.
    struct servo_config {
        double acquire_bandwidth_hz    = 10.0;   ///< stage-1 loop bandwidth
        double track_bandwidth_hz      = 1.0;    ///< stage-2 loop bandwidth
        double quiet_bandwidth_hz      = 0.05;   ///< stage-3 loop bandwidth
        double damping                 = 1.0;    ///< zeta; 1.0 = critically damped
        double acquire_smoother_hz     = 50.0;   ///< one-pole error prefilter, acquire
        double track_smoother_hz       = 5.0;    ///< one-pole error prefilter, track
        double quiet_smoother_hz       = 0.5;    ///< 3-pole cascade corner (always runs)
        double lock_threshold_frames   = 1.0;    ///< |e| below this ...
        double lock_hold_seconds       = 0.5;    ///< ... this long => acquire -> track
        double quiet_hold_seconds      = 2.0;    ///< cascade-|e| hold => track -> quiet
        double unlock_threshold_frames = 24.0;   ///< |e| above this => demote a stage
        double max_deviation_ppm       = 1000.0; ///< epsHat clamp = +/- 1.5x this
        // ANCHOR_END: sv_config

        // ANCHOR: sv_scaled_to
        /// This config rescaled from the 48 kHz design rate to sampleRateHz:
        /// the loop bandwidths and error-smoother corners are absolute Hz and
        /// must track the rate, or the slip-sawtooth beat (ppm * fs) walks out
        /// from under the smoothers — measured as a ~32 dB quality loss at
        /// 16 kHz with unscaled defaults. Hold times scale inversely so the
        /// promotion gates wait the same number of loop time constants.
        /// Frame-denominated thresholds and ppm limits are rate-invariant and
        /// stay put. See Config::forSampleRate.
        servo_config scaled_to(double sample_rate_hz) const noexcept {
            constexpr double k_design_rate_hz = 48000.0;
            const double     r                = sample_rate_hz / k_design_rate_hz;
            servo_config     s                = *this;
            s.acquire_bandwidth_hz *= r;
            s.track_bandwidth_hz *= r;
            s.quiet_bandwidth_hz *= r;
            s.acquire_smoother_hz *= r;
            s.track_smoother_hz *= r;
            s.quiet_smoother_hz *= r;
            s.lock_hold_seconds /= r;
            s.quiet_hold_seconds /= r;
            return s;
        }
        // ANCHOR_END: sv_scaled_to
    };

    /// PI loop filter + three-stage lock-state machine. Pure double-precision
    /// math, no allocation; every method is RT-safe.
    class pi_servo {
      public:
        enum class lock_stage : int { acquire, track, quiet };

        pi_servo(const servo_config& cfg, double sample_rate_hz, double target_frames) noexcept
            : m_cfg(cfg)
            , m_fs(sample_rate_hz)
            , m_target(target_frames) {
            compute_gains(m_cfg.acquire_bandwidth_hz, m_kp_acquire, m_ki_acquire);
            compute_gains(m_cfg.track_bandwidth_hz, m_kp_track, m_ki_track);
            compute_gains(m_cfg.quiet_bandwidth_hz, m_kp_quiet, m_ki_quiet);
            reset(false);
        }

        // ANCHOR: sv_reset
        /// Re-arm the loop. keepIntegrator preserves the accumulated ppm estimate
        /// (the right choice after a dropout: the clocks have not changed).
        void reset(bool keep_integrator) noexcept {
            if (!keep_integrator) {
                m_integ = 0.0;
            }
            m_eps_hat = m_integ;
            seed(m_target);
            m_stage      = lock_stage::acquire;
            m_hold_timer = 0.0;
        }

        /// Seed the error smoothers (call when the observable jumps for a known
        /// reason: acquisition start, hard resync) so the loop does not chase the
        /// step.
        void seed(double occ_plus_mu) noexcept { m_lp_fast = m_q1 = m_q2 = m_q3 = occ_plus_mu; }

        /// Move the occupancy setpoint. The integrator (ppm estimate) is kept and
        /// the smoothers are left tracking the real observable, so the loop slews
        /// to the new setpoint at its clamped rate with no transient discontinuity
        /// — used by the converter's adaptive pull-block setpoint raise.
        void set_target(double target_frames) noexcept { m_target = target_frames; }
        // ANCHOR_END: sv_reset

        // ANCHOR: sv_update_smooth
        /// One control update; call once per pull() before synthesis.
        /// \param occFrames raw backlog in frames (FIFO + staged frames)
        /// \param mu        current fractional read position; occ + mu changes
        ///                  continuously across whole-sample slips, removing the
        ///                  +/-1 frame staircase from the observable
        /// \param dt        seconds covered by this update (framesPulled / fs)
        /// \return epsHat, the rate-deviation estimate (phase advance = 1 + epsHat)
        double update(double occ_frames, double mu, double dt) noexcept {
            const double meas    = occ_frames + mu;
            const double fast_hz = m_stage == lock_stage::acquire ? m_cfg.acquire_smoother_hz : m_cfg.track_smoother_hz;
            m_lp_fast += alpha(fast_hz, dt) * (meas - m_lp_fast);
            const double aq = alpha(m_cfg.quiet_smoother_hz, dt);
            m_q1 += aq * (meas - m_q1);
            m_q2 += aq * (m_q1 - m_q2);
            m_q3 += aq * (m_q2 - m_q3);
            const double e_fast  = m_lp_fast - m_target;
            const double e_quiet = m_q3 - m_target;
            // ANCHOR_END: sv_update_smooth

            // ANCHOR: sv_update_stages
            const double limit = 1.5 * m_cfg.max_deviation_ppm * 1e-6;
            switch (m_stage) {
            case lock_stage::acquire:
                if (advance_hold(e_fast, m_cfg.lock_threshold_frames, m_cfg.lock_hold_seconds, dt)) {
                    m_stage = lock_stage::track;
                    m_integ = std::clamp(m_eps_avg, -limit, limit);
                }
                break;
            case lock_stage::track:
                if (std::abs(e_fast) > m_cfg.unlock_threshold_frames) {
                    m_stage      = lock_stage::acquire;
                    m_hold_timer = 0.0;
                }
                else if (advance_hold(e_quiet, m_cfg.lock_threshold_frames, m_cfg.quiet_hold_seconds, dt)) {
                    m_stage = lock_stage::quiet;
                    m_integ = std::clamp(m_eps_avg, -limit, limit);
                }
                break;
            case lock_stage::quiet:
                if (std::abs(e_quiet) > m_cfg.unlock_threshold_frames) {
                    m_stage      = lock_stage::track;
                    m_hold_timer = 0.0;
                }
                break;
            }
            // ANCHOR_END: sv_update_stages

            // ANCHOR: sv_update_out
            double kp = 0.0;
            double ki = 0.0;
            double e  = 0.0;
            switch (m_stage) {
            case lock_stage::acquire:
                kp = m_kp_acquire, ki = m_ki_acquire, e = e_fast;
                break;
            case lock_stage::track:
                kp = m_kp_track, ki = m_ki_track, e = e_fast;
                break;
            case lock_stage::quiet:
                kp = m_kp_quiet, ki = m_ki_quiet, e = e_quiet;
                break;
            }
            m_integ   = std::clamp(m_integ + ki * e * dt, -limit, limit); // anti-windup
            m_eps_hat = std::clamp(kp * e + m_integ, -limit, limit);
            return m_eps_hat;
        }
        // ANCHOR_END: sv_update_out

        lock_stage stage() const noexcept { return m_stage; }
        bool       locked() const noexcept { return m_stage != lock_stage::acquire; }
        double     eps_hat() const noexcept { return m_eps_hat; }
        double     smoothed_occupancy() const noexcept { return m_stage == lock_stage::quiet ? m_q3 : m_lp_fast; }
        double     error() const noexcept { return smoothed_occupancy() - m_target; }

      private:
        static double alpha(double corner_hz, double dt) noexcept {
            return 1.0 - std::exp(-2.0 * std::numbers::pi * corner_hz * dt);
        }

        // ANCHOR: sv_hold
        /// Hold-window logic shared by both promotions: |e| must stay below the
        /// threshold for holdSeconds; meanwhile epsHat is averaged (time constant
        /// holdSeconds/5) so the promotion can hand a clean estimate to the
        /// narrower stage's integrator.
        bool advance_hold(double e, double threshold, double hold_seconds, double dt) noexcept {
            if (std::abs(e) >= threshold) {
                m_hold_timer = 0.0;
                return false;
            }
            if (m_hold_timer == 0.0) {
                m_eps_avg = m_eps_hat;
            }
            else {
                m_eps_avg += (1.0 - std::exp(-5.0 * dt / hold_seconds)) * (m_eps_hat - m_eps_avg);
            }
            m_hold_timer += dt;
            if (m_hold_timer < hold_seconds) {
                return false;
            }
            m_hold_timer = 0.0;
            return true;
        }
        // ANCHOR_END: sv_hold

        // ANCHOR: sv_gains
        void compute_gains(double bandwidth_hz, double& kp, double& ki) const noexcept {
            const double wn = 2.0 * std::numbers::pi * bandwidth_hz;
            kp              = 2.0 * m_cfg.damping * wn / m_fs;
            ki              = wn * wn / m_fs;
        }
        // ANCHOR_END: sv_gains

        servo_config m_cfg;
        double       m_fs;
        double       m_target;
        double       m_kp_acquire = 0.0, m_ki_acquire = 0.0;
        double       m_kp_track = 0.0, m_ki_track = 0.0;
        double       m_kp_quiet = 0.0, m_ki_quiet = 0.0;
        double       m_lp_fast = 0.0;                    // acquire/track error smoother
        double       m_q1 = 0.0, m_q2 = 0.0, m_q3 = 0.0; // quiet 3-pole cascade
        double       m_integ      = 0.0;
        double       m_eps_hat    = 0.0;
        double       m_eps_avg    = 0.0;
        double       m_hold_timer = 0.0;
        lock_stage   m_stage      = lock_stage::acquire;
    };

} // namespace srt
