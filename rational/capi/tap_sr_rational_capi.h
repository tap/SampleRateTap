/// @file tap_sr_rational_capi.h
/// @brief Minimal C ABI over the chains and stages in float, Q15 and Q31, for FFI consumers.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The verification layer's seam (family convention): the notebooks drive the
// SHIPPING C++ through this ABI via ctypes rather than re-implementing
// anything in Python, in float, the golden-model profile. The ABI also
// carries the Q15 and Q31 profiles — the shape this engine set and the
// siblings' ABIs then adopted — for fixed-point FFI consumers
// (Bluetooth-adjacent M33 / M55 deployments, the reason the profiles
// exist): the same C++ in each format, its contracts pinned by the C++
// test suite (PLAN.md section 6).
//
// What a caller constructs is named, never looked up (D12): a chain is one of
// the coverage matrix's within-family chains (PLAN.md 3.3 / 3.4, R16), a
// chain constant below; a single stage for composing a chain through bridge is a
// ratio of the vocabulary with the design divisor the matrix row states
// (PLAN.md 3.1). There is no (in_hz, out_hz) entry point.
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Export: on Windows the C entry points are dllexport while the library
 * is built (TAP_SR_RATIONAL_CAPI_BUILDING, set by capi/CMakeLists.txt) and
 * dllimport for a consumer; elsewhere default visibility on a library built
 * hidden, so the dynamic symbol table is exactly the tap_sr_rational_* entry
 * points (pinned by rational.Family.ExportedSymbolsArePinned). */
#if defined(_WIN32)
#if defined(TAP_SR_RATIONAL_CAPI_BUILDING)
#define TAP_SR_RATIONAL_API __declspec(dllexport)
#else
#define TAP_SR_RATIONAL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define TAP_SR_RATIONAL_API __attribute__((visibility("default")))
#else
#define TAP_SR_RATIONAL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tap_sr_rational_converter tap_sr_rational_converter;

/// The named within-family chains (PLAN.md 3.3 / 3.4): the eight one-stage
/// chains, then the multi-stage ones, named by their stages in order. The
/// chain argument of tap_sr_rational_create. Every value is stable (X7 of
/// docs/X4_X8_PLAN.md): a new chain takes a new value, a retired chain's
/// value is never reused, and create rejects it.
///
/// The coverage matrix selects a chain per attenuation tier (X4): the 70 dB
/// tier's at economy, the 120 dB tier's at transparent. Seven chains serve
/// only the 120 dB tier: UP2_RATIO3OVER2 (3/1), UP2_UP6 (12/1), UP2_UP8
/// (16/1), UP2_UP6_UP4 (48/1), DOWN6_DOWN2 (1/12), DOWN8_DOWN2 (1/16) and
/// DOWN4_DOWN6_DOWN2 (1/48). Every chain constructs at every profile: the
/// tier says where the matrix selects it, not what it may run at.
///
/// Retired in 0.8.0 (X-a3, the 4th-band stage admitted to the matrix, which
/// no longer runs them): 10 (up 2, 4/3, up 3), 11 (up 3, 8/3), 14 (up 2,
/// up 6, up 2), 15 (up 2, up 8, up 2), 16 (up 2, up 8, up 3), 21 (down 3,
/// 3/4, down 2), 22 (3/8, down 3), 25 (down 2, down 6, down 2), 26 (down 2,
/// down 8, down 2), 27 (down 3, down 8, down 2).
#define TAP_SR_RATIONAL_UP2 0
#define TAP_SR_RATIONAL_DOWN2 1
#define TAP_SR_RATIONAL_UP3 2
#define TAP_SR_RATIONAL_DOWN3 3
#define TAP_SR_RATIONAL_RATIO3OVER2 4
#define TAP_SR_RATIONAL_RATIO2OVER3 5
#define TAP_SR_RATIONAL_RATIO4OVER3 6
#define TAP_SR_RATIONAL_RATIO3OVER4 7
#define TAP_SR_RATIONAL_UP2_UP2 8
#define TAP_SR_RATIONAL_UP2_UP3 9
#define TAP_SR_RATIONAL_UP2_UP6 12
#define TAP_SR_RATIONAL_UP2_UP8 13
#define TAP_SR_RATIONAL_UP2_RATIO4OVER3 17
#define TAP_SR_RATIONAL_RATIO3OVER4_DOWN2 18
#define TAP_SR_RATIONAL_DOWN2_DOWN2 19
#define TAP_SR_RATIONAL_DOWN3_DOWN2 20
#define TAP_SR_RATIONAL_DOWN6_DOWN2 23
#define TAP_SR_RATIONAL_DOWN8_DOWN2 24
#define TAP_SR_RATIONAL_UP2_UP4 28
#define TAP_SR_RATIONAL_UP3_UP4 29
#define TAP_SR_RATIONAL_UP2_UP4_UP2 30
#define TAP_SR_RATIONAL_UP2_UP3_UP4 31
#define TAP_SR_RATIONAL_UP2_UP4_UP4 32
#define TAP_SR_RATIONAL_UP3_UP4_UP4 33
#define TAP_SR_RATIONAL_DOWN4_DOWN2 34
#define TAP_SR_RATIONAL_DOWN4_DOWN3 35
#define TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2 36
#define TAP_SR_RATIONAL_DOWN4_DOWN3_DOWN2 37
#define TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN2 38
#define TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3 39
#define TAP_SR_RATIONAL_UP2_RATIO3OVER2 40
#define TAP_SR_RATIONAL_UP2_UP6_UP4 41
#define TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2 42
/// One past the largest chain value ever assigned: create rejects
/// chain >= TAP_SR_RATIONAL_CHAIN_END and every retired value below it.
#define TAP_SR_RATIONAL_CHAIN_END 43

/// The sample formats: float (the golden-model profile the notebooks
/// measure), Q15 (int16_t Q0.15 samples) and Q31 (int32_t Q0.31), each the
/// C++ basic_chain / basic_stage of that sample type bit for bit, with the
/// fixed-point contracts PLAN.md section 6 states (exact unity DC,
/// saturation, the attained stopband per stage). Values are stable.
#define TAP_SR_RATIONAL_FORMAT_FLOAT 0
#define TAP_SR_RATIONAL_FORMAT_Q15 1
#define TAP_SR_RATIONAL_FORMAT_Q31 2

/// Every function below requires a valid converter from a successful create;
/// passing NULL is undefined behavior. The one exception is
/// tap_sr_rational_destroy, where NULL is a safe no-op (the free() convention).
///
/// Thread contract (identical to the C++ API): one stream per converter,
/// driven by one thread at a time; no function is reentrant on the same
/// handle, create and destroy run on any single thread and never
/// concurrently with the handle's other calls; distinct handles are
/// independent. The accessors (format, ratio, taps, latency, the counts)
/// read state the processing calls write and follow the same rule.

/// chain:    one of the TAP_SR_RATIONAL_* chain constants above.
/// profile:  0 = economy (default tier), 1 = transparent, 2 = balanced,
///           3 = super_economy (bridge's C ABI tags).
/// channels: at least 1. In C++ the count is a converter's type; here one and
///           two channels run those types natively, and any other count runs
///           that many one-channel converters behind the handle, the frames
///           split to them and interleaved back: bit-identical to the native
///           type of that count, since the channels share only the schedule
///           (SampleRateTap's A13, CHANNELS_PLAN.md K8, option (b)).
/// Returns NULL on invalid arguments. A float converter.
TAP_SR_RATIONAL_API tap_sr_rational_converter* tap_sr_rational_create(int chain, int profile, unsigned channels);
/// As tap_sr_rational_create in a stated format (TAP_SR_RATIONAL_FORMAT_*);
/// NULL on an unknown format too.
TAP_SR_RATIONAL_API tap_sr_rational_converter* tap_sr_rational_create_format(int chain, int profile, int format,
                                                                             unsigned channels);

/// One stage at ratio L/M (one of the vocabulary's sixteen: 2/1, 1/2, 3/1,
/// 1/3, 4/1, 1/4, 6/1, 1/6, 8/1, 1/8, 3/2, 2/3, 4/3, 3/4, 8/3, 3/8; the 4th
/// band since 0.8.0) designed at the profile relaxed by the design divisor
/// divisor_num / divisor_den (the stage's lower rate over its chain's
/// lowest, PLAN.md 3.1): what a chain through bridge composes, as the
/// coverage-matrix test builds it. Returns NULL on invalid arguments.
TAP_SR_RATIONAL_API tap_sr_rational_converter* tap_sr_rational_create_stage(unsigned L, unsigned M, int profile,
                                                                            uint32_t divisor_num, uint32_t divisor_den,
                                                                            unsigned channels);
/// As tap_sr_rational_create_stage in a stated format.
TAP_SR_RATIONAL_API tap_sr_rational_converter* tap_sr_rational_create_stage_format(unsigned L, unsigned M, int profile,
                                                                                   uint32_t divisor_num,
                                                                                   uint32_t divisor_den, int format,
                                                                                   unsigned channels);
TAP_SR_RATIONAL_API void                       tap_sr_rational_destroy(tap_sr_rational_converter* c);

/// The converter's ratio, reduced: output frames per input frame = L / M.
TAP_SR_RATIONAL_API void tap_sr_rational_ratio(const tap_sr_rational_converter* c, unsigned* L, unsigned* M);

/// Exact accounting from the current position (see tap::dsp::chain).
TAP_SR_RATIONAL_API uint64_t tap_sr_rational_outputs_for(const tap_sr_rational_converter* c, uint64_t in_frames);
TAP_SR_RATIONAL_API uint64_t tap_sr_rational_frames_needed(const tap_sr_rational_converter* c, uint64_t out_frames);

/// The converter's sample format (TAP_SR_RATIONAL_FORMAT_*).
TAP_SR_RATIONAL_API int tap_sr_rational_format(const tap_sr_rational_converter* c);

/// Push-transform over interleaved frames of the converter's format; returns
/// frames written. out must hold tap_sr_rational_outputs_for(c, in_frames)
/// frames. A call in another format than the converter's returns 0 and
/// consumes and writes nothing.
TAP_SR_RATIONAL_API size_t tap_sr_rational_process(tap_sr_rational_converter* c, const float* in, size_t in_frames,
                                                   float* out);
TAP_SR_RATIONAL_API size_t tap_sr_rational_process_q15(tap_sr_rational_converter* c, const int16_t* in,
                                                       size_t in_frames, int16_t* out);
TAP_SR_RATIONAL_API size_t tap_sr_rational_process_q31(tap_sr_rational_converter* c, const int32_t* in,
                                                       size_t in_frames, int32_t* out);

/// Drains the tail, bit-identical to zero padding (out must hold
/// tap_sr_rational_flush_output_frames(c) frames); in another format than
/// the converter's, returns 0 and writes nothing.
TAP_SR_RATIONAL_API size_t   tap_sr_rational_flush(tap_sr_rational_converter* c, float* out);
TAP_SR_RATIONAL_API size_t   tap_sr_rational_flush_q15(tap_sr_rational_converter* c, int16_t* out);
TAP_SR_RATIONAL_API size_t   tap_sr_rational_flush_q31(tap_sr_rational_converter* c, int32_t* out);
TAP_SR_RATIONAL_API uint64_t tap_sr_rational_flush_output_frames(const tap_sr_rational_converter* c);

TAP_SR_RATIONAL_API void tap_sr_rational_reset(tap_sr_rational_converter* c);

/// Group delay in output frames as an exact reduced rational (R7), and in
/// seconds at a given output rate.
TAP_SR_RATIONAL_API void   tap_sr_rational_latency_output_frames(const tap_sr_rational_converter* c, uint64_t* num,
                                                                 uint64_t* den);
TAP_SR_RATIONAL_API double tap_sr_rational_latency_seconds(const tap_sr_rational_converter* c, double out_rate_hz);

/// Multiply-accumulates per output frame per channel, an exact reduced
/// rational (the trimmed rows the kernels execute, PLAN.md 3.3; in Q15 the
/// quantized spans, which may trim outer taps that round to zero).
TAP_SR_RATIONAL_API void tap_sr_rational_macs_per_output(const tap_sr_rational_converter* c, uint64_t* num,
                                                         uint64_t* den);

/// The number of stages, and stage i's Nyquist design length N (0 when i is
/// out of range).
TAP_SR_RATIONAL_API size_t tap_sr_rational_stages(const tap_sr_rational_converter* c);
TAP_SR_RATIONAL_API size_t tap_sr_rational_stage_taps(const tap_sr_rational_converter* c, size_t i);

/// Library version, packed (major << 16) | (minor << 8) | patch.
TAP_SR_RATIONAL_API unsigned tap_sr_rational_version(void);

#ifdef __cplusplus
} // extern "C"
#endif
