// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Test runner main for bare-metal emulated targets (Cortex-M33/M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded is the transparent
// design's search (31 half-band and 25 third-band candidates of a 1024-
// point soft-double sweep each: target-independent design math already
// covered on every host), keeping the on-target run to the structure and
// the economy-tier counts.
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    ::testing::GTEST_FLAG(filter) = "-Design.TransparentMeetsTheSpecWithTheMargin";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo selects zero tests and RUN_ALL_TESTS() returns 0 — an
    // empty run must not pass green. The M1 selection is 12 tests; 8 leaves
    // headroom for legitimate removals without masking a typo.
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 8) {
        std::printf("only %d tests selected (expected >= 8): filter is broken\n", selected);
        std::printf("TAP_SR_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    std::printf("TAP_SR_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
}
