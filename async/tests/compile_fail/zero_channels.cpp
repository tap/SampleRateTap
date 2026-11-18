// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: a converter's channel count is a compile-time constant of
// at least 1 (A13, CHANNELS_PLAN.md K3), so a converter of none is rejected by
// basic_converter's static_assert. Checked by compile_fail/CMakeLists.txt,
// which also requires the message.
#include "tap/sr/async/converter.h"

[[maybe_unused]] constexpr std::size_t k_channels = tap::sr::async::basic_converter<float, 0>::channels();
