// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// Must NOT compile: the nonlinear recursive stage has no exact delay path
// (docs/IIR_PLAN.md I5), so the round-trip scaffold refuses it at compile
// time (docs/X4_X8_PLAN.md X10): tap::dsp::subband_delta constrains its
// stages to exact_delay_stage. Checked by compile_fail/CMakeLists.txt (the
// "iir" case set).
#include <cstddef>

#include "tap/dsp/subband.h"
#include "tap/sr/rational/iir_chain.h"

namespace {
    struct no_change {
        using sample = float;
        static constexpr std::size_t channels() noexcept { return 1; }
        std::size_t                  latency_frames() const noexcept { return 0; }
        void                         process(const float*, std::size_t, float*) noexcept {}
        void                         reset() noexcept {}
    };
    using down_t = tap::sr::rational::iir_down_by<float, 1, tap::sr::rational::iir_phase::nonlinear, 4>;
    using up_t   = tap::sr::rational::iir_up_by<float, 1, tap::sr::rational::iir_phase::nonlinear, 4>;
} // namespace

[[maybe_unused]] constexpr std::size_t k_factor = tap::dsp::subband_delta<down_t, up_t, no_change>::k_factor;
