// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// D13 (PLAN.md): one family version, TAP_SR_VERSION_*, defined
// in each engine's umbrella header with no shared header (so the engines
// stay independent, 4.2 check 1). The three definitions must agree, and
// this is the family's pinned release. The same holds for the DspTap floor
// (the roadmap's A9): TAP_SR_DSPTAP_MINIMUM, defined in each umbrella header,
// must agree, be the release the root README's tested pairs name, and not
// exceed the pinned substrate's TAP_DSP_VERSION. Compile-only: its
// static_asserts are the test (tests/CMakeLists.txt builds this target as
// family.VersionMacrosAgree).
#include "tap/sr/async/async.h"

namespace {
    constexpr int k_async_dsptap_minimum = TAP_SR_DSPTAP_MINIMUM;
    constexpr int k_async_major          = TAP_SR_VERSION_MAJOR;
    constexpr int k_async_minor          = TAP_SR_VERSION_MINOR;
    constexpr int k_async_patch          = TAP_SR_VERSION_PATCH;
} // namespace

#undef TAP_SR_VERSION_MAJOR
#undef TAP_SR_VERSION_MINOR
#undef TAP_SR_VERSION_PATCH
#undef TAP_SR_DSPTAP_MINIMUM
#include "tap/sr/bridge/ratio.h"

static_assert(k_async_dsptap_minimum == TAP_SR_DSPTAP_MINIMUM,
              "TAP_SR_DSPTAP_MINIMUM must be identical in async.h and ratio.h (A9)");

static_assert(k_async_major == TAP_SR_VERSION_MAJOR && k_async_minor == TAP_SR_VERSION_MINOR
                  && k_async_patch == TAP_SR_VERSION_PATCH,
              "TAP_SR_VERSION_* must be identical in async.h and ratio.h (D13)");

#undef TAP_SR_VERSION_MAJOR
#undef TAP_SR_VERSION_MINOR
#undef TAP_SR_VERSION_PATCH
#undef TAP_SR_DSPTAP_MINIMUM
#include "tap/sr/rational/rational.h"

static_assert(k_async_dsptap_minimum == TAP_SR_DSPTAP_MINIMUM,
              "TAP_SR_DSPTAP_MINIMUM must be identical in async.h and rational.h (A9)");

static_assert(k_async_major == TAP_SR_VERSION_MAJOR && k_async_minor == TAP_SR_VERSION_MINOR
                  && k_async_patch == TAP_SR_VERSION_PATCH,
              "TAP_SR_VERSION_* must be identical in async.h and rational.h (D13)");
static_assert(k_async_major == 0 && k_async_minor == 6 && k_async_patch == 0,
              "the family version is 0.6.0 (D13); re-pin here when it is bumped");
static_assert(k_async_dsptap_minimum == TAP_DSP_VERSION_ENCODE(0, 1, 0),
              "the family's DspTap floor is 0.1.0 (A9); re-pin here, and in the root README's tested pairs, "
              "when it moves");
static_assert(TAP_DSP_VERSION >= k_async_dsptap_minimum, "the pinned DspTap is older than the family's floor");
