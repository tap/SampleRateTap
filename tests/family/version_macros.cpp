// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// D13 (PLAN.md): one family version, TAP_SR_VERSION_*, defined
// in each engine's umbrella header with no shared header (so the engines
// stay independent, 4.2 check 1). The two definitions must agree, and this
// is the family's pinned release. Compile-only: its static_asserts are the
// test (tests/CMakeLists.txt builds this target as family.VersionMacrosAgree).
#include "tap/sr/async/async.h"

namespace {
    constexpr int k_async_major = TAP_SR_VERSION_MAJOR;
    constexpr int k_async_minor = TAP_SR_VERSION_MINOR;
    constexpr int k_async_patch = TAP_SR_VERSION_PATCH;
} // namespace

#undef TAP_SR_VERSION_MAJOR
#undef TAP_SR_VERSION_MINOR
#undef TAP_SR_VERSION_PATCH
#include "tap/sr/bridge/ratio.h"

static_assert(k_async_major == TAP_SR_VERSION_MAJOR && k_async_minor == TAP_SR_VERSION_MINOR
                  && k_async_patch == TAP_SR_VERSION_PATCH,
              "TAP_SR_VERSION_* must be identical in async.h and ratio.h (D13)");
static_assert(k_async_major == 0 && k_async_minor == 4 && k_async_patch == 0,
              "the family version is 0.4.0 (D13); re-pin here when it is bumped");
