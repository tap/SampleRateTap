// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: a recursive stage has no exact latency (docs/IIR_PLAN.md
// I5), so a tap::dsp::chain holding one cannot report latency_output_frames()
// (its nominal is nominal_latency_output_frames()). Checked by
// compile_fail/CMakeLists.txt (the "iir" case set).
#include "tap/dsp/chain.h"
#include "tap/sr/rational/iir_stage.h"

namespace {
    using stage_t = tap::sr::rational::basic_iir_stage<float, 1, tap::sr::rational::down2,
                                                       tap::sr::rational::iir_phase::near_linear>;
} // namespace

tap::dsp::exact_ratio latency(const tap::dsp::chain<stage_t>& c) {
    return c.latency_output_frames();
}
