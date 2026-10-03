/// @file tap_sr_rational_capi.h
/// @brief Minimal C ABI over the chains and stages in float, Q15 and Q31, for FFI consumers.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The verification layer's seam (family convention): the notebooks drive the
// SHIPPING C++ through this ABI via ctypes rather than re-implementing
// anything in Python, in float, the golden-model profile. Unlike the
// siblings' float-only ABIs, this one also carries the Q15 and Q31 profiles,
// for fixed-point FFI consumers (Bluetooth-adjacent M33 / M55 deployments,
// the reason the profiles exist): the same C++ in each format, its
// contracts pinned by the C++ test suite (PLAN.md section 6).
//
// What a caller constructs is named, never looked up (D12): a chain is one of
// the coverage matrix's within-family chains (PLAN.md 3.3 / 3.4, R16), a
// chain constant below; a single stage for composing a chain through bridge is a
// ratio of the vocabulary with the design divisor the matrix row states
// (PLAN.md 3.1). There is no (in_hz, out_hz) entry point.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tap_sr_rational_converter tap_sr_rational_converter;

/// The named within-family chains (PLAN.md 3.3 / 3.4): the eight one-stage
/// chains, then the twenty multi-stage ones, named by their stages in order.
/// The chain argument of tap_sr_rational_create; values are stable.
#define TAP_SR_RATIONAL_UP_2 0
#define TAP_SR_RATIONAL_DOWN_2 1
#define TAP_SR_RATIONAL_UP_3 2
#define TAP_SR_RATIONAL_DOWN_3 3
#define TAP_SR_RATIONAL_RATIO_3_2 4
#define TAP_SR_RATIONAL_RATIO_2_3 5
#define TAP_SR_RATIONAL_RATIO_4_3 6
#define TAP_SR_RATIONAL_RATIO_3_4 7
#define TAP_SR_RATIONAL_UP_2_UP_2 8
#define TAP_SR_RATIONAL_UP_2_UP_3 9
#define TAP_SR_RATIONAL_UP_2_RATIO_4_3_UP_3 10
#define TAP_SR_RATIONAL_UP_3_RATIO_8_3 11
#define TAP_SR_RATIONAL_UP_2_UP_6 12
#define TAP_SR_RATIONAL_UP_2_UP_8 13
#define TAP_SR_RATIONAL_UP_2_UP_6_UP_2 14
#define TAP_SR_RATIONAL_UP_2_UP_8_UP_2 15
#define TAP_SR_RATIONAL_UP_2_UP_8_UP_3 16
#define TAP_SR_RATIONAL_UP_2_RATIO_4_3 17
#define TAP_SR_RATIONAL_RATIO_3_4_DOWN_2 18
#define TAP_SR_RATIONAL_DOWN_2_DOWN_2 19
#define TAP_SR_RATIONAL_DOWN_3_DOWN_2 20
#define TAP_SR_RATIONAL_DOWN_3_RATIO_3_4_DOWN_2 21
#define TAP_SR_RATIONAL_RATIO_3_8_DOWN_3 22
#define TAP_SR_RATIONAL_DOWN_6_DOWN_2 23
#define TAP_SR_RATIONAL_DOWN_8_DOWN_2 24
#define TAP_SR_RATIONAL_DOWN_2_DOWN_6_DOWN_2 25
#define TAP_SR_RATIONAL_DOWN_2_DOWN_8_DOWN_2 26
#define TAP_SR_RATIONAL_DOWN_3_DOWN_8_DOWN_2 27
#define TAP_SR_RATIONAL_CHAIN_COUNT 28

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

/// chain:   one of the TAP_SR_RATIONAL_* chain constants above.
/// profile: 0 = economy (default tier), 1 = transparent, 2 = balanced,
///          3 = super_economy (bridge's C ABI tags).
/// Returns NULL on invalid arguments. A float converter.
tap_sr_rational_converter* tap_sr_rational_create(int chain, int profile, unsigned channels);
/// As tap_sr_rational_create in a stated format (TAP_SR_RATIONAL_FORMAT_*);
/// NULL on an unknown format too.
tap_sr_rational_converter* tap_sr_rational_create_format(int chain, int profile, int format, unsigned channels);

/// One stage at ratio L/M (one of the vocabulary's fourteen: 2/1, 1/2, 3/1,
/// 1/3, 6/1, 1/6, 8/1, 1/8, 3/2, 2/3, 4/3, 3/4, 8/3, 3/8) designed at the
/// profile relaxed by the design divisor divisor_num / divisor_den (the
/// stage's lower rate over its chain's lowest, PLAN.md 3.1): what a chain
/// through bridge composes, as the coverage-matrix test builds it. Returns
/// NULL on invalid arguments.
tap_sr_rational_converter* tap_sr_rational_create_stage(unsigned L, unsigned M, int profile, uint32_t divisor_num,
                                                        uint32_t divisor_den, unsigned channels);
/// As tap_sr_rational_create_stage in a stated format.
tap_sr_rational_converter* tap_sr_rational_create_stage_format(unsigned L, unsigned M, int profile,
                                                               uint32_t divisor_num, uint32_t divisor_den, int format,
                                                               unsigned channels);
void                       tap_sr_rational_destroy(tap_sr_rational_converter* c);

/// The converter's ratio, reduced: output frames per input frame = L / M.
void tap_sr_rational_ratio(const tap_sr_rational_converter* c, unsigned* L, unsigned* M);

/// Exact accounting from the current position (see tap::dsp::chain).
uint64_t tap_sr_rational_outputs_for(const tap_sr_rational_converter* c, uint64_t in_frames);
uint64_t tap_sr_rational_frames_needed(const tap_sr_rational_converter* c, uint64_t out_frames);

/// The converter's sample format (TAP_SR_RATIONAL_FORMAT_*).
int tap_sr_rational_format(const tap_sr_rational_converter* c);

/// Push-transform over interleaved frames of the converter's format; returns
/// frames written. out must hold tap_sr_rational_outputs_for(c, in_frames)
/// frames. A call in another format than the converter's returns 0 and
/// consumes and writes nothing.
size_t tap_sr_rational_process(tap_sr_rational_converter* c, const float* in, size_t in_frames, float* out);
size_t tap_sr_rational_process_q15(tap_sr_rational_converter* c, const int16_t* in, size_t in_frames, int16_t* out);
size_t tap_sr_rational_process_q31(tap_sr_rational_converter* c, const int32_t* in, size_t in_frames, int32_t* out);

/// Drains the tail, bit-identical to zero padding (out must hold
/// tap_sr_rational_flush_output_frames(c) frames); in another format than
/// the converter's, returns 0 and writes nothing.
size_t   tap_sr_rational_flush(tap_sr_rational_converter* c, float* out);
size_t   tap_sr_rational_flush_q15(tap_sr_rational_converter* c, int16_t* out);
size_t   tap_sr_rational_flush_q31(tap_sr_rational_converter* c, int32_t* out);
uint64_t tap_sr_rational_flush_output_frames(const tap_sr_rational_converter* c);

void tap_sr_rational_reset(tap_sr_rational_converter* c);

/// Group delay in output frames as an exact reduced rational (R7), and in
/// seconds at a given output rate.
void   tap_sr_rational_latency_output_frames(const tap_sr_rational_converter* c, uint64_t* num, uint64_t* den);
double tap_sr_rational_latency_seconds(const tap_sr_rational_converter* c, double out_rate_hz);

/// Multiply-accumulates per output frame per channel, an exact reduced
/// rational (the trimmed rows the kernels execute, PLAN.md 3.3; in Q15 the
/// quantized spans, which may trim outer taps that round to zero).
void tap_sr_rational_macs_per_output(const tap_sr_rational_converter* c, uint64_t* num, uint64_t* den);

/// The number of stages, and stage i's Nyquist design length N (0 when i is
/// out of range).
size_t tap_sr_rational_stages(const tap_sr_rational_converter* c);
size_t tap_sr_rational_stage_taps(const tap_sr_rational_converter* c, size_t i);

/// Library version, packed (major << 16) | (minor << 8) | patch.
unsigned tap_sr_rational_version(void);

#ifdef __cplusplus
} // extern "C"
#endif
