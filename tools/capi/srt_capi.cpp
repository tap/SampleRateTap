/// \file srt_capi.cpp
/// \brief C ABI shim over the float converter, for FFI consumers (ctypes,
/// cffi, Julia, ...). Build with SRT_BUILD_CAPI=ON; see
/// notebooks/asrc_demo.ipynb for a worked client.
///
/// The shim is intentionally minimal: an opaque handle, the push/pull hot
/// path, telemetry, and designed latency. Errors surface as null handles or
/// zero return values; the hot-path functions keep the library's noexcept
/// guarantee.
#include <cstddef>
#include <cstdint>
#include <new>

#include "srt/srt.hpp"

extern "C" {

struct SrtHandle; // opaque

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

void srt_destroy(SrtHandle* h) noexcept {
    delete reinterpret_cast<srt::AsyncSampleRateConverter*>(h);
}

std::size_t srt_push(SrtHandle* h, const float* interleaved, std::size_t frames) noexcept {
    return reinterpret_cast<srt::AsyncSampleRateConverter*>(h)->push(interleaved, frames);
}

std::size_t srt_pull(SrtHandle* h, float* interleaved, std::size_t frames) noexcept {
    return reinterpret_cast<srt::AsyncSampleRateConverter*>(h)->pull(interleaved, frames);
}

/// out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
/// out[2]=fifoFillFrames, out[3]=underruns, out[4]=overruns, out[5]=resyncs.
void srt_status(const SrtHandle* h, double out[6]) noexcept {
    const srt::Status s = reinterpret_cast<const srt::AsyncSampleRateConverter*>(h)->status();
    out[0] = static_cast<double>(static_cast<int>(s.state));
    out[1] = s.ppm;
    out[2] = s.fifoFillFrames;
    out[3] = static_cast<double>(s.underruns);
    out[4] = static_cast<double>(s.overruns);
    out[5] = static_cast<double>(s.resyncs);
}

double srt_designed_latency_seconds(const SrtHandle* h) noexcept {
    return reinterpret_cast<const srt::AsyncSampleRateConverter*>(h)->designedLatencySeconds();
}

void srt_reset_from_consumer(SrtHandle* h) noexcept {
    reinterpret_cast<srt::AsyncSampleRateConverter*>(h)->resetFromConsumer();
}

} // extern "C"
