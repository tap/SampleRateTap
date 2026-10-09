// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// The async engine absorbs a clock: two audio clock domains at nominally the
// same rate (48 kHz on both sides, say) on independent oscillators, each
// within a few hundred ppm of nominal and drifting slowly. A producer thread
// push()es input frames at the input clock, a consumer thread pull()s output
// frames at the output clock; a lock-free FIFO sits between them and its
// occupancy drives a type-2 PI servo that estimates the instantaneous rate
// deviation, which a polyphase bank with inter-phase interpolation applies
// as a creeping fractional delay. It converts the clock, never the number.
//
// The boundaries are identity, not policy (PLAN.md D12): a rate pair is a
// different engine. 44.1 <-> 48 on one clock is bridge; a small-factor L/M
// inside one rate family is rational; 44.1 <-> 48 across independent clocks
// is bridge then this engine, composed by the caller
// (bridge/examples/bluetooth_bridge.cpp). Nothing here is looked up from a
// rate pair, and the 1000/1001 pull-down rates, inside this engine's
// +-1000 ppm, are never served by it.
//
// Built on DspTap (tap::dsp): the Kaiser prototype design, the sample-format
// traits (float / Q15 / Q31), the dot kernels and the measurement
// instruments the family's other engines use; this engine layers the
// inter-phase coefficient blend (sample_traits.h), the bank
// (polyphase_filter.h), the ring (spsc_ring.h), the servo (pi_servo.h) and
// their composition (converter.h) on top.
//
// Profiles: fast / balanced (default) / transparent / program, the engine's
// own ladder (filter_spec in polyphase_filter.h; the numbers are image
// rejection through the interpolated bank, a different kind from the
// siblings' Nyquist stopbands, so the names are not theirs: the root
// README's table states all three ladders).
#pragma once

#define TAP_SR_VERSION_MAJOR 0
#define TAP_SR_VERSION_MINOR 6
#define TAP_SR_VERSION_PATCH 0

#include "tap/sr/async/converter.h"
