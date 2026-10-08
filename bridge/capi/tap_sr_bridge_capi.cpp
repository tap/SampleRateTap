/// @file tap_sr_bridge_capi.cpp
/// @brief C ABI implementation: one interface over the two directions in float, Q15 and Q31.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors

#include "tap_sr_bridge_capi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "tap/sr/bridge/ratio.h"

namespace {

    namespace br = tap::sr::bridge;

    // Direction and sample type are compile-time template parameters in C++;
    // the C ABI makes both runtime tags over the six instantiations, behind
    // one interface. A process / flush call in another format than the
    // converter's is refused (returns 0, consumes and writes nothing).
    struct engine {
        virtual ~engine()                                                                       = default;
        virtual int           format() const                                                    = 0;
        virtual std::size_t   process(const float* in, std::size_t n, float* out)               = 0;
        virtual std::size_t   process(const std::int16_t* in, std::size_t n, std::int16_t* out) = 0;
        virtual std::size_t   process(const std::int32_t* in, std::size_t n, std::int32_t* out) = 0;
        virtual std::size_t   flush(float* out)                                                 = 0;
        virtual std::size_t   flush(std::int16_t* out)                                          = 0;
        virtual std::size_t   flush(std::int32_t* out)                                          = 0;
        virtual std::uint64_t outputs_for(std::uint64_t n) const                                = 0;
        virtual std::uint64_t frames_needed(std::uint64_t k) const                              = 0;
        virtual std::uint64_t flush_output_frames() const                                       = 0;
        virtual void          reset()                                                           = 0;
        virtual double        latency_input_frames() const                                      = 0;
        virtual std::size_t   taps() const                                                      = 0;
    };

    template <typename S>
    constexpr int format_of() {
        return std::is_same_v<S, float>          ? TAP_SR_BRIDGE_FORMAT_FLOAT
               : std::is_same_v<S, std::int16_t> ? TAP_SR_BRIDGE_FORMAT_Q15
                                                 : TAP_SR_BRIDGE_FORMAT_Q31;
    }

    template <typename S, br::direction D>
    class converter_engine final : public engine {
      public:
        converter_engine(unsigned channels, const br::profile& p)
            : m_c(channels, p) {}

        int         format() const override { return format_of<S>(); }
        std::size_t process(const float* in, std::size_t n, float* out) override { return run(in, n, out); }
        std::size_t process(const std::int16_t* in, std::size_t n, std::int16_t* out) override {
            return run(in, n, out);
        }
        std::size_t process(const std::int32_t* in, std::size_t n, std::int32_t* out) override {
            return run(in, n, out);
        }
        std::size_t   flush(float* out) override { return drain(out); }
        std::size_t   flush(std::int16_t* out) override { return drain(out); }
        std::size_t   flush(std::int32_t* out) override { return drain(out); }
        std::uint64_t outputs_for(std::uint64_t n) const override { return m_c.outputs_for(n); }
        std::uint64_t frames_needed(std::uint64_t k) const override { return m_c.frames_needed(k); }
        std::uint64_t flush_output_frames() const override { return m_c.flush_output_frames(); }
        void          reset() override { m_c.reset(); }
        double        latency_input_frames() const override { return m_c.latency_input_frames(); }
        std::size_t   taps() const override { return m_c.taps(); }

      private:
        template <typename T>
        std::size_t run(const T* in, std::size_t n, T* out) {
            if constexpr (std::is_same_v<T, S>) {
                return m_c.process(in, n, out);
            }
            else {
                return 0; // another format's converter
            }
        }

        template <typename T>
        std::size_t drain(T* out) {
            if constexpr (std::is_same_v<T, S>) {
                return m_c.flush(out);
            }
            else {
                return 0;
            }
        }

        br::basic_converter<S, D> m_c;
    };

    template <typename S>
    std::unique_ptr<engine> make(int direction, const br::profile& p, unsigned channels) {
        if (direction == 0) {
            return std::make_unique<converter_engine<S, br::direction::up_to_48k>>(channels, p);
        }
        return std::make_unique<converter_engine<S, br::direction::down_to_44k1>>(channels, p);
    }

} // namespace

struct tap_sr_bridge_converter {
    std::unique_ptr<engine> e;
};

// The library builds with hidden visibility (CMakeLists.txt); only the C entry
// points below are exported.
#if defined(__GNUC__)
#pragma GCC visibility push(default)
#endif

extern "C" {

tap_sr_bridge_converter* tap_sr_bridge_create_format(int direction, int profile, int format, unsigned channels) {
    if ((direction != 0 && direction != 1) || profile < 0 || profile > 3 || channels == 0) {
        return nullptr;
    }
    const br::profile p = profile == 0   ? br::profile::economy()
                          : profile == 1 ? br::profile::transparent()
                          : profile == 2 ? br::profile::balanced()
                                         : br::profile::super_economy();
    try {
        // unique_ptr owns the wrapper until the converter constructor has
        // succeeded, so a throw below cannot leak it.
        auto c = std::make_unique<tap_sr_bridge_converter>();
        switch (format) {
        case TAP_SR_BRIDGE_FORMAT_FLOAT:
            c->e = make<float>(direction, p, channels);
            break;
        case TAP_SR_BRIDGE_FORMAT_Q15:
            c->e = make<std::int16_t>(direction, p, channels);
            break;
        case TAP_SR_BRIDGE_FORMAT_Q31:
            c->e = make<std::int32_t>(direction, p, channels);
            break;
        default:
            return nullptr;
        }
        return c.release();
    }
    catch (...) {
        return nullptr;
    }
}

tap_sr_bridge_converter* tap_sr_bridge_create(int direction, int profile, unsigned channels) {
    return tap_sr_bridge_create_format(direction, profile, TAP_SR_BRIDGE_FORMAT_FLOAT, channels);
}

void tap_sr_bridge_destroy(tap_sr_bridge_converter* c) {
    delete c;
}

int tap_sr_bridge_format(const tap_sr_bridge_converter* c) {
    return c->e->format();
}

uint64_t tap_sr_bridge_outputs_for(const tap_sr_bridge_converter* c, uint64_t in_frames) {
    return c->e->outputs_for(in_frames);
}

uint64_t tap_sr_bridge_frames_needed(const tap_sr_bridge_converter* c, uint64_t out_frames) {
    return c->e->frames_needed(out_frames);
}

size_t tap_sr_bridge_process(tap_sr_bridge_converter* c, const float* in, size_t in_frames, float* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_bridge_process_q15(tap_sr_bridge_converter* c, const int16_t* in, size_t in_frames, int16_t* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_bridge_process_q31(tap_sr_bridge_converter* c, const int32_t* in, size_t in_frames, int32_t* out) {
    return c->e->process(in, in_frames, out);
}

size_t tap_sr_bridge_flush(tap_sr_bridge_converter* c, float* out) {
    return c->e->flush(out);
}

size_t tap_sr_bridge_flush_q15(tap_sr_bridge_converter* c, int16_t* out) {
    return c->e->flush(out);
}

size_t tap_sr_bridge_flush_q31(tap_sr_bridge_converter* c, int32_t* out) {
    return c->e->flush(out);
}

uint64_t tap_sr_bridge_flush_output_frames(const tap_sr_bridge_converter* c) {
    return c->e->flush_output_frames();
}

void tap_sr_bridge_reset(tap_sr_bridge_converter* c) {
    c->e->reset();
}

double tap_sr_bridge_latency_input_frames(const tap_sr_bridge_converter* c) {
    return c->e->latency_input_frames();
}

size_t tap_sr_bridge_taps(const tap_sr_bridge_converter* c) {
    return c->e->taps();
}

unsigned tap_sr_bridge_version(void) {
    return (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH;
}

} // extern "C"

#if defined(__GNUC__)
#pragma GCC visibility pop
#endif
