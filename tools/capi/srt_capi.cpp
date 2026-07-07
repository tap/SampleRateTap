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
#include <cstddef>
#include <cstdint>
#include <new>

#include "srt/srt.h"

// ANCHOR: abi_impl
extern "C" {
struct SrtHandle; // opaque
}

namespace {
srt::AsyncSampleRateConverter* impl(SrtHandle* h) noexcept {
    return reinterpret_cast<srt::AsyncSampleRateConverter*>(h);
}
const srt::AsyncSampleRateConverter* impl(const SrtHandle* h) noexcept {
    return reinterpret_cast<const srt::AsyncSampleRateConverter*>(h);
}
} // namespace
// ANCHOR_END: abi_impl

extern "C" {

unsigned srt_version(void) noexcept {
    return SRT_VERSION_MAJOR * 10000u + SRT_VERSION_MINOR * 100u + SRT_VERSION_PATCH;
}

// ANCHOR: abi_create
/// preset: 0 = fast, 1 = balanced, 2 = transparent.
SrtHandle* srt_create(double sampleRateHz, std::size_t channels, std::size_t targetLatencyFrames,
                      int preset) noexcept {
    srt::Config cfg;
    cfg.sampleRateHz = sampleRateHz;
    cfg.channels = channels;
    if (targetLatencyFrames != 0)
        cfg.targetLatencyFrames = targetLatencyFrames;
    cfg.filter = preset == 0   ? srt::FilterSpec::fast()
                 : preset == 2 ? srt::FilterSpec::transparent()
                               : srt::FilterSpec::balanced();
    try {
        return reinterpret_cast<SrtHandle*>(new srt::AsyncSampleRateConverter(cfg));
    } catch (...) {
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
/// out[2]=fifoFillFrames, out[3]=underruns, out[4]=overruns, out[5]=resyncs.
void srt_status(const SrtHandle* h, double out[6]) noexcept {
    if (!h) {
        for (int i = 0; i < 6; ++i)
            out[i] = 0.0;
        return;
    }
    const srt::Status s = impl(h)->status();
    out[0] = static_cast<double>(static_cast<int>(s.state));
    out[1] = s.ppm;
    out[2] = s.fifoFillFrames;
    out[3] = static_cast<double>(s.underruns);
    out[4] = static_cast<double>(s.overruns);
    out[5] = static_cast<double>(s.resyncs);
}

double srt_designed_latency_seconds(const SrtHandle* h) noexcept {
    return h ? impl(h)->designedLatencySeconds() : 0.0;
}

void srt_reset_from_consumer(SrtHandle* h) noexcept {
    if (h)
        impl(h)->resetFromConsumer();
}

} // extern "C"
