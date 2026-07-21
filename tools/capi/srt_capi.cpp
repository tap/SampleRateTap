// ANCHOR: abi_doc
/// \file srt_capi.cpp
/// \brief C ABI shim over the float converter, for FFI consumers (ctypes,
/// cffi, Julia, ...). Build with SRT_BUILD_CAPI=ON; srt_capi.h is the
/// contract (thread affinity, error convention); see
/// notebooks/asrc_demo.ipynb for a worked client.
///
/// The shim is intentionally minimal: an opaque handle, the push/pull hot
/// path, telemetry, and designed latency. Errors surface as null handles or
/// zero return values, and every entry point tolerates a null handle — the
/// documented error convention ("check srt_create for NULL") otherwise
/// invites a crash on exactly the path where the caller forgot to check.
// ANCHOR_END: abi_doc
// SPDX-License-Identifier: MIT
// Copyright 2026 SampleRateTap contributors
#include <cstddef>
#include <cstdint>
#include <new>

#include "srt/srt.h"

// ANCHOR: abi_impl
extern "C" {
struct SrtHandle; // opaque
}

namespace {
    tap::samplerate::async_sample_rate_converter* impl(SrtHandle* h) noexcept {
        return reinterpret_cast<tap::samplerate::async_sample_rate_converter*>(h);
    }
    const tap::samplerate::async_sample_rate_converter* impl(const SrtHandle* h) noexcept {
        return reinterpret_cast<const tap::samplerate::async_sample_rate_converter*>(h);
    }
} // namespace
// ANCHOR_END: abi_impl

extern "C" {

unsigned srt_version(void) noexcept {
    return SRT_VERSION_MAJOR * 10000u + SRT_VERSION_MINOR * 100u + SRT_VERSION_PATCH;
}

// ANCHOR: abi_create
/// preset: 0 = fast, 1 = balanced, 2 = transparent.
SrtHandle* srt_create(double sample_rate_hz, std::size_t channels, std::size_t target_latency_frames,
                      int preset) noexcept {
    tap::samplerate::config cfg;
    cfg.sample_rate_hz = sample_rate_hz;
    cfg.channels       = channels;
    if (target_latency_frames != 0)
        cfg.target_latency_frames = target_latency_frames;
    cfg.filter = preset == 0   ? tap::samplerate::filter_spec::fast()
                 : preset == 2 ? tap::samplerate::filter_spec::transparent()
                               : tap::samplerate::filter_spec::balanced();
    try {
        return reinterpret_cast<SrtHandle*>(new tap::samplerate::async_sample_rate_converter(cfg));
    }
    catch (...) {
        return nullptr;
    }
}
// ANCHOR_END: abi_create

void srt_destroy(SrtHandle* h) noexcept {
    delete impl(h);
}

// ANCHOR: abi_null
std::size_t srt_push(SrtHandle* h, const float* interleaved, std::size_t frames) noexcept {
    return h ? impl(h)->push(interleaved, frames) : 0;
}

std::size_t srt_pull(SrtHandle* h, float* interleaved, std::size_t frames) noexcept {
    return h ? impl(h)->pull(interleaved, frames) : 0;
}
// ANCHOR_END: abi_null

/// out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
/// out[2]=fifo_fill_frames, out[3]=underruns, out[4]=overruns, out[5]=resyncs.
void srt_status(const SrtHandle* h, double out[6]) noexcept {
    if (!h) {
        for (int i = 0; i < 6; ++i)
            out[i] = 0.0;
        return;
    }
    const tap::samplerate::converter_status s = impl(h)->status();
    out[0]                        = static_cast<double>(static_cast<int>(s.state));
    out[1]                        = s.ppm;
    out[2]                        = s.fifo_fill_frames;
    out[3]                        = static_cast<double>(s.underruns);
    out[4]                        = static_cast<double>(s.overruns);
    out[5]                        = static_cast<double>(s.resyncs);
}

double srt_designed_latency_seconds(const SrtHandle* h) noexcept {
    return h ? impl(h)->designed_latency_seconds() : 0.0;
}

void srt_reset_from_consumer(SrtHandle* h) noexcept {
    if (h)
        impl(h)->reset_from_consumer();
}

} // extern "C"
