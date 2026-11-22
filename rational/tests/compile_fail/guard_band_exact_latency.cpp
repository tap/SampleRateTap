// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: the guard band filter's Chebyshev II form has a nonlinear
// phase and so no exact latency (docs/IIR_PLAN.md I5, I7), and a
// tap::dsp::chain holding it, even beside FIR stages, cannot report
// latency_output_frames() (its nominal is nominal_latency_output_frames()).
// Checked by compile_fail/CMakeLists.txt (the "iir" case set).
#include "tap/dsp/chain.h"
#include "tap/sr/rational/guard_band.h"
#include "tap/sr/rational/stage.h"

namespace {
    using fir_t   = tap::sr::rational::basic_stage<float, 1, tap::sr::rational::down2>;
    using guard_t = tap::sr::rational::guard_band_filter<float, 1, tap::sr::rational::guard_phase::nonlinear>;
} // namespace

tap::dsp::exact_ratio latency(const tap::dsp::chain<fir_t, guard_t>& c) {
    return c.latency_output_frames();
}
