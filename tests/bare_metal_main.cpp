// Test runner main for bare-metal emulated targets (e.g. Cortex-M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded are the long-running
// servo/lock/quality simulations — minutes of soft-float virtual audio that
// validate target-independent control math already covered on every host
// platform — keeping the on-target run focused on datapath correctness
// (kernel accuracy, fixed-point arithmetic, ring buffer, end-to-end latency
// path).
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    // MultiChannelShort.* stays in: it is the only on-target coverage of the
    // N-channel deinterleave and the wide-MAC dotRow paths at N > 2.
    // "AsrcQuality*" (no dot) excludes every quality suite: in gtest filters
    // '.' is a literal, so "AsrcQuality.*" would not cover AsrcQuality16k.
    ::testing::GTEST_FLAG(filter) = "-AsrcQuality*:AsrcLock.*:Servo.*:Kaiser.*MeetsSpec:"
                                    "FixedPoint.AsrcQuality*:"
                                    "FixedPoint.FullScaleSineDoesNotWrapQ15:"
                                    "MultiChannel.*";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // CTest's pass criterion: printed only if we get all the way here, so a
    // crash after gtest's summary cannot register as a pass.
    std::printf("SRT_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
}
