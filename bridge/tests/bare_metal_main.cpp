// Test runner main for bare-metal emulated targets (Cortex-M33/M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded are the measurement
// suites — seconds of soft-double virtual audio (sine fits, stopband
// sweeps, the SampleRateTap cross-validation) that prove target-independent
// DSP math already covered on every host platform — keeping the on-target
// run focused on datapath correctness: exact accounting from every phase,
// impulse/table identity, the committed scipy vectors sample-for-sample,
// fixed-point DC exactness and wrap safety, flush/reset/pull contracts.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    ::testing::GTEST_FLAG(filter) = "-CrossValidation.*:Design.*MeetsSpec:"
                                    "Converter.PassbandSine*:Converter.StopbandTone*:"
                                    "FixedPoint.*SineQuality*";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo selects zero tests and RUN_ALL_TESTS() returns 0 — an
    // empty run must not pass green. Checked after the run because gtest
    // only applies the filter inside RUN_ALL_TESTS (the count reads 0
    // before it). The on-target selection is ~42 tests; 25 leaves headroom
    // for legitimate removals without masking a typo.
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 25) {
        std::printf("only %d tests selected (expected >= 25): filter is broken\n", selected);
        std::printf("TAP_RATIO_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    // CTest's pass criterion: printed only if we get all the way here, so a
    // crash after gtest's summary cannot register as a pass.
    std::printf("TAP_RATIO_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
}
