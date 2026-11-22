// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: the four-path stage by 4 was measured and does not ship
// (docs/IIR_PLAN.md I12), and the static_assert says so. Checked by
// compile_fail/CMakeLists.txt (the "iir" case set).
#include "tap/sr/rational/iir_stage.h"

[[maybe_unused]] const char* k_reason =
    tap::sr::rational::basic_iir_stage<float, 1, tap::sr::rational::down4,
                                       tap::sr::rational::iir_phase::nonlinear>::validate();
