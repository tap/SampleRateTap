// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
/// \file tap_sr_cmp_speex_shim.cpp
/// \brief C entry points over SpeexDSP's resampler, so the family's
/// comparison notebooks (async/notebooks/asrc_comparison.ipynb,
/// bridge/notebooks/bridge_comparison.ipynb) can measure the real engine
/// through ctypes, in both of its arithmetic builds. Build with
/// TAP_SR_BUILD_COMPARE_SHIM=ON (tools/compare/speexdsp.cmake).
///
/// Speex is the one competitor with a fixed-point build, so each entry point
/// takes the build as an argument: 0 runs the FLOATING_POINT library on float
/// samples, 1 runs the FIXED_POINT library on Q15 samples (the float input
/// rounded and saturated to int16, the int16 output scaled back by 1/32768 —
/// what a Q15 caller's interface does). A whole-buffer conversion in one
/// call, the library's own latency skipped at the front and the tail flushed
/// with zeros, as its documentation prescribes; and the streaming latency,
/// speex_resampler_get_input_latency().
///
/// Errors surface as a nonzero return / -1, never as an exception across the
/// C boundary.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Two builds of the same header: the header is read twice, once per prefix
// and arithmetic, each time into its own namespace (the include guard
// undefined in between), so both libraries' prototypes are declared here.
// The header's speex_resampler_* names are macros over RANDOM_PREFIX,
// expanded where they are used, so the calls name the prefixed symbols.
namespace fixed {
#define OUTSIDE_SPEEX 1
#define EXPORT
#define RANDOM_PREFIX tap_sr_cmp_fixed
#define FIXED_POINT 1
#include <speex_resampler.h>
} // namespace fixed

namespace {

    int16_t to_q15(float v) {
        const float s = std::round(v * 32767.0F);
        return static_cast<int16_t>(std::clamp(s, -32768.0F, 32767.0F));
    }

    // One-shot through the fixed-point build on Q15 samples.
    int oneshot_fixed(const float* in, int n_in, double src_hz, double dst_hz, int quality, float* out, int n_out) {
        int   err = 0;
        auto* st  = fixed::tap_sr_cmp_fixed_resampler_init(1, static_cast<spx_uint32_t>(src_hz),
                                                           static_cast<spx_uint32_t>(dst_hz), quality, &err);
        if (st == nullptr) {
            return -1;
        }
        std::vector<int16_t> q(static_cast<std::size_t>(n_in));
        for (int i = 0; i < n_in; ++i) {
            q[static_cast<std::size_t>(i)] = to_q15(in[i]);
        }
        // Skip the filter delay so output sample k lines up with input time k,
        // then feed zeros until n_out is filled (the tail).
        fixed::tap_sr_cmp_fixed_resampler_skip_zeros(st);
        std::vector<int16_t> o(static_cast<std::size_t>(n_out));
        spx_uint32_t         in_len  = static_cast<spx_uint32_t>(n_in);
        spx_uint32_t         out_len = static_cast<spx_uint32_t>(n_out);
        if (fixed::tap_sr_cmp_fixed_resampler_process_int(st, 0, q.data(), &in_len, o.data(), &out_len) != 0) {
            fixed::tap_sr_cmp_fixed_resampler_destroy(st);
            return -1;
        }
        std::size_t done = out_len;
        while (done < static_cast<std::size_t>(n_out)) {
            int16_t      zeros[256] = {};
            spx_uint32_t zl         = 256;
            spx_uint32_t ol         = static_cast<spx_uint32_t>(static_cast<std::size_t>(n_out) - done);
            if (fixed::tap_sr_cmp_fixed_resampler_process_int(st, 0, zeros, &zl, o.data() + done, &ol) != 0
                || ol == 0) {
                break;
            }
            done += ol;
        }
        fixed::tap_sr_cmp_fixed_resampler_destroy(st);
        for (int i = 0; i < n_out; ++i) {
            out[i] = static_cast<float>(o[static_cast<std::size_t>(i)]) / 32768.0F;
        }
        return 0;
    }

    int latency_fixed(double src_hz, double dst_hz, int quality) {
        int   err = 0;
        auto* st  = fixed::tap_sr_cmp_fixed_resampler_init(1, static_cast<spx_uint32_t>(src_hz),
                                                           static_cast<spx_uint32_t>(dst_hz), quality, &err);
        if (st == nullptr) {
            return -1;
        }
        const int lat = fixed::tap_sr_cmp_fixed_resampler_get_input_latency(st);
        fixed::tap_sr_cmp_fixed_resampler_destroy(st);
        return lat;
    }

} // namespace

// The float build's prototypes, declared after the fixed build's so the
// macros do not collide.
namespace floating {
#undef SPEEX_RESAMPLER_H // the include guard: the header is read again under the other prefix
#undef RANDOM_PREFIX
#undef FIXED_POINT
#define RANDOM_PREFIX tap_sr_cmp_float
#define FLOATING_POINT 1
#include <speex_resampler.h>
} // namespace floating

namespace {

    int oneshot_float(const float* in, int n_in, double src_hz, double dst_hz, int quality, float* out, int n_out) {
        int   err = 0;
        auto* st  = floating::tap_sr_cmp_float_resampler_init(1, static_cast<spx_uint32_t>(src_hz),
                                                              static_cast<spx_uint32_t>(dst_hz), quality, &err);
        if (st == nullptr) {
            return -1;
        }
        floating::tap_sr_cmp_float_resampler_skip_zeros(st);
        spx_uint32_t in_len  = static_cast<spx_uint32_t>(n_in);
        spx_uint32_t out_len = static_cast<spx_uint32_t>(n_out);
        if (floating::tap_sr_cmp_float_resampler_process_float(st, 0, in, &in_len, out, &out_len) != 0) {
            floating::tap_sr_cmp_float_resampler_destroy(st);
            return -1;
        }
        std::size_t done = out_len;
        while (done < static_cast<std::size_t>(n_out)) {
            float        zeros[256] = {};
            spx_uint32_t zl         = 256;
            spx_uint32_t ol         = static_cast<spx_uint32_t>(static_cast<std::size_t>(n_out) - done);
            if (floating::tap_sr_cmp_float_resampler_process_float(st, 0, zeros, &zl, out + done, &ol) != 0
                || ol == 0) {
                break;
            }
            done += ol;
        }
        floating::tap_sr_cmp_float_resampler_destroy(st);
        return 0;
    }

    int latency_float(double src_hz, double dst_hz, int quality) {
        int   err = 0;
        auto* st  = floating::tap_sr_cmp_float_resampler_init(1, static_cast<spx_uint32_t>(src_hz),
                                                              static_cast<spx_uint32_t>(dst_hz), quality, &err);
        if (st == nullptr) {
            return -1;
        }
        const int lat = floating::tap_sr_cmp_float_resampler_get_input_latency(st);
        floating::tap_sr_cmp_float_resampler_destroy(st);
        return lat;
    }

} // namespace

extern "C" {

/// Convert `n_in` mono float frames at `src_hz` into exactly `n_out` frames at
/// `dst_hz` through SpeexDSP at `quality` (0..10); `fixed_point` selects the
/// build. Returns 0 on success, -1 on invalid arguments or failure.
int tap_sr_cmp_speex_oneshot(const float* in, int n_in, double src_hz, double dst_hz, int quality, int fixed_point,
                             float* out, int n_out) noexcept {
    if (in == nullptr || out == nullptr || n_in < 0 || n_out < 0 || src_hz <= 0.0 || dst_hz <= 0.0 || quality < 0
        || quality > 10) {
        return -1;
    }
    return fixed_point != 0 ? oneshot_fixed(in, n_in, src_hz, dst_hz, quality, out, n_out)
                            : oneshot_float(in, n_in, src_hz, dst_hz, quality, out, n_out);
}

/// Streaming latency in input frames (speex_resampler_get_input_latency).
/// Returns -1 on invalid arguments or failure.
int tap_sr_cmp_speex_latency_frames(double src_hz, double dst_hz, int quality, int fixed_point) noexcept {
    if (src_hz <= 0.0 || dst_hz <= 0.0 || quality < 0 || quality > 10) {
        return -1;
    }
    return fixed_point != 0 ? latency_fixed(src_hz, dst_hz, quality) : latency_float(src_hz, dst_hz, quality);
}

} // extern "C"
