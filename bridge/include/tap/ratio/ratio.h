/// @file ratio.h
/// @brief RatioTap umbrella header: version constants and the library charter.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// RatioTap converts between 44.1 kHz and 48 kHz, synchronously, as fast as
// possible. One rational ratio pair (L/M = 160/147 up, 147/160 down), one
// clock, and the entire optimization budget spent on exactly that.
//
// The boundaries are identity, not policy:
//   - No other ratios. The public surface is 44.1 <-> 48 only, so the
//     optimization work (superblock codegen, baked tables, multistage) may
//     hard-commit to L in {147, 160}.
//   - No asynchronous conversion. If the two ends of a signal chain run on
//     different crystals — even at nominally 44.1-vs-48 — that is
//     SampleRateTap's near-unity ASRC problem, reached by composition (the
//     bluetooth_bridge example, once it lands). Which engine applies is a
//     property of the clock topology, not the number; the caller states it
//     by choosing a type, and nothing is ever inferred from a float ratio.
//
// Built on the Tap family's shared FIR substrate from DspTap (tap::dsp:
// kaiser design, sample-format traits including the Q15/Q31 embedded
// profiles, dot kernels, row-sum quantization, measurement instruments),
// consumed as the submodules/dsptap submodule.
//
// Status: M2 — direction/profile/design (design.h), the compile-time
// (phase, advance) schedule (schedule.h), and the phase-major quantized
// coefficient table (phase_table.h) are in; the engine lands in M3.
// PLAN.md is the authoritative roadmap; HANDOFF.md is the original design
// brief.
#pragma once

#include "tap/ratio/design.h"      // IWYU pragma: export
#include "tap/ratio/phase_table.h" // IWYU pragma: export
#include "tap/ratio/schedule.h"    // IWYU pragma: export

#define TAP_RATIO_VERSION_MAJOR 0
#define TAP_RATIO_VERSION_MINOR 1
#define TAP_RATIO_VERSION_PATCH 0

namespace tap::ratio {

    /// The fixed rational ratio pair this library exists for: 44.1 -> 48 kHz
    /// upsamples by 160/147; 48 -> 44.1 kHz downsamples by 147/160. Phase
    /// sequences repeat with exactly these periods, which is what makes
    /// exhaustive per-phase testing (and later, straight-line superblock
    /// code) possible.
    inline constexpr unsigned k_phases_up   = 160; ///< L for 44.1 -> 48
    inline constexpr unsigned k_phases_down = 147; ///< L for 48 -> 44.1

} // namespace tap::ratio
