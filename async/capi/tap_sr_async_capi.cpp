// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
// ANCHOR: abi_doc
/// \file tap_sr_async_capi.cpp
/// \brief C ABI shim over the converter in float, Q15 and Q31, for FFI
/// consumers (ctypes, cffi, Julia, ...). Build with TAP_SR_BUILD_CAPI=ON;
/// tap_sr_async_capi.h is the contract (thread affinity, error and format
/// conventions); see notebooks/asrc_demo.ipynb for a worked client.
///
/// The shim is intentionally minimal: an opaque handle, the push/pull hot
/// path in the converter's format, telemetry, and designed latency. Errors
/// surface as null handles or zero return values, and every entry point
/// tolerates a null handle — the documented error convention ("check
/// tap_sr_async_create for NULL") otherwise invites a crash on exactly the
/// path where the caller forgot to check.
// ANCHOR_END: abi_doc
#include "tap_sr_async_capi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

#include "tap/sr/async/async.h"

// ANCHOR: abi_impl
extern "C" {
struct tap_sr_async_converter; // opaque: declared by the header, never defined
}

namespace {
    // The sample type is a compile-time template parameter in C++; the C ABI
    // makes it a runtime tag over the three instantiations, behind one
    // interface. A push / pull call in another format than the converter's
    // is refused (returns 0, moves nothing).
    struct engine {
        virtual ~engine()                                                                                  = default;
        virtual int                              format() const noexcept                                   = 0;
        virtual std::size_t                      push(const float* in, std::size_t frames) noexcept        = 0;
        virtual std::size_t                      push(const std::int16_t* in, std::size_t frames) noexcept = 0;
        virtual std::size_t                      push(const std::int32_t* in, std::size_t frames) noexcept = 0;
        virtual std::size_t                      pull(float* out, std::size_t frames) noexcept             = 0;
        virtual std::size_t                      pull(std::int16_t* out, std::size_t frames) noexcept      = 0;
        virtual std::size_t                      pull(std::int32_t* out, std::size_t frames) noexcept      = 0;
        virtual tap::sr::async::converter_status status() const noexcept                                   = 0;
        virtual double                           designed_latency_seconds() const noexcept                 = 0;
        virtual void                             reset_from_consumer() noexcept                            = 0;
    };

    template <typename S>
    class typed_engine final : public engine {
      public:
        explicit typed_engine(const tap::sr::async::config& cfg)
            : m_c(cfg) {}

        int format() const noexcept override {
            return std::is_same_v<S, float>          ? TAP_SR_ASYNC_FORMAT_FLOAT
                   : std::is_same_v<S, std::int16_t> ? TAP_SR_ASYNC_FORMAT_Q15
                                                     : TAP_SR_ASYNC_FORMAT_Q31;
        }
        std::size_t push(const float* in, std::size_t frames) noexcept override { return offer(in, frames); }
        std::size_t push(const std::int16_t* in, std::size_t frames) noexcept override { return offer(in, frames); }
        std::size_t push(const std::int32_t* in, std::size_t frames) noexcept override { return offer(in, frames); }
        std::size_t pull(float* out, std::size_t frames) noexcept override { return take(out, frames); }
        std::size_t pull(std::int16_t* out, std::size_t frames) noexcept override { return take(out, frames); }
        std::size_t pull(std::int32_t* out, std::size_t frames) noexcept override { return take(out, frames); }
        tap::sr::async::converter_status status() const noexcept override { return m_c.status(); }
        double designed_latency_seconds() const noexcept override { return m_c.designed_latency_seconds(); }
        void   reset_from_consumer() noexcept override { m_c.reset_from_consumer(); }

      private:
        template <typename T>
        std::size_t offer(const T* in, std::size_t frames) noexcept {
            if constexpr (std::is_same_v<T, S>) {
                return m_c.push(in, frames);
            }
            else {
                return 0; // another format's converter
            }
        }

        template <typename T>
        std::size_t take(T* out, std::size_t frames) noexcept {
            if constexpr (std::is_same_v<T, S>) {
                return m_c.pull(out, frames);
            }
            else {
                return 0;
            }
        }

        tap::sr::async::basic_converter<S> m_c;
    };

    // The handle is the engine pointer in disguise.
    engine* impl(tap_sr_async_converter* h) noexcept {
        return reinterpret_cast<engine*>(h);
    }
    const engine* impl(const tap_sr_async_converter* h) noexcept {
        return reinterpret_cast<const engine*>(h);
    }
} // namespace
// ANCHOR_END: abi_impl

// The library builds with hidden visibility (CMakeLists.txt); only the C entry
// points below are exported.
#if defined(__GNUC__)
#pragma GCC visibility push(default)
#endif

extern "C" {

unsigned tap_sr_async_version(void) {
    // The family encoding (D13), the same value tap_sr_bridge_version returns.
    return (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH;
}

// ANCHOR: abi_create
/// preset: 0 = fast, 1 = balanced, 2 = transparent; format: a
/// TAP_SR_ASYNC_FORMAT_* value.
tap_sr_async_converter* tap_sr_async_create_format(double sample_rate_hz, std::size_t channels,
                                                   std::size_t target_latency_frames, int preset, int format) {
    tap::sr::async::config cfg;
    cfg.sample_rate_hz = sample_rate_hz;
    cfg.channels       = channels;
    if (target_latency_frames != 0) {
        cfg.target_latency_frames = target_latency_frames;
    }
    cfg.filter = preset == 0   ? tap::sr::async::filter_spec::fast()
                 : preset == 2 ? tap::sr::async::filter_spec::transparent()
                               : tap::sr::async::filter_spec::balanced();
    try {
        std::unique_ptr<engine> e;
        switch (format) {
        case TAP_SR_ASYNC_FORMAT_FLOAT:
            e = std::make_unique<typed_engine<float>>(cfg);
            break;
        case TAP_SR_ASYNC_FORMAT_Q15:
            e = std::make_unique<typed_engine<std::int16_t>>(cfg);
            break;
        case TAP_SR_ASYNC_FORMAT_Q31:
            e = std::make_unique<typed_engine<std::int32_t>>(cfg);
            break;
        default:
            return nullptr;
        }
        return reinterpret_cast<tap_sr_async_converter*>(e.release());
    }
    catch (...) {
        return nullptr;
    }
}

tap_sr_async_converter* tap_sr_async_create(double sample_rate_hz, std::size_t channels,
                                            std::size_t target_latency_frames, int preset) {
    return tap_sr_async_create_format(sample_rate_hz, channels, target_latency_frames, preset,
                                      TAP_SR_ASYNC_FORMAT_FLOAT);
}
// ANCHOR_END: abi_create

void tap_sr_async_destroy(tap_sr_async_converter* h) {
    delete impl(h);
}

int tap_sr_async_format(const tap_sr_async_converter* h) {
    return h ? impl(h)->format() : 0;
}

// ANCHOR: abi_null
std::size_t tap_sr_async_push(tap_sr_async_converter* h, const float* interleaved, std::size_t frames) {
    return h ? impl(h)->push(interleaved, frames) : 0;
}

std::size_t tap_sr_async_pull(tap_sr_async_converter* h, float* interleaved, std::size_t frames) {
    return h ? impl(h)->pull(interleaved, frames) : 0;
}
// ANCHOR_END: abi_null

std::size_t tap_sr_async_push_q15(tap_sr_async_converter* h, const std::int16_t* interleaved, std::size_t frames) {
    return h ? impl(h)->push(interleaved, frames) : 0;
}

std::size_t tap_sr_async_push_q31(tap_sr_async_converter* h, const std::int32_t* interleaved, std::size_t frames) {
    return h ? impl(h)->push(interleaved, frames) : 0;
}

std::size_t tap_sr_async_pull_q15(tap_sr_async_converter* h, std::int16_t* interleaved, std::size_t frames) {
    return h ? impl(h)->pull(interleaved, frames) : 0;
}

std::size_t tap_sr_async_pull_q31(tap_sr_async_converter* h, std::int32_t* interleaved, std::size_t frames) {
    return h ? impl(h)->pull(interleaved, frames) : 0;
}

/// out[0]=state (0 Filling, 1 Acquiring, 2 Locked), out[1]=ppm,
/// out[2]=fifo_fill_frames, out[3]=underruns, out[4]=overruns, out[5]=resyncs.
void tap_sr_async_status(const tap_sr_async_converter* h, double out[6]) {
    if (!h) {
        for (int i = 0; i < 6; ++i) {
            out[i] = 0.0;
        }
        return;
    }
    const tap::sr::async::converter_status s = impl(h)->status();
    out[0]                                   = static_cast<double>(static_cast<int>(s.state));
    out[1]                                   = s.ppm;
    out[2]                                   = s.fifo_fill_frames;
    out[3]                                   = static_cast<double>(s.underruns);
    out[4]                                   = static_cast<double>(s.overruns);
    out[5]                                   = static_cast<double>(s.resyncs);
}

double tap_sr_async_designed_latency_seconds(const tap_sr_async_converter* h) {
    return h ? impl(h)->designed_latency_seconds() : 0.0;
}

void tap_sr_async_reset_from_consumer(tap_sr_async_converter* h) {
    if (h) {
        impl(h)->reset_from_consumer();
    }
}

} // extern "C"

#if defined(__GNUC__)
#pragma GCC visibility pop
#endif
