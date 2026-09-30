// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Re-export of the shared tap::dsp::analysis sine-fit instrument. The
// implementation moved to DspTap with the measurement harness; this header
// keeps the historical include path and the async_test names working.
#pragma once

#include "tap/dsp/analysis/sine_analysis.h"

namespace async_test {

    using tap::dsp::analysis::fit_sine;
    using tap::dsp::analysis::fit_sine_tracked;
    using tap::dsp::analysis::sine_fit;
    using tap::dsp::analysis::snr_db;

} // namespace async_test
