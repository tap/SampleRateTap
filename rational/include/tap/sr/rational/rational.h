/// @file rational.h
/// @brief rational umbrella header: version constants and the engine charter.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The rational engine converts within a rate family, synchronously, as fast
// as possible: ratios L/M with L, M in {2^a * 3^b}, gcd(L, M) = 1, realised
// as chains of Nyquist (L-th-band) stages. It converts the number, never
// the clock.
//
// The boundaries are identity, not policy (PLAN.md section 1):
//   - Within a family only: the 48 kHz family (8 .. 384 kHz) and the
//     44.1 kHz family (11.025 .. 176.4 kHz). Crossing them is the bridge
//     engine (147/160), reached by a chain the caller writes; absorbing a
//     clock is async, reached by composition.
//   - Never routed to by rate (D12): ratio<L, M> names the number and a
//     chain names the stages; there is no (in_hz, out_hz) lookup anywhere.
//   - Speed-first, like bridge: the ratio is a compile-time type, every
//     trip count and schedule a compile-time fact, the stage factoring
//     chosen by MACs (R3).
//
// Built on DspTap (tap::dsp): the L-th-band designer and the stage
// composition landed there first (nyquist.h, chain.h; PLAN.md R6), with the
// sample-format traits, the dot kernels and the row-sum quantization the
// family's other engines use.
//
// Status: M1 — the skeleton, ratio types and stage design. The stages,
// chains and converters follow (PLAN.md section 6).
#pragma once

#include "tap/sr/rational/design.h" // IWYU pragma: export
#include "tap/sr/rational/ratio.h"  // IWYU pragma: export

#define TAP_SR_VERSION_MAJOR 0
#define TAP_SR_VERSION_MINOR 4
#define TAP_SR_VERSION_PATCH 0

namespace tap::sr::rational {

    /// The single-stage vocabulary of the 48 kHz family (PLAN.md section 1):
    /// the integer factors and the mixed ratios one stage serves, as named
    /// ratios. by_4 is a ratio the chain by 4 is named by, not a stage (R3,
    /// decision 6: two half-bands).
    using up_2   = ratio<2, 1>;
    using down_2 = ratio<1, 2>;
    using up_3   = ratio<3, 1>;
    using down_3 = ratio<1, 3>;
    using up_6   = ratio<6, 1>;
    using down_6 = ratio<1, 6>;
    using up_8   = ratio<8, 1>;
    using down_8 = ratio<1, 8>;
    /// The mixed ratios, named by L/M.
    using ratio_3_2 = ratio<3, 2>;
    using ratio_2_3 = ratio<2, 3>;
    using ratio_4_3 = ratio<4, 3>;
    using ratio_3_4 = ratio<3, 4>;
    using ratio_8_3 = ratio<8, 3>;
    using ratio_3_8 = ratio<3, 8>;

} // namespace tap::sr::rational
