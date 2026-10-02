/// @file converter.h
/// @brief The one-stage public converters: basic_converter<S, R> and the float / Q15 / Q31 aliases.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// A single-stage conversion by one ratio of the charter is a basic_stage
// (stage.h) under the family's converter name, with bridge's call shapes:
// process() (push), pull() (callback-driven), exact outputs_for() /
// frames_needed(), flush() / flush_output_frames(), reset(), the latency as
// an exact rational. Multi-stage conversions (PLAN.md section 3's chains)
// are chains of stages, named in chain.h (M4).
//
// Aliases (R8): converter<R> is float, the golden-model profile against the
// committed scipy vectors; converter_q15<R> and converter_q31<R> are the
// fixed-point profiles through tap::dsp::sample_traits (their measured
// floors are M5's); basic_converter<double, R> is the double oracle.
#pragma once

#include <cstdint>

#include "tap/sr/rational/ratio.h"
#include "tap/sr/rational/stage.h"

namespace tap::sr::rational {

    /// The one-stage converter at ratio R over sample format S.
    template <tap::dsp::sample_type S, rational_ratio R>
    using basic_converter = basic_stage<S, R>;

    /// The float converter at ratio R (the golden-model profile).
    template <rational_ratio R>
    using converter = basic_converter<float, R>;
    /// Q15 (int16_t samples) and Q31 (int32_t) converters at ratio R.
    template <rational_ratio R>
    using converter_q15 = basic_converter<std::int16_t, R>;
    template <rational_ratio R>
    using converter_q31 = basic_converter<std::int32_t, R>;

} // namespace tap::sr::rational
