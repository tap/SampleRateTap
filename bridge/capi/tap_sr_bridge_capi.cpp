/// @file tap_sr_bridge_capi.cpp
/// @brief C ABI implementation: one interface over the two directions in float, Q15 and Q31.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors

#include "tap_sr_bridge_capi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "tap/dsp/detail/expects.h"
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

    template <typename S, br::direction D, std::size_t Ch>
    class converter_engine final : public engine {
      public:
        explicit converter_engine(const br::profile& p)
            : m_c(p) {}

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

        br::basic_converter<S, Ch, D> m_c;
    };

    /// Any channel count but one and two (A13's K8, option (b)): that many
    /// one-channel converters behind one handle, the interleaved input split
    /// to each and their outputs interleaved back, a chunk at a time. Bit for
    /// bit the converter type of that count, since its channels share only
    /// the schedule (pinned by CApi.FanOutIsTheNativeConverterBitForBit).
    template <typename S>
    class fan_out_engine final : public engine {
      public:
        explicit fan_out_engine(std::vector<std::unique_ptr<engine>> monos)
            : m_monos(std::move(monos))
            , m_in(k_chunk)
            , m_out(2 * k_chunk + 8)
            , m_flush(static_cast<std::size_t>(m_monos.front()->flush_output_frames()) + 8) {}

        int         format() const override { return m_monos.front()->format(); }
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
        std::uint64_t outputs_for(std::uint64_t n) const override { return m_monos.front()->outputs_for(n); }
        std::uint64_t frames_needed(std::uint64_t k) const override { return m_monos.front()->frames_needed(k); }
        std::uint64_t flush_output_frames() const override { return m_monos.front()->flush_output_frames(); }
        void          reset() override {
            for (auto& m : m_monos) {
                m->reset();
            }
        }
        double      latency_input_frames() const override { return m_monos.front()->latency_input_frames(); }
        std::size_t taps() const override { return m_monos.front()->taps(); }

      private:
        /// Input frames staged per pass; at most 160/147 outputs per input, so
        /// a chunk's outputs fit twice its length.
        static constexpr std::size_t k_chunk = 256;

        template <typename T>
        std::size_t run(const T* in, std::size_t n, T* out) {
            if constexpr (!std::is_same_v<T, S>) {
                return 0; // another format's converter
            }
            else {
                const std::size_t ch   = m_monos.size();
                std::size_t       made = 0;
                while (n > 0) {
                    const std::size_t take     = std::min(n, k_chunk);
                    std::size_t       produced = 0;
                    for (std::size_t c = 0; c < ch; ++c) {
                        for (std::size_t i = 0; i < take; ++i) {
                            m_in[i] = in[i * ch + c];
                        }
                        produced = m_monos[c]->process(m_in.data(), take, m_out.data());
                        for (std::size_t k = 0; k < produced; ++k) {
                            out[(made + k) * ch + c] = m_out[k];
                        }
                    }
                    made += produced;
                    in += take * ch;
                    n -= take;
                }
                return made;
            }
        }

        template <typename T>
        std::size_t drain(T* out) {
            if constexpr (!std::is_same_v<T, S>) {
                return 0;
            }
            else {
                const std::size_t ch   = m_monos.size();
                const auto        need = static_cast<std::size_t>(flush_output_frames());
                if (need > m_flush.size()) {
                    m_flush.resize(need); // end of stream, not the streaming path
                }
                std::size_t made = 0;
                for (std::size_t c = 0; c < ch; ++c) {
                    made = m_monos[c]->flush(m_flush.data());
                    for (std::size_t k = 0; k < made; ++k) {
                        out[k * ch + c] = m_flush[k];
                    }
                }
                return made;
            }
        }

        std::vector<std::unique_ptr<engine>> m_monos;
        std::vector<S>                       m_in;
        std::vector<S>                       m_out;
        std::vector<S>                       m_flush;
    };

    /// The native one- and two-channel converters, and the fan-out for any
    /// other count (K8, option (b)).
    template <typename S, br::direction D>
    std::unique_ptr<engine> make_direction(const br::profile& p, unsigned channels) {
        if (channels == 1) {
            return std::make_unique<converter_engine<S, D, 1>>(p);
        }
        if (channels == 2) {
            return std::make_unique<converter_engine<S, D, 2>>(p);
        }
        std::vector<std::unique_ptr<engine>> monos;
        monos.reserve(channels);
        for (unsigned c = 0; c < channels; ++c) {
            monos.push_back(std::make_unique<converter_engine<S, D, 1>>(p));
        }
        return std::make_unique<fan_out_engine<S>>(std::move(monos));
    }

    template <typename S>
    std::unique_ptr<engine> make(int direction, const br::profile& p, unsigned channels) {
        if (direction == 0) {
            return make_direction<S, br::direction::up_to_48k>(p, channels);
        }
        return make_direction<S, br::direction::down_to_44k1>(p, channels);
    }

} // namespace

struct tap_sr_bridge_converter {
    std::unique_ptr<engine> e;
};

// The library builds with hidden visibility (CMakeLists.txt); only the C entry
// points below are exported, through TAP_SR_BRIDGE_API on their declarations
// in the header (default visibility here, dllexport on Windows).
extern "C" {

tap_sr_bridge_converter* tap_sr_bridge_create_format(int direction, int profile, int format, unsigned channels) {
    if ((direction != 0 && direction != 1) || profile < 0 || profile > 3 || channels == 0) {
        return nullptr;
    }
    const br::profile p = profile == 0   ? br::profile::economy()
                          : profile == 1 ? br::profile::transparent()
                          : profile == 2 ? br::profile::balanced()
                                         : br::profile::super_economy();
    // The constructor's own precheck (the profile's validity does not depend
    // on the sample format): a rejection is NULL here, never a throw — and,
    // built without exceptions, never a terminate.
    const char* const why = direction == 0 ? br::basic_converter<float, 1, br::direction::up_to_48k>::validate(p)
                                           : br::basic_converter<float, 1, br::direction::down_to_44k1>::validate(p);
    if (why != nullptr) {
        return nullptr;
    }
    const auto construct = [&]() -> tap_sr_bridge_converter* {
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
    };
#if TAP_DSP_HAS_EXCEPTIONS
    try {
        return construct();
    }
    catch (...) {
        return nullptr; // a refused allocation; the arguments were validated above
    }
#else
    return construct();
#endif
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
