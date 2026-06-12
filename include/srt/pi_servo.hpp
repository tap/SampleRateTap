/// \file pi_servo.hpp
/// \brief Type-2 (PI) clock-tracking servo driven by FIFO occupancy.
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
#ifndef SRT_PI_SERVO_HPP
#define SRT_PI_SERVO_HPP

#include <algorithm>
#include <cmath>
#include <numbers>

namespace srt {

/// Servo tuning. Defaults suit a 48 kHz near-unity converter.
/// unlockThresholdFrames should stay comfortably above half the push/pull
/// block size, since block-quantized occupancy legitimately excursions by
/// that much without the clocks having moved.
struct ServoConfig {
    double acquireBandwidthHz = 10.0;    ///< stage-1 loop bandwidth
    double trackBandwidthHz = 1.0;       ///< stage-2 loop bandwidth
    double quietBandwidthHz = 0.05;      ///< stage-3 loop bandwidth
    double damping = 1.0;                ///< zeta; 1.0 = critically damped
    double acquireSmootherHz = 50.0;     ///< one-pole error prefilter, acquire
    double trackSmootherHz = 5.0;        ///< one-pole error prefilter, track
    double quietSmootherHz = 0.5;        ///< 3-pole cascade corner (always runs)
    double lockThresholdFrames = 1.0;    ///< |e| below this ...
    double lockHoldSeconds = 0.5;        ///< ... this long => acquire -> track
    double quietHoldSeconds = 2.0;       ///< cascade-|e| hold => track -> quiet
    double unlockThresholdFrames = 24.0; ///< |e| above this => demote a stage
    double maxDeviationPpm = 1000.0;     ///< epsHat clamp = +/- 1.5x this

    /// This config rescaled from the 48 kHz design rate to sampleRateHz:
    /// the loop bandwidths and error-smoother corners are absolute Hz and
    /// must track the rate, or the slip-sawtooth beat (ppm * fs) walks out
    /// from under the smoothers — measured as a ~32 dB quality loss at
    /// 16 kHz with unscaled defaults. Hold times scale inversely so the
    /// promotion gates wait the same number of loop time constants.
    /// Frame-denominated thresholds and ppm limits are rate-invariant and
    /// stay put. See Config::forSampleRate.
    ServoConfig scaledTo(double sampleRateHz) const noexcept {
        constexpr double kDesignRateHz = 48000.0;
        const double r = sampleRateHz / kDesignRateHz;
        ServoConfig s = *this;
        s.acquireBandwidthHz *= r;
        s.trackBandwidthHz *= r;
        s.quietBandwidthHz *= r;
        s.acquireSmootherHz *= r;
        s.trackSmootherHz *= r;
        s.quietSmootherHz *= r;
        s.lockHoldSeconds /= r;
        s.quietHoldSeconds /= r;
        return s;
    }
};

/// PI loop filter + three-stage lock-state machine. Pure double-precision
/// math, no allocation; every method is RT-safe.
class PiServo {
public:
    enum class Stage : int { Acquire, Track, Quiet };

    PiServo(const ServoConfig& cfg, double sampleRateHz, double targetFrames) noexcept
        : cfg_(cfg), fs_(sampleRateHz), target_(targetFrames) {
        computeGains(cfg_.acquireBandwidthHz, kpAcquire_, kiAcquire_);
        computeGains(cfg_.trackBandwidthHz, kpTrack_, kiTrack_);
        computeGains(cfg_.quietBandwidthHz, kpQuiet_, kiQuiet_);
        reset(false);
    }

    /// Re-arm the loop. keepIntegrator preserves the accumulated ppm estimate
    /// (the right choice after a dropout: the clocks have not changed).
    void reset(bool keepIntegrator) noexcept {
        if (!keepIntegrator)
            integ_ = 0.0;
        epsHat_ = integ_;
        seed(target_);
        stage_ = Stage::Acquire;
        holdTimer_ = 0.0;
    }

    /// Seed the error smoothers (call when the observable jumps for a known
    /// reason: acquisition start, hard resync) so the loop does not chase the
    /// step.
    void seed(double occPlusMu) noexcept { lpFast_ = q1_ = q2_ = q3_ = occPlusMu; }

    /// One control update; call once per pull() before synthesis.
    /// \param occFrames raw backlog in frames (FIFO + staged frames)
    /// \param mu        current fractional read position; occ + mu changes
    ///                  continuously across whole-sample slips, removing the
    ///                  +/-1 frame staircase from the observable
    /// \param dt        seconds covered by this update (framesPulled / fs)
    /// \return epsHat, the rate-deviation estimate (phase advance = 1 + epsHat)
    double update(double occFrames, double mu, double dt) noexcept {
        const double meas = occFrames + mu;
        const double fastHz =
            stage_ == Stage::Acquire ? cfg_.acquireSmootherHz : cfg_.trackSmootherHz;
        lpFast_ += alpha(fastHz, dt) * (meas - lpFast_);
        const double aq = alpha(cfg_.quietSmootherHz, dt);
        q1_ += aq * (meas - q1_);
        q2_ += aq * (q1_ - q2_);
        q3_ += aq * (q2_ - q3_);
        const double eFast = lpFast_ - target_;
        const double eQuiet = q3_ - target_;

        const double limit = 1.5 * cfg_.maxDeviationPpm * 1e-6;
        switch (stage_) {
        case Stage::Acquire:
            if (advanceHold(eFast, cfg_.lockThresholdFrames, cfg_.lockHoldSeconds, dt)) {
                stage_ = Stage::Track;
                integ_ = std::clamp(epsAvg_, -limit, limit);
            }
            break;
        case Stage::Track:
            if (std::abs(eFast) > cfg_.unlockThresholdFrames) {
                stage_ = Stage::Acquire;
                holdTimer_ = 0.0;
            } else if (advanceHold(eQuiet, cfg_.lockThresholdFrames, cfg_.quietHoldSeconds, dt)) {
                stage_ = Stage::Quiet;
                integ_ = std::clamp(epsAvg_, -limit, limit);
            }
            break;
        case Stage::Quiet:
            if (std::abs(eQuiet) > cfg_.unlockThresholdFrames) {
                stage_ = Stage::Track;
                holdTimer_ = 0.0;
            }
            break;
        }

        double kp = 0.0;
        double ki = 0.0;
        double e = 0.0;
        switch (stage_) {
        case Stage::Acquire:
            kp = kpAcquire_, ki = kiAcquire_, e = eFast;
            break;
        case Stage::Track:
            kp = kpTrack_, ki = kiTrack_, e = eFast;
            break;
        case Stage::Quiet:
            kp = kpQuiet_, ki = kiQuiet_, e = eQuiet;
            break;
        }
        integ_ = std::clamp(integ_ + ki * e * dt, -limit, limit); // anti-windup
        epsHat_ = std::clamp(kp * e + integ_, -limit, limit);
        return epsHat_;
    }

    Stage stage() const noexcept { return stage_; }
    bool locked() const noexcept { return stage_ != Stage::Acquire; }
    double epsHat() const noexcept { return epsHat_; }
    double smoothedOccupancy() const noexcept { return stage_ == Stage::Quiet ? q3_ : lpFast_; }
    double error() const noexcept { return smoothedOccupancy() - target_; }

private:
    static double alpha(double cornerHz, double dt) noexcept {
        return 1.0 - std::exp(-2.0 * std::numbers::pi * cornerHz * dt);
    }

    /// Hold-window logic shared by both promotions: |e| must stay below the
    /// threshold for holdSeconds; meanwhile epsHat is averaged (time constant
    /// holdSeconds/5) so the promotion can hand a clean estimate to the
    /// narrower stage's integrator.
    bool advanceHold(double e, double threshold, double holdSeconds, double dt) noexcept {
        if (std::abs(e) >= threshold) {
            holdTimer_ = 0.0;
            return false;
        }
        if (holdTimer_ == 0.0)
            epsAvg_ = epsHat_;
        else
            epsAvg_ += (1.0 - std::exp(-5.0 * dt / holdSeconds)) * (epsHat_ - epsAvg_);
        holdTimer_ += dt;
        if (holdTimer_ < holdSeconds)
            return false;
        holdTimer_ = 0.0;
        return true;
    }

    void computeGains(double bandwidthHz, double& kp, double& ki) const noexcept {
        const double wn = 2.0 * std::numbers::pi * bandwidthHz;
        kp = 2.0 * cfg_.damping * wn / fs_;
        ki = wn * wn / fs_;
    }

    ServoConfig cfg_;
    double fs_;
    double target_;
    double kpAcquire_ = 0.0, kiAcquire_ = 0.0;
    double kpTrack_ = 0.0, kiTrack_ = 0.0;
    double kpQuiet_ = 0.0, kiQuiet_ = 0.0;
    double lpFast_ = 0.0;                   // acquire/track error smoother
    double q1_ = 0.0, q2_ = 0.0, q3_ = 0.0; // quiet 3-pole cascade
    double integ_ = 0.0;
    double epsHat_ = 0.0;
    double epsAvg_ = 0.0;
    double holdTimer_ = 0.0;
    Stage stage_ = Stage::Acquire;
};

} // namespace srt

#endif // SRT_PI_SERVO_HPP
