// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: a recursive stage runs by 2 only; by 3, the three-path
// structure, is planned (docs/IIR_PLAN.md) and the static_assert says so.
// Checked by compile_fail/CMakeLists.txt (the "iir" case set).
#include "tap/sr/rational/iir_stage.h"

[[maybe_unused]] constexpr std::size_t k_channels =
    tap::sr::rational::basic_iir_stage<float, 1, tap::sr::rational::ratio<3, 1>,
                                       tap::sr::rational::iir_phase::nonlinear>::channels();

[[maybe_unused]] const char* k_reason =
    tap::sr::rational::basic_iir_stage<float, 1, tap::sr::rational::ratio<3, 1>,
                                       tap::sr::rational::iir_phase::nonlinear>::validate();
