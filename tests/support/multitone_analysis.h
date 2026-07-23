// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
// Re-export of the shared tap::dsp::analysis program-weighted multitone
// instrument (pink tone comb + joint LS fit). The implementation moved to
// DspTap with the measurement harness; this header keeps the historical
// include path and the srt_test names working. The design history is the
// book's epilogue chapter.
#pragma once

#include "tap/dsp/analysis/multitone_analysis.h"

namespace srt_test {

    using tap::dsp::analysis::fit_tone_fixed;
    using tap::dsp::analysis::joint_fit_residual_power;
    using tap::dsp::analysis::program_weighted_snr_db;
    using tap::dsp::analysis::tone_comb;
    using tap::dsp::analysis::tone_fit;
    using tap::dsp::analysis::track_tone_freq;

} // namespace srt_test
