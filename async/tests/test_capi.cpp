// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
//
// The C ABI's version probe pins the family encoding (D13): bit-packed
// (major << 16) | (minor << 8) | patch, the same value the bridge engine's
// probe returns. Links the shipped shared library, so it exists wherever
// TAP_SR_BUILD_CAPI builds one.
#include <gtest/gtest.h>

#include "tap/sr/async/async.h"

// The probe as the C ABI exports it (capi/tap_sr_async_capi.h); declared here so
// the test pins the shipped symbol without the C header in this TU.
extern "C" unsigned tap_sr_async_version(void);

namespace {

    TEST(CApi, VersionIsBitPacked) {
        constexpr unsigned k_packed =
            static_cast<unsigned>((TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH);
        const unsigned v = tap_sr_async_version();
        EXPECT_EQ(v, k_packed);
        EXPECT_EQ(v, 0x000400u); // 0.4.0
        EXPECT_EQ(v >> 16, 0u);
        EXPECT_EQ((v >> 8) & 0xFFu, 4u);
        EXPECT_EQ(v & 0xFFu, 0u);
    }

} // namespace
