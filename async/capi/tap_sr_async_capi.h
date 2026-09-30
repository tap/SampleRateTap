/* ANCHOR: abi_contract */
/* SampleRateTap C ABI — FFI surface over the float converter.
 *
 * Build the shared library with -DTAP_SR_BUILD_CAPI=ON. This header is the
 * contract for C/cffi/Julia consumers (the ctypes notebooks re-declare the
 * same prototypes); it must stay in sync with tap_sr_async_capi.cpp.
 *
 * Thread contract (identical to the C++ API): one producer thread calls
 * tap_sr_async_push at the input clock, one consumer thread calls tap_sr_async_pull at the
 * output clock; tap_sr_async_status may be called from any thread;
 * tap_sr_async_reset_from_consumer only from the consumer thread; tap_sr_async_create /
 * tap_sr_async_destroy from any single thread, never concurrently with push/pull.
 *
 * Errors: tap_sr_async_create returns NULL on invalid configuration or allocation
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
typedef struct tap_sr_async_converter tap_sr_async_converter;

/* ABI/version probe: the family version, bit-packed as
 * (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH
 * (0x000400 for 0.4.0); tap_sr_bridge_version returns the same value. */
unsigned tap_sr_async_version(void);

/* preset: 0 = fast, 1 = balanced, 2 = transparent.
 * targetLatencyFrames = 0 selects the library default (48). */
tap_sr_async_converter* tap_sr_async_create(double sampleRateHz, size_t channels, size_t targetLatencyFrames,
                                            int preset);

void tap_sr_async_destroy(tap_sr_async_converter* h);

/* Producer thread. Returns frames accepted (< frames on FIFO-full). */
size_t tap_sr_async_push(tap_sr_async_converter* h, const float* interleaved, size_t frames);

/* Consumer thread. Always fills `frames` output frames (silence while
 * filling / on underrun); returns frames synthesized from real input. */
size_t tap_sr_async_pull(tap_sr_async_converter* h, float* interleaved, size_t frames);

/* out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
 * out[2]=fifoFillFrames, out[3]=underruns, out[4]=overruns,
 * out[5]=resyncs. */
void tap_sr_async_status(const tap_sr_async_converter* h, double out[6]);

double tap_sr_async_designed_latency_seconds(const tap_sr_async_converter* h);

/* Consumer thread: discard all buffered input, forget the ppm estimate,
 * return to Filling. */
void tap_sr_async_reset_from_consumer(tap_sr_async_converter* h);
/* ANCHOR_END: abi_surface */

#ifdef __cplusplus
}
#endif
