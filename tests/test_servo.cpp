#include <cmath>

#include <gtest/gtest.h>

#include "srt/pi_servo.hpp"

namespace {

constexpr double kFs = 48000.0;
constexpr double kTarget = 48.0;
constexpr std::size_t kBlock = 32;
constexpr double kDt = static_cast<double>(kBlock) / kFs;

// Pure plant simulation: the FIFO integrates the rate mismatch.
struct Plant {
    double occ = kTarget;
    void step(double epsTrue, double epsHat) { occ += (epsTrue - epsHat) * kFs * kDt; }
};

TEST(Servo, LocksFromConstantOffsetAndNullsError) {
    srt::PiServo servo(srt::ServoConfig{}, kFs, kTarget);
    Plant plant;
    const double epsTrue = 300e-6;
    bool lockedWithin1_5s = false;
    double t = 0.0;
    for (; t < 30.0; t += kDt) { // locked loop is 0.05 Hz: allow it to settle
        const double eps = servo.update(plant.occ, 0.0, kDt);
        plant.step(epsTrue, eps);
        if (t < 1.5 && servo.locked())
            lockedWithin1_5s = true;
    }
    EXPECT_TRUE(lockedWithin1_5s);
    EXPECT_TRUE(servo.locked());
    // Type-2 loop: constant ppm offset leaves zero standing occupancy error.
    EXPECT_NEAR(plant.occ, kTarget, 0.05);
    EXPECT_NEAR(servo.epsHat(), epsTrue, 1e-6); // within 1 ppm
}

TEST(Servo, TracksSlowDriftRampWithBoundedLag) {
    srt::PiServo servo(srt::ServoConfig{}, kFs, kTarget);
    Plant plant;
    // Settle at 0 ppm first.
    for (double t = 0.0; t < 5.0; t += kDt)
        plant.step(0.0, servo.update(plant.occ, 0.0, kDt));
    ASSERT_TRUE(servo.locked());
    // Then ramp 1 ppm/s for 20 s (temperature-style drift).
    double maxErr = 0.0;
    double epsTrue = 0.0;
    for (double t = 0.0; t < 20.0; t += kDt) {
        epsTrue = 1e-6 * t;
        plant.step(epsTrue, servo.update(plant.occ, 0.0, kDt));
        if (t > 5.0)
            maxErr = std::max(maxErr, std::abs(plant.occ - kTarget));
    }
    EXPECT_TRUE(servo.locked());
    // Type-2 acceleration error: e_ss = (deps/dt * fs) / wn^2 ~ 0.49 frames
    // for 1 ppm/s at the 0.05 Hz locked bandwidth.
    EXPECT_LT(maxErr, 1.0);
    EXPECT_NEAR(servo.epsHat(), epsTrue, 2e-6);
}

TEST(Servo, BandwidthSwitchIsTransientFree) {
    srt::PiServo servo(srt::ServoConfig{}, kFs, kTarget);
    Plant plant;
    const double epsTrue = 200e-6;
    // Run until just locked.
    double t = 0.0;
    while (!servo.locked() && t < 5.0) {
        plant.step(epsTrue, servo.update(plant.occ, 0.0, kDt));
        t += kDt;
    }
    ASSERT_TRUE(servo.locked());
    // The narrow-bandwidth handoff keeps the integrator, so the occupancy
    // must not be disturbed beyond the lock threshold afterwards.
    double maxErr = 0.0;
    for (double s = 0.0; s < 10.0; s += kDt) {
        plant.step(epsTrue, servo.update(plant.occ, 0.0, kDt));
        maxErr = std::max(maxErr, std::abs(plant.occ - kTarget));
    }
    EXPECT_LT(maxErr, srt::ServoConfig{}.lockThresholdFrames);
    EXPECT_TRUE(servo.locked());
}

TEST(Servo, ClampsToMaxDeviation) {
    srt::ServoConfig cfg;
    cfg.maxDeviationPpm = 100.0;
    srt::PiServo servo(cfg, kFs, kTarget);
    // Huge occupancy error must saturate at 1.5x the configured range.
    const double eps = servo.update(kTarget + 10000.0, 0.0, kDt);
    EXPECT_LE(eps, 1.5 * 100e-6 + 1e-12);
}

TEST(Servo, DropoutResetKeepsPpmEstimate) {
    srt::PiServo servo(srt::ServoConfig{}, kFs, kTarget);
    Plant plant;
    const double epsTrue = 250e-6;
    for (double t = 0.0; t < 6.0; t += kDt)
        plant.step(epsTrue, servo.update(plant.occ, 0.0, kDt));
    ASSERT_NEAR(servo.epsHat(), epsTrue, 2e-6);
    servo.reset(true); // dropout: keep the integrator
    EXPECT_NEAR(servo.epsHat(), epsTrue, 5e-6);
    servo.reset(false); // full reset: forget it
    EXPECT_DOUBLE_EQ(servo.epsHat(), 0.0);
}

} // namespace
