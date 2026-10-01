// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: ratio<2, 2> is 1, no conversion (PLAN.md R1). Checked by
// compile_fail/CMakeLists.txt, which also requires the charter's message.
#include "tap/sr/rational/ratio.h"

[[maybe_unused]] constexpr unsigned k_up = tap::sr::rational::ratio_traits<tap::sr::rational::ratio<2, 2>>::k_up;
