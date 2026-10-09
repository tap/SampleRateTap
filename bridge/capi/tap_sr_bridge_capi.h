/// @file tap_sr_bridge_capi.h
/// @brief Minimal C ABI over the converters in float, Q15 and Q31, for FFI consumers.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The verification layer's seam (family convention): the notebooks drive the
// SHIPPING C++ through this ABI via ctypes rather than re-implementing
// anything in Python, in float, the golden-model profile. The ABI also
// carries the Q15 and Q31 profiles, the family's shape since rational's ABI
// set it, for fixed-point FFI consumers (Bluetooth-adjacent M33 / M55
// deployments, the reason the profiles exist): the same C++ in each format,
// its contracts pinned by the C++ test suite.
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Export: on Windows the C entry points are dllexport while the library
 * is built (TAP_SR_BRIDGE_CAPI_BUILDING, set by capi/CMakeLists.txt) and
 * dllimport for a consumer; elsewhere default visibility on a library built
 * hidden, so the dynamic symbol table is exactly the tap_sr_bridge_* entry
 * points (pinned by bridge.Family.ExportedSymbolsArePinned). */
#if defined(_WIN32)
#if defined(TAP_SR_BRIDGE_CAPI_BUILDING)
#define TAP_SR_BRIDGE_API __declspec(dllexport)
#else
#define TAP_SR_BRIDGE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define TAP_SR_BRIDGE_API __attribute__((visibility("default")))
#else
#define TAP_SR_BRIDGE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tap_sr_bridge_converter tap_sr_bridge_converter;

/// The sample formats: float (the golden-model profile the notebooks
/// measure), Q15 (int16_t Q0.15 samples) and Q31 (int32_t Q0.31), each the
/// C++ basic_converter of that sample type bit for bit. Values are stable,
/// and the same as the siblings' TAP_SR_ASYNC_FORMAT_* / TAP_SR_RATIONAL_FORMAT_*.
#define TAP_SR_BRIDGE_FORMAT_FLOAT 0
#define TAP_SR_BRIDGE_FORMAT_Q15 1
#define TAP_SR_BRIDGE_FORMAT_Q31 2

/// Every function below requires a valid converter from a successful create;
/// passing NULL is undefined behavior. The one exception is
/// tap_sr_bridge_destroy, where NULL is a safe no-op (the free() convention).
///
/// Thread contract (identical to the C++ API): one stream per converter,
/// driven by one thread at a time; no function is reentrant on the same
/// handle, create and destroy run on any single thread and never
/// concurrently with the handle's other calls; distinct handles are
/// independent. The accessors (format, ratio, taps, latency, the counts)
/// read state the processing calls write and follow the same rule.

/// direction: 0 = up (44.1 -> 48), 1 = down (48 -> 44.1).
/// profile:   0 = economy (default tier), 1 = transparent, 2 = balanced,
///            3 = super_economy (the voice/comms tier).
/// Returns NULL on invalid arguments. A float converter.
TAP_SR_BRIDGE_API tap_sr_bridge_converter* tap_sr_bridge_create(int direction, int profile, unsigned channels);
/// As tap_sr_bridge_create in a stated format (TAP_SR_BRIDGE_FORMAT_*); NULL
/// on an unknown format too.
TAP_SR_BRIDGE_API tap_sr_bridge_converter* tap_sr_bridge_create_format(int direction, int profile, int format,
                                                                       unsigned channels);
TAP_SR_BRIDGE_API void                     tap_sr_bridge_destroy(tap_sr_bridge_converter* c);

/// The converter's sample format (TAP_SR_BRIDGE_FORMAT_*).
TAP_SR_BRIDGE_API int tap_sr_bridge_format(const tap_sr_bridge_converter* c);

/// Exact accounting (see tap::sr::bridge::basic_converter).
TAP_SR_BRIDGE_API uint64_t tap_sr_bridge_outputs_for(const tap_sr_bridge_converter* c, uint64_t in_frames);
TAP_SR_BRIDGE_API uint64_t tap_sr_bridge_frames_needed(const tap_sr_bridge_converter* c, uint64_t out_frames);

/// Push-transform over interleaved frames of the converter's format; returns
/// frames written. out must hold tap_sr_bridge_outputs_for(c, in_frames)
/// frames. A call in another format than the converter's returns 0 and
/// consumes and writes nothing.
TAP_SR_BRIDGE_API size_t tap_sr_bridge_process(tap_sr_bridge_converter* c, const float* in, size_t in_frames,
                                               float* out);
TAP_SR_BRIDGE_API size_t tap_sr_bridge_process_q15(tap_sr_bridge_converter* c, const int16_t* in, size_t in_frames,
                                                   int16_t* out);
TAP_SR_BRIDGE_API size_t tap_sr_bridge_process_q31(tap_sr_bridge_converter* c, const int32_t* in, size_t in_frames,
                                                   int32_t* out);

/// Drains the tail (out must hold tap_sr_bridge_flush_output_frames(c)
/// frames); in another format than the converter's, returns 0 and writes
/// nothing.
TAP_SR_BRIDGE_API size_t   tap_sr_bridge_flush(tap_sr_bridge_converter* c, float* out);
TAP_SR_BRIDGE_API size_t   tap_sr_bridge_flush_q15(tap_sr_bridge_converter* c, int16_t* out);
TAP_SR_BRIDGE_API size_t   tap_sr_bridge_flush_q31(tap_sr_bridge_converter* c, int32_t* out);
TAP_SR_BRIDGE_API uint64_t tap_sr_bridge_flush_output_frames(const tap_sr_bridge_converter* c);

TAP_SR_BRIDGE_API void   tap_sr_bridge_reset(tap_sr_bridge_converter* c);
TAP_SR_BRIDGE_API double tap_sr_bridge_latency_input_frames(const tap_sr_bridge_converter* c);
TAP_SR_BRIDGE_API size_t tap_sr_bridge_taps(const tap_sr_bridge_converter* c);

/// Library version, packed (major << 16) | (minor << 8) | patch.
TAP_SR_BRIDGE_API unsigned tap_sr_bridge_version(void);

#ifdef __cplusplus
} // extern "C"
#endif
