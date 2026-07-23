/// \file kaiser.h
/// \brief Re-export of the shared tap::dsp Kaiser prototype designer.
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
//
// The Kaiser-window FIR design math (bessel_i0, kaiser_beta, estimate_taps,
// sinc, design_prototype, design_prototype_compensated, solve_dense) used to
// live here. It now lives in DspTap (tap::dsp), consumed via the
// submodules/dsptap submodule — one implementation shared with RatioTap and
// the rest of the family. This header keeps the historical include path and
// the unqualified names working inside tap::samplerate::detail.
#pragma once

#include "tap/dsp/kaiser.h"

namespace tap::samplerate::detail {

    using tap::dsp::bessel_i0;
    using tap::dsp::design_prototype;
    using tap::dsp::design_prototype_compensated;
    using tap::dsp::estimate_taps;
    using tap::dsp::kaiser_beta;
    using tap::dsp::sinc;
    using tap::dsp::solve_dense;

} // namespace tap::samplerate::detail
