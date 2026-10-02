// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Test runner main for bare-metal emulated targets (Cortex-M33/M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded are the tests that
// sweep the 16384-point design grid over long designs or search it (the
// transparent rows up to N = 399 and the relaxation tables, whose searches
// run every candidate length through the grid, up to N = 735, the
// coarse-grid comparison, the unpinned search) and the
// coverage-matrix suite (182 chains in double through a tone battery, a
// host measurement): target-independent design math and measurement
// already covered on every host.
// The 70 dB rows' pins, minimality and structure run on target.
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    ::testing::GTEST_FLAG(filter) = "-Design.TransparentMeetsTheSpecWithTheMargin:"
                                    "Design.PinsAreMinimalAndMeasuredAtTransparent:"
                                    "Design.TheCoarseGridWouldUnderPinTwoRows:"
                                    "Design.UnpinnedProfileIsSearchedOnTheDesignGrid:"
                                    "Design.UnmeetableSpecThrows:"
                                    "Design.RelaxationTablesAreTheSearchAt70dB:"
                                    "Design.RelaxationTablesAreTheSearchAtTransparent:"
                                    "Matrix.*";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo selects zero tests and RUN_ALL_TESTS() returns 0 — an
    // empty run must not pass green. The M2 selection is 15 tests; 8 leaves
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
