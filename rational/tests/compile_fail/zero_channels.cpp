// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: a stage's channel count is a compile-time constant of at
// least 1 (A13, CHANNELS_PLAN.md K3), so a stage of none is rejected by
// basic_stage's static_assert. Checked by compile_fail/CMakeLists.txt (the
// "channels" case set), which also requires the message.
#include "tap/sr/rational/stage.h"

[[maybe_unused]] constexpr std::size_t k_channels =
    tap::sr::rational::basic_stage<float, 0, tap::sr::rational::ratio<2, 1>>::channels();
