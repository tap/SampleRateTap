// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Test runner main for bare-metal emulated targets (Cortex-M33/M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate filter is baked in. Excluded are the tests that
// sweep the 16384-point design grid over long designs or search it (the
// transparent rows up to N = 399 and the relaxation tables, whose searches
// run every candidate length through the grid, up to N = 735, the
// coarse-grid comparison, the unpinned search, the quantized tables'
// stopbands through a 16384-point DFT, the relaxed Q15 tables' likewise, the
// 4th-band stage's at every matrix divisor too) and the coverage-matrix suite (182
// chains in double through a tone battery, a host measurement):
// target-independent design math and measurement already covered on every
// host.
// The 70 dB rows' pins, minimality and structure run on target. The
// recursive stages' spec sweep (iir_double.EveryStageMeetsItsSpecAtEvery-
// ProfileAndDivisor: 32 impulse responses through a 2049-point DTFT per
// band, in double, soft-float on the M33) is a host measurement too; every
// other recursive test runs on target, the golden leg and the bit pins
// included.
//
// Built with TAP_SR_RATIONAL_BARE_METAL_SUBNORMALS it is the runner of
// tap_sr_rational_iir_subnormals instead (test_iir_subnormals.cpp, an image
// of its own built with TAP_DSP_ALLOW_SUBNORMALS): every test, at least the
// six it holds. Built with TAP_SR_RATIONAL_BARE_METAL_GUARD_BAND it is the
// runner of tap_sr_rational_guard_band_tests (test_guard_band.cpp and
// test_iir_composition.cpp, an image of its own for its size: tests/
// CMakeLists.txt), which leaves out four host measurements of the guard band
// filter: its spec sweep (eight impulse responses through 2049-point DTFTs
// in double), the custom linear designs (DspTap's Kaiser length search,
// 4097-point responses per candidate: 0.5 s on x86-64 for one that meets its
// spec, 15 s for the one no length up to 512 taps meets) and the grid of 294
// run-time Chebyshev II designs whose coefficients the fixed-point formats
// must hold; its golden leg, bit pins, flush, limit cycle and composition
// tests run on target.
#include <cstdio>

#include <gtest/gtest.h>

int main() {
#if defined(TAP_SR_RATIONAL_BARE_METAL_SUBNORMALS)
    ::testing::InitGoogleTest();
    const int rc       = RUN_ALL_TESTS();
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 6) {
        std::printf("only %d tests selected (expected >= 6)\n", selected);
        std::printf("TAP_SR_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    std::printf("TAP_SR_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
#elif defined(TAP_SR_RATIONAL_BARE_METAL_GUARD_BAND)
    ::testing::GTEST_FLAG(filter) = "-guard_band.EachFormMeetsItsSpec:"
                                    "guard_band.AnUnmeetableLinearSettingIsRefused:"
                                    "guard_band.CustomSettingsMeetTheirSpec:"
                                    "guard_band.EveryValidDesignFitsTheFixedPointFormats";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // 79 tests hosted, 75 here; 60 fails a broken filter (a dropped '-'
    // selects the four).
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 60) {
        std::printf("only %d tests selected (expected >= 60): filter is broken\n", selected);
        std::printf("TAP_SR_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    std::printf("TAP_SR_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
#else
    ::testing::GTEST_FLAG(filter) = "-Design.TransparentMeetsTheSpecWithTheMargin:"
                                    "Design.PinsAreMinimalAndMeasuredAtTransparent:"
                                    "Design.TheCoarseGridWouldUnderPinTwoRows:"
                                    "Design.UnpinnedProfileIsSearchedOnTheDesignGrid:"
                                    "Design.UnmeetableSpecThrows:"
                                    "Stage.ValidateAgreesWithTheConstructorOnAnUnmeetableSpec:"
                                    "Design.RelaxationTablesAreTheSearchAt70dB:"
                                    "Design.RelaxationTablesAreTheSearchAtTransparent:"
                                    "Matrix.*:"
                                    "FixedPoint.QuantizedTablesAttainTheirStatedStopbands:"
                                    "FixedPoint.RelaxedQ15TablesStopbandsAreStated:"
                                    "FixedPoint.FourthBandRelaxedQ15TablesStopbandsAreStated:"
                                    "FixedPoint.TheTransparentTiersNewRelaxedQ15TablesStopbandsAreStated:"
                                    "iir_double.EveryStageMeetsItsSpecAtEveryProfileAndDivisor";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo must not pass green: an empty run returns 0 from
    // RUN_ALL_TESTS(), and a dropped leading '-' would select only the
    // excluded names (eight Design tests) and pass them. The selection is
    // 99 tests at the audit response's S1 (the host suite minus Matrix.*,
    // CApi.* and the host-only measurements); 80 leaves headroom for legitimate removals and fails
    // both typos.
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 80) {
        std::printf("only %d tests selected (expected >= 80): filter is broken\n", selected);
        std::printf("TAP_SR_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    std::printf("TAP_SR_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
#endif
}
