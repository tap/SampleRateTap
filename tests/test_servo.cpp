#include <cmath>

#include <gtest/gtest.h>

#include "srt/pi_servo.h"

namespace {

    constexpr double      k_fs     = 48000.0;
    constexpr double      k_target = 48.0;
    constexpr std::size_t k_block  = 32;
    constexpr double      k_dt     = static_cast<double>(k_block) / k_fs;

    // Pure plant simulation: the FIFO integrates the rate mismatch.
    struct plant {
        double occ = k_target;
        void   step(double eps_true, double eps_hat) { occ += (eps_true - eps_hat) * k_fs * k_dt; }
    };

    TEST(Servo, LocksFromConstantOffsetAndNullsError) {
        tap::samplerate::pi_servo servo(tap::samplerate::servo_config{}, k_fs, k_target);
        plant                     plant;
        const double              eps_true          = 300e-6;
        bool                      locked_within1_5s = false;
        double                    t                 = 0.0;
        for (; t < 30.0; t += k_dt) { // locked loop is 0.05 Hz: allow it to settle
            const double eps = servo.update(plant.occ, 0.0, k_dt);
            plant.step(eps_true, eps);
            if (t < 1.5 && servo.locked()) {
                locked_within1_5s = true;
            }
        }
        EXPECT_TRUE(locked_within1_5s);
        EXPECT_TRUE(servo.locked());
        // Type-2 loop: constant ppm offset leaves zero standing occupancy error.
        EXPECT_NEAR(plant.occ, k_target, 0.05);
        EXPECT_NEAR(servo.eps_hat(), eps_true, 1e-6); // within 1 ppm
    }

    TEST(Servo, TracksSlowDriftRampWithBoundedLag) {
        tap::samplerate::pi_servo servo(tap::samplerate::servo_config{}, k_fs, k_target);
        plant                     plant;
        // Settle at 0 ppm first.
        for (double t = 0.0; t < 5.0; t += k_dt) {
            plant.step(0.0, servo.update(plant.occ, 0.0, k_dt));
        }
        ASSERT_TRUE(servo.locked());
        // Then ramp 1 ppm/s for 20 s (temperature-style drift).
        double max_err  = 0.0;
        double eps_true = 0.0;
        for (double t = 0.0; t < 20.0; t += k_dt) {
            eps_true = 1e-6 * t;
            plant.step(eps_true, servo.update(plant.occ, 0.0, k_dt));
            if (t > 5.0) {
                max_err = std::max(max_err, std::abs(plant.occ - k_target));
            }
        }
        EXPECT_TRUE(servo.locked());
        // Type-2 acceleration error: e_ss = (deps/dt * fs) / wn^2 ~ 0.49 frames
        // for 1 ppm/s at the 0.05 Hz locked bandwidth.
        EXPECT_LT(max_err, 1.0);
        EXPECT_NEAR(servo.eps_hat(), eps_true, 2e-6);
    }

    TEST(Servo, BandwidthSwitchIsTransientFree) {
        tap::samplerate::pi_servo servo(tap::samplerate::servo_config{}, k_fs, k_target);
        plant                     plant;
        const double              eps_true = 200e-6;
        // Run until just locked.
        double t = 0.0;
        while (!servo.locked() && t < 5.0) {
            plant.step(eps_true, servo.update(plant.occ, 0.0, k_dt));
            t += k_dt;
        }
        ASSERT_TRUE(servo.locked());
        // The narrow-bandwidth handoff keeps the integrator, so the occupancy
        // must not be disturbed beyond the lock threshold afterwards.
        double max_err = 0.0;
        for (double s = 0.0; s < 10.0; s += k_dt) {
            plant.step(eps_true, servo.update(plant.occ, 0.0, k_dt));
            max_err = std::max(max_err, std::abs(plant.occ - k_target));
        }
        EXPECT_LT(max_err, tap::samplerate::servo_config{}.lock_threshold_frames);
        EXPECT_TRUE(servo.locked());
    }

    TEST(Servo, ClampsToMaxDeviation) {
        tap::samplerate::servo_config cfg;
        cfg.max_deviation_ppm = 100.0;
        tap::samplerate::pi_servo servo(cfg, k_fs, k_target);
        // Huge occupancy error must saturate at 1.5x the configured range.
        const double eps = servo.update(k_target + 10000.0, 0.0, k_dt);
        EXPECT_LE(eps, 1.5 * 100e-6 + 1e-12);
    }

    TEST(Servo, DropoutResetKeepsPpmEstimate) {
        tap::samplerate::pi_servo servo(tap::samplerate::servo_config{}, k_fs, k_target);
        plant                     plant;
        const double              eps_true = 250e-6;
        for (double t = 0.0; t < 6.0; t += k_dt) {
            plant.step(eps_true, servo.update(plant.occ, 0.0, k_dt));
        }
        ASSERT_NEAR(servo.eps_hat(), eps_true, 2e-6);
        servo.reset(true); // dropout: keep the integrator
        EXPECT_NEAR(servo.eps_hat(), eps_true, 5e-6);
        servo.reset(false); // full reset: forget it
        EXPECT_DOUBLE_EQ(servo.eps_hat(), 0.0);
    }

} // namespace
