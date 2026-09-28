/* ANCHOR: abi_contract */
/* SampleRateTap C ABI — FFI surface over the float converter.
 *
 * Build the shared library with -DSRT_BUILD_CAPI=ON. This header is the
 * contract for C/cffi/Julia consumers (the ctypes notebooks re-declare the
 * same prototypes); it must stay in sync with srt_capi.cpp.
 *
 * Thread contract (identical to the C++ API): one producer thread calls
 * srt_push at the input clock, one consumer thread calls srt_pull at the
 * output clock; srt_status may be called from any thread;
 * srt_reset_from_consumer only from the consumer thread; srt_create /
 * srt_destroy from any single thread, never concurrently with push/pull.
 *
 * Errors: srt_create returns NULL on invalid configuration or allocation
 * failure. Every function tolerates a NULL handle (no-op / zero return),
 * so an unchecked failed create degrades to silence, not a crash.
 *
 * size_t in these signatures follows the platform ABI (32-bit on 32-bit
 * targets) — declare foreign types accordingly.
 */
/* ANCHOR_END: abi_contract */
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ANCHOR: abi_surface */
typedef struct SrtHandle SrtHandle;

/* ABI/version probe: returns SRT_VERSION_MAJOR*10000 +
 * SRT_VERSION_MINOR*100 + SRT_VERSION_PATCH (e.g. 100 for 0.1.0). */
unsigned srt_version(void);

/* preset: 0 = fast, 1 = balanced, 2 = transparent.
 * targetLatencyFrames = 0 selects the library default (48). */
SrtHandle* srt_create(double sampleRateHz, size_t channels, size_t targetLatencyFrames, int preset);

void srt_destroy(SrtHandle* h);

/* Producer thread. Returns frames accepted (< frames on FIFO-full). */
size_t srt_push(SrtHandle* h, const float* interleaved, size_t frames);

/* Consumer thread. Always fills `frames` output frames (silence while
 * filling / on underrun); returns frames synthesized from real input. */
size_t srt_pull(SrtHandle* h, float* interleaved, size_t frames);

/* out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
 * out[2]=fifoFillFrames, out[3]=underruns, out[4]=overruns,
 * out[5]=resyncs. */
void srt_status(const SrtHandle* h, double out[6]);

double srt_designed_latency_seconds(const SrtHandle* h);

/* Consumer thread: discard all buffered input, forget the ppm estimate,
 * return to Filling. */
void srt_reset_from_consumer(SrtHandle* h);
/* ANCHOR_END: abi_surface */

#ifdef __cplusplus
}
#endif
