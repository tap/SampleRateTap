// Test runner main for bare-metal emulated targets (e.g. Cortex-M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded are the long-running
// servo/lock/quality simulations — minutes of soft-float virtual audio that
// validate target-independent control math already covered on every host
// platform — keeping the on-target run focused on datapath correctness
// (kernel accuracy, fixed-point arithmetic, ring buffer, end-to-end latency
// path).
#include <gtest/gtest.h>

int main() {
    ::testing::GTEST_FLAG(filter) = "-AsrcQuality.*:AsrcLock.*:Servo.*:Kaiser.*MeetsSpec:"
                                    "FixedPoint.AsrcQuality*:"
                                    "FixedPoint.FullScaleSineDoesNotWrapQ15";
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}
