// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: ratio<5, 1> is not 2^a * 3^b (PLAN.md R1). Checked by
// compile_fail/CMakeLists.txt, which also requires the charter's message.
#include "tap/sr/rational/ratio.h"

[[maybe_unused]] constexpr unsigned k_up = tap::sr::rational::ratio_traits<tap::sr::rational::ratio<5, 1>>::k_up;
