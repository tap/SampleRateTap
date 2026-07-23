/// @file ratio_capi.h
/// @brief Minimal C ABI over the float converters, for FFI consumers.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.
//
// The verification layer's seam (family convention): the notebooks drive the
// SHIPPING C++ through this ABI via ctypes rather than re-implementing
// anything in Python. Float only — the notebooks measure the golden model;
// the fixed-point contracts are pinned by the C++ test suite.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ratio_converter ratio_converter;

/// direction: 0 = up (44.1 -> 48), 1 = down (48 -> 44.1).
/// profile:   0 = economy (default tier), 1 = transparent.
/// Returns NULL on invalid arguments.
ratio_converter* ratio_create(int direction, int profile, unsigned channels);
void             ratio_destroy(ratio_converter* c);

/// Exact accounting (see tap::ratio::basic_converter).
uint64_t ratio_outputs_for(const ratio_converter* c, uint64_t in_frames);
uint64_t ratio_frames_needed(const ratio_converter* c, uint64_t out_frames);

/// Push-transform over interleaved float frames; returns frames written.
/// out must hold ratio_outputs_for(c, in_frames) frames.
size_t ratio_process(ratio_converter* c, const float* in, size_t in_frames, float* out);

/// Drains the tail (out must hold ratio_flush_output_frames(c) frames).
size_t   ratio_flush(ratio_converter* c, float* out);
uint64_t ratio_flush_output_frames(const ratio_converter* c);

void   ratio_reset(ratio_converter* c);
double ratio_latency_input_frames(const ratio_converter* c);
size_t ratio_taps(const ratio_converter* c);

/// Library version, packed (major << 16) | (minor << 8) | patch.
unsigned ratio_version(void);

#ifdef __cplusplus
} // extern "C"
#endif
