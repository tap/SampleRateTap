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
// Status: M3 — the ratio types, the pinned stage designs (M2) and the
// single stages with the family's call shapes (stage.h, converter.h). The
// chains and the coverage matrix follow (PLAN.md section 6, M4).
#pragma once

#include "tap/sr/rational/chain.h"     // IWYU pragma: export
#include "tap/sr/rational/converter.h" // IWYU pragma: export
#include "tap/sr/rational/design.h"    // IWYU pragma: export
#include "tap/sr/rational/ratio.h"     // IWYU pragma: export
#include "tap/sr/rational/stage.h"     // IWYU pragma: export

#define TAP_SR_VERSION_MAJOR 0
#define TAP_SR_VERSION_MINOR 4
#define TAP_SR_VERSION_PATCH 0
