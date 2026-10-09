// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
/* ANCHOR: abi_contract */
/* SampleRateTap C ABI — FFI surface over the converter in float, Q15 and Q31.
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
 * Errors: tap_sr_async_create returns NULL on invalid configuration (a rate
 * or channel count of zero or less, a preset outside 0..2, an unknown
 * format) or allocation failure. Every function tolerates a NULL handle (no-op / zero return),
 * so an unchecked failed create degrades to silence, not a crash.
 *
 * Formats: a converter runs one sample format, chosen at create (float by
 * tap_sr_async_create; float, Q15 or Q31 by tap_sr_async_create_format), and
 * its push / pull entry points are the ones of that format. A call in
 * another format returns 0 and moves nothing: no frames accepted, no
 * output written.
 *
 * size_t in these signatures follows the platform ABI (32-bit on 32-bit
 * targets) — declare foreign types accordingly.
 */
/* ANCHOR_END: abi_contract */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Export: on Windows the C entry points are dllexport while the library
 * is built (TAP_SR_ASYNC_CAPI_BUILDING, set by capi/CMakeLists.txt) and
 * dllimport for a consumer; elsewhere default visibility on a library built
 * hidden, so the dynamic symbol table is exactly the tap_sr_async_* entry
 * points (pinned by async.Family.ExportedSymbolsArePinned). */
#if defined(_WIN32)
#if defined(TAP_SR_ASYNC_CAPI_BUILDING)
#define TAP_SR_ASYNC_API __declspec(dllexport)
#else
#define TAP_SR_ASYNC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define TAP_SR_ASYNC_API __attribute__((visibility("default")))
#else
#define TAP_SR_ASYNC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ANCHOR: abi_surface */
typedef struct tap_sr_async_converter tap_sr_async_converter;

/* The sample formats: float (the golden-model profile the notebooks measure),
 * Q15 (int16_t Q0.15 samples) and Q31 (int32_t Q0.31), each the C++
 * basic_converter of that sample type bit for bit. Values are stable, and
 * the same as the siblings' TAP_SR_BRIDGE_FORMAT_* / TAP_SR_RATIONAL_FORMAT_*. */
#define TAP_SR_ASYNC_FORMAT_FLOAT 0
#define TAP_SR_ASYNC_FORMAT_Q15 1
#define TAP_SR_ASYNC_FORMAT_Q31 2

/* ABI/version probe: the family version, bit-packed as
 * (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH
 * (0x000500 for 0.5.0); tap_sr_bridge_version and tap_sr_rational_version
 * return the same value. */
TAP_SR_ASYNC_API unsigned tap_sr_async_version(void);

/* preset: 0 = fast, 1 = balanced, 2 = transparent.
 * target_latency_frames = 0 selects the library default (48).
 * A float converter. */
TAP_SR_ASYNC_API tap_sr_async_converter* tap_sr_async_create(double sample_rate_hz, size_t channels,
                                                             size_t target_latency_frames, int preset);

/* As tap_sr_async_create in a stated format (TAP_SR_ASYNC_FORMAT_*); NULL on
 * an unknown format too. */
TAP_SR_ASYNC_API tap_sr_async_converter* tap_sr_async_create_format(double sample_rate_hz, size_t channels,
                                                                    size_t target_latency_frames, int preset,
                                                                    int format);

TAP_SR_ASYNC_API void tap_sr_async_destroy(tap_sr_async_converter* h);

/* The converter's sample format (TAP_SR_ASYNC_FORMAT_*); 0 for NULL. */
TAP_SR_ASYNC_API int tap_sr_async_format(const tap_sr_async_converter* h);

/* Producer thread. Returns frames accepted (< frames on FIFO-full). */
TAP_SR_ASYNC_API size_t tap_sr_async_push(tap_sr_async_converter* h, const float* interleaved, size_t frames);
TAP_SR_ASYNC_API size_t tap_sr_async_push_q15(tap_sr_async_converter* h, const int16_t* interleaved, size_t frames);
TAP_SR_ASYNC_API size_t tap_sr_async_push_q31(tap_sr_async_converter* h, const int32_t* interleaved, size_t frames);

/* Consumer thread. Always fills `frames` output frames (silence while
 * filling / on underrun); returns frames synthesized from real input. */
TAP_SR_ASYNC_API size_t tap_sr_async_pull(tap_sr_async_converter* h, float* interleaved, size_t frames);
TAP_SR_ASYNC_API size_t tap_sr_async_pull_q15(tap_sr_async_converter* h, int16_t* interleaved, size_t frames);
TAP_SR_ASYNC_API size_t tap_sr_async_pull_q31(tap_sr_async_converter* h, int32_t* interleaved, size_t frames);

/* out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
 * out[2]=fifo_fill_frames, out[3]=underruns, out[4]=overruns,
 * out[5]=resyncs. */
TAP_SR_ASYNC_API void tap_sr_async_status(const tap_sr_async_converter* h, double out[6]);

TAP_SR_ASYNC_API double tap_sr_async_designed_latency_seconds(const tap_sr_async_converter* h);

/* Consumer thread: discard all buffered input, forget the ppm estimate,
 * return to Filling. */
TAP_SR_ASYNC_API void tap_sr_async_reset_from_consumer(tap_sr_async_converter* h);
/* ANCHOR_END: abi_surface */

#ifdef __cplusplus
}
#endif
